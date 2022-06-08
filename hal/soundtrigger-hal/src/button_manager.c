#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <poll.h>
#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <linux/input.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "button_manager.h"
#include "logger.h"

#define INPUT_DEVICE         "/dev/input/event1"

#define TERMINATE_CMD        1
#define READ_SOCKET          1
#define WRITE_SOCKET         0
#define EVT_BUF_SIZE         32
// Where did we get this magic num from?!
#define MAGIC_NUM            7
// Long press button duration 3 seconds
#define LONG_PRESS_DURATION  (3 * MAGIC_NUM)
// Key press event types
#define KEY_RELEASE          0
#define KEY_PRESS            1
#define KEY_HOLD             2

struct button_mgr {
    pthread_t event_thrd;
    int event_thrd_sock[2];

    button_mgr_event_cb event_cb;
    void *cb_cookie;
};

static void *event_thread_loop(void *context)
{
    struct button_mgr *hdl = (struct button_mgr*) context;
    struct pollfd pfd[2];
    int err = 0;
    bool should_exit = false;
    struct input_event ev[EVT_BUF_SIZE];
    int long_press_cnt = 0;
    int rd = 0, i = 0;

    if (NULL == hdl) {
        ALOGE("%s: ERROR: Invalid handle passed", __func__);
        err = -1;
        goto exit;
    }

    memset(&pfd, 0, sizeof(struct pollfd) * 2);
    int timeout = -1; // Wait for event indefinitely
    pfd[0].events = POLLIN;
    pfd[0].fd = hdl->event_thrd_sock[READ_SOCKET];

    pfd[1].events = POLLIN;
    pfd[1].fd = open(INPUT_DEVICE, O_RDONLY);
    if (pfd[1].fd < 0) {
        ALOGE("%s: Failed to open the %s device", __func__, INPUT_DEVICE);
        err = -EINVAL;
        goto exit;
    }

    while (false == should_exit) {
        err = poll (pfd, 2, timeout);
        if (err < 0) {
            ALOGE("%s: Error in poll: %d (%s)", __func__, errno, strerror(errno));
            break;
        }

        if (pfd[0].revents & POLLIN) {
            int cmd;
            read(pfd[0].fd, &cmd, sizeof(cmd));
            should_exit = true;
            ALOGE("%s: Termniation command exiting now", __func__);
        } else if (pfd[1].revents & POLLIN) {
            rd = read(pfd[1].fd, ev, sizeof(struct input_event) * EVT_BUF_SIZE);
            if (rd < (int) sizeof(struct input_event)) {
                ALOGE("%s: error reading", __func__);
                continue;
            }

            for (i = 0; i < rd / sizeof(struct input_event); i++) {
                /* Key release event */
                if (ev[i].type == EV_KEY && (ev[i].value == KEY_RELEASE)) {
                    /* consider short press if long press counter was not reached */
                    if(long_press_cnt < LONG_PRESS_DURATION) {
                        ALOGE("### SHORT KEY PRESSED code %d ###",ev[i].code);
                        switch (ev[i].code){
                            case KEY_MUTE:
                                hdl->event_cb(hdl->cb_cookie,
                                              SHORTPRESS_KEY_MUTE,
                                              NULL);
                            break;
                            case KEY_VOLUMEUP:
                                 hdl->event_cb(hdl->cb_cookie,
                                              SHORTPRESS_KEY_VOLUME_UP,
                                              NULL);
                            break;
                            case KEY_VOLUMEDOWN:
                                 hdl->event_cb(hdl->cb_cookie,
                                              SHORTPRESS_KEY_VOLUME_DOWN,
                                              NULL);
                            break;
                            default:
                                ALOGE("%s: Unknown Key pressed", __func__);
                            break;
                        }
                    }
                    /* reset long press counter */
                    long_press_cnt = 0;
                /* Key press hold event */
                } else if (ev[i].type == EV_KEY && (ev[i].value == KEY_HOLD)) {
                    /* increase counter to check if key press is long or short */
                    long_press_cnt ++;
                    /* consider long press only if counter reaches to threshold */
                    if (long_press_cnt == LONG_PRESS_DURATION) {
                        ALOGE("### LONG KEY PRESSED code %d ###",ev[i].code);
                        switch (ev[i].code) {
                            case KEY_MUTE:
                                hdl->event_cb(hdl->cb_cookie,
                                              LONGPRESS_KEY_MUTE,
                                              NULL);
                            break;
                            case KEY_VOLUMEUP:
                                hdl->event_cb(hdl->cb_cookie,
                                              LONGPRESS_KEY_VOLUME_UP,
                                              NULL);
                            break;
                            case KEY_VOLUMEDOWN:
                                hdl->event_cb(hdl->cb_cookie,
                                              LONGPRESS_KEY_VOLUME_DOWN,
                                              NULL);
                            break;
                            default:
                                ALOGE("%s: Unknown Key pressed", __func__);
                            break;
                        }
                    }
                /* Key press event */
                } else if (ev[i].type == EV_KEY && (ev[i].value == KEY_PRESS)) {
                    /* reset long press counter */
                    long_press_cnt = 0;
                }
            }
        } else {
            ALOGE("%s: Unknown poll event", __func__);
        }
    }

exit:
    return (void *)(long)err;
}

