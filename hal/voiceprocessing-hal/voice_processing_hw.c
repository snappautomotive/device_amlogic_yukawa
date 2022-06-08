#define LOG_TAG "ia_voice_processing_hal"
#define LOG_NDEBUG 0

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <string.h>
#include <math.h>

#include <log/log.h>
#include <tinyalsa/asoundlib.h>

#include <uapi/linux/mfd/adnc/iaxxx-odsp.h>

#include "voice_processing_hw.h"
#include "ia_constants.h"

#define FUNCTION_ENTRY_LOG ALOGV("Entering %s", __func__);
#define FUNCTION_EXIT_LOG ALOGV("Exiting %s", __func__);

#define DEV_NODE "/dev/iaxxx-odsp-celldrv"

// Param ID's defined by DSP
#define DSP_KSP_PEQ_RAF_ENABLE (1)
#define DSP_KSP_MBC_RAF_ALL_PARAMS (0)
#define DSP_KSP_MBC_RAF_PARAMS_DOWNSTREAM_GAIN_DB (3)
#define DSP_KSP_MBC_RAF_ENABLE (1)
#define DSP_KSP_MBC_RAF_AUTOMAKEUP_GAIN (2)

struct ia_voice_processing_hal {
    FILE *dev_node;
};

/**
 * Enable the Voice Processing Module, needs to be called only once.
 *
 * Input  - NA
 * Output - Handle to the VoieProcessing HAL.
 *          Zero on failure, non zero on Success
 */
struct ia_voice_processing_hal* ia_enable_voice_processing() {
    struct ia_voice_processing_hal * vpd;

    vpd = (struct ia_voice_processing_hal*) malloc(sizeof(struct ia_voice_processing_hal));
    if (NULL == vpd) {
        ALOGE("%s: ERROR Failed to allocated memory for ia_voice_processing_hal", __func__);
        return NULL;
    }

    if((vpd->dev_node = fopen(DEV_NODE, "rw")) == NULL) {
        ALOGE("%s: ERROR file %s open for write error: %s\n", __func__, DEV_NODE, strerror(errno));
        free(vpd);
        return NULL;
    }

    return vpd;
}

/**
 * Disables the Voice Processing Module. If there are any active algorithms they
 * will be disabled. The listener cb will be de-registered and no more events
 * can be received from the Voice Processing module.
 *
 * Input  - vp_hdl - Handle to the VoiceProcessing HAL.
 * Output - Zero on success, errno on failure.
 */
int ia_disable_voice_processing(struct ia_voice_processing_hal *vpd) {
    if (NULL == vpd) {
        ALOGE("%s: ERROR NULL handle sent", __func__);
        return -1;
    }

    if (vpd->dev_node)
        fclose(vpd->dev_node);

    free(vpd);
    return 0;
}

