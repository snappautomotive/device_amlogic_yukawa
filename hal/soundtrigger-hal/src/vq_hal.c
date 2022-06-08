#define LOG_TAG			    "vq_hal"

#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <poll.h>
#include <pthread.h>
#include <math.h>
#include <unistd.h>
#include <stdlib.h>
#include <limits.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/time.h>

#include "vq_hal.h"
#include "vq_plugin.h"
#include "uevent.h"
#include "logger.h"

#define UEVENT_MSG_LEN              1024

#define READ_SOCKET                 1
#define WRITE_SOCKET                0
#define TERMINATE_CMD               50
#define START_AUDIO_BUFFERING       51
#define STOP_AUDIO_BUFFERING        52
#define READ_AUDIO_BUFFER           53

// Ring buffer capable of buffering for 8 seconds of audio
#define RING_BUFFER_SIZE            (32000 * 8)
#define AUDIO_FRAME_SIZE            (320)
#define READ_WAIT_TIME              1

#define IAXXX_RECOVERY_EVENT_STR    "IAXXX_RECOVERY_EVENT"
#define IAXXX_FW_CRASH_EVENT_STR    "IAXXX_CRASH_EVENT"

extern struct vq_plugin_register vq_registrar[];

enum vq_hal_states {
    INIT,
    DEINIT,
    RECOGNIZING,
    PAUSED,
    STREAMING
};

static const char* g_state_str[] = {
    [INIT] = "INIT",
    [DEINIT] = "DEINIT",
    [RECOGNIZING] = "RECOGNIZING",
    [PAUSED] = "PAUSED",
    [STREAMING] = "STREAMING",
};

struct vq_hal {
    pthread_t event_thrd;
    int event_thrd_sock[2];

    vq_hal_event_cb event_cb;
    void *cb_cookie;

    struct vq_plugin *vq_plg;
    void *plg_hdl;

    pthread_mutex_t vq_lock;

    pthread_t buffer_thrd;
    int buffer_thrd_sock[2];

    bool is_multiturn;

    bool host_buffering;
    bool is_buffering;
    unsigned char *aud_ring_buf;
    unsigned char *read_idx, *write_idx;
    int bytes_avail_for_read;
    pthread_mutex_t rb_lock;
    pthread_cond_t rb_underflow_sig;
    pthread_cond_t rb_overflow_sig;

    enum vq_hal_states state;
    vq_hal_music_status music_status;
    bool send_music_status;
};

static int write_to_rb(struct vq_hal *hdl, unsigned char *buf, int write_sz) {
    int bw = 0, err = 0;
    unsigned char *rb_end;
    struct timespec ts;

    if (NULL == hdl) {
        ALOGE("%s: ERROR Null handle sent", __func__);
        goto exit;
    }

    pthread_mutex_lock(&hdl->rb_lock);

    /*
     * If there is no free space available for writing into the ring buffer.
     * Wait until the consumer consumes some data or until we stop the
     * buffering.
     */
    if (hdl->bytes_avail_for_read >= RING_BUFFER_SIZE) {
        ALOGE("%s: Buffer is full waiting to be read", __func__);
        while (true == hdl->is_buffering) {
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec += READ_WAIT_TIME;

            err = pthread_cond_timedwait(&hdl->rb_overflow_sig,
                                         &hdl->rb_lock, &ts);
            if (ETIMEDOUT == err) {
                ALOGE("%s: Warning waiting for data to be consumed"
                        , __func__);
            }
        }

        // If we got signal'ed because we are stopping the buffering then
        // nothing to write into the ring buffer, exit from here.
        if (false == hdl->is_buffering) {
            ALOGE("%s: Buffering is not in progress exiting", __func__);
            goto exit;
        }
    }

    rb_end = hdl->aud_ring_buf + RING_BUFFER_SIZE;

    if (hdl->write_idx + write_sz <= rb_end) {
        memcpy (hdl->write_idx, buf, write_sz);
        hdl->write_idx += write_sz;
    } else {
        // The buffer wraps around here. Copy as much data as possible towards
        // the end of the buffer and then copy the remaining to the front of
        // the buffer.
        int space_avail_at_end = 0;

        space_avail_at_end = rb_end - hdl->write_idx;
        memcpy (hdl->write_idx, buf, space_avail_at_end);
        hdl->write_idx = hdl->aud_ring_buf;
        memcpy (hdl->write_idx,
                buf + space_avail_at_end,
                write_sz - space_avail_at_end);
        hdl->write_idx += (write_sz - space_avail_at_end);
    }

    hdl->bytes_avail_for_read += write_sz;
    bw = write_sz;

    // Incase the consumer was waiting for data, send a signal to the consumer
    // about the availability of the data.
    pthread_cond_signal(&hdl->rb_underflow_sig);

exit:
    if (hdl)
        pthread_mutex_unlock(&hdl->rb_lock);

    return bw;
}

