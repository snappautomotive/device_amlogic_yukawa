/*
 * Copyright (C) 2020 Knowles Electronics
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "SoundTriggerHAL"
#define LOG_NDEBUG 0

#include <errno.h>
#include <sys/socket.h>
#include <poll.h>
#include <pthread.h>
#include <malloc.h>
#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>
#include <log/log.h>
#include "vq_hal.h"

#include <hardware/hardware.h>
#include <hardware/sound_trigger.h>

#define MAX_GENERIC_SOUND_MODELS    (1)
#define MAX_KEY_PHRASES             (1)
#define MAX_MODELS                  (MAX_GENERIC_SOUND_MODELS + MAX_KEY_PHRASES)

#define MAX_USERS                   (1)
#define MAX_BUFFER_MS               (3000)
#define POWER_CONSUMPTION           (0) // TBD

#define OK_GOOGLE_KW_ID             (0)
#define OEM_MODEL_KW_ID             (1)
#define OEM_MODEL                   "d2122d45-4124-4817-b9b1-3f9e63393412"

#define IS_HOST_SIDE_BUFFERING_ENABLED  (false)

#define READ_SOCKET                 1
#define WRITE_SOCKET                0
#define FW_RECOVERY                 60
#define TERMINATE_CMD               61

static struct sound_trigger_properties_extended_1_3 hw_properties = {
    {
        SOUND_TRIGGER_DEVICE_API_VERSION_1_3, //ST version
        sizeof(struct sound_trigger_properties_extended_1_3)
    },
    {
        "Knowles Electronics",      // implementor
        "Continuous VoiceQ",        // description
        1,                          // library version
        // Version UUID
        { 0x80f7dcd5, 0xbb62, 0x4816, 0xa931, {0x9c, 0xaa, 0x52, 0x5d, 0xf5, 0xc7}},
        MAX_MODELS,                 // max_sound_models
        MAX_KEY_PHRASES,            // max_key_phrases
        MAX_USERS,                  // max_users
        RECOGNITION_MODE_VOICE_TRIGGER | // recognition_mode
        RECOGNITION_MODE_GENERIC_TRIGGER,
        true,                       // capture_transition
        MAX_BUFFER_MS,              // max_capture_ms
        true,                       // concurrent_capture
        false,                      // trigger_in_event
        POWER_CONSUMPTION           // power_consumption_mw
    },
    "", //supported arch
    0,                              // audio capability
};

struct model_info {
    void *recognition_cookie;
    void *sound_model_cookie;
    sound_model_handle_t model_handle;
    sound_trigger_uuid_t uuid;
    recognition_callback_t recognition_callback;
    sound_model_callback_t sound_model_callback;
    struct sound_trigger_recognition_config *config;
    int kw_id;
    sound_trigger_sound_model_type_t type;

    void *data;
    int data_sz;
    bool is_loaded;
    bool is_active;
    bool is_state_query;
};

struct knowles_sound_trigger_device {
    struct sound_trigger_hw_device device;
    struct model_info models[MAX_MODELS];
    int opened;
    struct sound_trigger_recognition_config *last_keyword_detected_config;
    sound_trigger_uuid_t oem_model_uuid;
    bool is_st_hal_ready;
    bool is_st_streaming;
    bool is_kwd_loaded;

    //VQ HAL handle
    struct vq_hal *vq_hdl;
    pthread_mutex_t lock;
    pthread_t event_thread;
    int event_thrd_sock[2];
};

/*
 * Since there's only ever one sound_trigger_device, keep it as a global so
 * that other people can dlopen this lib to get at the streaming audio.
 */

static struct knowles_sound_trigger_device g_stdev =
{
    .lock = PTHREAD_MUTEX_INITIALIZER
};

/* static bool check_uuid_equality(sound_trigger_uuid_t uuid1,
                                sound_trigger_uuid_t uuid2)
{
    if (uuid1.timeLow != uuid2.timeLow ||
        uuid1.timeMid != uuid2.timeMid ||
        uuid1.timeHiAndVersion != uuid2.timeHiAndVersion ||
        uuid1.clockSeq != uuid2.clockSeq) {
        return false;
    }

    for (int i = 0; i < 6; i++) {
        if(uuid1.node[i] != uuid2.node[i]) {
            return false;
        }
    }

    return true;
}*/