static bool ia_check_param_id_validity(enum ia_param_id id) {
    bool is_valid;

    switch (id) {
    case VP_PROCESSING_MODE:
    case VP_MIC_TO_USE_WHEN_DISABLED:

    case AEC_ENABLE:
    case AEC_LEARNING_RATE:
    case AEC_REF_DELAY:

    case SDE_ENABLE:
    case SDE_SAL_THR:
    case SDE_LOG_FLOOR:
    case SDE_GAMMA:
    case SDE_NUM_DIRECTIONS:
    case SDE_SOURCE_TRACKING_ENABLE:
    case SDE_SOURCE_COUNT_BUFFER_S:
    case SDE_FILTER_HANGON_TIME_S:

    case TGTENH_HL_BEAMFORMER_MODE:
    case TGTENH_HL_TARGET_DIRECTION:
    case TGTENH_HL_TARGET_DIRECTION_WIDTH:
    case TGTENH_HL_NOISE_TRACKING_SMOOTHING_CONST:
    case TGTENH_HL_NT_ENABLE:
    case TGTENH_HL_NOISE_TRACKING_VAD:
    case TGTENH_HL_TARGET_DETECTION_NT_MASK_THRESH:
    case TGTENH_HL_TARGET_DETECTION_SENSITIVITY:
    case TGTENH_HL_TARGET_DETECTION_VAD:
    case TGTENH_HL_SF_CANCELLER_TIME_CONST:
    case TGTENH_HL_SF_TARGET_BEARING_TIME_CONST:
    case TGTENH_HL_SUP_MODE:
    case TGTENH_HL_SUP_MAX_STAT_SUPPRESS_dB:
    case TGTENH_HL_SUP_CONTROL:
    case TGTENH_HL_SUP_ECHO_DT_BACKOFF:
    case TGTENH_HL_SUP_ECHO_NEAD_THRESH:
    case TGTENH_HL_SUP_ECHO_GAIN_THRESH:
    case TGTENH_HL_SUP_ECHO_GAIN_RATIO:
    case TGTENH_HL_SUP_MAX_ECHO_SUPPRESS_DB:
    case TGTENH_HL_SUP_MAX_MODEL_SUPPRESS_DB:
    case TGTENH_HL_MASK_BACKOFF_DB:

    case TGTENH_ML_BEAMFORMER_MODE:
    case TGTENH_ML_TARGET_DIRECTION:
    case TGTENH_ML_TARGET_DIRECTION_WIDTH:
    case TGTENH_ML_NOISE_TRACKING_SMOOTHING_CONST:
    case TGTENH_ML_NT_ENABLE:
    case TGTENH_ML_NOISE_TRACKING_VAD:
    case TGTENH_ML_TARGET_DETECTION_SENSITIVITY:
    case TGTENH_ML_TARGET_DETECTION_VAD:
    case TGTENH_ML_TARGET_DETECTION_NT_MASK_THRESH:
    case TGTENH_ML_SF_CANCELLER_TIME_CONST:
    case TGTENH_ML_SF_TARGET_BEARING_TIME_CONST:
    case TGTENH_ML_SUP_MODE:
    case TGTENH_ML_SUP_MAX_STAT_SUPPRESS_dB:
    case TGTENH_ML_SUP_CONTROL:
    case TGTENH_ML_SUP_ECHO_DT_BACKOFF:
    case TGTENH_ML_SUP_ECHO_NEAD_THRESH:
    case TGTENH_ML_SUP_ECHO_GAIN_THRESH:
    case TGTENH_ML_SUP_ECHO_GAIN_RATIO:
    case TGTENH_ML_SUP_MAX_ECHO_SUPPRESS_DB:
    case TGTENH_ML_SUP_MAX_MODEL_SUPPRESS_DB:
    case TGTENH_ML_MASK_BACKOFF_DB:

    case AGC_HL_ENABLE:
    case AGC_HL_TARGET_LEVEL:
    case AGC_HL_ATTACK_TIME_S:
    case AGC_HL_DECAY_TIME_S:
    case AGC_HL_COMP_THRESH_DB:
    case AGC_HL_COMP_RATIO:
    case AGC_HL_NOISE_FLOOR:
    case AGC_HL_HYSTERESIS:

    case AGC_ML_ENABLE:
    case AGC_ML_TARGET_LEVEL:
    case AGC_ML_ATTACK_TIME_S:
    case AGC_ML_DECAY_TIME_S:
    case AGC_ML_COMP_THRESH_DB:
    case AGC_ML_COMP_RATIO:
    case AGC_ML_NOISE_FLOOR:
    case AGC_ML_HYSTERESIS:

    case PEQ_ENABLE:

    case MBC_RAF_ALL_PARAMS:
    case MBC_RAF_PARAMS_DOWNSTREAM_GAIN:
    case MBC_ENABLE:
    case MBC_RAF_AUTOMAKEUP_GAIN:
        is_valid = true;
    break;

    default:
        ALOGE("Unknown ID received");
        is_valid = false;
    break;
    }

    return is_valid;
}

static enum ia_param_val_type ia_get_data_type(enum ia_param_id id) {
    enum ia_param_val_type type = FLOAT;

