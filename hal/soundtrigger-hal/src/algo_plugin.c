#define LOG_TAG "HotWord"
#define LOG_NDEBUG 0

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
#ifdef ANDROID
#include <log/log.h>
#else
#include "logger.h"
#endif

#include "iaxxx-system-identifiers.h"

#include "vq_hal.h"
#include "vq_plugin.h"
#include "mixer_utils.h"
#include "iaxxx_odsp_hw.h"
#include "knowles_tunnel_pcm.h"

#define CHELSEA_DEV_ID             0

#ifdef ANDROID
#define FOURSTART_MIC_CREATE_CONFIG  "audience/ia8x01/FourStarCreateConfig.bin"
#define INTERLEAVE_CREATE_CONFIG     "audience/ia8x01/NMicInterleaveCreateconfig.bin"
#else
#define CALYPSO_MIC_CREATE_CONFIG    "audience/ia8x01/Calypso836CreateConfig.bin"
#endif
#define HOTWORD_PACKAGE              "audience/ia8x01/HotWordPackage.bin"
#define NMICINTERLEAVE_PACKAGE       "audience/ia8x01/NMicInterleavePackage.bin"
#define HOTWORD_KW_MODEL             "audience/ia8x01/Google_Hotword_model.bin"
#define BUFFER_CREATE_CONFIG         "audience/ia8x01/BufferConfig_2s_Q15.bin"
#define DUMMY_BUFFER_CREATE_CONFIG   "audience/ia8x01/Dummy_Buffer.bin"


#define HOTWORD_BLOCK_ID           0   //hotword blk id for DMX
#define HOTWORD_PLUGIN_IDX         0   //hotword plugin idx
#define HOTWORD_PKG_ID             0x3
#define HOTWORD_INST_ID            0
#define HOTWORD_PRIORITY           1
#define HOTWORD_KW_BLK_ID          1 // Doubt!

#define BUFFER_BLOCK_ID             0
#define BUFFER_PLUGIN_IDX           0
#define BUFFER_PKG_ID               0x04
#define BUFFER_PRIORITY             0
#define BUFFER_1_INST_ID            2
#define BUFFER_2_INST_ID            1

#define INTERLEAVE_BLOCK_ID           0   //hotword blk id for DMX
#define INTERLEAVE_PLUGIN_IDX         0   //hotword plugin idx
#define INTERLEAVE_PKG_ID             0x05
#define INTERLEAVE_INST_ID            3
#define INTERLEAVE_PRIORITY           1
#define INTERLEAVE_KW_BLK_ID          1 // Doubt!

#define SET_EVENT_MASK              0xF
#define EVENT_ID_KW_ID              0
#define EVENT_ID_START_FRAME        1
#define EVENT_ID_END_FRAME          2
#define EVENT_ID_KW_REJECT          3

#define AEC_ENABLE_ID               1
#define ENABLE_AEC                  1
#define DISABLE_AEC                 0

#define KW_DETECT_REENABLE_ID       2
#define KW_DETECT_REENABLE_VAL      1

#define BUFFER_END_POINT            IAXXX_SYSID_PLUGIN_2_OUT_EP_0
#define DEBUG_END_POINT             IAXXX_SYSID_PLUGIN_0_OUT_EP_0
#define TUN_BUF_SIZE                0
// hotword vq frame size in milli seconds
#define HOTWORD_VQ_FRAME_SIZE_IN_MS  10

#define FUNCTION_ENTRY_LOG  ALOGE("Entering %s", __func__)
#define FUNCTION_EXIT_LOG   ALOGE("Exiting %s", __func__)


enum event_states {
    START,
    EVENT_0,
    EVENT_1,
    EVENT_2,
    EVENT_3
};

struct hotword_hal {
    struct iaxxx_odsp_hw *odsp_hdl;
    struct mixer *mixer_hdl;

    int curr_event_state;
    struct event_data ed;

    struct kt_pcm *kt_pcm_hdl;
    bool is_multiturn_in_prog;
    bool is_first_100ms_dropped;

    vq_hal_music_status music_status;
};

static const char* evt_to_str(int eventid)
{
    if (START == eventid) {
        return "START";
    } else if (EVENT_0 == eventid) {
        return "EVENT_KW_DETECT";
    } else if (EVENT_1 == eventid) {
        return "EVENT_START_FRAME";
    } else if (EVENT_2 == eventid) {
        return "EVENT_END_FRAME";
    } else if (EVENT_3 == eventid) {
        return "EVENT_KW_REJECT";
    }

    return "";
}

/**
 * Initialize the VQ HAL
 *
 * Input  - NA
 * Output - Handle to vq_hal on success, NULL on failure
 */