static int read_from_rb(struct vq_hal *hdl, unsigned char *buf, int read_sz) {
    int br = 0, err = 0;
    unsigned char *rb_end;
    struct timespec ts;

    if (NULL == hdl) {
        ALOGE("%s: ERROR Null handle sent", __func__);
        goto exit;
    }

    pthread_mutex_lock(&hdl->rb_lock);
    rb_end = hdl->aud_ring_buf + RING_BUFFER_SIZE;

    /*
     * If there is no data available for read then block until the producer
     * puts some data into the ring buffer or for our timer to expire. If the
     * timer expired then we waited for sufficient amount of time but the
     * producer is unable to generate data, so we exit.
     */
    while (read_sz > hdl->bytes_avail_for_read) {
        ALOGE("%s: No data to read waiting for buffer to be avail", __func__);
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += READ_WAIT_TIME;

        err = pthread_cond_timedwait(&hdl->rb_underflow_sig,
                                     &hdl->rb_lock, &ts);
        if (ETIMEDOUT == err) {
            ALOGE("%s: ERROR Failed to read data from Ring buffer after 1 sec"
                    , __func__);
            goto exit;
        }
    }

    if (hdl->read_idx + read_sz <= rb_end) {
        memcpy(buf, hdl->read_idx, read_sz);
        hdl->read_idx += read_sz;
    } else {
        // The buffer will wrap around, so read as much data as possible at the
        // back of the buffer and the remaining from the front of the buffer
        int bytes_avail_at_end = rb_end - hdl->read_idx;
        memcpy(buf, hdl->read_idx, bytes_avail_at_end);
        hdl->read_idx = hdl->aud_ring_buf;
        memcpy((buf + bytes_avail_at_end),
               hdl->read_idx,
               (read_sz - bytes_avail_at_end));
        hdl->read_idx += (read_sz - bytes_avail_at_end);
    }

    hdl->bytes_avail_for_read -= read_sz;
    br = read_sz;
    // Signal to the producer that we have read some data and there is free
    // space available to be written into.
    pthread_cond_signal(&hdl->rb_overflow_sig);

exit:
    if (hdl)
        pthread_mutex_unlock(&hdl->rb_lock);

    return br;
}

// Responsible for reading the data into the ring buffer.
static void *buffer_thread_loop(void *context)
{
    struct vq_hal *hdl = (struct vq_hal*) context;
    struct pollfd pfd;
    unsigned char audio_frame[AUDIO_FRAME_SIZE];
    int err = 0;
    bool should_exit = false;
    int bytes = 0;

    if (NULL == hdl) {
        ALOGE("%s: ERROR: Invalid handle passed", __func__);
        err = -1;
        goto exit;
    }

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, hdl->buffer_thrd_sock) == -1) {
        ALOGE("%s: Failed to create termination socket", __func__);
        err = -1;
        goto exit;
    }

    memset(&pfd, 0, sizeof(struct pollfd));
    int timeout = -1; // Wait for event indefinitely
    pfd.events = POLLIN;
    pfd.fd = hdl->buffer_thrd_sock[READ_SOCKET];

    hdl->aud_ring_buf = (unsigned char*)malloc(RING_BUFFER_SIZE);
    if (NULL == hdl->aud_ring_buf) {
        ALOGE("%s: ERROR Failed to allocate memory", __func__);
        err = -ENOMEM;
        goto exit;
    }

    ALOGE("Listening for Buffering events");
    while (false == should_exit) {
        err = poll (&pfd, 1, timeout);
        if (err < 0) {
            ALOGE("%s: Error in poll: %d (%s)", __func__, errno, strerror(errno));
            break;
        }

        if (pfd.revents & POLLIN) {
            int cmd;
            read(pfd.fd, &cmd, sizeof(cmd));
            switch(cmd) {
                case READ_AUDIO_BUFFER:
                    pthread_mutex_lock(&hdl->vq_lock);
                    if (hdl->is_buffering) {
                        bytes = hdl->vq_plg->read_audio(hdl->plg_hdl,
                                                        audio_frame,
                                                        AUDIO_FRAME_SIZE);
                    }
                    pthread_mutex_unlock(&hdl->vq_lock);

                    if (bytes > 0) {
                        write_to_rb(hdl, audio_frame, bytes);
                        bytes = 0;

                        pthread_mutex_lock(&hdl->vq_lock);
                        if (hdl->is_buffering) {
                            cmd = READ_AUDIO_BUFFER;
                            write(hdl->buffer_thrd_sock[WRITE_SOCKET],
                                    &cmd, sizeof(cmd));
                        }
                        pthread_mutex_unlock(&hdl->vq_lock);
                    } else {
                        if (hdl->is_buffering) {
                            ALOGE("%s: ERROR Failed to read audio data",
                                    __func__);
                        } else {
                            ALOGE("%s: Buffer thread IDLE", __func__);
                        }
                    }
                break;
                case TERMINATE_CMD:
                    ALOGI("%s: Termination message", __func__);
                    should_exit = true;
                break;
                default:
                    ALOGE("%s: Unknown cmd, ignoring", __func__);
                break;
            }
        } else {
            ALOGE("%s: Message ignored", __func__);
        }
    }

