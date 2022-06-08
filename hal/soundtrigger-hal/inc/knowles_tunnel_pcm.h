#ifndef __KNOWLES_TUNNEL_PCM__
#define __KNOWLES_TUNNEL_PCM__

#if __cplusplus
extern "C"
{
#endif

struct kt_preroll {
    unsigned int kw_start_frame;
    unsigned int preroll_time_in_ms;
    float frame_size_in_ms;
    bool preroll_en;
};

struct kt_config {
    int end_point;
    int tunnel_output_buffer_size;
};

/**
 * Open the Knowles Tunnel PCM library
 *
 * Input  - kt_config -
 *              end_point - End point from which the audio data should be read
 *              tunnel_output_buffer_size - Set the output buffer size for the
 *                                          tunnel
 *        - preroll - If present it will strip the extra data from the audio
 *                    data
 * Output - Handle to Knowles Tunnel PCM library on success
 *        - NULL on failure
 */
struct kt_pcm* kt_pcm_open(struct kt_config *kc, struct kt_preroll *preroll);

/**
 * Get the PCM data corresponding to the end point that was opened.
 *
 * Input  - kt_pcm_hdl - Valid handle to the Knowles Tunnel PCM library
 *        - buffer - Buffer into which the data will be filled
 *        - bytes - Size of the buffer in bytes
 * Output - Number of bytes filled into the buffer
 */
int kt_pcm_read(struct kt_pcm *kt_pcm_hdl, void *buffer, const int bytes);

/**
 * Closes Knowles Tunnel PCM library
 *
 * Input  - kt_pcm_hdl - Valid handle to the Knowles Tunnel PCM library
 * Output - Zero on success, errno  on failure.
 */
int kt_pcm_close(struct kt_pcm *kt_pcm_hdl);

#if __cplusplus
} // extern "C"
#endif

#endif // ifndef __KNOWLES_TUNNEL_PCM_