bool str_to_uuid(char* uuid_str, sound_trigger_uuid_t* uuid)
{
    if (uuid_str == NULL) {
        ALOGI("Invalid str_to_uuid input.");
        return false;
    }

    int tmp[10];
    if (sscanf(uuid_str, "%08x-%04x-%04x-%04x-%02x%02x%02x%02x%02x%02x",
            tmp, tmp+1, tmp+2, tmp+3, tmp+4, tmp+5,
            tmp+6, tmp+7, tmp+8, tmp+9) < 10) {
        ALOGI("Invalid UUID, got: %s", uuid_str);
        return false;
    }
    uuid->timeLow = (unsigned int)tmp[0];
    uuid->timeMid = (unsigned short)tmp[1];
    uuid->timeHiAndVersion = (unsigned short)tmp[2];
    uuid->clockSeq = (unsigned short)tmp[3];
    uuid->node[0] = (unsigned char)tmp[4];
    uuid->node[1] = (unsigned char)tmp[5];
    uuid->node[2] = (unsigned char)tmp[6];
    uuid->node[3] = (unsigned char)tmp[7];
    uuid->node[4] = (unsigned char)tmp[8];
    uuid->node[5] = (unsigned char)tmp[9];

    return true;
}

static int find_empty_model_slot(struct knowles_sound_trigger_device *st_dev)
{
    int i = -1;
    for (i = 0; i < MAX_MODELS; i++) {
        if (st_dev->models[i].is_loaded == false)
            break;
    }

    if (i >= MAX_MODELS) {
        i = -1;
    }

    return i;
}

static int find_handle_for_kw_id(
                        struct knowles_sound_trigger_device *st_dev, int kw_id)
{
    int i = 0;
    for (i = 0; i < MAX_MODELS; i++) {
        if (kw_id == st_dev->models[i].kw_id)
            break;
    }

    return i;
}

static int stdev_get_properties(
                            const struct sound_trigger_hw_device *dev __unused,
                            struct sound_trigger_properties *properties)
{
    ALOGV("+%s+", __func__);
    if (properties == NULL)
        return -EINVAL;
    memcpy(properties, &hw_properties.base, sizeof(*properties));
    ALOGV("-%s-", __func__);
    return 0;
}

static const struct sound_trigger_properties_header* stdev_get_properties_extended(
                                const struct sound_trigger_hw_device *dev)
{
    struct knowles_sound_trigger_device *stdev =
        (struct knowles_sound_trigger_device *)dev;

    ALOGV("+%s+", __func__);
    pthread_mutex_lock(&stdev->lock);
    if (hw_properties.header.version >= SOUND_TRIGGER_DEVICE_API_VERSION_1_3) {
        hw_properties.base.version = 0;

        /* Per GMS requirement new to Android R, the supported_model_arch field
           must be the Google hotword firmware version comma separated with the
           supported_model_arch platform identifier.
         */
        //int_prefix_str(hw_properties.supported_model_arch, SOUND_TRIGGER_MAX_STRING_LEN, hw_properties.base.version, "%u,");
        ALOGW("SoundTrigger supported model arch identifier");
    } else {
        ALOGE("STHAL Version is %u", hw_properties.header.version);
        pthread_mutex_unlock(&stdev->lock);
        return NULL;
    }

    pthread_mutex_unlock(&stdev->lock);
    ALOGV("-%s-", __func__);
    return &hw_properties.header;
}

