#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <limits.h>
#include <errno.h>

#include "iaxxx-system-identifiers.h"

#include "logger.h"
#include "knowles_tunnel_pcm.h"
#include "iaxxx_odsp_hw.h"

// FIX ME
// This is HARDCODED for Retune
#define RETUNE_END_POINT    IAXXX_SYSID_PLUGIN_0_OUT_EP_1
#define MIC_END_POINT       IAXXX_SYSID_CHANNEL_RX_0_EP_0
#define TUN_BUF_SIZE 0

#define KW_DETECT_REENABLE_ID   2
#define KW_DETECT_REENABLE_VAL  1
#define RETUNE_BLOCK_ID         1   //retune blk id for HMD
#define RETUNE_INST_ID          0

#define INVALID_START_FRAME     (0)
#define INVALID_END_FRAME       (0)

#define RETUNE_VQ_FRAME_SIZE_IN_MS    12.5       // retune vq frame size in milli seconds

//#define ENABLE_DUMP

struct kt_pcm *g_kt_pcm_hdl;

int handle_asr_stream_start(int start_frame, int end_frame) {
    struct kt_config kc;
    struct kt_preroll kp;

    ALOGD("%s: start_frame %d end_frame %d", __func__, start_frame, end_frame);

    if ((INVALID_START_FRAME == start_frame &&
         INVALID_END_FRAME == end_frame)) {
        ALOGD("%s: Streaming from mic end point", __func__);
        kc.end_point = MIC_END_POINT;
        kp.preroll_en = false;
        kp.preroll_time_in_ms = 0;
        kp.kw_start_frame = 0;
        kp.frame_size_in_ms = RETUNE_VQ_FRAME_SIZE_IN_MS;
    } else {
        ALOGD("%s: Streaming from retune end point", __func__);
        kc.end_point = RETUNE_END_POINT;
        kp.preroll_en = false;
        kp.preroll_time_in_ms = 0;
        kp.kw_start_frame = 0;
        kp.frame_size_in_ms = RETUNE_VQ_FRAME_SIZE_IN_MS;
    }

    kc.tunnel_output_buffer_size = TUN_BUF_SIZE;

    g_kt_pcm_hdl = kt_pcm_open(&kc, &kp);
    if (NULL == g_kt_pcm_hdl) {
        ALOGE("%s: ERROR Failed to open kt_pcm_open", __func__);
        return -1;
    }

    return 0;
}

int handle_asr_read_stream(unsigned char *data, int size) {
    int bytes = 0;
#ifdef ENABLE_DUMP
    FILE *fp = fopen("/home/root/asr_dump.pcm", "ab");
#endif

    bytes = kt_pcm_read(g_kt_pcm_hdl, data, size);

#ifdef ENABLE_DUMP
    fwrite(data, 1, bytes, fp);
    fflush(fp);
    fclose(fp);
#endif

    return bytes;
}

int handle_asr_stream_stop() {
    int err = 0;
    struct iaxxx_odsp_hw *odsp_hdl = NULL;

    err = kt_pcm_close(g_kt_pcm_hdl);
    if (0 != err) {
        ALOGE("%s: ERROR Failed to close kt_pcm_close", __func__);
    }

    odsp_hdl = iaxxx_odsp_init();
    if (NULL == odsp_hdl) {
        ALOGE("%s: ERROR Failed init ODSP HAL", __func__);
        err = -EINVAL;
        goto exit;
    }

    err = iaxxx_odsp_plugin_set_parameter(odsp_hdl, RETUNE_INST_ID,
                                          RETUNE_BLOCK_ID,
                                          KW_DETECT_REENABLE_ID,
                                          KW_DETECT_REENABLE_VAL);
    if (0 != err) {
        ALOGE("%s: ERROR Failed to set the parameter for kw detect", __func__);
        goto exit;
    }

    err = iaxxx_odsp_deinit(odsp_hdl);
    if (0 != err) {
        ALOGE("%s: ERROR Failed to deinit the ODSP HAL", __func__);
        goto exit;
    }

exit:
    return err;
}