exit:
    close(hdl->buffer_thrd_sock[READ_SOCKET]);
    close(hdl->buffer_thrd_sock[WRITE_SOCKET]);

    if (hdl->aud_ring_buf)
        free(hdl->aud_ring_buf);

    return (void *)(long)err;
}

// All events related to the VQ HAL are handled here.
static void *event_thread_loop(void *context)
{
    struct vq_hal *hdl = (struct vq_hal*) context;
    struct pollfd fds[2];
    char msg[UEVENT_MSG_LEN];
    int err = 0, cmd, n, i;
    bool is_kw_detected = false;
    bool should_exit = false;

    if (NULL == hdl) {
        ALOGE("%s: ERROR: Invalid handle passed", __func__);
        err = -1;
        goto exit;
    }
    pthread_mutex_lock(&hdl->vq_lock);

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, hdl->event_thrd_sock) == -1) {
        ALOGE("%s: Failed to create termination socket", __func__);
        err = -1;
        goto exit;
    }

    memset(fds, 0, 2 * sizeof(struct pollfd));
    int timeout = -1; // Wait for event indefinitely
    fds[0].events = POLLIN;
    fds[0].fd = uevent_open_socket(64*1024, true);
    if (fds[0].fd == -1) {
        ALOGE("%s: Error opening socket for hotplug uevent errno %d(%s)",
                __func__, errno, strerror(errno));
        goto exit;
    }
    fds[1].events = POLLIN;
    fds[1].fd = hdl->event_thrd_sock[READ_SOCKET];

    pthread_mutex_unlock(&hdl->vq_lock);

    ALOGE("Listening for uevents");
    while (false == should_exit) {
        err = poll (fds, 2, timeout);
        if (err < 0) {
            ALOGE("%s: Error in poll: %d (%s)", __func__, errno, strerror(errno));
            break;
        }
        pthread_mutex_lock(&hdl->vq_lock);

        if (fds[0].revents & POLLIN) {
            n = uevent_kernel_multicast_recv(fds[0].fd, msg, UEVENT_MSG_LEN);
            if (n <= 0) {
                pthread_mutex_unlock(&hdl->vq_lock);
                continue;
            }

            for (i = 0; i < n;) {
                if (strstr(msg + i, IAXXX_FW_CRASH_EVENT_STR)) {
                    ALOGD("%s: IAXXX_FW_CRASH_EVENT", __func__);
                    hdl->event_cb(hdl->cb_cookie, EVENT_FW_CRASH, NULL);
                } else if (strstr(msg + i, IAXXX_RECOVERY_EVENT_STR)) {
                    ALOGD("%s: IAXXX_RECOVER_EVENT_STR", __func__);
                    hdl->event_cb(hdl->cb_cookie, EVENT_FW_RECOVERED, NULL);
                }
                i += strlen(msg + i) + 1;
            }

            is_kw_detected = hdl->vq_plg->process_uevent(hdl->plg_hdl, msg, n,
                                                hdl->event_cb, hdl->cb_cookie);
            // Check if the keyword has been detected and if host side
            // buffering is enabled, if yes then start the buffering.
            if (is_kw_detected && true == hdl->host_buffering) {
                int cmd = START_AUDIO_BUFFERING;
                write(hdl->event_thrd_sock[WRITE_SOCKET], &cmd, sizeof(int));
            }
        } else if (fds[READ_SOCKET].revents & POLLIN) {
            read(fds[READ_SOCKET].fd, &cmd, sizeof(cmd));

            switch (cmd) {
                case START_AUDIO_BUFFERING:
                    hdl->read_idx = hdl->aud_ring_buf;
                    hdl->write_idx = hdl->aud_ring_buf;
                    hdl->bytes_avail_for_read = 0;

                    err = hdl->vq_plg->start_audio(hdl->plg_hdl, false);
                    if (0 != err) {
                        ALOGE("%s: ERROR: Failed to start audio buffering"
                                , __func__);
                        pthread_mutex_unlock(&hdl->vq_lock);
                        continue;
                    }
                    hdl->is_buffering = true;

                    // Start the streaming on the buffer thread
                    ALOGE("%s: Sending READ_AUDIO_BUFFER message", __func__);
                    cmd = READ_AUDIO_BUFFER;
                    write(hdl->buffer_thrd_sock[WRITE_SOCKET],
                          &cmd, sizeof(cmd));
                break;
                case STOP_AUDIO_BUFFERING:
                    if (hdl->is_buffering) {
                        err = hdl->vq_plg->stop_audio(hdl->plg_hdl);
                        if (0 != err) {
                            ALOGE("%s: ERROR: Failed to stop audio buffering"
                                    , __func__);
                        }
                        hdl->is_buffering = false;

                        if (hdl->send_music_status) {
                            err = hdl->vq_plg->set_music_status(hdl->plg_hdl,
                                                            hdl->music_status);
                            hdl->send_music_status = false;
                        }

                        // If the ring buffer is full we could be waiting for
                        // it to be consumed but no one will consume it, since
                        // we are stopping the streaming, so signal for the
                        // buffer thread to stop waiting
                        pthread_mutex_lock(&hdl->rb_lock);
                        pthread_cond_signal(&hdl->rb_overflow_sig);
                        pthread_mutex_unlock(&hdl->rb_lock);
                    } else {
                        ALOGE("%s: Stop called when not buffering", __func__);
                    }
                break;
                case TERMINATE_CMD:
                    ALOGI("%s: Termination message", __func__);
                    should_exit = true;
                break;
                default:
                    ALOGE("%s: Unknown cmd, ignoring", __func__);
                break;
            }
            pthread_mutex_unlock(&hdl->vq_lock);
        } else {
            ALOGE("%s: Message ignored", __func__);
        }

        pthread_mutex_unlock(&hdl->vq_lock);
    }

    close(hdl->event_thrd_sock[READ_SOCKET]);
    close(hdl->event_thrd_sock[WRITE_SOCKET]);
    close(fds[0].fd);