/**
 * Initialize the Button Manager
 *
 * Input  - NA
 * Output - Handle to Button Manager on success, NULL on failure
 */
struct button_mgr* button_mgr_init() {
    struct button_mgr *hdl = NULL;

    hdl = (struct button_mgr *) malloc(sizeof(struct button_mgr));
    if (NULL == hdl) {
        ALOGE("%s: ERROR: Failed to allocate memory for Button Manager", __func__);
        goto on_error;
    }

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, hdl->event_thrd_sock) == -1) {
        ALOGE("%s: Failed to create termination socket", __func__);
        goto on_error;
    }

    pthread_create(&hdl->event_thrd, (const pthread_attr_t *) NULL,
                   event_thread_loop, hdl);

    return hdl;

on_error:
    if (hdl) {
        free(hdl);
    }

    return NULL;
}

/**
 * De-Initialize the Button Manager
 *
 * Input  - button_mgr - Handle to button_mgr structure
 * Output - 0 on success, on failure < 0
 */
int button_mgr_deinit(struct button_mgr *hdl) {
    int err = 0;

    if (NULL == hdl) {
        ALOGE("%s: ERROR NULL handle sent", __func__);
        err = -1;
        goto exit;
    }

    int cmd = TERMINATE_CMD;
    write(hdl->event_thrd_sock[WRITE_SOCKET], &cmd, sizeof(cmd));
    pthread_join(hdl->event_thrd, NULL);

    close(hdl->event_thrd_sock[WRITE_SOCKET]);
    close(hdl->event_thrd_sock[READ_SOCKET]);
    free(hdl);

exit:
    return err;
}

/**
 * Register for callback from the Button Manager.
 *
 * Input  - button_mgr - Handle to button_mgr structure
 *        - cookie - Opaque pointer that an app would required to passed with
 *                   the callback
 *        - cb - Function pointer
 *
 * Output - 0 on success, on failure < 0
 */
int button_mgr_register_cb(struct button_mgr *hdl, void *cookie,
                           button_mgr_event_cb cb) {
    int err = 0;

    if (NULL == hdl) {
        ALOGE("%s: ERROR Invalid handle sent", __func__);
        err = -1;
        goto exit;
    }

    if (NULL != hdl->event_cb) {
        ALOGE("%s: ERROR Callback is already registered", __func__);
        err = -1;
        goto exit;
    }

    hdl->cb_cookie   = cookie;
    hdl->event_cb    = cb;

exit:
    return err;
}

/**
 * Un-Register for callback from the Button Manager.
 *
 * Input  - button_mgr - Handle to button_mgr structure
 * Output - 0 on success, on failure < 0
 */
int button_mgr_unregister_cb(struct button_mgr *hdl) {
    int err = 0;

    if (NULL == hdl) {
        ALOGE("%s: ERROR Invalid handle sent", __func__);
        err = -1;
        goto exit;
    }

    if (NULL == hdl->event_cb) {
        ALOGE("%s: ERROR Callback is not registered", __func__);
        err = -1;
        goto exit;
    }

    hdl->cb_cookie   = NULL;
    hdl->event_cb    = NULL;

exit:
    return err;
}