bool isRouteSetup = false;
static int stdev_load_sound_model(const struct sound_trigger_hw_device *dev,
                                struct sound_trigger_sound_model *sound_model,
                                sound_model_callback_t callback,
                                void *cookie,
                                sound_model_handle_t *handle)
{
    struct knowles_sound_trigger_device *stdev =
        (struct knowles_sound_trigger_device *)dev;
    int ret = 0;
    int kw_model_sz = 0;
    int i = 0;

    unsigned char *kw_buffer = NULL;

    ALOGD("+%s+", __func__);
    pthread_mutex_lock(&stdev->lock);

    if (stdev->is_st_hal_ready == false) {
        ALOGE("%s: ST HAL is not ready yet", __func__);
        ret = -EAGAIN;
        goto exit;
    }

    if (handle == NULL || sound_model == NULL) {
        ALOGE("%s: handle/sound_model is NULL", __func__);
        ret = -EINVAL;
        goto exit;
    }

    if (sound_model->data_size == 0 ||
        sound_model->data_offset < sizeof(struct sound_trigger_sound_model)) {
        ALOGE("%s: Invalid sound model data", __func__);
        ret = -EINVAL;
        goto exit;
    }

    // Find an empty slot to load the model
    i = find_empty_model_slot(stdev);
    if (i == -1) {
        ALOGE("%s: Can't load model no free slots available", __func__);
        ret = -ENOSYS;
        goto exit;
    }

    kw_buffer = (unsigned char *) sound_model + sound_model->data_offset;
    kw_model_sz = sound_model->data_size;
    ALOGV("%s: kw_model_sz %d", __func__, kw_model_sz);

    stdev->models[i].data = malloc(kw_model_sz);
    if (stdev->models[i].data == NULL) {
        stdev->models[i].data_sz = 0;
        ALOGE("%s: could not allocate memory for keyword model data",
            __func__);
        ret = -ENOMEM;
        goto exit;
    } else {
        memcpy(stdev->models[i].data, kw_buffer, kw_model_sz);
        stdev->models[i].data_sz = kw_model_sz;
    }
/*
    if (check_uuid_equality(sound_model->uuid,
                            stdev->oem_model_uuid)) {
        stdev->models[i].kw_id = OEM_MODEL_KW_ID;
    } else {
        ALOGE("%s: ERROR: unknown keyword model file", __func__);
        ret = -EINVAL;
        goto error;
    }
*/
    *handle = i;
    ALOGV("%s: Loading keyword model handle(%d) type(%d)", __func__,
        *handle, sound_model->type);

    stdev->models[i].model_handle = *handle;
    stdev->models[i].type = sound_model->type;
    stdev->models[i].uuid = sound_model->vendor_uuid;
    stdev->models[i].sound_model_callback = callback;
    stdev->models[i].sound_model_cookie = cookie;
    stdev->models[i].recognition_callback = NULL;
    stdev->models[i].recognition_cookie = NULL;

    stdev->models[i].is_loaded = true;
    /*
    We need to start recognition when the load model is called, because, if
    multi trigger/multiple recognition option is enabled in the ST APP, we
    might get the start recognition in between while streaming. So if we try
    to start the recognition at that point again, we will endup setting up the
    route multiple times, which may lead to a crash.
    ret = vq_start_recognition(stdev->vq_hdl,MUSIC_PLAYBACK_STOPPED);
     if (0 != ret){
         ALOGE("%s: ERROR Failed to start recognition", __func__);
     } else
    */
    g_stdev.is_kwd_loaded = true;

//error:
    if (ret != 0) {
        if (stdev->models[i].data) {
            free(stdev->models[i].data);
            stdev->models[i].data = NULL;
            stdev->models[i].data_sz = 0;
        }
    }
    if(isRouteSetup == false)
    {
        vq_start_recognition(g_stdev.vq_hdl, MUSIC_PLAYBACK_STOPPED);
        isRouteSetup = true;
    }
exit:
    pthread_mutex_unlock(&stdev->lock);
    ALOGD("-%s handle %d-", __func__, *handle);
    return ret;
}

static int stdev_unload_sound_model(const struct sound_trigger_hw_device *dev,
                                    sound_model_handle_t handle)
{
    struct knowles_sound_trigger_device *stdev =
        (struct knowles_sound_trigger_device *)dev;
    int ret = 0;
    ALOGD("+%s handle %d+", __func__, handle);
    pthread_mutex_lock(&stdev->lock);

    if (stdev->is_st_hal_ready == false) {
        ALOGE("%s: ST HAL is not ready yet", __func__);
        ret = -EAGAIN;
        goto exit;
    }

    // Just confirm the model was previously loaded
    if (stdev->models[handle].is_loaded == false) {
        ALOGE("%s: Invalid model(%d) being called for unload",
                __func__, handle);
        ret = -EINVAL;
        goto exit;
    }

    if (stdev->models[handle].is_active == true) {
        ALOGE("%s: ERROR unload is called without stopping the model", __func__);
        ret = -EINVAL;
        goto exit;
    }

    stdev->models[handle].sound_model_callback = NULL;
    stdev->models[handle].sound_model_cookie = NULL;

    if (stdev->models[handle].data) {
        free(stdev->models[handle].data);
        stdev->models[handle].data = NULL;
        stdev->models[handle].data_sz = 0;
    }

    //We need to stop recognition when the unload model is called, because, if
    //multi trigger/multiple recognition option is enabled in the ST APP, we
    //might get the stop recognition in between while streaming. So if we try
    //to stop the recognition at that point again, we will endup tearing down
    //the route multiple times, which may lead to a crash.
    /*
    ret = vq_stop_recognition(stdev->vq_hdl);
    if (0 != ret) {
        ALOGE("%s: ERROR Failed to stop recognition", __func__);
        ret = -EINVAL;
        goto exit;
    }
     */

    stdev->models[handle].is_loaded = false;
    g_stdev.is_kwd_loaded = false;

    ALOGD("%s: Successfully unloaded the model, handle - %d",
        __func__, handle);
exit:
    pthread_mutex_unlock(&stdev->lock);
    ALOGD("-%s handle %d-", __func__, handle);
    return ret;
}

