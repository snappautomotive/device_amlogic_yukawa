/*
 * knowles_util.c
 *
 *  Created on: Jan 7, 2019
 *      Author: nhs
 */

#define LOG_TAG "knowles_util"

#include <stdlib.h>
#include <cutils/list.h>
#include <hardware/audio.h>
#include <tinyalsa/asoundlib.h>
#include <unistd.h>

#include <audio_route/audio_route.h>

#include <log/log.h>
#include <cutils/str_parms.h>
#include <dlfcn.h>
#include <inttypes.h>

#include <ia_constants.h>
#include "knowles_util.h"


#ifdef __LP64__
#define KNOWLES_ST_LIBRARY_PATH "/vendor/lib64/hw/sound_trigger.primary.default.so"
#else
#define KNOWLES_ST_LIBRARY_PATH "/vendor/lib/hw/sound_trigger.primary.default.so"
#endif // __LP_64


#define MAX_RETRIES     (50)
#define MAX_SND_CARD    (8)
#define RETRY_US        (500000)

#define CARD_NAME               "audio-iaxxx"
#define MIXER_PATH              "/vendor/etc/mixer_paths_ia8x01.xml"


#define CHANNEL_MONO 1
#define CHANNEL_STEREO 2
#define TUNNEL_BUFFER_SIZE 4096

/**
 * Initialize the Knowles device
 *
 * Input  - kdev  - Handle to kn device structure
 * Output - 0 on success, on failure < 0
 */
int initKnowlesDevice(struct kn_device *kdev) {
    int ret = 0;
    FUNCTION_ENTRY_LOG

    if (NULL == kdev) {
        ret = -1;
        ALOGE("adev is null");
        goto ERROR;
    }

    //use ST HAL library for streaming
    if (-1 == openSTHALLib(kdev)) {
        ret = -1;
        ALOGE("openSTHALLib failed");
        goto ERROR;
    }

ERROR:
    FUNCTION_EXIT_LOG
    return ret;
}

/**
 * Initialize Route
 *
 * Input  - kdev     - Handle to kn device structure
 *          snd_card - sound card index
 * Output - 0 on success, on failure < 0
 */
int initRoute(struct kn_device *kdev, int snd_card) {
    int ret = 0;

    FUNCTION_ENTRY_LOG

    if (NULL == kdev) {
        ret = -1;
        ALOGE("adev is null");
        goto ERROR;
    }

    if (NULL != kdev->route_hdl) {
        ALOGD("Audio route is already initialized");
        goto ERROR;
    }

    if(-1 == snd_card) {
        ALOGE("Unable to find the sound card %s", CARD_NAME);
        ret = -1;
        goto ERROR;
    }

    kdev->route_hdl = audio_route_init(snd_card, MIXER_PATH);
    if (NULL == kdev->route_hdl) {
        ALOGE("Could not init the route");
        ret = -1;
        goto ERROR;
    }

ERROR:
    FUNCTION_EXIT_LOG
    return ret;
}

/**
 * Open Knowles Input Stream
 *
 * Input  - kin  - Handle to kn input stream structure
 *          kdev - Handle to kn device structure
 * Output - 0 on success, on failure < 0
 */
int openKInputStream(struct kn_stream_in * kin, struct kn_device *kdev) {
    int ret = 0;
    FUNCTION_ENTRY_LOG
    if ((NULL == kin) || (NULL == kdev)) {
        ALOGE("kin/kdev is null");
        return ret;
    }

    ALOGD("audio_source(%d)", kin->source);
    if (!(AUDIO_SOURCE_HOTWORD == kin->source) ) {
        ALOGD("Not knowles streaming usecase");
        return ret;
    }

    ALOGD("Its hotword usecase");

    if (kdev->stdev_strm_open) {
        if (!kdev->stdev_strm_open()) {
            kin->is_strm_opened = false;
            ALOGE("kdev->stdev_strm_open failed");
            ret = -EIO;
            goto ERROR;
        } else {
            kin->is_strm_opened = true;
            kin->channel_mask = audio_channel_in_mask_from_count(CHANNEL_STEREO);
            kin->buffer_size = TUNNEL_BUFFER_SIZE;
            ALOGD("kdev->stdev_strm_open successful for hotword");
        }
    } else {
        ALOGE("Failed to grab kdev->stdev_strm_open");
        ret = -EIO;
    }

ERROR:
    FUNCTION_EXIT_LOG
    return ret;
}