exit:
    return (void *)(long)err;
}

/*
 * Valid state transitions for the VQ HAL
 *                       +--------+          +----------+
 *                       |  INIT  <---------->  DEINIT  |
 *                       +---^----+          +----------+
 *                           |
 *                           |
 *    +----------+     +-----v---------+
 *    |  PAUSED  <----->  RECOGNIZING  |
 *    +----------+     +------^--------+
 *                            |
 *                     +------v------+
 *                     |  STREAMING  |
 *                     +-------------+
 */
bool is_valid_state_transition(enum vq_hal_states old_state,
                               enum vq_hal_states new_state) {
    bool is_valid = true;

    ALOGE("%s: %s -> %s", __func__, g_state_str[old_state], g_state_str[new_state]);

    switch (old_state) {
        case INIT:
            if (new_state != DEINIT && new_state != RECOGNIZING)
                is_valid = false;
        break;
        case DEINIT:
            if (new_state != INIT)
                is_valid = false;
        break;
        case RECOGNIZING:
            if (new_state != PAUSED && new_state != INIT &&
                new_state != STREAMING) {
                    is_valid = false;
            }
        break;
        case PAUSED:
            if (new_state != RECOGNIZING)
                is_valid = false;
        break;
        case STREAMING:
            if (new_state != RECOGNIZING)
                is_valid = false;
        break;
        default:
            is_valid = false;
        break;
    }

    /* This is for PoC in Yukawa platforms */
    is_valid = true;
    return is_valid;
}

/**
 * Initialize the VQ HAL
 *
 * Input  - enable_host_side_buffering - When this is set - buffering will be
 *          started on the host when the keyword is detected.
 * Output - Handle to vq_hal on success, NULL on failure
 */
struct vq_hal* vq_hal_init(bool enable_host_side_buffering)
{
    struct vq_hal *hal = NULL;
    int err = 0;
    void *plg_hdl = NULL;