static int stdev_start_recognition(
                        const struct sound_trigger_hw_device *dev,
                        sound_model_handle_t handle,
                        const struct sound_trigger_recognition_config *config,
                        recognition_callback_t callback,
                        void *cookie)
{
    struct knowles_sound_trigger_device *stdev =
        (struct knowles_sound_trigger_device *)dev;
    int status = 0;
    struct model_info *model = &stdev->models[handle];

    ALOGD("%s stdev %p, sound model %d", __func__, stdev, handle);
    static int i=0;
    i++;
    pthread_mutex_lock(&stdev->lock);

    if (stdev->is_st_hal_ready == false) {
        ALOGE("%s: ST HAL is not ready yet", __func__);
        status = -EAGAIN;
        goto exit;
    }

    if (callback == NULL) {
        ALOGE("%s: recognition_callback is null", __func__);
        status = -EINVAL;
        goto exit;
    }

    // Just confirm the model was previously loaded
    if (model->is_loaded == false) {
        ALOGE("%s: Invalid model(%d) being called for start recognition",
                __func__, handle);
        status = -EINVAL;
        goto exit;
    }

    if (model->config != NULL) {
        free(model->config);
        model->config = NULL;
    }

    if (config != NULL) {
        model->config = (struct sound_trigger_recognition_config *)
                            malloc(sizeof(*config));
        if (model->config == NULL) {
            ALOGE("%s: Failed to allocate memory for model config", __func__);
            status = -ENOMEM;
            goto exit;
        }
        memcpy(model->config, config, sizeof(*config));

        ALOGD("%s: Is capture requested %d",
            __func__, config->capture_requested);
    } else {
        ALOGD("%s: config is null", __func__);
        model->config = NULL;
    }

    model->recognition_callback = callback;
    model->recognition_cookie = cookie;

    ALOGE("%s: model->is_active=%d",__func__,model->is_active);
    if (model->is_active == true) {
        // This model is already active, do nothing except updating callbacks,
        // configs and cookie
        goto exit;
    }

    model->is_active = true;
exit:
    pthread_mutex_unlock(&stdev->lock);
    ALOGD("-%s sound model %d-", __func__, handle);
    return status;
}

static int stdev_stop_recognition(
                        const struct sound_trigger_hw_device *dev,
                        sound_model_handle_t handle)
{
    struct knowles_sound_trigger_device *stdev =
        (struct knowles_sound_trigger_device *)dev;
    struct model_info *model = &stdev->models[handle];
    int status = 0;

    pthread_mutex_lock(&stdev->lock);
    ALOGD("+%s sound model %d+", __func__, handle);

    if (stdev->is_st_hal_ready == false) {
        ALOGE("%s: ST HAL is not ready yet", __func__);
        status = -EAGAIN;
        goto exit;
    }

    if (model->config != NULL) {
        free(model->config);
        model->config = NULL;
    }

    model->recognition_callback = NULL;
    model->recognition_cookie = NULL;
    model->is_active = false;
exit:

    ALOGD("-%s sound model %d-", __func__, handle);
    pthread_mutex_unlock(&stdev->lock);

    return status;
}

static int stdev_close(hw_device_t *device)
{
    struct knowles_sound_trigger_device *stdev =
        (struct knowles_sound_trigger_device *)device;
    int ret = 0;
    int cmd = TERMINATE_CMD;
    ALOGD("+%s+", __func__);
    pthread_mutex_lock(&stdev->lock);

    if (!stdev->opened) {
        ALOGE("%s: device already closed", __func__);
        ret = -EFAULT;
        goto exit;
    }

    if (stdev->is_st_hal_ready == false) {
        ALOGE("%s: ST HAL is not ready yet", __func__);
        ret = -EAGAIN;
        goto exit;
    }

    ret = vq_unregister_cb(stdev->vq_hdl);
    if (0 != ret) {
        ALOGE("%s: ERROR Failed to unregister vq cb", __func__);
        ret = -EAGAIN;
        goto exit;
    }

    ret = vq_hal_deinit(stdev->vq_hdl);
    if (0 != ret) {
        ALOGE("%s: ERROR Failed to deinit vq hal", __func__);
        ret = -EAGAIN;
        goto exit;
    }

    write(stdev->event_thrd_sock[WRITE_SOCKET], &cmd, sizeof(cmd));
    pthread_join(stdev->event_thread, NULL);
    stdev->opened = false;
exit:
    pthread_mutex_unlock(&stdev->lock);
    ALOGD("-%s-", __func__);
    return ret;
}

__attribute__ ((visibility ("default")))
audio_io_handle_t stdev_get_audio_handle()
{
    if (g_stdev.last_keyword_detected_config == NULL) {
        ALOGI("%s: Config is NULL so returning audio handle as 0", __func__);
        return 0;
    }

    ALOGI("%s: Audio Handle is %d",
        __func__, g_stdev.last_keyword_detected_config->capture_handle);

    return g_stdev.last_keyword_detected_config->capture_handle;
}