    switch (id) {
    // Int values
    case VP_PROCESSING_MODE:
    case VP_MIC_TO_USE_WHEN_DISABLED:

    case AEC_ENABLE:

    case SDE_ENABLE:
    case SDE_NUM_DIRECTIONS:
    case SDE_SOURCE_TRACKING_ENABLE:

    case TGTENH_HL_TARGET_DIRECTION_WIDTH:
    case TGTENH_HL_NT_ENABLE:
    case TGTENH_HL_NOISE_TRACKING_VAD:
    case TGTENH_HL_TARGET_DETECTION_VAD:
    case TGTENH_HL_BEAMFORMER_MODE:
    case TGTENH_HL_SUP_MODE:

    case TGTENH_ML_TARGET_DIRECTION_WIDTH:
    case TGTENH_ML_NT_ENABLE:
    case TGTENH_ML_NOISE_TRACKING_VAD:
    case TGTENH_ML_TARGET_DETECTION_VAD:
    case TGTENH_ML_BEAMFORMER_MODE:
    case TGTENH_ML_SUP_MODE:

    case AGC_HL_ENABLE:

    case AGC_ML_ENABLE:

    case PEQ_ENABLE:

    case MBC_ENABLE:
    case MBC_RAF_AUTOMAKEUP_GAIN:
       type = INT;
    break;

    // ----- End Fall through -----

    // Float values
    case AEC_LEARNING_RATE:
    case AEC_REF_DELAY:

    case SDE_SAL_THR:
    case SDE_LOG_FLOOR:
    case SDE_GAMMA:
    case SDE_SOURCE_COUNT_BUFFER_S:
    case SDE_FILTER_HANGON_TIME_S:

    case TGTENH_HL_TARGET_DIRECTION:
    case TGTENH_HL_NOISE_TRACKING_SMOOTHING_CONST:
    case TGTENH_HL_TARGET_DETECTION_NT_MASK_THRESH:
    case TGTENH_HL_TARGET_DETECTION_SENSITIVITY:
    case TGTENH_HL_SF_CANCELLER_TIME_CONST:
    case TGTENH_HL_SF_TARGET_BEARING_TIME_CONST:
    case TGTENH_HL_SUP_MAX_STAT_SUPPRESS_dB:
    case TGTENH_HL_SUP_CONTROL:
    case TGTENH_HL_SUP_ECHO_DT_BACKOFF:
    case TGTENH_HL_SUP_ECHO_NEAD_THRESH:
    case TGTENH_HL_SUP_ECHO_GAIN_THRESH:
    case TGTENH_HL_SUP_ECHO_GAIN_RATIO:
    case TGTENH_HL_SUP_MAX_ECHO_SUPPRESS_DB:
    case TGTENH_HL_SUP_MAX_MODEL_SUPPRESS_DB:
    case TGTENH_HL_MASK_BACKOFF_DB:

    case TGTENH_ML_TARGET_DIRECTION:
    case TGTENH_ML_NOISE_TRACKING_SMOOTHING_CONST:
    case TGTENH_ML_TARGET_DETECTION_SENSITIVITY:
    case TGTENH_ML_TARGET_DETECTION_NT_MASK_THRESH:
    case TGTENH_ML_SF_CANCELLER_TIME_CONST:
    case TGTENH_ML_SF_TARGET_BEARING_TIME_CONST:
    case TGTENH_ML_SUP_MAX_STAT_SUPPRESS_dB:
    case TGTENH_ML_SUP_CONTROL:
    case TGTENH_ML_SUP_ECHO_DT_BACKOFF:
    case TGTENH_ML_SUP_ECHO_NEAD_THRESH:
    case TGTENH_ML_SUP_ECHO_GAIN_THRESH:
    case TGTENH_ML_SUP_ECHO_GAIN_RATIO:
    case TGTENH_ML_SUP_MAX_ECHO_SUPPRESS_DB:
    case TGTENH_ML_SUP_MAX_MODEL_SUPPRESS_DB:
    case TGTENH_ML_MASK_BACKOFF_DB:

    case AGC_HL_TARGET_LEVEL:
    case AGC_HL_ATTACK_TIME_S:
    case AGC_HL_DECAY_TIME_S:
    case AGC_HL_COMP_THRESH_DB:
    case AGC_HL_COMP_RATIO:
    case AGC_HL_NOISE_FLOOR:
    case AGC_HL_HYSTERESIS:

    case AGC_ML_TARGET_LEVEL:
    case AGC_ML_ATTACK_TIME_S:
    case AGC_ML_DECAY_TIME_S:
    case AGC_ML_COMP_THRESH_DB:
    case AGC_ML_COMP_RATIO:
    case AGC_ML_NOISE_FLOOR:
    case AGC_ML_HYSTERESIS:

    case MBC_RAF_ALL_PARAMS:
    case MBC_RAF_PARAMS_DOWNSTREAM_GAIN:
        type = FLOAT;
    break;

    default:
        ALOGE("Invalid param id passed");
    break;
    }

