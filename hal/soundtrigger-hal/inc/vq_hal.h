#ifndef __VQ_HAL__
#define __VQ_HAL__

#if __cplusplus
extern "C"
{
#endif

struct vq_hal;

typedef enum {
    EVENT_KEYWORD_RECOGNITION = 0,
    EVENT_FW_CRASH,
    EVENT_FW_RECOVERED,
    EVENT_ERROR
} vq_hal_event_type;

typedef enum {
    MUSIC_PLAYBACK_STARTED,
    MUSIC_PLAYBACK_STOPPED
} vq_hal_music_status;

struct event_data {
    int kw_id;              // Keyword id
    int start_frame;        // Start frame number where the keyword starts
    int end_frame;          // End frame number where the keyword ends
    float confidence_lvl;   // Confidence level of the detected keyword
};

// function pointer to be supplied to receive various events like model
// recognition
/**
 * Prototype of the VQ Callback function
 *
 * Input  - cookie      - Opaque pointer that was passed during the callback
 *                        registration
 *        - event       - Type of event
 *        - event_data  - Event data associated with event
 * Output - 0 on success, on failure < 0
 */
typedef int (*vq_hal_event_cb)(void *cookie,
                               vq_hal_event_type event,
                               void *event_data);
/**
 * Initialize the VQ HAL
 *
 * Input  - enable_host_side_buffering - Setting this to true will enable the
 *          buffering on the host
 * Output - Handle to vq_hal on success, NULL on failure
 */
struct vq_hal* vq_hal_init(bool enable_host_side_buffering);

/**
 * De-Initialize the VQ HAL
 *
 * Input  - vq_hdl - Handle to vq_hal structure
 * Output - 0 on success, on failure < 0
 */
int vq_hal_deinit(struct vq_hal *hdl);

/**
 * Start recognition of the keyword model
 *
 * Input  - vq_hdl - Handle to vq_hal structure
 *        - status - Status of the music playback
 * Output - 0 on success, on failure < 0
 */
int vq_start_recognition(struct vq_hal *vq_hal, vq_hal_music_status status);

/**
 * Stop recognition of the keyword model
 *
 * Input  - vq_hdl - Handle to vq_hal structure
 * Output - 0 on success, on failure < 0
 */
int vq_stop_recognition(struct vq_hal *vq_hdl);

/**
 * Pause recognition of the keyword model.
 * The microphone will not be used at this point however the algorithm
 * may remain in the memory to resume the recognition quickly.
 *
 * Input - vq_hdl - Handle to vq_hal structure
 * Output - 0 on success, on failure < 0
 */
int vq_pause_recognition(struct vq_hal *vq_hdl);

/**
 * Resume recognition of the keyword model.
 * This can be called only the recognition is paused. The algorithm
 * will start analysing the microphone data again.
 *
 * Input - vq_hdl - Handle to vq_hal structure
 * Output - 0 on success, on failure < 0
 */
int vq_resume_recognition(struct vq_hal *vq_hdl);

/**
 * Register for callback from the VQ HAL. Events from the VQ HAL include -
 * keyword detections and errors if any.
 *
 * Input  - vq_hdl - Handle to vq_hal structure
 *        - cookie - Opaque pointer that an app would required to passed with
 *                   the callback
 *        - cb - Function pointer
 *
 * Output - 0 on success, on failure < 0
 */
int vq_register_cb(struct vq_hal *vq_hdl, void *cookie, vq_hal_event_cb cb);

/**
 * Un-Register for callback from the VQ HAL.
 *
 * Input  - vq_hdl - Handle to vq_hal structure
 * Output - 0 on success, on failure < 0
 */
int vq_unregister_cb(struct vq_hal *vq_hdl);

/**
 * Start audio streaming
 *
 * Input  - vq_hdl - Handle to vq_hal structure
 *        - is_multiturn - For AVS, you can stream data either via mic or
 *                         buffer. If the plugin doesn't support AVS then this
 *                         field is ignored.
 * Output - 0 on success, on failure < 0
 */
int vq_start_audio(struct vq_hal *vq_hdl, bool is_multiturn);

/**
 * Stop audio streaming
 *
 * Input  - vq_hdl - Handle to vq_hal structure
 * Output - 0 on success, on failure < 0
 */
int vq_stop_audio(struct vq_hal *vq_hdl);

/**
 * Read audio data
 *
 * Input  - vq_hdl - Handle to vq_hal structure
 *        - buf - Buffer into which audio data is read into
 *        - buf_size - Size of the buffer
 * Output - Number of bytes read
 */
int vq_read_audio(struct vq_hal *vq_hdl, void *buf, int buf_size);

/**
 * Get Audio frame length
 *
 * Input  - vq_hdl - Handle to vq_hal structure
 * Output - Audio frame length in milliseconds
 */
float vq_get_audio_frame_length(struct vq_hal *vq_hdl);

/**
 * Set the audio music playback status
 *
 * Input  - vq_hdl - Handle to vq_hal structure
 *          status - Depicts the status of the music playback
 * Output - 0 on success, on failure < 0
 */
int vq_set_music_status(struct vq_hal *vq_hdl, vq_hal_music_status status);

#if __cplusplus
} // extern "C"
#endif

#endif // #ifndef __VQ_HAL__
