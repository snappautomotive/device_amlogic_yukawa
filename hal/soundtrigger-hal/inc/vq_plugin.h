#ifndef __VQ_PLUGIN_H__
#define __VQ_PLUGIN_H__

struct vq_plugin {
    void* (*init)();
    int (*deinit)(void *hdl);
    int (*start_recognition)(void *hdl, vq_hal_music_status status);
    int (*stop_recognition)(void *hdl);
    bool (*process_uevent)(void *hdl,
                           char *msg,
                           int msg_len,
                           vq_hal_event_cb cb,
                           void *cb_cookie);
    int (*start_audio)(void *hdl, bool is_multiturn);
    int (*stop_audio)(void *hdl);
    int (*read_audio)(void *hdl, void *buf, int buf_len);
    float (*get_audio_frame_length)(void *hdl);
    int (*resume_recognition)(void *hdl);
    int (*pause_recognition)(void *hdl);
    int (*set_music_status)(void *hdl, vq_hal_music_status status);
};

struct vq_plugin_register {
    const char *name;
    struct vq_plugin *plg_funcs;
};

unsigned int vq_registrar_size();

#endif // #define __VQ_PLUGIN_H__