    return type;
}

/**
 * Set the algo parameters
 *
 * Input  - vp_hdl - Handle to the VoiceProcessing HAL.
 *          data   - Algo param data structure instance
 * Output - Zero on success, errno on failure.
 */
int ia_set_algo_param(struct ia_voice_processing_hal *vp_hdl, struct ia_algo_param iap) {
    FUNCTION_ENTRY_LOG;
    int err = 0;
    struct iaxxx_plugin_param pp;

    if (NULL == vp_hdl) {
        ALOGE("%s: NULL handle passed", __func__);
        err = -EINVAL;
        goto exit;
    }

    if (false == ia_check_param_id_validity(iap.id)) {
        ALOGE("Invalid param id is passed");
        err = -EINVAL;
        goto exit;
    }

    if (PEQ_ENABLE == iap.id) {
        // Should be wrt to PEQ plugin
        pp.inst_id = PEQ_INST_ID;
        pp.block_id = PEQ_BLOCK_ID;
        pp.param_id = DSP_KSP_PEQ_RAF_ENABLE;
    } else if (MBC_RAF_ALL_PARAMS == iap.id || MBC_RAF_PARAMS_DOWNSTREAM_GAIN == iap.id || MBC_ENABLE == iap.id ||
               MBC_RAF_AUTOMAKEUP_GAIN == iap.id) {
        // Should be wrt to MBC plugin
        pp.inst_id = MBC_INST_ID;
        pp.block_id = MBC_BLOCK_ID;
        switch (iap.id) {
        case MBC_RAF_ALL_PARAMS:
            pp.param_id = DSP_KSP_MBC_RAF_ALL_PARAMS;
        break;
        case MBC_RAF_PARAMS_DOWNSTREAM_GAIN:
            pp.param_id = DSP_KSP_MBC_RAF_PARAMS_DOWNSTREAM_GAIN_DB;
        break;
        case MBC_ENABLE:
            pp.param_id = DSP_KSP_MBC_RAF_ENABLE;
        break;
        case MBC_RAF_AUTOMAKEUP_GAIN:
            pp.param_id = DSP_KSP_MBC_RAF_AUTOMAKEUP_GAIN;
        break;
        default:
            ALOGE("Something went terribly wrong");
            err = -EINVAL;
            goto exit;
        break;
        }
    } else {
        // These should be wrt to the VP Plugin create
        pp.inst_id      = VP_INST_ID;
        pp.block_id     = VP_BLOCK_ID;

        pp.param_id = iap.id;
    }

    switch (iap.val_type) {
    case INT:
        ALOGD("pp.param_id 0x%X, param value %d", pp.param_id, iap.val.i_data);
        pp.param_val = iap.val.i_data;
    break;
    case FLOAT:
        ALOGD("pp.param_id 0x%X, param value %f", pp.param_id, iap.val.f_data);
        memcpy(&pp.param_val, &iap.val.f_data, sizeof(float));
    break;
    default:
        ALOGE("Invalid data type enum");
        err = -EINVAL;
        goto exit;
    break;
    }

    err = ioctl(fileno(vp_hdl->dev_node), ODSP_PLG_SET_PARAM, (unsigned long) &pp);
    if (-1 == err) {
        ALOGE("%s: ERROR: ODSP_PLG_SET_PARAM IOCTL failed with error %d(%s)", __func__, errno, strerror(errno));
        return err;
    }

exit:
    return err;
}

