/*
 * Copyright (C) 2016 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef _YUKAWA_AUDIO_AEC_H_
#define _YUKAWA_AUDIO_AEC_H_

#include <stdint.h>
#include <pthread.h>
#include <sys/time.h>
#include <hardware/audio.h>
#include <audio_utils/resampler.h>
#include "audio_hw.h"
#include "fifo_wrapper.h"

struct ts_fifo_payload {
    struct timespec tstamp;
    unsigned int avail;
    ssize_t bytes;
};

struct aec_itfe {
	pthread_mutex_t lock;
    int32_t *mic_buf;
    ssize_t mic_buf_size_bytes;
    ssize_t mic_frame_size;
    uint32_t mic_sampling_rate;
    struct ts_fifo_payload last_mic_ts;
    int32_t *spk_buf;
    ssize_t spk_buf_size_bytes;
    ssize_t spk_frame_size;
    uint32_t spk_sampling_rate;
    struct ts_fifo_payload last_spk_ts;
    int16_t *spk_buf_playback_format;
    int16_t *spk_buf_resampler_out;
    struct audio_fifo_itfe *spk_fifo;
    struct audio_fifo_itfe *ts_fifo;
    ssize_t read_write_diff_bytes;
    struct resampler_itfe *spk_resampler;
    bool spk_running;
    bool prev_spk_running;
};

void write_to_spk_fifo (struct alsa_stream_out *out, void *buffer, size_t bytes, ssize_t *ret);
void process_aec (struct alsa_stream_in *stream, void* buffer, size_t bytes, ssize_t *ret);
void init_aec_spk_config (struct alsa_stream_out *out, ssize_t *ret);
void init_aec_mic_config (struct alsa_stream_in *in, ssize_t *ret);
void destroy_aec_spk_config (struct alsa_stream_out *out);
void destroy_aec_mic_config (struct alsa_stream_in *in);
void aec_set_spk_running (struct alsa_stream_out *out, bool state);

#ifndef AEC_HAL

#define write_to_spk_fifo(...) ((void)0)
#define process_aec(...) ((void)0)
#define init_aec_spk_config(...) ((void)0)
#define init_aec_mic_config(...) ((void)0)
#define destroy_aec_spk_config(...) ((void)0)
#define destroy_aec_mic_config(...) ((void)0)
#define aec_set_spk_running(...) ((void)0)

#endif


#endif /* _YUKAWA_AUDIO_AEC_H_ */