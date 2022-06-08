#ifndef _VOICE_PROCESSING_HW_H_
#define _VOICE_PROCESSING_HW_H_

#include <stdbool.h>

#if __cplusplus
extern "C"
{
#endif

// Please refer to algorithm documentation for information about these parameters
enum ia_param_id {
    VP_PROCESSING_MODE = 0x00000001,
    VP_MIC_TO_USE_WHEN_DISABLED = 0x00000002,

    AEC_ENABLE = 0x00010001,
    AEC_LEARNING_RATE = 0x00010002,
    AEC_REF_DELAY = 0x00010003,

    SDE_ENABLE = 0x00020001,
    SDE_SAL_THR = 0x00020002,
    SDE_LOG_FLOOR = 0x00020003,
    SDE_GAMMA = 0x00020004,
    SDE_NUM_DIRECTIONS = 0x00020005,
    SDE_MULTI_DIR_THR = 0x00020006,             // Deprecated
    SDE_MULTI_DIR_GAMMA = 0x00020007,           // Deprecated
    SDE_SOURCE_TRACKING_ENABLE = 0x00020008,
    SDE_SOURCE_COUNT_BUFFER_S = 0x00020009,
    SDE_FILTER_HANGON_TIME_S = 0x0002000A,

    TGTENH_HL_TARGET_DIRECTION = 0x00030001,
    TGTENH_HL_TARGET_DIRECTION_WIDTH = 0x00030002,
    TGTENH_HL_NOISE_TRACKING_SMOOTHING_CONST = 0x00030003,
    TGTENH_HL_NT_ENABLE = 0x00030004,
    TGTENH_HL_NOISE_TRACKING_VAD = 0x00030005,
    TGTENH_HL_TARGET_DETECTION_SENSITIVITY = 0x00031001,
    TGTENH_HL_TARGET_DETECTION_VAD = 0x00031002,
    TGTENH_HL_TARGET_DETECTION_NT_MASK_THRESH = 0x00031003,
    TGTENH_HL_BEAMFORMER_MODE = 0x00032001,
    TGTENH_HL_SF_CANCELLER_TIME_CONST = 0x00032002,
    TGTENH_HL_SF_TARGET_BEARING_TIME_CONST = 0x00032003,
    TGTENH_HL_SUP_MODE = 0x00033001,
    TGTENH_HL_SUP_MAX_STAT_SUPPRESS_dB = 0x00033002,
    TGTENH_HL_SUP_CONTROL = 0x00033003,
    TGTENH_HL_SUP_ECHO_DT_BACKOFF = 0x00033004,
    TGTENH_HL_SUP_ECHO_NEAD_THRESH = 0x00033005,
    TGTENH_HL_SUP_ECHO_GAIN_THRESH = 0x00033006,
    TGTENH_HL_SUP_ECHO_GAIN_RATIO = 0x00033007,
    TGTENH_HL_SUP_MAX_ECHO_SUPPRESS_DB = 0x00033008,
    TGTENH_HL_SUP_MAX_MODEL_SUPPRESS_DB = 0x00033009,
    TGTENH_HL_MASK_BACKOFF_DB = 0x0003300A,

    TGTENH_ML_TARGET_DIRECTION = 0x00030101,
    TGTENH_ML_TARGET_DIRECTION_WIDTH = 0x00030102,
    TGTENH_ML_NOISE_TRACKING_SMOOTHING_CONST = 0x00030103,
    TGTENH_ML_NT_ENABLE = 0x00030104,
    TGTENH_ML_NOISE_TRACKING_VAD = 0x00030105,
    TGTENH_ML_TARGET_DETECTION_SENSITIVITY = 0x00031101,
    TGTENH_ML_TARGET_DETECTION_VAD = 0x00031102,
    TGTENH_ML_TARGET_DETECTION_NT_MASK_THRESH = 0x00031103,
    TGTENH_ML_BEAMFORMER_MODE = 0x00032101,
    TGTENH_ML_SF_CANCELLER_TIME_CONST = 0x00032102,
    TGTENH_ML_SF_TARGET_BEARING_TIME_CONST = 0x00032103,
    TGTENH_ML_SUP_MODE = 0x00033101,
    TGTENH_ML_SUP_MAX_STAT_SUPPRESS_dB = 0x00033102,
    TGTENH_ML_SUP_CONTROL = 0x00033103,
    TGTENH_ML_SUP_ECHO_DT_BACKOFF = 0x00033104,
    TGTENH_ML_SUP_ECHO_NEAD_THRESH = 0x00033105,
    TGTENH_ML_SUP_ECHO_GAIN_THRESH = 0x00033106,
    TGTENH_ML_SUP_ECHO_GAIN_RATIO = 0x00033107,
    TGTENH_ML_SUP_MAX_ECHO_SUPPRESS_DB = 0x00033108,
    TGTENH_ML_SUP_MAX_MODEL_SUPPRESS_DB = 0x00033109,
    TGTENH_ML_MASK_BACKOFF_DB = 0x0003310A,

    AGC_HL_ENABLE = 0x00060001,
    AGC_HL_TARGET_LEVEL = 0x00060002,
    AGC_HL_ATTACK_TIME_S = 0x00060003,
    AGC_HL_DECAY_TIME_S = 0x00060004,
    AGC_HL_COMP_THRESH_DB = 0x00060005,
    AGC_HL_COMP_RATIO = 0x00060006,
    AGC_HL_NOISE_FLOOR = 0x00060007,
    AGC_HL_HYSTERESIS = 0x00060008,

    AGC_ML_ENABLE = 0x00060101,
    AGC_ML_TARGET_LEVEL = 0x00060102,
    AGC_ML_ATTACK_TIME_S = 0x00060103,
    AGC_ML_DECAY_TIME_S = 0x00060104,
    AGC_ML_COMP_THRESH_DB = 0x00060105,
    AGC_ML_COMP_RATIO = 0x00060106,
    AGC_ML_NOISE_FLOOR = 0x00060107,
    AGC_ML_HYSTERESIS = 0x00060108,

    PEQ_ENABLE = 0x00100001,

    MBC_RAF_ALL_PARAMS = 0x00200001,
    MBC_RAF_PARAMS_DOWNSTREAM_GAIN = 0x00200002,
    MBC_ENABLE = 0x00200003,
    MBC_RAF_AUTOMAKEUP_GAIN = 0x00200004
};

enum ia_param_val_type {
    FLOAT,
    INT
};

union ia_param_val {
    float f_data;
    int i_data;
};

struct ia_algo_param {
    enum ia_param_id id;
    union ia_param_val val;
    enum ia_param_val_type val_type;
};

enum ia_plugin {
    VQ = 0,
    VP,
    BUFFER,
    MIXER,
    MBC,
    PEQ,
    IA_PLUGIN_MAX
};

struct ia_plugin_param_block {
    enum ia_plugin plugin_id;
    void *buf;
    unsigned int buf_sz;
};

struct ia_voice_processing_hal;

/**
 * Enable the Voice Processing Module, needs to be called only once.
 *
 * Input  - NA
 * Output - Handle to the VoieProcessing HAL.
 *          NULL on failure, non null on Success
 */
struct ia_voice_processing_hal* ia_enable_voice_processing();

/**
 * Disables the Voice Processing Module. If there are any active algorithms they
 * will be disabled. The listener cb will be de-registered and no more events
 * can be received from the Voice Processing module.
 *
 * Input  - vp_hdl - Handle to the VoiceProcessing HAL.
 * Output - Zero on success, errno on failure.
 */
int ia_disable_voice_processing(struct ia_voice_processing_hal *vp_hdl);

/**
 * Set the algo parameters
 *
 * Input  - vp_hdl - Handle to the VoiceProcessing HAL.
 *          data   - Algo param data structure instance
 * Output - Zero on success, errno on failure.
 */
int ia_set_algo_param(struct ia_voice_processing_hal *vp_hdl, struct ia_algo_param data);

/**
 * Get the current value of an algo parameter
 *
 * Input  - vp_hdl - Handle to the VoiceProcessing HAL.
 *          data   - Algo param data structure instance
 * Output - Zero on success, errno on failure.
 *          data   - Algo param data structure instance
 */
int ia_get_algo_param(struct ia_voice_processing_hal *vp_hdl, struct ia_algo_param *data);

/**
 * Reset the plugin to it's default state
 *
 * Input  - vp_hdl - Handle to the VoiceProcessing HAL.
 *          plugin_type - The plugin that needs to be reset.
 * Output - Zero on success, errno on failure.
 */
int ia_reset_plugin(struct ia_voice_processing_hal *vp_hdl, enum ia_plugin plugin_type);

/**
 * Set the parameter block for a plugin. Currently only PEQ, MBC plugins can
 * accept param data block
 *
 * Input  - vp_hdl - Handle to the VoiceProcessing HAL.
 *          data   - Plugin param data structure instance
 * Output - Zero on success, errno on failure.
 */
int ia_set_plugin_param_block(struct ia_voice_processing_hal *vp_hdl, struct ia_plugin_param_block data);

#if __cplusplus
} // extern "C"
#endif

#endif