static char *stdev_keyphrase_event_alloc(sound_model_handle_t handle,
                                struct sound_trigger_recognition_config *config,
                                int recognition_status)
{
    char *data;
    struct sound_trigger_phrase_recognition_event *event;
    data = (char *)calloc(1,
                        sizeof(struct sound_trigger_phrase_recognition_event));
    if (!data)
        return NULL;
    event = (struct sound_trigger_phrase_recognition_event *)data;
    event->common.status = recognition_status;
    event->common.type = SOUND_MODEL_TYPE_KEYPHRASE;
    event->common.model = handle;
    event->common.capture_available = false;

    if (config) {
        unsigned int i;

        event->num_phrases = config->num_phrases;
        if (event->num_phrases > SOUND_TRIGGER_MAX_PHRASES)
            event->num_phrases = SOUND_TRIGGER_MAX_PHRASES;
        for (i = 0; i < event->num_phrases; i++)
            memcpy(&event->phrase_extras[i],
                &config->phrases[i],
                sizeof(struct sound_trigger_phrase_recognition_extra));
    }

    event->num_phrases = 1;
    event->phrase_extras[0].confidence_level = 100;
    event->phrase_extras[0].num_levels = 1;
    event->phrase_extras[0].levels[0].level = 100;
    event->phrase_extras[0].levels[0].user_id = 0;
    /*
     * Signify that all the data is comming through streaming
     * and not through the buffer.
     */
    event->common.capture_available = true;
    event->common.capture_delay_ms = 0;
    event->common.capture_preamble_ms = 0;
    event->common.audio_config = AUDIO_CONFIG_INITIALIZER;
    event->common.audio_config.sample_rate = 16000;
    event->common.audio_config.channel_mask = AUDIO_CHANNEL_IN_STEREO;
    event->common.audio_config.format = AUDIO_FORMAT_PCM_16_BIT;

    return data;
}

static char *stdev_generic_event_alloc(int model_handle,
                                                  void *payload,
                                                  unsigned int payload_size,
                                                  int recognition_status)
{
    ALOGD("+%s+", __func__);
    char *data;
    struct sound_trigger_generic_recognition_event *event;

    data = (char *)calloc(1,
                        sizeof(struct sound_trigger_generic_recognition_event) +
                        payload_size);
    if (!data) {
        ALOGE("%s: Failed to allocate memory for recog event", __func__);
        return NULL;
    }

    event = (struct sound_trigger_generic_recognition_event *)data;
    event->common.status = recognition_status;
    event->common.type   = SOUND_MODEL_TYPE_GENERIC;
    event->common.model  = model_handle;

    // Signify that all the data is comming through streaming and
    // not through the buffer.
    event->common.capture_available         = true;
    event->common.audio_config              = AUDIO_CONFIG_INITIALIZER;
    event->common.audio_config.sample_rate  = 16000;
    event->common.audio_config.channel_mask = AUDIO_CHANNEL_IN_STEREO;
    event->common.audio_config.format       = AUDIO_FORMAT_PCM_16_BIT;

    if (payload && payload_size > 0) {
        event->common.data_size = payload_size;
        event->common.data_offset =
                        sizeof(struct sound_trigger_generic_recognition_event);

        memcpy((data + event->common.data_offset), payload, payload_size);
    }
    ALOGD("-%s-", __func__);
    return data;
}

// stdev needs to be locked before calling this function
static int restart_recognition(struct knowles_sound_trigger_device *stdev)
{
    int err = 0;
    int i = 0;

    // Download all the keyword models files that were previously loaded
    for (i = 0; i < MAX_MODELS; i++) {
        if (stdev->models[i].is_active == true) {
            if ( 1/* TODO: check_uuid_equality(stdev->models[i].uuid,
                    stdev->oem_model_uuid)*/) {
                err = vq_start_recognition(stdev->vq_hdl,
                                           MUSIC_PLAYBACK_STOPPED);
                if (0 != err){
                    ALOGE("%s: ERROR Failed to start recognition", __func__);
                    goto exit;
                }

            }
        }
    }
exit:
    return err;
}

// stdev needs to be locked before calling this function
static int crash_recovery(struct knowles_sound_trigger_device *stdev)
{
    int err = 0;

    // Redownload the keyword model files and start recognition
    err = restart_recognition(stdev);
    if (err != 0) {
        ALOGE("%s: ERROR: Failed to download the keyword models and restarting"
            " recognition", __func__);
        goto exit;
    }

    // Reset the flag only after successful recovery
    stdev->is_st_hal_ready = true;

exit:
    return err;
}