/**
 * Get the current value of an algo parameter
 *
 * Input  - vp_hdl - Handle to the VoiceProcessing HAL.
 *          data   - Algo param data structure instance
 * Output - Zero on success, errno on failure.
 *          data   - Algo param data structure instance
 */
int ia_get_algo_param(struct ia_voice_processing_hal *vp_hdl, struct ia_algo_param *iap) {
    FUNCTION_ENTRY_LOG;
    int err = 0;
    struct iaxxx_plugin_param pp;
    enum ia_param_val_type type;

    if (NULL == vp_hdl) {
        ALOGE("%s: NULL handle passed", __func__);
        err = -EINVAL;
        goto exit;
    }

    if (false == ia_check_param_id_validity(iap->id)) {
        ALOGE("Invalid param id is passed");
        err = -EINVAL;
        goto exit;
    }

    if (PEQ_ENABLE == iap->id) {
        // Should be wrt to PEQ plugin
        pp.inst_id = PEQ_INST_ID;
        pp.block_id = PEQ_BLOCK_ID;
        pp.param_id = DSP_KSP_PEQ_RAF_ENABLE;
    } else if (MBC_RAF_ALL_PARAMS == iap->id || MBC_RAF_PARAMS_DOWNSTREAM_GAIN == iap->id || MBC_ENABLE == iap->id ||
               MBC_RAF_AUTOMAKEUP_GAIN == iap->id) {
        // Should be wrt to MBC plugin
        pp.inst_id = MBC_INST_ID;
        pp.block_id = MBC_BLOCK_ID;
        switch (iap->id) {
        case MBC_RAF_ALL_PARAMS:
            pp.param_id = DSP_KSP_MBC_RAF_ALL_PARAMS;
        break;
        case MBC_RAF_PARAMS_DOWNSTREAM_GAIN:
            pp.param_id = DSP_KSP_MBC_RAF_PARAMS_DOWNSTREAM_GAIN_DB;
        break;
        case MBC_ENABLE:
            pp.param_id = DSP_KSP_MBC_RAF_ENABLE;
        break;
        case MBC_RAF_AUTOMAKEUP_GAIN:
            pp.param_id = DSP_KSP_MBC_RAF_AUTOMAKEUP_GAIN;
        break;
        default:
            ALOGE("Something went terribly wrong");
            err = -EINVAL;
            goto exit;
        break;
        }
    } else {
        // These should be wrt to the VP Plugin create
        pp.inst_id      = VP_INST_ID;
        pp.block_id     = VP_BLOCK_ID;

        pp.param_id = iap->id;
    }

    err = ioctl(fileno(vp_hdl->dev_node), ODSP_PLG_GET_PARAM, (unsigned long) &pp);
    if (-1 == err) {
        ALOGE("%s: ERROR: ODSP_PLG_GET_PARAM IOCTL failed with error %d(%s)",
                                            __func__, errno, strerror(errno));
        return err;
    }

    type = ia_get_data_type(pp.param_id);
    switch (type) {
    case INT:
        iap->val.i_data = pp.param_val;
        iap->val_type = INT;
        ALOGD("Get param returned pp.param_id 0x%X, param value %d",
                                            pp.param_id, iap->val.i_data);
    break;
    case FLOAT:
        memcpy(&iap->val.f_data, &pp.param_val, sizeof(float));
        iap->val_type = FLOAT;
        ALOGD("Get param returned pp.param_id 0x%X, param value %f",
                                            pp.param_id, iap->val.f_data);
    break;
    }

exit:
    return err;

}

/**
 * Reset the plugin to it's default state
 *
 * Input  - vp_hdl - Handle to the VoiceProcessing HAL.
 *          plugin_type - The plugin that needs to be reset.
 * Output - Zero on success, errno on failure.
 */