int openKMicStream(struct kn_stream_in * kin, struct kn_device *kdev) {
    int ret = 0;
    FUNCTION_ENTRY_LOG
    if ((NULL == kin) || (NULL == kdev)) {
        ALOGE("kin/kdev is null");
        return ret;
    }

    ALOGD("audio_source(%d)", kin->source);
    if ((AUDIO_SOURCE_HOTWORD == kin->source) ) {
        ALOGD("Streaming usecase\n");
        return ret;
    }

    if (kdev->mic_strm_open) {
        if (kdev->mic_strm_open()) {
            kin->is_strm_opened = false;
            ALOGE("kdev->mic_strm_open failed");
            ret = -EIO;
            goto ERROR;
        } else {
            kin->is_strm_opened = true;
            kin->channel_mask = audio_channel_in_mask_from_count(CHANNEL_STEREO);
            kin->buffer_size = TUNNEL_BUFFER_SIZE;
            ALOGD("kdev->mic_strm_open successful for hotword");
        }
    } else {
        ALOGE("Failed to grab kdev->stdev_strm_open");
        ret = -EIO;
    }

ERROR:
    FUNCTION_EXIT_LOG
    return ret;
}

/**
 * Close Knowles Input Stream
 *
 * Input  - kin  - Handle to kn input stream structure
 *          kdev - Handle to kn device structure
 * Output - 0 on success, on failure < 0
 */
static int closeKInputStream(struct kn_stream_in * kin, struct kn_device *kdev) {
    int ret = -1;
    FUNCTION_ENTRY_LOG
    if ((NULL == kin) || (NULL == kdev)) {
        ALOGE("kin/kdev is null");
        goto ERROR;
    }

    if(kin->is_strm_opened == true) {
        ret = 0;
        if (kdev->stdev_strm_close)
            kdev->stdev_strm_close();
        ALOGD("kin stream is closed");
        kin->is_strm_opened = false;
    }

ERROR:
    FUNCTION_EXIT_LOG
    return ret;
}

int closeKMicStream(struct kn_stream_in * kin, struct kn_device *kdev) {
    int ret = -1;
    FUNCTION_ENTRY_LOG
    if ((NULL == kin) || (NULL == kdev)) {
        ALOGE("kin/kdev is null");
        goto ERROR;
    }

    if(kin->is_strm_opened == true) {
        ret = 0;
        if (kdev->mic_strm_close)
            kdev->mic_strm_close();
        ALOGD("kin stream is closed");
        kin->is_strm_opened = false;
    }

ERROR:
    FUNCTION_EXIT_LOG
    return ret;
}

/**
 * Read Knowles Input Stream
 *
 * Input  - kin    - Handle to kn input stream structure
 *          kdev   - Handle to kn device structure
 *          buffer - Pointer to buffer
 *          bytes  - Pointer to size of data
 * Output - bytes  - Number of bytes read
 */
size_t readKInputStream(struct kn_device *kdev, struct kn_stream_in * kin,
                                void *buffer, size_t *bytes) {

    if (!(AUDIO_SOURCE_HOTWORD == kin->source) ) {
        return NOT_KNOWLES_INPUT_USECASE;
    }

    if (kin->is_strm_opened == true) {
        if (kdev->stdev_strm_read) {
            *bytes = kdev->stdev_strm_read(buffer, *bytes);
        } else {
            ALOGE("kdev->stdev_strm_read is NULL, something wrong!!!");
            usleep(((*bytes)/32)*1000);
        }
    }

    return *bytes;
}


size_t readKMicStream(struct kn_device *kdev, struct kn_stream_in * kin,
                                void *buffer, size_t *bytes) {

    if ((AUDIO_SOURCE_HOTWORD == kin->source) ) {
        return NOT_KNOWLES_INPUT_USECASE;
    }

    if (kin->is_strm_opened == true) {
        if (kdev->mic_strm_read) {
            *bytes = kdev->mic_strm_read(buffer, *bytes);
        } else {
            ALOGE("kdev->mic_strm_read is NULL, something wrong!!!");
            usleep(((*bytes)/32)*1000);
        }
    }

    return *bytes;
}

int startKInputRoute(struct kn_device *kdev)
{
	return  kdev->stdev_start_st_route();
}

int stopKInputRoute(struct kn_device *kdev)
{
	return  kdev->stdev_stop_st_route();
}
/**
 * knowles Input Standby
 *
 * Input  - kin  - Handle to kn input stream structure
 *          kdev - Handle to kn device structure
 * Output - ret  - True for Tunnel/Hotword usecase, False for Recording
 */
