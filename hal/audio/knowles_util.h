/*
 * knowles_util.h
 *
 *  Created on: Dec 28, 2018
 *      Author: nhs
 */

#ifndef KNOWLES_UTIL_H_
#define KNOWLES_UTIL_H_
#include "mixer_ctl.h"

#define MIC_RECORDING_ROUTE     "mic-recording"
#define INIT_PDM_ROUTE          "init_pdm"
#define INIT_TUNNEL          "init_tunnel"


#define NOT_KNOWLES_INPUT_USECASE 130485
#define FUNCTION_ENTRY_LOG ALOGD("+%s+", __func__);
#define FUNCTION_EXIT_LOG ALOGD("-%s-", __func__);

#ifdef __cplusplus
extern "C" {
#endif

typedef enum _vp_config_ {
    VP_CONFIG_TOP_MIC = 0,
    VP_CONFIG_BOTTOM_MIC,
    VP_CONFIG_ON
}vp_config_e;

struct kn_device {
    struct iaxxx_odsp_hw* iaxxx_odsp;
    struct audio_route *route_hdl;
    int isBottomMic;
    vp_config_e vpConfig;
    int8_t targetGain;
    uint16_t gainRamp;

    //Sound trigger lib
    void* st_lib;

    //adnc lib
    void* adnc_lib;
    int    (*stdev_strm_open)();
    size_t (*stdev_strm_read)(void*, size_t);
    int    (*stdev_strm_close)();
    int    (*stdev_start_st_route)();
    int    (*stdev_stop_st_route)();
    int    (*mic_strm_open)();
    size_t (*mic_strm_read)(void*, size_t);
    int    (*mic_strm_close)();

};

struct kn_stream_in {
    bool is_strm_opened;
    int adnc_handle;
    audio_devices_t device;
    audio_channel_mask_t channel_mask;
    audio_format_t format;
    unsigned int sample_rate;
    int source;
    int buffer_size;
};

/**
 * Initialize the Knowles device
 *
 * Input  - kdev  - Handle to kn device structure
 * Output - 0 on success, on failure < 0
 */
int initKnowlesDevice(struct kn_device *kdev);

/**
 * Initialize Route
 *
 * Input  - kdev     - Handle to kn device structure
 *          snd_card - sound card index
 * Output - 0 on success, on failure < 0
 */
int initRoute(struct kn_device *kdev, int snd_card);

/**
 * Open Knowles Input Stream
 *
 * Input  - kin  - Handle to kn input stream structure
 *          kdev - Handle to kn device structure
 * Output - 0 on success, on failure < 0
 */
int openKInputStream(struct kn_stream_in * kin, struct kn_device *kdev);

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
                                void *buffer, size_t *bytes);

/**
 * knowles Input Standby
 *
 * Input  - kin  - Handle to kn input stream structure
 *          kdev - Handle to kn device structure
 * Output - ret  - True for Tunnel/Hotword usecase, False for Recording
 */
bool is_kin_standby(struct kn_stream_in * kin, struct kn_device *kdev);

/**
 * Set Knowles Parameters
 *
 * Input  - kdev  - Handle to kn device structure
 *          parms - Handle to structure parameters
 * Output - NA
 */
void setKnowlesPrams(struct kn_device *kdev, struct str_parms *parms);

/**
 * Get Knowles Parameters
 *
 * Input  - kdev  - Handle to kn device structure
 *          query - Handle to structure parameters
 *          reply - Handle to structure parameters
 * Output - 0 on success, on failure < 0
 */
int getKnowlesPrams(struct kn_device *kdev, struct str_parms *query, struct str_parms *reply);

/**
 * Search for Knowles Sound Card
 *
 * Input  - NA
 * Output - Sound Card Number
 */
int find_sound_card();

/**
 * Execute Route
 *
 * Input  - kdev   - Handle to kn device structure
 *          enable - Bool for enable or disable
 * Output - NA
 */
int execute_route(struct kn_device *kdev, char *path, bool enable);

/**
 * Open ST HAL library
 *
 * Input  - kdev  - Handle to kn device structure
 * Output - 0 on success, on failure < 0
 */
static int openSTHALLib(struct kn_device *kdev);
int startKInputRoute(struct kn_device *kdev);
int stopKInputRoute(struct kn_device *kdev);
int openKMicStream(struct kn_stream_in * kin, struct kn_device *kdev);
size_t readKMicStream(struct kn_device *kdev, struct kn_stream_in * kin,
		void *buffer, size_t *bytes);
int closeKMicStream(struct kn_stream_in * kin, struct kn_device *kdev);
#ifdef __cplusplus
}
#endif
#endif /* KNOWLES_UTIL_H_ */
