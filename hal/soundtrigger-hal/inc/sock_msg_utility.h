#ifndef __SOCK_MSG_UTILITY_H__
#define __SOCK_MSG_UTILITY_H__

#if __cplusplus
extern "C"
{
#endif

#define AUDIO_DATA_BUF_SZ 320

enum msg_type {
    KW_DETECT,
    START_AUDIO,
    GET_AUDIO_DATA,
    AUDIO_DATA,
    STOP_AUDIO,
    GET_AUDIO_FRAME_LENGTH,
    AUDIO_FRAME_LENGTH,
    AUDIO_PLAYBACK_STATUS
};

enum audio_playback_state {
    PLAYBACK_STARTED,
    PLAYBACK_STOPPED
};

struct sock_msg {
    enum msg_type mt;
};

struct kw_detect_msg {
    struct sock_msg sm;
    int kw_id;
    int start_frame;
    int end_frame;
};

struct audio_start_msg {
    struct sock_msg sm;
    bool is_multiturn;
};

struct audio_data_msg {
    struct sock_msg sm;
    unsigned char buf[AUDIO_DATA_BUF_SZ];
    int buf_size;
};

struct audio_stop_msg {
    struct sock_msg sm;
};

struct get_audio_data_msg {
    struct sock_msg sm;
};

struct get_audio_frame_length {
    struct sock_msg sm;
};

struct audio_frame_length {
    struct sock_msg sm;
    float audio_frame_len;
};

struct audio_playback_status {
    struct sock_msg sm;
    enum audio_playback_state apst;
};

static inline int get_max_msg_sz() {
    union all_msgs {
        struct sock_msg sm;
        struct kw_detect_msg kdm;
        struct audio_start_msg asam;
        struct audio_data_msg adm;
        struct audio_stop_msg asom;
        struct get_audio_data_msg gadm;
        struct get_audio_frame_length gafl;
        struct audio_frame_length afl;
        struct audio_playback_status aps;
    };

    return sizeof(union all_msgs);
}

#if __cplusplus
} // extern "C"
#endif

#endif // #ifndef __SOCK_MSG_UTILITY_H__