void* hotword_init()
{
    struct hotword_hal *hal = NULL;

    hal = (struct hotword_hal *) malloc(sizeof(struct hotword_hal));
    if (NULL == hal) {
        ALOGE("%s: ERROR: Failed to allocate memory for VQ HAL", __func__);
        goto on_error;
    }

    hal->mixer_hdl = open_mixer_ctl();
    if (NULL == hal->mixer_hdl) {
        ALOGE("%s: ERROR: Failed to open the mixer control", __func__);
        goto on_error;
    }

    hal->odsp_hdl = iaxxx_odsp_init(CHELSEA_DEV_ID);
    if(NULL == hal->odsp_hdl) {
        ALOGE("%s: ERROR ODSP init failed: %s\n", __func__, strerror(errno));
        goto on_error;
    }

    hal->is_multiturn_in_prog = false;

    return (void*)hal;

on_error:
    if (hal) {
        if (hal->odsp_hdl) {
            iaxxx_odsp_deinit(hal->odsp_hdl);
        }

        if (hal->mixer_hdl) {
            close_mixer_ctl(hal->mixer_hdl);
        }

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
int hotword_deinit(void *handle)
{
    int err = 0;
    struct hotword_hal *hdl = (struct hotword_hal*) handle;

    if (NULL == hdl) {
        ALOGE("%s: ERROR NULL handle sent", __func__);
        err = -1;
        goto exit;
    }

    if (hdl->odsp_hdl) {
        iaxxx_odsp_deinit(hdl->odsp_hdl);
    }

    if (hdl->mixer_hdl) {
        close_mixer_ctl(hdl->mixer_hdl);
    }

    free(hdl);

exit:
    return err;
}

int setup_hotword_vq_events(struct hotword_hal *hdl)
{
    int err = 0;

    if (NULL == hdl) {
        ALOGE("%s: ERROR NULL handle sent", __func__);
        err = -1;
        goto exit;
    }

    ALOGE("Entering %s", __func__);

    // Subscribe for events
    err = iaxxx_odsp_evt_subscribe(hdl->odsp_hdl,
                                   IAXXX_SYSID_PLUGIN_INSTANCE_0,
                                   EVENT_ID_KW_ID, IAXXX_SYSID_HOST, 0);
    if (-1 == err) {
        ALOGE("%s: ERROR: ODSP_EVENT_SUBSCRIBE for event_id 0 IOCTL failed "
              "with error %d(%s)", __func__,  errno, strerror(errno));
        return err;
    }

exit:
    return err;
}

static int enable_bargein_route(struct hotword_hal *hdl) {
 //TODO:
 return 0;
}

static int disable_bargein_route(struct hotword_hal *hdl) {
    /* Default value of Word length, Data format mono/ stereo(in bit format), Tristate Disable */
    int pcm_cfg[] = {0, 0, 0};
    int err = 0, i;
    unsigned int buffer_instances[] = {BUFFER_1_INST_ID, BUFFER_2_INST_ID};
    int num_buf_inst = sizeof(buffer_instances) / sizeof(buffer_instances[0]);

    FUNCTION_ENTRY_LOG;


   /* set_mixer_ctl_val(hdl->mixer_hdl, "strm0 CH Mask En", 0);
    set_mixer_ctl_str(hdl->mixer_hdl, "strm0 Port Enc", "ENCODING_OPAQUE");
    set_mixer_ctl_str(hdl->mixer_hdl, "strm0 Port", "NONE");
    set_mixer_ctl_val(hdl->mixer_hdl, "strm0 inter strm delay", 0);
    set_mixer_ctl_val(hdl->mixer_hdl, "strm0 Dir", 0);
    set_mixer_ctl_str(hdl->mixer_hdl, "strm0 Format Enc", "ENCODING_OPAQUE");
    set_mixer_ctl_str(hdl->mixer_hdl, "strm0 Format Sr", "RATE_8K");
    set_mixer_ctl_str(hdl->mixer_hdl, "strm0 Format FrLn", "NONE");
    set_mixer_ctl_str(hdl->mixer_hdl, "strm0 Master Strm Id", "STREAMID_0");
    set_mixer_ctl_str(hdl->mixer_hdl, "strm0 SRC Mode", "DOWN_SAMPLE_BY_2");
    set_mixer_ctl_str(hdl->mixer_hdl, "strm0 ASRC Mode", "ASRC_ENABLE");
    set_mixer_ctl_str(hdl->mixer_hdl, "strm0 Xfer Mode", "SSP");
    set_mixer_ctl_val(hdl->mixer_hdl, "strm0 DC block en", 0);
    set_mixer_ctl_val(hdl->mixer_hdl, "strm0 droop comp en", 0);*/
    //set_mixer_ctl_val(hdl->mixer_hdl, "Update Block0 Req", 1);
    //ALOGE("Hrishi: disabled stream0 sleeping for 2 s");
    //sleep(2);

    set_mixer_ctl_val(hdl->mixer_hdl, "Plgin0Blk0En", 0);
    set_mixer_ctl_str(hdl->mixer_hdl, "Plgin0Ip Ep0 Conf", "UNKNOWN");

    set_mixer_ctl_val(hdl->mixer_hdl, "Plgin2Blk0En", 0);
    set_mixer_ctl_str(hdl->mixer_hdl, "Plgin2Ip Ep0 Conf", "UNKNOWN");

    set_mixer_ctl_val(hdl->mixer_hdl, "PDM CDC1 Clk Start", 0);
    set_mixer_ctl_val(hdl->mixer_hdl, "PDM CDC0 Clk Start", 0);
    set_mixer_ctl_val(hdl->mixer_hdl, "PDM Hos Rx", 0);
    set_mixer_ctl_str(hdl->mixer_hdl, "PDM DMIC MONO In2 En", "DISABLE");
    set_mixer_ctl_str(hdl->mixer_hdl, "PDM DMIC MONO In0 En", "DISABLE");
    set_mixer_ctl_str(hdl->mixer_hdl, "PDM DMIC MONO In3 En", "DISABLE");
    set_mixer_ctl_str(hdl->mixer_hdl, "PDM DMIC MONO In0 Port", "NONE");
    set_mixer_ctl_str(hdl->mixer_hdl, "PDM DMIC MONO In3 Port", "NONE");
    set_mixer_ctl_str(hdl->mixer_hdl, "PDM I2S Gen4 Master Src", "I2S_GEN4");
    set_mixer_ctl_str(hdl->mixer_hdl, "PDM I2S Gen5 Master Src", "I2S_GEN5");
    set_mixer_ctl_val(hdl->mixer_hdl, "PDM I2S Gen2 Setup", 0);
    set_mixer_ctl_str(hdl->mixer_hdl, "PDM Port AudCLK", "IAXXX_AUD_PORT_NONE");
    set_mixer_ctl_str(hdl->mixer_hdl, "PDM BCLK", "IAXXX_PDM_CLK_NONE");


    /*
     * Reset all the buffer plugins after disabling them, so that
     * any frames that are left over will be flushed out.
     */
    for (i = 0; i < num_buf_inst; i++) {
        err = iaxxx_odsp_plugin_reset(hdl->odsp_hdl, BUFFER_BLOCK_ID,
                                        buffer_instances[i]);
        if (0 != err) {
            ALOGE("%s: Failed to reset Buffer instance %d", __func__, i);
            break;
        }
    }

    set_mixer_ctl_val(hdl->mixer_hdl, "strm0 En", 0);
    set_mixer_ctl_str(hdl->mixer_hdl, "Route Status", "InActive");
    FUNCTION_EXIT_LOG;
    return err;
}

static int enable_nonbargein_route(struct hotword_hal *hdl) {
    int err = 0;
    FUNCTION_ENTRY_LOG;
/*
    set_mixer_ctl_str(hdl->mixer_hdl, "Route Status", "Active");
    set_mixer_ctl_str(hdl->mixer_hdl, "Port Apll Input Freq", "APLL_SRC_FREQ_1536");
    set_mixer_ctl_str(hdl->mixer_hdl, "Port ApllCLK", "IAXXX_ACLK_FREQ_49152");
    set_mixer_ctl_str(hdl->mixer_hdl, "Port Apll Src", "PLL_SRC_OSC_CLK");
    set_mixer_ctl_str(hdl->mixer_hdl, "PDM BCLK", "IAXXX_PDM_CLK_0P_768MHZ");
    set_mixer_ctl_str(hdl->mixer_hdl, "PDM Port AudCLK", "IAXXX_AUD_PORT_48K");
    set_mixer_ctl_val(hdl->mixer_hdl, "PDM I2S Gen2 Setup", 1);
    set_mixer_ctl_str(hdl->mixer_hdl, "PDM I2S Gen5 Master Src", "I2S_GEN2");
    set_mixer_ctl_str(hdl->mixer_hdl, "PDM I2S Gen4 Master Src", "I2S_GEN2");
    set_mixer_ctl_str(hdl->mixer_hdl, "PDM DMIC MONO In3 Port", "IAXXX_PORT_C");
    set_mixer_ctl_str(hdl->mixer_hdl, "PDM DMIC MONO In0 Port", "IAXXX_PORT_C");
    set_mixer_ctl_str(hdl->mixer_hdl, "PDM DMIC MONO In3 En", "CDC0_CLK_SRC");
    set_mixer_ctl_str(hdl->mixer_hdl, "PDM DMIC MONO In0 En", "CDC1_CLK_SRC");
    set_mixer_ctl_str(hdl->mixer_hdl, "PDM DMIC MONO In2 En", "CDC0_CLK_SRC");
    set_mixer_ctl_val(hdl->mixer_hdl, "PDM Hos Rx", 5);
    set_mixer_ctl_val(hdl->mixer_hdl, "PDM CDC0 Clk Start", 12);
    set_mixer_ctl_val(hdl->mixer_hdl, "PDM CDC1 Clk Start", 1);
*/
    set_mixer_ctl_str(hdl->mixer_hdl, "Route Status", "Active");
    set_mixer_ctl_str(hdl->mixer_hdl, "Port Apll Input Freq", "APLL_SRC_FREQ_1536");
    set_mixer_ctl_str(hdl->mixer_hdl, "Port ApllCLK", "IAXXX_ACLK_FREQ_49152");
    set_mixer_ctl_str(hdl->mixer_hdl, "Port Apll Src", "PLL_SRC_OSC_CLK");
    set_mixer_ctl_str(hdl->mixer_hdl, "PDM BCLK", "IAXXX_PDM_CLK_0P_768MHZ");
    set_mixer_ctl_str(hdl->mixer_hdl, "PDM Port AudCLK", "IAXXX_AUD_PORT_48K");
    set_mixer_ctl_val(hdl->mixer_hdl, "PDM I2S Gen2 Setup", 1);
    set_mixer_ctl_str(hdl->mixer_hdl, "PDM I2S Gen5 Master Src", "I2S_GEN2");

#ifdef FOURSTAR_MIC
    set_mixer_ctl_str(hdl->mixer_hdl, "PDM DMIC MONO In0 En", "CDC1_CLK_SRC");
    set_mixer_ctl_str(hdl->mixer_hdl, "PDM DMIC MONO In3 En", "CDC0_CLK_SRC");
#else //Red stripe Mic
    set_mixer_ctl_val(hdl->mixer_hdl, "PDM DMIC In0 En", 0);
    set_mixer_ctl_val(hdl->mixer_hdl, "PDM DMIC In3 En", 2);
#endif

    set_mixer_ctl_val(hdl->mixer_hdl, "PDM Hos Rx", 9);
#ifdef FOURSTAR_MIC
    set_mixer_ctl_val(hdl->mixer_hdl, "PDM CDC0 Clk Start", 8);
    set_mixer_ctl_val(hdl->mixer_hdl, "PDM CDC1 Clk Start", 1);
#else //Red stripe Mic
    set_mixer_ctl_val(hdl->mixer_hdl, "PDM DMIC0 Clk Start", 1);
    set_mixer_ctl_val(hdl->mixer_hdl, "PDM DMIC1 Clk Start", 8);
#endif

    set_mixer_ctl_val(hdl->mixer_hdl, "Rx0Chan GnRmp", 2);
    set_mixer_ctl_val(hdl->mixer_hdl, "Rx0Ch EpGain" , 20 );
    set_mixer_ctl_val(hdl->mixer_hdl, "Rx0Chan Gain En" , 1 );
    set_mixer_ctl_val(hdl->mixer_hdl, "Rx1Chan GnRmp", 2);
    set_mixer_ctl_val(hdl->mixer_hdl, "Rx1Ch EpGain" , 20 );
    set_mixer_ctl_val(hdl->mixer_hdl, "Rx1Chan Gain En" , 1 );

    set_mixer_ctl_val(hdl->mixer_hdl, "strm0 droop comp en", 0);
    set_mixer_ctl_val(hdl->mixer_hdl, "strm0 DC block en", 1);
    set_mixer_ctl_str(hdl->mixer_hdl, "strm0 Xfer Mode", "DMA");
    set_mixer_ctl_str(hdl->mixer_hdl, "strm0 ASRC Mode", "SRC_ENABLED" );
    set_mixer_ctl_str(hdl->mixer_hdl, "strm0 SRC Mode", "DOWN_SAMPLE_BY_3");
    set_mixer_ctl_str(hdl->mixer_hdl, "strm0 Master Strm Id", "STREAMID_0");
    set_mixer_ctl_str(hdl->mixer_hdl, "strm0 Format FrLn", "FRAME_16K10MS");
    set_mixer_ctl_str(hdl->mixer_hdl, "strm0 Format Sr", "RATE_16K");
    set_mixer_ctl_str(hdl->mixer_hdl, "strm0 Format Enc", "ENCODING_AFLOAT");
    set_mixer_ctl_val(hdl->mixer_hdl, "strm0 Dir", 0);
    set_mixer_ctl_val(hdl->mixer_hdl, "strm0 inter strm delay", 0);
    set_mixer_ctl_str(hdl->mixer_hdl, "strm0 Port", "PDMI0");
    set_mixer_ctl_str(hdl->mixer_hdl, "strm0 Port Enc", "ENCODING_Q31");
    set_mixer_ctl_val(hdl->mixer_hdl, "strm0 CH Mask En", 1);
    set_mixer_ctl_str(hdl->mixer_hdl, "strm0 Master Strm Id", "STREAMID_1");
    set_mixer_ctl_val(hdl->mixer_hdl, "strm0 En", 1);
    set_mixer_ctl_val(hdl->mixer_hdl, "Update Block0 Req", 1);

    /* PDM2 */
    set_mixer_ctl_val(hdl->mixer_hdl, "strm1 droop comp en", 0);
    set_mixer_ctl_val(hdl->mixer_hdl, "strm1 DC block en", 1);
    set_mixer_ctl_str(hdl->mixer_hdl, "strm1 Xfer Mode", "DMA");
    set_mixer_ctl_str(hdl->mixer_hdl, "strm1 ASRC Mode", "SRC_ENABLED" );
    set_mixer_ctl_str(hdl->mixer_hdl, "strm1 SRC Mode", "DOWN_SAMPLE_BY_3");
    set_mixer_ctl_str(hdl->mixer_hdl, "strm1 Master Strm Id", "STREAMID_1");
    set_mixer_ctl_str(hdl->mixer_hdl, "strm1 Format FrLn", "FRAME_16K10MS");
    set_mixer_ctl_str(hdl->mixer_hdl, "strm1 Format Sr", "RATE_16K");
    set_mixer_ctl_str(hdl->mixer_hdl, "strm1 Format Enc", "ENCODING_AFLOAT");
    set_mixer_ctl_val(hdl->mixer_hdl, "strm1 Dir", 0);
    set_mixer_ctl_val(hdl->mixer_hdl, "strm1 inter strm delay", 0);
    set_mixer_ctl_str(hdl->mixer_hdl, "strm1 Port", "PDMI3");
    set_mixer_ctl_str(hdl->mixer_hdl, "strm1 Port Enc", "ENCODING_Q31");
    set_mixer_ctl_val(hdl->mixer_hdl, "strm1 CH Mask En", 2);
    set_mixer_ctl_val(hdl->mixer_hdl, "strm1 En", 1);
    set_mixer_ctl_val(hdl->mixer_hdl, "Update Block0 Req", 1);

    //PDM0 - > Interleave plugin
    set_mixer_ctl_str(hdl->mixer_hdl, "Plgin3Ip Ep0 Conf", "RX0_ChanMgr");
    set_mixer_ctl_val(hdl->mixer_hdl, "Plgin3Blk0En", 1);

    //PDM1 - > Interleave plugin
    set_mixer_ctl_str(hdl->mixer_hdl, "Plgin3Ip Ep1 Conf", "RX1_ChanMgr");
    set_mixer_ctl_val(hdl->mixer_hdl, "Plgin3Blk0En", 1);

    //PDM0 -> Hotword plugin
    set_mixer_ctl_str(hdl->mixer_hdl, "Plgin0Ip Ep0 Conf", "RX0_ChanMgr");
    set_mixer_ctl_val(hdl->mixer_hdl, "Plgin0Blk0En", 1);

    //Interleave plugin -> Buffer Plugin
    set_mixer_ctl_str(hdl->mixer_hdl, "Plgin2Ip Ep0 Conf", "plugin3Out0");
    set_mixer_ctl_val(hdl->mixer_hdl, "Plgin2Blk0En", 1);


    set_mixer_ctl_val(hdl->mixer_hdl, "Update Plugin Block0 Req", 1);

    FUNCTION_EXIT_LOG;
    return err;
}

static int disable_nonbargein_route(struct hotword_hal *hdl) {
    FUNCTION_ENTRY_LOG;
    // Disabling route is the same in both scenarios as of now
    return disable_bargein_route(hdl);
    FUNCTION_EXIT_LOG;
}

/**
 * Start recognition of the keyword model
 *
 * Input  - vq_hdl - Handle to vq_hal structure
 *          status - Depicts the status of the music playback
 * Output - 0 on success, on failure < 0
 */
int hotword_start_recognition(void *handle, vq_hal_music_status status)
{
    int err = 0, i;
    const int max_attempts = 5;
    struct iaxxx_create_config_data cdata;
    struct hotword_hal *hdl = (struct hotword_hal *) handle;

    if (NULL == hdl) {
        ALOGE("%s: ERROR Null handle sent", __func__);
        err = -EINVAL;
        goto exit;
    }

    ALOGE("Entering %s status %d", __func__, status);
    hdl->music_status = status;

    ALOGD("Download hotword keyword package");
    // Download sensory package
    for (i = 0; i < max_attempts; i++) {
        err = iaxxx_odsp_package_load(hdl->odsp_hdl, HOTWORD_PACKAGE,
                                      HOTWORD_PKG_ID);
        if (-1 == err) {
            if (EEXIST == errno) {
                ALOGI("%s: WARNING: %s package already exists",
                        __func__, HOTWORD_PACKAGE);
                break;
            }
            ALOGE("%s: ERROR: ODSP_LOAD_PACKAGE failed %d(%s)",
                        __func__, errno, strerror(errno));
            if (i == (max_attempts - 1)) {
                ALOGE("%s: ERROR: Failed to load package for 5 times, "
                        "aborting init of plugins", __func__);
                goto exit;
            }
        } else {
            break;
        }
    }

    ALOGD("Download interleave package");
    // Download interleave package
    for (i = 0; i < max_attempts; i++) {
        err = iaxxx_odsp_package_load(hdl->odsp_hdl, NMICINTERLEAVE_PACKAGE,
                                      INTERLEAVE_PKG_ID);
        if (-1 == err) {
            if (EEXIST == errno) {
                ALOGI("%s: WARNING: %s package already exists",
                        __func__, NMICINTERLEAVE_PACKAGE);
                break;
            }
            ALOGE("%s: ERROR: ODSP_LOAD_PACKAGE failed %d(%s)",
                        __func__, errno, strerror(errno));
            if (i == (max_attempts - 1)) {
                ALOGE("%s: ERROR: Failed to load package for 5 times, "
                        "aborting init of plugins", __func__);
                goto exit;
            }
        } else {
            break;
        }
    }

    ALOGD("******* Create Plugins **********");
    cdata.type = CONFIG_FILE;
#ifdef ANDROID
    cdata.data.fdata.filename = FOURSTART_MIC_CREATE_CONFIG;
#else
    cdata.data.fdata.filename = CALYPSO_MIC_CREATE_CONFIG;
#endif
    err = iaxxx_odsp_plugin_set_creation_config(hdl->odsp_hdl, HOTWORD_INST_ID,
                                                HOTWORD_BLOCK_ID, cdata);
    if (err != 0) {
        ALOGE("%s: ERROR: Failed to set creation config for calypso mic", __func__);
        goto exit;
    }

    err = iaxxx_odsp_plugin_create(hdl->odsp_hdl, HOTWORD_PLUGIN_IDX,
                                   HOTWORD_PKG_ID, HOTWORD_BLOCK_ID,
                                   HOTWORD_INST_ID, HOTWORD_PRIORITY);
    if (err != 0) {
        ALOGE("%s: ERROR: Failed to create hotword plugin", __func__);
        goto exit;
    }

    // Download the google hotword model

        err = iaxxx_odsp_plugin_set_parameter_blk_from_file(hdl->odsp_hdl,
                                        HOTWORD_INST_ID, HOTWORD_KW_BLK_ID,
                                        HOTWORD_BLOCK_ID, HOTWORD_KW_MODEL);
        if (err != 0) {
            ALOGE("%s: ERROR: Failed to download %s model file", __func__,
                    HOTWORD_KW_MODEL);
            goto exit;
        }



        //Buffer Plugin 1 : Dummy buffer
        cdata.type = CONFIG_FILE;
        cdata.data.fdata.filename = DUMMY_BUFFER_CREATE_CONFIG;
        err = iaxxx_odsp_plugin_set_creation_config(hdl->odsp_hdl, BUFFER_2_INST_ID,
                                                    BUFFER_BLOCK_ID, cdata);
        if (err != 0) {
            ALOGE("%s: ERROR: Failed to set creation config for buffer_2", __func__);
            goto exit;
        }

    //Create Buffer plungin
    err = iaxxx_odsp_plugin_create(hdl->odsp_hdl, BUFFER_PLUGIN_IDX,
                                BUFFER_PKG_ID, BUFFER_BLOCK_ID,
                                BUFFER_2_INST_ID, BUFFER_PRIORITY);
    if (err != 0) {
        ALOGE("%s: ERROR: Failed to create buffer_2 plugin", __func__);
        goto exit;
    }

    cdata.type = CONFIG_FILE;
    cdata.data.fdata.filename = BUFFER_CREATE_CONFIG;
    err = iaxxx_odsp_plugin_set_creation_config(hdl->odsp_hdl, BUFFER_1_INST_ID,
                                                BUFFER_BLOCK_ID, cdata);
    if (err != 0) {
        ALOGE("%s: ERROR: Failed to set creation config for buffer_1", __func__);
        goto exit;
    }

    //Create Buffer plungin
    err = iaxxx_odsp_plugin_create(hdl->odsp_hdl, BUFFER_PLUGIN_IDX,
                                   BUFFER_PKG_ID, BUFFER_BLOCK_ID,
                                   BUFFER_1_INST_ID, BUFFER_PRIORITY);
    if (err != 0) {
        ALOGE("%s: ERROR: Failed to create buffer_1 plugin", __func__);
        goto exit;
    }

    cdata.type = CONFIG_FILE;
    cdata.data.fdata.filename = INTERLEAVE_CREATE_CONFIG;
    err = iaxxx_odsp_plugin_set_creation_config(hdl->odsp_hdl, INTERLEAVE_INST_ID,
                                                INTERLEAVE_BLOCK_ID, cdata);
    if (err != 0) {
        ALOGE("%s: ERROR: Failed to set creation config for INTERLEAVE PLUGIN", __func__);
        goto exit;
    }

    //Create Interleave plugin
    err = iaxxx_odsp_plugin_create(hdl->odsp_hdl, INTERLEAVE_PLUGIN_IDX,
                                   INTERLEAVE_PKG_ID, INTERLEAVE_BLOCK_ID,
                                   INTERLEAVE_INST_ID, INTERLEAVE_PRIORITY);
    if (err != 0) {
        ALOGE("%s: ERROR: Failed to create interleave plugin", __func__);
        goto exit;
    }

    ALOGE("****** Plugins created **********");
    ALOGE("****** Setup Route  *************");
    if (MUSIC_PLAYBACK_STARTED == hdl->music_status) {
        ALOGE("%s: MUSIC_PLAYBACK_STARTED", __func__);
        err = enable_bargein_route(hdl);
    } else {
        ALOGE("%s: MUSIC_PLAYBACK_STOPPED", __func__);
        err = enable_nonbargein_route(hdl);
    }
    if (err != 0) {
        ALOGE("%s: ERROR Failed to setup routes", __func__);
        goto exit;
    }
    ALOGD("*** Route setup successfully ****");

    err = setup_hotword_vq_events(hdl);
    if (err != 0) {
        ALOGE("%s: ERROR Failed to setup vq events", __func__);
        goto exit;
    }

exit:
    return err;
}

/**
 * Stop recognition of the keyword model
 *
 * Input  - vq_hdl - Handle to vq_hal structure
 * Output - 0 on success, on failure < 0
 */
int hotword_stop_recognition(void *handle)
{
    int ret = 0;
    struct hotword_hal *hdl = (struct hotword_hal *) handle;

    if (hdl == NULL) {
        ALOGE("%s: ERROR Null handle sent", __func__);
        ret = -EINVAL;
        goto exit;
    }

    // TODO check music status and then perform teardown
    if (MUSIC_PLAYBACK_STARTED == hdl->music_status) {
        ret = disable_bargein_route(hdl);
    } else {
        ret = disable_nonbargein_route(hdl);
    }

    ret = iaxxx_odsp_plugin_destroy(hdl->odsp_hdl, BUFFER_BLOCK_ID,
                                                    BUFFER_1_INST_ID);
    if (ret != 0) {
        ALOGE("%s: ERROR Failed to destroy the Buffer plugin 1", __func__);
        goto exit;
    }

    ret = iaxxx_odsp_plugin_destroy(hdl->odsp_hdl, HOTWORD_BLOCK_ID,
                                                    HOTWORD_INST_ID);
    if (ret != 0) {
        ALOGE("%s: ERROR Failed to destroy the hotword plugin", __func__);
        goto exit;
    }

    ret = iaxxx_odsp_package_unload(hdl->odsp_hdl, HOTWORD_PKG_ID);
    if (ret != 0) {
        ALOGE("%s: ERROR Failed to unload the hotword package", __func__);
        goto exit;
    }

exit:
    return ret;
}

static bool hotword_process_uevent(void *handle, char *msg, int msg_len,
                                   vq_hal_event_cb cb, void *cookie)
{
    int i = 0, err = 0;
    struct iaxxx_get_event_info ge;
    struct hotword_hal *hdl = (struct hotword_hal*) handle;
    bool is_kw_detected = false;

    for (i = 0; i < msg_len;) {
        if (strstr(msg + i, "IAXXX_VQ_EVENT")) {
            ALOGD("IAXXX_VQ_EVENT");
            ALOGD("Current state %s", evt_to_str(hdl->curr_event_state));

            err = iaxxx_odsp_evt_getevent(hdl->odsp_hdl, &ge);
            if (0 == err) {
                if (EVENT_ID_KW_ID == ge.event_id) {
                    ALOGI("Eventid received is EVENT_ID_KW_ID");
                    hdl->ed.kw_id = ge.data;
                    hdl->curr_event_state = EVENT_0;
                    is_kw_detected = true;
                    ALOGD("New state %s", evt_to_str(hdl->curr_event_state));
                } else if (EVENT_ID_START_FRAME == ge.event_id) {
                    if (EVENT_0 == hdl->curr_event_state) {
                        ALOGD("Eventid received is "
                                "EVENT_ID_START_FRAME");
                        hdl->ed.start_frame = ge.data;
                        hdl->curr_event_state = EVENT_1;
                        ALOGD("New state %s",
                                evt_to_str(hdl->curr_event_state));
                    } else {
                        ALOGE("Incorrect event ID received, expecting "
                                "%s got %s", evt_to_str(EVENT_0),
                                "EVENT_ID_START_FRAME");
                    }
                } else if (EVENT_ID_END_FRAME == ge.event_id) {
                    if (EVENT_1 == hdl->curr_event_state) {
                        ALOGD("Eventid received is EVENT_ID_END_FRAME");
                        hdl->ed.end_frame = ge.data;
                        hdl->curr_event_state = EVENT_2;
                        ALOGD("New state %s",
                                evt_to_str(hdl->curr_event_state));
                    } else {
                        ALOGE("Incorrect event ID received, expecting "
                                "%s got %s", evt_to_str(EVENT_1),
                                "EVENT_ID_END_FRAME");
                    }
                } else if (EVENT_ID_KW_REJECT == ge.event_id) {
                    ALOGD("Eventid received is EVENT_ID_KW_REJECT resetting"
                          " event state machine");
                    hdl->curr_event_state = START;
                } else {
                    ALOGE("Unknown event id received, ignoring %d",
                            ge.event_id);
                }
            } else {
                ALOGE("get_event failed with error %d", err);
            }
        }

        i += strlen(msg + i) + 1;
    }

        if(true == is_kw_detected)
        {
            cb(cookie, EVENT_KEYWORD_RECOGNITION, &hdl->ed);
        // Reset all states
            hdl->curr_event_state = START;
            hdl->ed.start_frame = 0;
            hdl->ed.end_frame = 0;
            hdl->ed.kw_id = -1;
        }
 //   }

    return is_kw_detected;
}

int hotword_start_audio(void *handle, bool is_multiturn) {
    int ret = 0;
    struct hotword_hal *hdl = (struct hotword_hal *) handle;
    struct kt_config kc;
    struct kt_preroll kp;

    if (hdl == NULL) {
        ALOGE("%s: ERROR Null handle sent", __func__);
        ret = -EINVAL;
        goto exit;
    }

    if (NULL != hdl->kt_pcm_hdl) {
        ALOGE("%s: ERROR Starting audio when already started", __func__);
        ret = -EINVAL;
        goto exit;
    }

    if (true == is_multiturn) {
        ALOGD("%s: Streaming from mic end point", __func__);
        kc.end_point = DEBUG_END_POINT;
        hdl->is_multiturn_in_prog = true;
    } else {
        ALOGD("%s: Streaming from Buffer end point", __func__);
        kc.end_point = BUFFER_END_POINT;
    }
    kc.tunnel_output_buffer_size = TUN_BUF_SIZE;
    //kc.dev_id = CHELSEA_DEV_ID;

    kp.preroll_en = false;
    kp.preroll_time_in_ms = 0;
    kp.kw_start_frame = 0;
    kp.frame_size_in_ms = HOTWORD_VQ_FRAME_SIZE_IN_MS;

    hdl->kt_pcm_hdl = kt_pcm_open(&kc, &kp);
    if (NULL == hdl->kt_pcm_hdl) {
        ALOGE("%s: ERROR Failed to open kt_pcm_open", __func__);
        hdl->is_multiturn_in_prog = false;
        ret = -1;
        goto exit;
    }

    hdl->is_first_100ms_dropped = false;

exit:
    return ret;
}

int hotword_stop_audio(void *handle) {
    int err = 0;
    struct hotword_hal *hdl = (struct hotword_hal *) handle;

    if (hdl == NULL || hdl->kt_pcm_hdl == NULL) {
        ALOGE("%s: ERROR Null handle sent", __func__);
        err = -EINVAL;
        goto exit;
    }

    err = kt_pcm_close(hdl->kt_pcm_hdl);
    if (0 != err) {
        ALOGE("%s: ERROR Failed to close kt_pcm_close", __func__);
    }
    hdl->kt_pcm_hdl = NULL;

exit:
    return err;
}

int hotword_read_audio(void *handle, void *data, int size) {
    int bytes = 0;
    struct hotword_hal *hdl = (struct hotword_hal *) handle;
#ifdef ENABLE_DUMP
    FILE *fp = fopen("/home/root/asr_dump.pcm", "ab");
#endif

    if (hdl == NULL || hdl->kt_pcm_hdl == NULL) {
        ALOGE("%s: ERROR Null handle sent", __func__);
        bytes = -EINVAL;
        goto exit;
    }

    if (false == hdl->is_first_100ms_dropped &&
        false == hdl->is_multiturn_in_prog) {
        const int bytes_to_drop = 16000 * 2 * 1 * 100 / 1000;
        unsigned char buf[bytes_to_drop];
        bytes = kt_pcm_read(hdl->kt_pcm_hdl, &buf, bytes_to_drop);
        if (bytes != bytes_to_drop) {
            ALOGE("%s: WARNING: Failed to drop the first 100ms!!", __func__);
        }

        hdl->is_first_100ms_dropped = true;
    }

    bytes = kt_pcm_read(hdl->kt_pcm_hdl, data, size);

#ifdef ENABLE_DUMP
    fwrite(data, 1, bytes, fp);
    fflush(fp);
    fclose(fp);
#endif

exit:
    return bytes;
}

float hotword_audio_frame_len(void *handle) {
    return HOTWORD_VQ_FRAME_SIZE_IN_MS;
}

int hotword_resume_recognition(void *handle) {
    int ret = 0;
    struct hotword_hal *hdl = (struct hotword_hal*) handle;

    if (hdl == NULL) {
        ALOGE("%s: ERROR Null handle sent", __func__);
        ret = -EINVAL;
        goto exit;
    }

    set_mixer_ctl_val(hdl->mixer_hdl, "PDM I2S Gen2 Setup", 1);

exit:
    return ret;
}

int hotword_pause_recognition(void *handle) {
    int ret = 0;
    struct hotword_hal *hdl = (struct hotword_hal*) handle;

    if (hdl == NULL) {
        ALOGE("%s: ERROR Null handle sent", __func__);
        ret = -EINVAL;
        goto exit;
    }

    set_mixer_ctl_val(hdl->mixer_hdl, "PDM I2S Gen2 Setup", 0);

exit:
    return ret;
}

int hotword_set_music_status(void *handle, vq_hal_music_status status) {
    int err = 0;
    struct hotword_hal *hdl = (struct hotword_hal*) handle;

    if (hdl == NULL) {
        ALOGE("%s: ERROR Null handle sent", __func__);
        err = -EINVAL;
        goto exit;
    }

    // We are already is the same status nothing to do
    if (hdl->music_status == status) {
        ALOGE("%s: We are already in the required state nothing to do", __func__);
        err = 0;
        goto exit;
    }

    hdl->music_status = status;
    if (status == MUSIC_PLAYBACK_STARTED) {
        disable_nonbargein_route(hdl);
        enable_bargein_route(hdl);
    } else {
        disable_bargein_route(hdl);
        enable_nonbargein_route(hdl);
    }

exit:
    return err;
}

struct vq_plugin algo_plugin = {
    .init = hotword_init,
    .deinit = hotword_deinit,
    .start_recognition = hotword_start_recognition,
    .stop_recognition = hotword_stop_recognition,
    .process_uevent = hotword_process_uevent,
    .start_audio = hotword_start_audio,
    .stop_audio = hotword_stop_audio,
    .read_audio = hotword_read_audio,
    .get_audio_frame_length = hotword_audio_frame_len,
    .resume_recognition = hotword_resume_recognition,
    .pause_recognition = hotword_pause_recognition,
    .set_music_status = hotword_set_music_status
};