bool is_kin_standby(struct kn_stream_in * kin, struct kn_device *kdev) {
    bool ret = false;
    FUNCTION_ENTRY_LOG
    if (AUDIO_SOURCE_HOTWORD == kin->source) {
        closeKInputStream(kin, kdev);
        ret = true;
    }
    ALOGD("%s returning %d", __func__, ret);
    FUNCTION_EXIT_LOG
    return ret;
}

#define TARGET_GAIN "targetgain"
#define GAIN_RAMP "gainramp"
#define TARGET_GAIN_MAX_VALUE 127
#define TARGET_GAIN_MIN_VALUE -128
#define GAIN_RAMP_MAX_VALUE 65535

/**
 * Set Knowles Parameters
 *
 * Input  - kdev  - Handle to kn device structure
 *          parms - Handle to structure parameters
 * Output - NA
 */
void setKnowlesPrams(struct kn_device *kdev, struct str_parms *parms) {
    int ret = 0;
    char value[32];
    FUNCTION_ENTRY_LOG

    if ((NULL == kdev) || (NULL == parms)) {
        ALOGE("adev/parms is null");
        goto ERROR;
    }

    ret = str_parms_get_str(parms, TARGET_GAIN, value, sizeof(value));
    if (ret >= 0) {
        ALOGD("targetgain=%s", value);
        kdev->targetGain = atoi(value);
        ALOGD("In int targetgain(%d)", kdev->targetGain);
        if (kdev->targetGain < TARGET_GAIN_MIN_VALUE) {
            kdev->targetGain = TARGET_GAIN_MIN_VALUE;
            ALOGD("Invalid value of target gain, reset done(%d)", kdev->targetGain);
        } else if (kdev->targetGain > TARGET_GAIN_MAX_VALUE) {
            kdev->targetGain = TARGET_GAIN_MAX_VALUE;
            ALOGD("Invalid value of target gain, reset done(%d)", kdev->targetGain);
        }
        ALOGD("@@@ Setting Target Gain to %d", kdev->targetGain);
    }

    ret = str_parms_get_str(parms, GAIN_RAMP, value, sizeof(value));
    if (ret >= 0) {
        ALOGD("gainramp=%s", value);
        kdev->gainRamp= atoi(value);
        ALOGD("In int gainramp(%d)", kdev->gainRamp);
        if (kdev->gainRamp < 0) {
            kdev->gainRamp = 0;
            ALOGD("Invalid value of gain ramp, reset done(%d)", kdev->gainRamp);
        } else if (kdev->gainRamp > GAIN_RAMP_MAX_VALUE) {
            kdev->gainRamp = GAIN_RAMP_MAX_VALUE;
            ALOGD("Invalid value of gain ramp, reset done(%d)", kdev->gainRamp);
        }
        //setChannelGain(kdev);
        ALOGD("@@@ Setting Gain Ramp to %d", kdev->gainRamp);
    }

ERROR:
    FUNCTION_EXIT_LOG
    return;
}

/**
 * Get Knowles Parameters
 *
 * Input  - kdev  - Handle to kn device structure
 *          query - Handle to structure parameters
 *          reply - Handle to structure parameters
 * Output - 0 on success, on failure < 0
 */
int getKnowlesPrams(struct kn_device *kdev, struct str_parms *query, struct str_parms *reply){
    int ret = 0;
    FUNCTION_ENTRY_LOG

    if ((NULL == kdev) || (NULL == query) || (NULL == reply)) {
        ret = -1;
        ALOGE("adev/query/reply is null");
        goto ERROR;
    }
ERROR:
    FUNCTION_EXIT_LOG
    return ret;
}

/**
 * Open ST HAL library
 *
 * Input  - kdev  - Handle to kn device structure
 * Output - 0 on success, on failure < 0
 */
