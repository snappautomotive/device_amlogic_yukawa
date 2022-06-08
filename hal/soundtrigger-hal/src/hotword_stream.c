#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <limits.h>
#include <errno.h>

#include "iaxxx-system-identifiers.h"

#include "logger.h"
#include "knowles_tunnel_pcm.h"

// FIX ME
#define MIC_END_POINT	    IAXXX_SYSID_PLUGIN_3_OUT_EP_0
#define TUN_BUF_SIZE 0

#define INVALID_START_FRAME     (0)
#define INVALID_END_FRAME       (0)

#define VQ_FRAME_SIZE_IN_MS    10       // retune vq frame size in milli seconds

//#define ENABLE_DUMP

struct kt_pcm *g_kt_pcm_hdl;

int mic_record_stream_start(int start_frame, int end_frame) {
    struct kt_config kc;
    struct kt_preroll kp;

    ALOGD("%s: start_frame %d end_frame %d", __func__, start_frame, end_frame);

    kc.end_point = MIC_END_POINT;
    kp.preroll_en = false;
    kp.preroll_time_in_ms = 0;
    kp.kw_start_frame = 0;
    kp.frame_size_in_ms = VQ_FRAME_SIZE_IN_MS;

    kc.tunnel_output_buffer_size = TUN_BUF_SIZE;

    g_kt_pcm_hdl = kt_pcm_open(&kc, &kp);
    if (NULL == g_kt_pcm_hdl) {
        ALOGE("%s: ERROR Failed to open kt_pcm_open", __func__);
        return -1;
    }

    return 0;
}

int mic_record_read_stream(unsigned char *data, int size) {
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

int mic_record_stream_stop() {
    int err = 0;

    err = kt_pcm_close(g_kt_pcm_hdl);
    if (0 != err) {
        ALOGE("%s: ERROR Failed to close kt_pcm_close", __func__);
    }

    return err;
}