    ALOGE("%s: Start", __func__);
    // There will always be only one algo
    plg_hdl = vq_registrar[0].plg_funcs->init();
    if (NULL == plg_hdl) {
        ALOGE("%s: ERROR: Failed to init plugin", __func__);
        goto on_error;
    }

    hal = (struct vq_hal *) malloc(sizeof(struct vq_hal));
    if (NULL == hal) {
        ALOGE("%s: ERROR: Failed to allocate memory for VQ HAL", __func__);
        goto on_error;
    }

    hal->cb_cookie = NULL;
    hal->event_cb = NULL;
    hal->plg_hdl = plg_hdl;
    ALOGE("%s: Setting up the plugin functions", __func__);
    hal->vq_plg = vq_registrar[0].plg_funcs;

    err = pthread_mutex_init(&hal->vq_lock, NULL);
    if (0 != err) {
        ALOGE("%s: ERROR Failed to initialize the mutex lock with error %d(%s)",
                __func__, errno, strerror(errno));
        goto on_error;
    }

    // Create a thread to listen to the uevents
    pthread_create(&hal->event_thrd, (const pthread_attr_t *) NULL,
                   event_thread_loop, hal);

    if (enable_host_side_buffering) {
        hal->host_buffering = true;

        err = pthread_mutex_init(&hal->rb_lock, NULL);
        if (0 != err) {
            ALOGE("%s: ERROR Failed to initialize the mutex lock with error %d(%s)",
                    __func__, errno, strerror(errno));
            goto on_error;
        }

        err = pthread_cond_init(&hal->rb_underflow_sig, NULL);
        if (0 != err) {
            ALOGE("%s: ERROR Failed to initialize the cond with error %d(%s)",
                    __func__, errno, strerror(errno));
            goto on_error;
        }

        err = pthread_cond_init(&hal->rb_overflow_sig, NULL);
        if (0 != err) {
            ALOGE("%s: ERROR Failed to initialize the cond with error %d(%s)",
                    __func__, errno, strerror(errno));
            goto on_error;
        }

        pthread_create(&hal->buffer_thrd, (const pthread_attr_t *) NULL,
                    buffer_thread_loop, hal);
    } else {
        hal->host_buffering = false;
    }

    hal->state = INIT;
    hal->music_status = MUSIC_PLAYBACK_STOPPED;
    hal->send_music_status = false;

    return hal;

on_error:
    if (hal) {
        free(hal);
    }

    return NULL;
}

/**
 * De-Initialize the VQ HAL
 *
 * Input  - vq_hdl - Handle to vq_hal structure
 * Output - 0 on success, on failure < 0
 */
int vq_hal_deinit(struct vq_hal *hdl)
{
    int err = 0;
    int cmd = TERMINATE_CMD;

    if (NULL == hdl) {
        ALOGE("%s: ERROR NULL handle sent", __func__);
        err = -1;
        goto exit;
    }

    if (false == is_valid_state_transition(hdl->state, DEINIT)) {
        ALOGE("%s: ERROR Invalid state transition %s -> %s",
                __func__, g_state_str[hdl->state], g_state_str[DEINIT]);
        err = -EINVAL;
        goto exit;
    }

    pthread_mutex_lock(&hdl->vq_lock);

    err = hdl->vq_plg->deinit(hdl->plg_hdl);
    if (0 != err) {
        ALOGE("%s: ERROR: Failed to deinit plugin", __func__);
    }

    hdl->state = DEINIT;

    pthread_mutex_unlock(&hdl->vq_lock);
    write(hdl->event_thrd_sock[WRITE_SOCKET], &cmd, sizeof(cmd));
    pthread_join(hdl->event_thrd, NULL);
    if (true == hdl->host_buffering) {
        write(hdl->buffer_thrd_sock[WRITE_SOCKET], &cmd, sizeof(cmd));
        pthread_join(hdl->buffer_thrd, NULL);

        pthread_mutex_destroy(&hdl->rb_lock);
        pthread_cond_destroy(&hdl->rb_underflow_sig);
        pthread_cond_destroy(&hdl->rb_overflow_sig);
    }
    pthread_mutex_destroy(&hdl->vq_lock);

    free(hdl);

exit:
    return err;
}


/**
 * Start recognition of the keyword model
 *
 * Input  - vq_hdl - Handle to vq_hal structure
 *        - status - Status of the music playback
 * Output - 0 on success, on failure < 0
 */