static void* st_event_thread(void *context)
{
    int err;
    struct knowles_sound_trigger_device *stdev =
        (struct knowles_sound_trigger_device *)context;

    struct pollfd pfd;
    bool should_exit = false;

    if (stdev == NULL) {
        ALOGE("%s ERROR Invalid handle passed\n", __func__);
        err = -1;
        return (void *)(long)err;
    }

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, stdev->event_thrd_sock) == -1) {
        ALOGE("%s: Failed to create socket", __func__);
        err = -1;
        goto exit;
    }

    memset(&pfd, 0, sizeof(struct pollfd));
    pfd.events = POLLIN;
    pfd.fd = stdev->event_thrd_sock[READ_SOCKET];

    ALOGE("Litening for events");
    while (false == should_exit) {
        err = poll(&pfd, 1, -1);
        if (err < 0) {
            ALOGE("%s: Error in poll: %d", __func__, errno);
            break;
        }

        if (pfd.revents & POLLIN) {
            int cmd;
            read(pfd.fd, &cmd, sizeof(cmd));
            switch(cmd) {
                case FW_RECOVERY:
                   err =  crash_recovery(stdev);
                   if (err != 0)
                       ALOGE("Crash recovery failed");
                   else
                       ALOGE("%s: Recovered from firmware crash", __func__);

                   break;
                case TERMINATE_CMD:
                   ALOGE("%s: Termination message", __func__);
                   should_exit = true;
                   break;
                default:
                   ALOGE("%s: Uknown cmd, ignoring",  __func__);
                   break;
            }
        } else {
            ALOGE("%s:  Message ignored", __func__);
        }
    }
exit:
    close(stdev->event_thrd_sock[READ_SOCKET]);
    close(stdev->event_thrd_sock[WRITE_SOCKET]);
    return (void *)(long)err;
}

static int vq_cb(void *cookie, vq_hal_event_type evt, void *evt_data)
{
    int idx =0;
    int kwid = 0;
    int recognition_status = 0;
    struct event_data *ed;
    void *payload = NULL;
    unsigned int payload_size = 0;

    struct knowles_sound_trigger_device *stdev =
        (struct knowles_sound_trigger_device *)cookie;

    ALOGE("+%s+",__func__);
    pthread_mutex_lock(&stdev->lock);

    if (EVENT_FW_CRASH == evt) {
        ALOGE("%s: Firmware crash has been detected", __func__);
        stdev->is_st_hal_ready = false;
    }
    else if (EVENT_FW_RECOVERED == evt) {
        int cmd = FW_RECOVERY;
        write(stdev->event_thrd_sock[WRITE_SOCKET], &cmd, sizeof(int));
    }
    else if (EVENT_KEYWORD_RECOGNITION == evt) {
        ed = (struct event_data *) evt_data;
        ALOGE("%s: KW detected:", __func__);
        ALOGE("%s: Start frame %d End Frame %d", __func__,
                                ed->start_frame, ed->end_frame);

        kwid = OK_GOOGLE_KW_ID;
        idx = find_handle_for_kw_id(stdev, kwid);
        if (idx < MAX_MODELS && stdev->models[idx].is_active == true) {
            recognition_status = RECOGNITION_STATUS_SUCCESS;
            ALOGE("Hotword detected on Chelsea : KW detection started by GSA : Sending Event to GSA");
            //send event to the ST framework
            if (stdev->models[idx].type == SOUND_MODEL_TYPE_KEYPHRASE) {
                struct sound_trigger_phrase_recognition_event *event;
                event = (struct sound_trigger_phrase_recognition_event*)
                            stdev_keyphrase_event_alloc(
                                        stdev->models[idx].model_handle,
                                        stdev->models[idx].config,
                                        recognition_status);
                if (event) {
                    struct model_info *model;
                    model = &stdev->models[idx];
                    if (true == model->is_active) {
                        ALOGD("Sending recognition callback for id %d",
                            kwid);
                        model->recognition_callback(&event->common,
                                            model->recognition_cookie);
                    } else {
                        ALOGE("Keyowrd is not active ignoring detection");
                    }
                    // Update the config so that it will be used
                    // during the streaming
                    stdev->last_keyword_detected_config = model->config;
                    free(event);
                } else {
                    ALOGE("Failed to allocate memory for the event");
                }
            } else if (stdev->models[idx].type == SOUND_MODEL_TYPE_GENERIC) {

                struct sound_trigger_generic_recognition_event *event;
                if(ed->kw_id != 0) {
                    payload_size = sizeof(ed->kw_id);
                    payload = malloc(payload_size);
                    if (payload != NULL) {
                        /* Copy the payload_buf */
                        memcpy(payload,&ed->kw_id,payload_size);
                    } else {
                        ALOGE("Failed to allocate memory for"
                        "payload");
                        goto exit;
                    }
                }

                event = (struct sound_trigger_generic_recognition_event*)
                        stdev_generic_event_alloc(
                                    stdev->models[idx].model_handle,
                                    payload,
                                    payload_size,
                                    recognition_status);
                if (event) {
                    struct model_info *model;
                    model = &stdev->models[idx];
                    ALOGD("Sending recognition callback for id %d",
                            kwid);
                    model->recognition_callback(&event->common,
                                            model->recognition_cookie);
                    // Update the config so that it will be used
                    // during the streaming
                    stdev->last_keyword_detected_config = model->config;
                    free(event);
                } else {
                    ALOGE("Failed to allocate memory for the event");
                }
            }
            if(payload != NULL)
                free(payload);
        } else {
            ALOGE("Hotword detected on Chelsea : KW detection not started by GSA : Ignoring the Event");
        }
    }
exit:
    pthread_mutex_unlock(&stdev->lock);
    ALOGE("-%s-",__func__);
    return 0;
}

