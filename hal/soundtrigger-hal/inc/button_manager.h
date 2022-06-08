#ifndef __BUTTON_MANAGER_H__
#define __BUTTON_MANAGER_H__

#if __cplusplus
extern "C"
{
#endif

struct button_mgr;

typedef enum {
    SHORTPRESS_KEY_MUTE = 0,
    SHORTPRESS_KEY_VOLUME_UP,
    SHORTPRESS_KEY_VOLUME_DOWN,
    LONGPRESS_KEY_MUTE,
    LONGPRESS_KEY_VOLUME_UP,
    LONGPRESS_KEY_VOLUME_DOWN
} button_mgr_event_type;

typedef int (*button_mgr_event_cb)(void *cookie,
                                   button_mgr_event_type event,
                                   void *event_data);

/**
 * Initialize the Button Manager
 *
 * Input  - NA
 * Output - Handle to Button Manager on success, NULL on failure
 */
struct button_mgr* button_mgr_init();

/**
 * De-Initialize the Button Manager
 *
 * Input  - button_mgr - Handle to button_mgr structure
 * Output - 0 on success, on failure < 0
 */
int button_mgr_deinit(struct button_mgr *hdl);

/**
 * Register for callback from the Button Manager.
 *
 * Input  - button_mgr - Handle to button_mgr structure
 *        - cookie - Opaque pointer that an app would required to passed with
 *                   the callback
 *        - cb - Function pointer
 *
 * Output - 0 on success, on failure < 0
 */
int button_mgr_register_cb(struct button_mgr *hdl, void *cookie,
                           button_mgr_event_cb cb);

/**
 * Un-Register for callback from the Button Manager.
 *
 * Input  - button_mgr - Handle to button_mgr structure
 * Output - 0 on success, on failure < 0
 */
int button_mgr_unregister_cb(struct button_mgr *hdl);

#if __cplusplus
} // extern "C"
#endif

#endif // #ifndef __BUTTON_MANAGER_H__