int vq_start_recognition(struct vq_hal *hdl, vq_hal_music_status status)
{
    int err = 0;
#ifdef ENABLE_PROFILING
    struct timeval st,et;
    long time;
#endif

    if (NULL == hdl) {
        ALOGE("%s: ERROR Null handle sent", __func__);
        err = -EINVAL;
        goto exit;
    }

    if (false == is_valid_state_transition(hdl->state, RECOGNIZING)) {
        ALOGE("%s: ERROR Invalid state transition %s -> %s",
                __func__, g_state_str[hdl->state], g_state_str[RECOGNIZING]);
        err = -EINVAL;
        goto exit;
    }

    pthread_mutex_lock(&hdl->vq_lock);

#ifdef ENABLE_PROFILING
    gettimeofday(&st, NULL);
#endif
    hdl->music_status = status;
    err = hdl->vq_plg->start_recognition(hdl->plg_hdl, hdl->music_status);
#ifdef ENABLE_PROFILING
    gettimeofday(&et, NULL);
#endif
    if (0 != err) {
        ALOGE("%s: ERROR: Failed to start recognition for algo", __func__);
    } else {
        hdl->state = RECOGNIZING;
    }

#ifdef ENABLE_PROFILING
    time = ((et.tv_sec * 1000000LL) + et.tv_usec) -
            ((st.tv_sec * 1000000LL) + st.tv_usec);
    ALOGE("%s: Start Recognition took %ld micro seconds", __func__, time);
#endif

exit:
    if (hdl) {
        pthread_mutex_unlock(&hdl->vq_lock);
    }

    return err;
}

/**
 * Stop recognition of the keyword model
 *
 * Input  - vq_hdl - Handle to vq_hal structure
 * Output - 0 on success, on failure < 0
 */
int vq_stop_recognition(struct vq_hal *hdl)
{
    int ret = 0;
#ifdef ENABLE_PROFILING
    struct timeval st,et;
    long time;
#endif

    if (hdl == NULL) {
        ALOGE("%s: ERROR Null handle sent", __func__);
        ret = -EINVAL;
        goto exit;
    }

    if (false == is_valid_state_transition(hdl->state, INIT)) {
        ALOGE("%s: ERROR Invalid state transition %s -> %s",
                __func__, g_state_str[hdl->state], g_state_str[DEINIT]);
        ret = -EINVAL;
        goto exit;
    }

    pthread_mutex_lock(&hdl->vq_lock);

#ifdef ENABLE_PROFILING
    gettimeofday(&st, NULL);
#endif
    ret = hdl->vq_plg->stop_recognition(hdl->plg_hdl);
#ifdef ENABLE_PROFILING
    gettimeofday(&et, NULL);
#endif
    if (0 != ret) {
        ALOGE("%s: ERROR: Failed to stop recognition for algo", __func__);
    }

    hdl->state = INIT;

#ifdef ENABLE_PROFILING
    time = ((et.tv_sec * 1000000LL) + et.tv_usec) -
            ((st.tv_sec * 1000000LL) + st.tv_usec);
    ALOGE("%s: Stop Recognition took %ld micro seconds", __func__, time);
#endif

exit:
    if (hdl) {
        pthread_mutex_unlock(&hdl->vq_lock);
    }

    return ret;
}

/**
 * Register for callback from the VQ HAL. Events from the VQ HAL include -
 * keyword detections and errors if any.
 *
 * Input  - vq_hdl - Handle to vq_hal structure
 *        - cookie - Opaque pointer that an app would required to passed with
 *                   the callback
 *        - cb - Function pointer
 *
 * Output - 0 on success, on failure < 0
 */
int vq_register_cb(struct vq_hal *vq_hdl, void *cookie, vq_hal_event_cb cb)
{
    int err = 0;

    if (NULL == vq_hdl) {
        ALOGE("%s: ERROR Invalid handle sent", __func__);
        err = -1;
        goto exit;
    }

    pthread_mutex_lock(&vq_hdl->vq_lock);

    if (NULL != vq_hdl->event_cb) {
        ALOGE("%s: ERROR Callback is already registered", __func__);
        err = -1;
        goto exit;
    }

    vq_hdl->cb_cookie   = cookie;
    vq_hdl->event_cb    = cb;

exit:
    if (vq_hdl) {
        pthread_mutex_unlock(&vq_hdl->vq_lock);
    }

    return err;
}

/**
 * Un-Register for callback from the VQ HAL.
 *
 * Input  - vq_hdl - Handle to vq_hal structure
 * Output - 0 on success, on failure < 0
 */