int stdev_strm_open()
{
    int ret = 0;
    ALOGD("+%s+", __func__);
    pthread_mutex_lock(&g_stdev.lock);

    if (!g_stdev.opened) {
        ALOGE("%s: device already closed", __func__);
        ret = -EFAULT;
        goto exit;
    }

    if (g_stdev.is_st_hal_ready == false) {
        ALOGE("%s: ST HAL is not ready yet", __func__);
        ret = -EAGAIN;
        goto exit;
    }
    ret = vq_start_audio(g_stdev.vq_hdl,false);
    if( ret == 0) {
        ALOGD("%s: vq_start_audio success", __func__);
        //return positive value (should be >0) if success
        ret = 1;
        g_stdev.is_st_streaming = true;
    }
    else {
        ALOGD("%s: vq_start_audio failed", __func__);
        ret = 0;
    }

exit:
    pthread_mutex_unlock(&g_stdev.lock);
    ALOGD("-%s-", __func__);
    return ret;

}

size_t stdev_strm_read(void* buf, size_t len)
{
    size_t ret = 0;
    pthread_mutex_lock(&g_stdev.lock);

    if(buf == NULL)
    {
        ALOGE("%s: invalid buffer",__func__);
        ret = -EFAULT;
        goto exit;
    }

    if (!g_stdev.opened) {
        ALOGE("%s: device already closed", __func__);
        ret = -EFAULT;
        goto exit;
    }

    if (g_stdev.is_st_hal_ready == false) {
        ALOGE("%s: ST HAL is not ready yet", __func__);
        ret = -EAGAIN;
        goto exit;
    }

    ret = vq_read_audio(g_stdev.vq_hdl,buf,len);
    if(ret == 0) {
        ALOGE("%s: failed to read",__func__);
        ret = 0;
    }
    else {
        ret = len;
    }

exit:
    pthread_mutex_unlock(&g_stdev.lock);
    ALOGD("-%s-", __func__);
    return ret;

}

int stdev_strm_close()
{
    int ret = 0;
    ALOGD("+%s+", __func__);
    pthread_mutex_lock(&g_stdev.lock);

    if (!g_stdev.opened) {
        ALOGE("%s: device already closed", __func__);
        ret = -EFAULT;
        goto exit;
    }

    if (g_stdev.is_st_hal_ready == false) {
        ALOGE("%s: ST HAL is not ready yet", __func__);
        ret = -EAGAIN;
        goto exit;
    }

    ret = vq_stop_audio(g_stdev.vq_hdl);
    g_stdev.is_st_streaming = false;

exit:
    pthread_mutex_unlock(&g_stdev.lock);
    ALOGD("-%s-", __func__);
    return ret;

}

int stdev_start_st_route()
{
    int ret = 0;
    ALOGE("+%s+", __func__);
    pthread_mutex_lock(&g_stdev.lock);

  /*   if (!g_stdev.opened) {
        ALOGE("%s: device already closed", __func__);
        ret = -EFAULT;
        goto exit;
    } */

    /* if (g_stdev.is_st_hal_ready == false) {
        ALOGE("%s: ST HAL is not ready yet\n", __func__);
        ret = -EAGAIN;
        goto exit;
    }

    if (!g_stdev.is_kwd_loaded)
    {
	    ALOGE("%s  No kwd was  active, returning\n", __func__);
	    return 0;
    } */

    if(isRouteSetup == false)
    {
        ret = vq_start_recognition(g_stdev.vq_hdl, MUSIC_PLAYBACK_STOPPED);
        isRouteSetup = true;
    }
    if (0 != ret) {
        ALOGE("%s: ERROR Failed to start recognition", __func__);
        ret = -EINVAL;
        goto exit;
    }

    //stdev->models[handle].is_loaded = false;

exit:
    pthread_mutex_unlock(&g_stdev.lock);
    ALOGE("-%s-", __func__);
    return ret;
}