int ia_reset_plugin(struct ia_voice_processing_hal *vp_hdl, enum ia_plugin plugin_type) {
    FUNCTION_ENTRY_LOG;
    int err = 0;
    struct iaxxx_plugin_info pi;

    if (NULL == vp_hdl) {
        ALOGE("%s: NULL handle passed", __func__);
        err = -EINVAL;
        goto exit;
    }

    if (plugin_type < VQ || plugin_type >= IA_PLUGIN_MAX) {
        ALOGE("Invalid plugin type is passed");
        err = -EINVAL;
        goto exit;
    }

    // Unused so set them to 0
    pi.pkg_id = 0;
    pi.plg_idx = 0;
    pi.priority = 0;

    switch (plugin_type) {
    case VQ:
        pi.block_id = VT_BLOCK_ID;
        pi.inst_id = VT_INST_ID;
    break;
    case VP:
        pi.block_id = VP_BLOCK_ID;
        pi.inst_id = VP_INST_ID;
    break;
    case BUFFER:
        pi.block_id = BUFFER_BLOCK_ID;
        pi.inst_id = BUFFER_INST_ID;
    break;
    case MIXER:
        pi.block_id = MIXER_BLOCK_ID;
        pi.inst_id = MIXER_INST_ID;
    break;
    case MBC:
        pi.block_id = MBC_BLOCK_ID;
        pi.inst_id = MBC_INST_ID;
    break;
    case PEQ:
        pi.block_id = PEQ_BLOCK_ID;
        pi.inst_id = PEQ_INST_ID;
    break;
    case IA_PLUGIN_MAX:
        // fall through
    default:
        ALOGE("Invalid plugin type");
        err = -EINVAL;
        goto exit;
    break;
    }

    err = ioctl(fileno(vp_hdl->dev_node), ODSP_PLG_RESET, (unsigned long) &pi);
    if (-1 == err) {
        ALOGE("%s: ERROR: ODSP_PLG_RESET IOCTL failed with error %d(%s)", __func__, errno, strerror(errno));
        return err;
    }

exit:
    return err;
}

/**
 * Set the parameter block for a plugin. Currently only PEQ, MBC plugins can
 * accept param data block
 *
 * Input  - vp_hdl - Handle to the VoiceProcessing HAL.
 *          data   - Plugin param data structure instance
 * Output - Zero on success, errno on failure.
 */
int ia_set_plugin_param_block(struct ia_voice_processing_hal *vp_hdl, struct ia_plugin_param_block data) {
    FUNCTION_ENTRY_LOG;
    int err = 0;
    struct iaxxx_plugin_param_blk ppb;

    if (NULL == vp_hdl) {
        ALOGE("%s: NULL handle passed", __func__);
        err = -EINVAL;
        goto exit;
    }

    switch (data.plugin_id) {
    case VP:
        ppb.block_id = VP_BLOCK_ID;
        ppb.inst_id = VP_INST_ID;
        ppb.id = VP_PARAM_BLOCK_ID;
    break;
    case PEQ:
        ppb.block_id = PEQ_BLOCK_ID;
        ppb.inst_id = PEQ_INST_ID;
        ppb.id = PEQ_PARAM_BLOCK_ID;
    break;
    case MBC:
        ppb.block_id = MBC_BLOCK_ID;
        ppb.inst_id = MBC_INST_ID;
        ppb.id = MBC_PARAM_BLOCK_ID;
    break;
    default:
        ALOGE("Invalid plugin id, supported plugins are VP, MBC and PEQ");
        err = -EINVAL;
        goto exit;
    }
    ppb.param_size = data.buf_sz;
    ppb.param_blk = (uintptr_t)data.buf;
    ppb.file_name[0] = '\0';
    err = ioctl(fileno(vp_hdl->dev_node), ODSP_PLG_SET_PARAM_BLK, &ppb);
    if (-1 == err) {
        ALOGE("%s: ERROR: ODSP_PLG_SET_PARAM_BLK IOCTL failed with error %d(%s)\n", __func__, errno, strerror(errno));
        return err;
    }

exit:
    return err;
}