int vq_unregister_cb(struct vq_hal *vq_hdl)
{
    int err = 0;

    if (NULL == vq_hdl) {
        ALOGE("%s: ERROR Invalid handle sent", __func__);
        err = -1;
        goto exit;
    }

    pthread_mutex_lock(&vq_hdl->vq_lock);

    if (NULL == vq_hdl->event_cb) {
        ALOGE("%s: ERROR Callback is not registered", __func__);
        err = -1;
        goto exit;
    }

    vq_hdl->cb_cookie   = NULL;
    vq_hdl->event_cb    = NULL;

exit:
    if (vq_hdl) {
        pthread_mutex_unlock(&vq_hdl->vq_lock);
    }

    return err;
}

/**
 * Start audio streaming
 *
 * Input  - vq_hdl - Handle to vq_hal structure
 *        - is_multiturn - For AVS, you can stream data either via mic or
 *                         buffer. If the plugin doesn't support AVS then this
 *                         field is ignored.
 * Output - 0 on success, on failure < 0
 */
int vq_start_audio(struct vq_hal *hdl, bool is_multiturn) {
    int err = 0;

    if (NULL == hdl) {
        ALOGE("%s: ERROR Null handle sent", __func__);
        err = -EINVAL;
        return err;
    }

    if (false == is_valid_state_transition(hdl->state, STREAMING)) {
        ALOGE("%s: ERROR Invalid state transition %s -> %s",
                __func__, g_state_str[hdl->state], g_state_str[STREAMING]);
        err = -EINVAL;
        goto exit;
    }

    if (true == is_multiturn || false == hdl->host_buffering) {
        pthread_mutex_lock(&hdl->vq_lock);
        hdl->is_multiturn = is_multiturn;

        err = hdl->vq_plg->start_audio(hdl->plg_hdl, is_multiturn);
        if (0 != err) {
            ALOGE("%s: ERROR: Failed to start audio", __func__);
        }

        pthread_mutex_unlock(&hdl->vq_lock);
    } else {
        if (false == hdl->is_buffering) {
            ALOGE("%s: ERROR: Buffering is not started!!", __func__);
            err = -1;
        } else {
            ALOGE("%s: Buffering in progress.", __func__);
        }
    }

    hdl->state = STREAMING;

exit:
    return err;
}

/**
 * Stop audio streaming
 *
 * Input  - vq_hdl - Handle to vq_hal structure
 * Output - 0 on success, on failure < 0
 */
int vq_stop_audio(struct vq_hal *hdl) {
    int err = 0;

    if (NULL == hdl) {
        ALOGE("%s: ERROR Null handle sent", __func__);
        err = -EINVAL;
        return err;
    }

    if (false == is_valid_state_transition(hdl->state, RECOGNIZING)) {
        ALOGE("%s: ERROR Invalid state transition %s -> %s",
                __func__, g_state_str[hdl->state], g_state_str[RECOGNIZING]);
        err = -EINVAL;
        goto exit;
    }

    if (true == hdl->is_multiturn || false == hdl->host_buffering) {
        pthread_mutex_lock(&hdl->vq_lock);

        err = hdl->vq_plg->stop_audio(hdl->plg_hdl);
        if (0 != err) {
            ALOGE("%s: ERROR: Failed to stop audio", __func__);
        }

        hdl->is_multiturn = false;
        if (hdl->send_music_status) {
            err = hdl->vq_plg->set_music_status(hdl->plg_hdl, hdl->music_status);
            hdl->send_music_status = false;
        }
        pthread_mutex_unlock(&hdl->vq_lock);
    } else {
        int cmd = STOP_AUDIO_BUFFERING;
        write(hdl->event_thrd_sock[WRITE_SOCKET], &cmd, sizeof(cmd));
    }

    hdl->state = RECOGNIZING;

exit:
    return err;
}

/**
 * Read audio data
 *
 * Input  - vq_hdl - Handle to vq_hal structure
 *        - buf - Buffer into which audio data is read into
 *        - buf_size - Size of the buffer
 * Output - Number of bytes read
 */
int vq_read_audio(struct vq_hal *hdl, void *buf, int buf_size) {
    int err = 0;

    if (NULL == hdl) {
        ALOGE("%s: ERROR Null handle sent", __func__);
        err = -EINVAL;
        return err;
    }

    if (STREAMING != hdl->state) {
        ALOGE("%s: ERROR Cannot get audio when not in STREAMING state", __func__);
        err = -EINVAL;
        return err;
    }

    if (true == hdl->is_multiturn || false == hdl->host_buffering) {
        pthread_mutex_lock(&hdl->vq_lock);
        err = hdl->vq_plg->read_audio(hdl->plg_hdl, buf, buf_size);
        pthread_mutex_unlock(&hdl->vq_lock);
    } else {
        err = read_from_rb(hdl, (unsigned char*)buf, buf_size);
    }

    return err;
}