int stdev_stop_st_route()
{
    int ret = 0;
    ALOGD("+%s+", __func__);
/* Commenting for the purpose of demo
     pthread_mutex_lock(&g_stdev.lock);

     if (!g_stdev.opened) {
         ALOGE("%s: device already closed", __func__);
         ret = -EFAULT;
         goto exit;
     }

     if (g_stdev.is_st_hal_ready == false) {
         ALOGE("%s: ST HAL is not ready yet\n", __func__);
         ret = -EAGAIN;
         goto exit;
     }

     if (!g_stdev.is_kwd_loaded) {
         ALOGE("%s  No kwd is active, returning\n", __func__);
         return 0;
     }

     if (g_stdev.is_st_streaming) {
         ALOGE("%s: ST streaming is already in progress\n", __func__);
         ret = -EBUSY;
         goto exit;
     }

     ret = vq_stop_recognition(g_stdev.vq_hdl);
     if (0 != ret) {
         ALOGE("%s: ERROR Failed to stop recognition", __func__);
        // ret = -EINVAL;
        ret = 0;
         goto exit;
     }

     //stdev->models[handle].is_loaded = false;

 exit:
     pthread_mutex_unlock(&g_stdev.lock);
*/
    ALOGD("-%s-", __func__);
    return ret;
}

static int stdev_open(const hw_module_t *module, const char *name,
        hw_device_t **device)
{
    struct knowles_sound_trigger_device *stdev;
    int ret = 0, i = 0;

    ALOGE("!! Knowles SoundTrigger v1!!");

    if (strcmp(name, SOUND_TRIGGER_HARDWARE_INTERFACE) != 0)
        return -EINVAL;

    if (device == NULL)
        return -EINVAL;

    stdev = &g_stdev;
    pthread_mutex_lock(&stdev->lock);

    if (stdev->opened) {
        ALOGE("%s: Only one sountrigger can be opened at a time", __func__);
        ret = -EBUSY;
        goto exit;
    }

    stdev->device.common.tag = HARDWARE_DEVICE_TAG;
    stdev->device.common.version = SOUND_TRIGGER_DEVICE_API_VERSION_1_3;
    stdev->device.common.module = (struct hw_module_t *)module;
    stdev->device.common.close = stdev_close;
    stdev->device.get_properties = stdev_get_properties;
    stdev->device.load_sound_model = stdev_load_sound_model;
    stdev->device.unload_sound_model = stdev_unload_sound_model;
    stdev->device.start_recognition = stdev_start_recognition;
    stdev->device.stop_recognition = stdev_stop_recognition;
    //stdev->device.get_model_state = stdev_get_model_state;
    stdev->device.get_properties_extended = stdev_get_properties_extended;

    stdev->opened = true;
    /* Initialize all member variable */
    for (i = 0; i < MAX_MODELS; i++) {
        stdev->models[i].type = SOUND_MODEL_TYPE_UNKNOWN;
        memset(&stdev->models[i].uuid, 0, sizeof(sound_trigger_uuid_t));
        stdev->models[i].config = NULL;
        stdev->models[i].data = NULL;
        stdev->models[i].data_sz = 0;
        stdev->models[i].is_loaded = false;
        stdev->models[i].is_active = false;
        stdev->last_keyword_detected_config = NULL;
        stdev->models[i].is_state_query = false;
    }

    str_to_uuid(OEM_MODEL, &stdev->oem_model_uuid);

    *device = &stdev->device.common; /* same address as stdev */

    //init VQ HAL
    stdev->vq_hdl = vq_hal_init(IS_HOST_SIDE_BUFFERING_ENABLED);
    if (NULL == stdev->vq_hdl) {
        ALOGE("%s: ERROR Failed to init VQ HAL", __func__);
        *device = NULL;
        goto exit;
    }

    ret = vq_register_cb(stdev->vq_hdl, stdev, vq_cb);
    if (0 != ret) {
        ALOGE("%s: ERROR Failed to register the cb to VQ HAL", __func__);
        *device = NULL;
        goto exit;
    }

    pthread_create(&stdev->event_thread, (const pthread_attr_t *) NULL,
                    st_event_thread, stdev);
    stdev->is_st_hal_ready = true;
exit:
    pthread_mutex_unlock(&stdev->lock);
    return ret;
}

static struct hw_module_methods_t hal_module_methods = {
    .open = stdev_open,
};

struct sound_trigger_module HAL_MODULE_INFO_SYM = {
    .common = {
        .tag = HARDWARE_MODULE_TAG,
        .module_api_version = SOUND_TRIGGER_MODULE_API_VERSION_1_0,
        .hal_api_version = HARDWARE_HAL_API_VERSION,
        .id = SOUND_TRIGGER_HARDWARE_MODULE_ID,
        .name = "Knowles Sound Trigger HAL",
        .author = "Knowles Electronics",
        .methods = &hal_module_methods,
    },
};