static int openSTHALLib(struct kn_device *kdev) {
    int ret = 0;
    FUNCTION_ENTRY_LOG

    if (NULL == kdev) {
        ALOGE("adev is null");
        ret = -1;
        goto ERROR;
    }

    ALOGD("opening %s", KNOWLES_ST_LIBRARY_PATH);
    if (access(KNOWLES_ST_LIBRARY_PATH, R_OK) == 0) {
        kdev->st_lib = dlopen(KNOWLES_ST_LIBRARY_PATH, RTLD_NOW);
        if (kdev->st_lib == NULL) {
            char const *err_str = dlerror();
            ALOGE("%s: module = %s error = %s", __func__, KNOWLES_ST_LIBRARY_PATH, err_str ? err_str : "unknown");
            ALOGE("%s: DLOPEN failed for %s", __func__, KNOWLES_ST_LIBRARY_PATH);
            ret = -1;
        } else {
            ALOGD("%s: DLOPEN successful for %s", __func__, KNOWLES_ST_LIBRARY_PATH);
            kdev->stdev_strm_open =
                (int (*)())dlsym(kdev->st_lib,
                "stdev_strm_open");
            kdev->stdev_strm_read =
                (size_t (*)(void *, size_t))dlsym(kdev->st_lib,
                "stdev_strm_read");
            kdev->stdev_strm_close =
                (int (*)())dlsym(kdev->st_lib,
                "stdev_strm_close");
            kdev->mic_strm_open =
                (int (*)())dlsym(kdev->st_lib,
                "mic_record_stream_start");
            kdev->mic_strm_read =
                (size_t (*)(void *, size_t))dlsym(kdev->st_lib,
                "mic_record_read_stream");
            kdev->mic_strm_close =
                (int (*)())dlsym(kdev->st_lib,
                "mic_record_stream_stop");
            kdev->stdev_start_st_route =
                (int (*)())dlsym(kdev->st_lib,
                "stdev_start_st_route");
            kdev->stdev_stop_st_route =
                (int (*)())dlsym(kdev->st_lib,
                "stdev_stop_st_route");
            if (!kdev->stdev_strm_open || !kdev->stdev_strm_close || !kdev->stdev_strm_close ||
			    !kdev->stdev_start_st_route || !kdev->stdev_stop_st_route) {
                ALOGE("%s: Error grabbing functions in %s", __func__, KNOWLES_ST_LIBRARY_PATH);
                kdev->stdev_strm_open = 0;
                kdev->stdev_strm_read = 0;
                kdev->stdev_strm_close = 0;
		kdev->stdev_start_st_route = 0;
		kdev->stdev_stop_st_route = 0;
                ret = -1;
            }
        }
    } else {
        ALOGE("Error in accessing Knowles ST HAL %s",KNOWLES_ST_LIBRARY_PATH);
    }

    ALOGD("opening %s successful", KNOWLES_ST_LIBRARY_PATH);

ERROR:
    FUNCTION_EXIT_LOG
    return ret;
}

/**
 * Search for Knowles Sound Card
 *
 * Input  - NA
 * Output - Sound Card Number
 */
int find_sound_card() {
    int retry_num = 0, snd_card_num = 0, ret = -1;
    const char *snd_card_name;
    struct mixer *mixer = NULL;

    FUNCTION_ENTRY_LOG
    while (snd_card_num < MAX_SND_CARD) {
        mixer = mixer_open(snd_card_num);
        while (!mixer && retry_num < MAX_RETRIES) {
            usleep(RETRY_US);
            mixer = mixer_open(snd_card_num);
            retry_num++;
        }

        if (!mixer) {
            ALOGE("%s: Unable to open the mixer card: %d", __func__,
                    snd_card_num);
            retry_num = 0;
            snd_card_num++;
            continue;
        }

        snd_card_name = mixer_get_name(mixer);
        ALOGV("%s: snd_card_name: %s", __func__, snd_card_name);
        mixer_close(mixer);

        if(strstr(snd_card_name, CARD_NAME)){
            ALOGD("Found %s at %d", CARD_NAME, snd_card_num);
            ret = snd_card_num;
            break;
        } else {
            snd_card_num++;
            continue;
        }
    }
    FUNCTION_EXIT_LOG
    return ret;
}

/**
 * Execute Route
 *
 * Input  - kdev   - Handle to kn device structure
 *          enable - Bool for enable or disable
 * Output - NA
 */
int execute_route(struct kn_device *kdev, char *path, bool enable) {
    int ret = 0;
    FUNCTION_ENTRY_LOG
    if ((NULL == kdev) || (NULL == kdev->route_hdl)) {
        ALOGD("kdev/route_hdl is null");
        ret = -1;
        goto exit;
    }
    ALOGD("We are %s %s route", enable?"enabling":"disabling", path);
    if (enable) {
        ret = audio_route_apply_and_update_path(kdev->route_hdl, path);
    } else {
        ret = audio_route_reset_and_update_path(kdev->route_hdl, path);
    }
exit:
    FUNCTION_EXIT_LOG
    return ret;
}