/**
 * Get Audio frame length
 *
 * Input  - vq_hdl - Handle to vq_hal structure
 * Output - Audio frame length in milli seconds
 */
float vq_get_audio_frame_length(struct vq_hal *hdl) {
    float sz = 0.0f;

    if (NULL == hdl) {
        ALOGE("%s: ERROR Null handle sent", __func__);
        return sz;
    }

    pthread_mutex_lock(&hdl->vq_lock);
    sz = hdl->vq_plg->get_audio_frame_length(hdl->plg_hdl);
    pthread_mutex_unlock(&hdl->vq_lock);

    return sz;
}

/**
 * Pause recognition of the keyword model.
 * The microphone will not be used at this point however the algorithm
 * may remain in the memory to resume the recognition quickly.
 *
 * Input - vq_hdl - Handle to vq_hal structure
 * Output - 0 on success, on failure < 0
 */
int vq_pause_recognition(struct vq_hal *hdl) {
    int err = 0;

    if (NULL == hdl) {
        ALOGE("%s: ERROR Null handle sent", __func__);
        err = -EINVAL;
        return err;
    }

    if (false == is_valid_state_transition(hdl->state, PAUSED)) {
        ALOGE("%s: ERROR Invalid state transition %s -> %s",
                __func__, g_state_str[hdl->state], g_state_str[PAUSED]);
        err = -EINVAL;
        goto exit;
    }

    pthread_mutex_lock(&hdl->vq_lock);
    err = hdl->vq_plg->pause_recognition(hdl->plg_hdl);
    hdl->state = PAUSED;
    pthread_mutex_unlock(&hdl->vq_lock);

exit:
    return err;
}

/**
 * Resume recognition of the keyword model.
 * This can be called only the recognition is paused. The algorithm
 * will start analysing the microphone data again.
 *
 * Input - vq_hdl - Handle to vq_hal structure
 * Output - 0 on success, on failure < 0
 */
int vq_resume_recognition(struct vq_hal *hdl) {
    int err = 0;

    if (NULL == hdl) {
        ALOGE("%s: ERROR Null handle sent", __func__);
        err = -EINVAL;
        return err;
    }

    if (false == is_valid_state_transition(hdl->state, RECOGNIZING)) {
        ALOGE("%s: ERROR Invalid state transition %s -> %s",
                __func__, g_state_str[hdl->state], g_state_str[RECOGNIZING]);
        err = -EINVAL;
        goto exit;
    }

    pthread_mutex_lock(&hdl->vq_lock);
    err = hdl->vq_plg->resume_recognition(hdl->plg_hdl);
    hdl->state = RECOGNIZING;
    if (hdl->send_music_status) {
        err = hdl->vq_plg->set_music_status(hdl->plg_hdl, hdl->music_status);
        hdl->send_music_status = false;
    }
    pthread_mutex_unlock(&hdl->vq_lock);

exit:
    return err;
}

/**
 * Set the audio music playback status
 *
 * Input  - vq_hdl - Handle to vq_hal structure
 *          status - Depicts the status of the music playback
 * Output - 0 on success, on failure < 0
 */
int vq_set_music_status(struct vq_hal *hdl, vq_hal_music_status status) {
    int err = 0;

    if (NULL == hdl) {
        ALOGE("%s: ERROR Null handle sent", __func__);
        err = -EINVAL;
        goto exit;
    }

    if (DEINIT == hdl->state) {
        ALOGE("%s: ERROR Invalid state %s", __func__, g_state_str[DEINIT]);
        err = -EINVAL;
        goto exit;
    }

    hdl->music_status = status;
    switch(hdl->state) {
        case STREAMING:
        case PAUSED:
            hdl->send_music_status = true;
        break;
        case RECOGNIZING:
            pthread_mutex_lock(&hdl->vq_lock);
            ALOGE("%s: Calling set music satus", __func__);
            err = hdl->vq_plg->set_music_status(hdl->plg_hdl, status);
            pthread_mutex_unlock(&hdl->vq_lock);
        break;
        case INIT:
        default:
            // Do nothing
        break;
    }

exit:
    return err;
}
