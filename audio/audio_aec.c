/*
 * Copyright (C) 2019 The Android Open Source Project
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

/*
 * Typical AEC signal flow:
 *
 *                                                                     +----------------+
 *          O+======                                                   | CAPTURE THREAD |
 * (from         .  +---------+                                        +----------------+
 *  mic          .            |
 *  codec)       .  =====+    |                                                  (to      +======>
 *          O+======     |    |                                                   caller) |
 *                       |    |                                                           |
 *                    +--v----v---------+                                           +-----+----+
 *                    | Microphone      |                                           | Output   |
 *     +--------------+ Audio/Timestamp +-------------------------------------------+ Audio    +--+
 *                    | Buffer          |                                           | Buffer   |
 *                    +--+----+---------+                                           +-----^----+
 *                       |    |              Microphone                                   |
 *                       |    |              Timestamps                                   |
 *                       |    +-----------------------------+                             |
 *                       |                                  |       +---------------+     |
 *                       |           +---------------+      |       |               |     |
 *            Microphone |           | Sample Rate   |      +------->               |     |
 *            Audio      |           | +             |              |               |     |
 *            Samples    +===========> Format        |==============>               |     |
 *                                   | Conversion    |              |               |     |
 *                                   | (if required) |              |   Acoustic    |     |
 *                                   +---------------+              |   Echo        |     |
 *                                                                  |   Canceller   |=====+
 *                                                                  |   (AEC)       |   Cleaned
 *                       Reference   +---------------+              |               |   Audio
 *                       Audio       | Sample Rate   |              |               |   Samples
 *                       Samples     | +             |              |               |
 *                     +=============> Format        |==============>               |
 *                     |             | Conversion    |              |               |
 *                     |             | (if required) |      +------->               |
 *                     |             +---------------+      |       |               |
 *                     |                                    |       +---------------+
 *                     |    +-------------------------------+
 *                     |    |  Reference Audio
 *                     |    |  Timestamps
 *                     |    |                                          +------------+
 *                  +--+----+---------+                                | AEC THREAD |
 *                  | Speaker         |                                +------------+
 *     +------------+ Audio/Timestamp +--------------------------------------------------------+
 *                  | Buffer          |                                +-----------------+
 *                  +--^----^---------+                                | PLAYBACK THREAD |
 *                     |    |                                          +-----------------+
 *                     |    |
 *                     |    |
 *                     |    |
 *           +\        |    |
 *           | +-+     +    |
 * (to       | | +-----C----+
 *  speaker  | | |     |                                       Playback
 *  codec)   | | <=====+====================================+  Audio
 *           | +-+                                             Samples
 *           +/
 *
 */

#define LOG_TAG "audio_hw_aec"
// #define LOG_NDEBUG 0

#include <audio_utils/primitives.h>
#include <stdio.h>
#include <inttypes.h>
#include <errno.h>
#include <malloc.h>
#include <sched.h>
#include <sys/prctl.h>
#include <sys/time.h>
#include <tinyalsa/asoundlib.h>
#include <log/log.h>
#include "audio_aec.h"
#include "audio_aec_process.h"

#define FIFO_NUM_ELEMENTS 4
#define DEBUG_AEC 0
#define MAX_TIMESTAMP_DIFF_USEC 200000

static uint64_t timespec_to_usec(struct timespec ts) {
    return (ts.tv_sec * 1e6L + ts.tv_nsec/1000);
}

static void timestamp_adjust(struct timespec *ts, size_t frames, uint32_t sampling_rate) {
    /* This function assumes the adjustment (in nsec) is less than the max value of long,
     * which for 32-bit long this is 2^31 * 1e-9 seconds, slightly over 2 seconds.
     * For 64-bit long it is  9e+9 seconds. */
    long adj_nsec = (frames / (float) sampling_rate) * 1E9L;
    ts->tv_nsec -= adj_nsec;
    if (ts->tv_nsec < 0) {
        ts->tv_sec--;
        ts->tv_nsec += 1E9L;
    }
}

static void get_reference_audio_in_place(struct aec_t *aec, size_t frames) {
    if (aec->num_reference_channels == aec->reference.num_channels) {
        /* Reference count equals speaker channels, nothing to do here. */
        return;
    } else if (aec->num_reference_channels != 1) {
        /* We don't have  a rule for non-mono references, show error on log */
        ALOGE("Invalid reference count - must be 1 or match number of playback channels!");
        return;
    }
    int16_t *src_Nch = &aec->reference_playback_format[0];
    int16_t *dst_1ch = &aec->reference_playback_format[0];
    int32_t num_channels = (int32_t)aec->reference.num_channels;
    size_t frame, ch;
    for (frame = 0; frame < frames; frame++) {
        int32_t acc = 0;
        for (ch = 0; ch < aec->reference.num_channels; ch++) {
            acc += src_Nch[ch];
        }
        *dst_1ch++ = clamp16(acc/num_channels);
        src_Nch += aec->reference.num_channels;
    }
}

void print_queue_status_to_log(struct aec_io *io) {
    ssize_t q1 = fifo_available_to_read(io->audio_fifo);
    ssize_t q2 = -1;

    if (io->ts_fifo != NULL) {
        q2 = fifo_available_to_read(io->ts_fifo);
    }

    ALOGV("Queue available: Audio %zd,TS %zd (count %zd)",
        q1, q2, q2/sizeof(struct ts_fifo_payload));
}

static void flush_aec_fifos(struct aec_t *aec) {
    if (aec == NULL) {
        return;
    }
    if (aec->reference.audio_fifo != NULL) {
        ALOGV("Flushing Speaker FIFO...");
        fifo_flush(aec->reference.audio_fifo);
    }
    if (aec->reference.ts_fifo != NULL) {
        ALOGV("Flushing Speaker Timestamp FIFO...");
        fifo_flush(aec->reference.ts_fifo);
    }
    if (aec->mic.audio_fifo != NULL) {
        ALOGV("Flushing Mic FIFO...");
        fifo_flush(aec->mic.audio_fifo);
    }
    if (aec->mic.ts_fifo != NULL) {
        ALOGV("Flushing Mic Timestamp FIFO...");
        fifo_flush(aec->mic.ts_fifo);
    }
    if (aec->out.audio_fifo != NULL) {
        ALOGV("Flushing output FIFO...");
        fifo_flush(aec->out.audio_fifo);
    }

    /* Reset FIFO read-write offset tracker */
    aec->mic.fifo_read_write_diff_bytes = 0;
    aec->reference.fifo_read_write_diff_bytes = 0;
}

static struct aec_t *init_aec_interface() {
    ALOGV("%s enter", __func__);
    struct aec_t *aec = (struct aec_t *)calloc(1, sizeof(struct aec_t));
    if (aec == NULL) {
        ALOGE("Failed to allocate memory for AEC interface!");
    } else {
        pthread_cond_init(&aec->ready_to_run, NULL);
        pthread_mutex_init(&aec->lock, NULL);
    }

    ALOGV("%s exit", __func__);
    return aec;
}

static void release_aec_interface(struct aec_t *aec) {
    ALOGV("%s enter", __func__);
    pthread_mutex_lock(&aec->lock);
    destroy_aec_mic_config(aec);
    destroy_aec_reference_config(aec);
    pthread_mutex_unlock(&aec->lock);
    pthread_mutex_destroy(&aec->lock);
    free(aec);
    ALOGV("%s exit", __func__);
}

int init_aec(int sampling_rate, int num_reference_channels,
                int num_microphone_channels, struct aec_t **aec_ptr) {
    ALOGV("%s enter", __func__);
    int ret = 0;
    int aec_ret = aec_spk_mic_init(
                    sampling_rate,
                    num_reference_channels,
                    num_microphone_channels);
    if (aec_ret) {
        ALOGE("AEC object failed to initialize!");
        ret = -EINVAL;
    }
    struct aec_t *aec = init_aec_interface();
    if (!ret) {
        aec->num_reference_channels = num_reference_channels;
        /* Set defaults, will be overridden by settings in init_aec_(mic|referece_config) */
        /* Capture uses 2-ch, 32-bit frames */
        aec->mic.sampling_rate = CAPTURE_CODEC_SAMPLING_RATE;
        aec->mic.frame_size_bytes = CHANNEL_STEREO * sizeof(int32_t);
        aec->mic.num_channels = CHANNEL_STEREO;

        /* Playback uses 2-ch, 16-bit frames */
        aec->reference.sampling_rate = PLAYBACK_CODEC_SAMPLING_RATE;
        aec->reference.frame_size_bytes = CHANNEL_STEREO * sizeof(int16_t);
        aec->reference.num_channels = CHANNEL_STEREO;
    }

    (*aec_ptr) = aec;
    ALOGV("%s exit", __func__);
    return ret;
}

void release_aec(struct aec_t *aec) {
    ALOGV("%s enter", __func__);
    if (aec == NULL) {
        return;
    }
    release_aec_interface(aec);
    aec_spk_mic_release();
    ALOGV("%s exit", __func__);
}

int init_aec_reference_config(struct aec_t *aec, struct alsa_stream_out *out) {
    ALOGV("%s enter", __func__);
    if (!aec) {
        ALOGE("AEC: No valid interface found!");
        return -EINVAL;
    }

    int ret = 0;
    pthread_mutex_lock(&aec->lock);
    aec->reference.audio_fifo = fifo_init(
            FIFO_NUM_ELEMENTS * out->config.period_size *
                audio_stream_out_frame_size(&out->stream),
            false /* reader_throttles_writer */);
    if (aec->reference.audio_fifo == NULL) {
        ALOGE("AEC: Speaker loopback FIFO Init failed!");
        ret = -EINVAL;
        goto exit;
    }
    aec->reference.ts_fifo = fifo_init(
            FIFO_NUM_ELEMENTS * sizeof(struct ts_fifo_payload),
            false /* reader_throttles_writer */);
    if (aec->reference.ts_fifo == NULL) {
        ALOGE("AEC: Speaker timestamp FIFO Init failed!");
        ret = -EINVAL;
        fifo_release(aec->reference.audio_fifo);
        goto exit;
    }

    aec->reference.sampling_rate = out->config.rate;
    aec->reference.frame_size_bytes = audio_stream_out_frame_size(&out->stream);
    aec->reference.num_channels = out->config.channels;
    aec->reference.pcm = out->pcm;

exit:
    pthread_mutex_unlock(&aec->lock);
    ALOGV("%s exit", __func__);
    return ret;
}

void aec_set_spk_running(struct aec_t *aec, bool state) {
    ALOGV("%s enter", __func__);
    pthread_mutex_lock(&aec->lock);
    aec->reference.running = state;
    pthread_mutex_unlock(&aec->lock);
    ALOGV("%s exit", __func__);
}

bool aec_get_spk_running(struct aec_t *aec) {
    ALOGV("%s enter", __func__);
    pthread_mutex_lock(&aec->lock);
    bool state = aec->reference.running;
    pthread_mutex_unlock(&aec->lock);
    ALOGV("%s exit", __func__);
    return state;
}

void destroy_aec_reference_config(struct aec_t *aec) {
    ALOGV("%s enter", __func__);
    if (aec == NULL) {
        ALOGV("%s exit", __func__);
        return;
    }
    pthread_mutex_lock(&aec->lock);
    aec_set_spk_running(aec, false);
    fifo_release(aec->reference.audio_fifo);
    fifo_release(aec->reference.ts_fifo);
    memset(&aec->reference.last_timestamp, 0, sizeof(struct ts_fifo_payload));
    pthread_mutex_unlock(&aec->lock);
    ALOGV("%s exit", __func__);
}

void destroy_aec_mic_config(struct aec_t *aec) {
    ALOGV("%s enter", __func__);
    if (aec == NULL) {
        ALOGV("%s exit", __func__);
        return;
    }
    aec->running = false;
    pthread_join(aec->run_thread_id, NULL);
    pthread_mutex_lock(&aec->lock);
    memset(&aec->args, 0, sizeof(struct aec_thread_args));
    release_resampler(aec->reference_resampler);
    free(aec->mic.buf);
    free(aec->reference.buf);
    free(aec->out.buf);
    free(aec->reference_playback_format);
    free(aec->reference_resampler_out);
    memset(&aec->mic.last_timestamp, 0, sizeof(struct ts_fifo_payload));
    fifo_release(aec->mic.audio_fifo);
    fifo_release(aec->mic.ts_fifo);
    fifo_release(aec->out.audio_fifo);
    pthread_mutex_unlock(&aec->lock);
    ALOGV("%s exit", __func__);
}

static int write_to_fifo(struct aec_io *io, void *buffer, size_t bytes) {
    ALOGV("%s enter", __func__);
    int ret = 0;

    /* Write audio samples to FIFO */
    ssize_t written_bytes = fifo_write(io->audio_fifo, buffer, bytes);
    if (written_bytes != bytes) {
        ALOGE("Could only write %zu of %zu bytes", written_bytes, bytes);
        ret = -ENOMEM;
    }

    /* Get current timestamp and write to FIFO */
    if (io->ts_fifo != NULL) {
        struct ts_fifo_payload ts;
        if (io->pcm != NULL) {
            pcm_get_htimestamp(io->pcm, &ts.available, &ts.timestamp);
            /* We need the timestamp of the first frame, adjust htimestamp */
            timestamp_adjust(
                &ts.timestamp,
                pcm_get_buffer_size(io->pcm) - ts.available,
                io->sampling_rate);
        } else {
            /* Fallback to realtime clock */
            clock_gettime(CLOCK_REALTIME, &ts.timestamp);
            ts.available = 0;
        }
        ts.bytes = written_bytes;
        fifo_write(io->ts_fifo, &ts, sizeof(struct ts_fifo_payload));
    }
    print_queue_status_to_log(io);

    ALOGV("%s exit", __func__);
    return ret;
}

int write_to_reference_fifo(struct aec_t *aec, void *buffer, size_t bytes) {
    return write_to_fifo(&aec->reference, buffer, bytes);
}

uint64_t get_timestamp(struct aec_io *io, ssize_t read_bytes) {
    uint64_t time_usec = 0;
    uint64_t time_offset_usec = 0;

    float usec_per_byte = 1E6 / ((float)(io->frame_size_bytes * io->sampling_rate));
    if (io->fifo_read_write_diff_bytes < 0) {
        /* We're still reading a previous write packet. (We only need the first sample's timestamp,
         * so even if we straddle packets we only care about the first one)
         * So we just use the previous timestamp, with an appropriate offset
         * based on the number of bytes remaining to be read from that write packet. */
        time_offset_usec = (io->last_timestamp.bytes + io->fifo_read_write_diff_bytes) *
                            usec_per_byte;
        ALOGV("Reusing previous timestamp, calculated offset (usec) %"PRIu64, time_offset_usec);
    } else {
        /* If read_write_diff_bytes > 0, there are no new writes, so there won't be timestamps
         * in the FIFO, and the check below will fail. */
        if (!fifo_available_to_read(io->ts_fifo)) {
            ALOGE("Timestamp error: no new timestamps!");
            goto exit;
        }
        /* We just read valid data, so if we're here, we should have a valid timestamp to use. */
        ssize_t ts_bytes = fifo_read(io->ts_fifo, &io->last_timestamp,
                                        sizeof(struct ts_fifo_payload));

        ALOGV("Read TS bytes: %zd, expected %zu", ts_bytes, sizeof(struct ts_fifo_payload));
        io->fifo_read_write_diff_bytes -= io->last_timestamp.bytes;
    }

    time_usec = timespec_to_usec(io->last_timestamp.timestamp) + time_offset_usec;

    io->fifo_read_write_diff_bytes += read_bytes;
    while (io->fifo_read_write_diff_bytes > 0) {
        /* If read_write_diff_bytes > 0, it means that there are more write packet timestamps
         * in FIFO (since there we read more valid data the size of the current timestamp's
         * packet). Keep reading timestamps from FIFO to get to the most recent one. */
        if (!fifo_available_to_read(io->ts_fifo)) {
            /* There are no more timestamps, we have the most recent one. */
            ALOGV("At the end of timestamp FIFO, breaking...");
            break;
        }
        fifo_read(io->ts_fifo, &io->last_timestamp, sizeof(struct ts_fifo_payload));
        ALOGV("Fast-forwarded timestamp by %zd bytes, remaining bytes: %zd,"
                " new timestamp (usec) %"PRIu64,
            io->last_timestamp.bytes,
            io->fifo_read_write_diff_bytes,
            timespec_to_usec(io->last_timestamp.timestamp));
        io->fifo_read_write_diff_bytes -= io->last_timestamp.bytes;
    }

exit:
    return time_usec;
}

int process_aec(struct aec_t *aec, void* buffer, size_t bytes) {
    ALOGV("%s enter", __func__);
    int ret = 0;

    if (aec == NULL) {
        ALOGE("AEC: Interface uninitialized! Cannot process.");
        return -EINVAL;
    }

    /* Write mic data to FIFO */
    write_to_fifo(&aec->mic, buffer, bytes);

    /*
     * Only run AEC if there is speaker playback.
     * The first time speaker state changes to running, flush FIFOs, so we're not stuck
     * processing stale reference input.
     */
    bool reference_running = aec_get_spk_running(aec);

    if (!reference_running) {
        /* No new playback samples, so don't run AEC.
         * 'buffer' already contains input samples. */
        ALOGV("Speaker not running, skipping AEC..");
        goto exit;
    }

    if (!aec->reference.prev_running) {
        ALOGV("Speaker just started running, flushing FIFOs");
        flush_aec_fifos(aec);
        aec_spk_mic_reset();
    }

    if(fifo_available_to_read(aec->mic.audio_fifo) < AEC_PROCESS_FRAME_UNIT) {
        /* Not enough data, don't run AEC */
        ALOGI("Not enough data in mic FIFO, skipping AEC... ");
        goto exit;
    }

    /* Signal to AEC thread that we have new data */
    pthread_mutex_lock(&aec->lock);
    aec->args.aec = aec;
    aec->args.bytes = bytes;
    aec->args.ret = -ENOSYS;
    pthread_cond_signal(&aec->ready_to_run);
    pthread_mutex_unlock(&aec->lock);

    /* Get data from output FIFO */
    ssize_t available_bytes = fifo_available_to_read(aec->out.audio_fifo);
    if (available_bytes >= bytes) {
        fifo_read(aec->out.audio_fifo, buffer, bytes);
    } else {
        ALOGE("AEC output buffer underrun! Only %zu of %zu bytes available",
                available_bytes, bytes);
        ret = -EINVAL;
    }

exit:
    aec->reference.prev_running = reference_running;

#if DEBUG_AEC
    FILE *fp_out = fopen("/data/local/traces/aec_out_final.pcm", "a+");
    if (fp_out) {
        fwrite((char *)buffer, 1, bytes, fp_out);
        fclose(fp_out);
    } else {
        ALOGE("AEC debug: Could not open file aec_out_final.pcm!");
    }
#endif /* #if DEBUG_AEC */

    ALOGV("%s exit", __func__);
    return ret;
}

static void *run_aec(void *data) {
    ALOGV("%s enter", __func__);
    struct aec_thread_args *args = (struct aec_thread_args *)data;
    struct aec_t *aec = args->aec;
    size_t bytes = args->bytes;
    int *ret = &args->ret;

    *ret = 0;

    if (aec == NULL) {
        ALOGE("AEC: Interface uninitialized! Cannot process.");
        (*ret) = -EINVAL;
        return NULL;
    }

    size_t frame_size = aec->mic.frame_size_bytes;
    size_t in_frames = bytes / frame_size;

    uint64_t mic_time = 0;
    uint64_t reference_time = 0;

    print_queue_status_to_log(&aec->mic);

    /* Read from mic FIFO */
    ssize_t available_bytes = fifo_available_to_read(aec->mic.audio_fifo);
    if (available_bytes < bytes) {
        ALOGV("Mic buffer only has %zu of minimum %zu bytes, cannot process AEC....",
            available_bytes, bytes);
        memset(aec->mic.buf, 0, bytes);
        (*ret) = -EINVAL;
        goto exit;
    }
    ssize_t read_bytes = fifo_read(aec->mic.audio_fifo, aec->mic.buf, bytes);
    mic_time = get_timestamp(&aec->mic, read_bytes);

    print_queue_status_to_log(&aec->reference);

    /* Read from speaker FIFO */
    size_t sample_rate_ratio = aec->reference.sampling_rate / aec->mic.sampling_rate;
    size_t resampler_in_frames = in_frames * sample_rate_ratio;
    size_t reference_req_bytes = resampler_in_frames * aec->reference.frame_size_bytes;
    if (fifo_available_to_read(aec->reference.audio_fifo) <= 0) {
        ALOGV("Echo reference buffer empty, skipping AEC....");
        memcpy(aec->out.buf, aec->mic.buf, bytes);
        goto exit;
    }
    read_bytes = fifo_read(aec->reference.audio_fifo, aec->reference_playback_format,
                            reference_req_bytes);
    reference_time = get_timestamp(&aec->reference, read_bytes);

    if (read_bytes < reference_req_bytes) {
        ALOGI("Could only read %zu of %zu bytes", read_bytes, reference_req_bytes);
        if (read_bytes > 0) {
            memmove(aec->reference_playback_format + reference_req_bytes - read_bytes,
                    aec->reference_playback_format,
                    read_bytes);
            memset(aec->reference_playback_format, 0, reference_req_bytes - read_bytes);
        } else {
            ALOGE("Fifo read returned code %zd ", read_bytes);
            (*ret) = -ENOMEM;
            goto exit;
        }
    }

    /* Get reference - could be mono, downmixed from multichannel.
     * Reference stored at reference_playback_format */
    get_reference_audio_in_place(aec, resampler_in_frames);

    /* Resample to mic sampling rate (16-bit resampler) */
    size_t in_frame_count = resampler_in_frames;
    size_t out_frame_count = in_frames;
    aec->reference_resampler->resample_from_input(
                            aec->reference_resampler,
                            aec->reference_playback_format,
                            &in_frame_count,
                            aec->reference_resampler_out,
                            &out_frame_count);

    /* Convert to 32 bit */
    int16_t *src16 = aec->reference_resampler_out;
    int32_t *dst32 = aec->reference.buf;
    size_t frame, ch;
    for (frame = 0; frame < in_frames; frame++) {
        for (ch = 0; ch < aec->num_reference_channels; ch++) {
           *dst32++ = ((int32_t)*src16++) << 16;
        }
    }


    int64_t time_diff = (mic_time > reference_time) ?
                            (mic_time - reference_time) : (reference_time - mic_time);
    if ((reference_time == 0) || (mic_time == 0) || (time_diff > MAX_TIMESTAMP_DIFF_USEC)) {
        ALOGV("Speaker-mic timestamps diverged, skipping AEC");
        flush_aec_fifos(aec);
        aec_spk_mic_reset();
        goto exit;
    }

    ALOGV("Mic time: %"PRIu64", reference time: %"PRIu64, mic_time, reference_time);

    /*
     * AEC processing call - output stored at 'buffer'
     */
    int32_t aec_status = aec_spk_mic_process(
        aec->reference.buf, reference_time,
        aec->mic.buf, mic_time,
        in_frames,
        aec->out.buf);

    if (!aec_status) {
        ALOGE("AEC processing failed!");
        (*ret) = -EINVAL;
    }

exit:
    ALOGV("Mic time: %"PRIu64", reference time: %"PRIu64, mic_time, reference_time);
    if (*ret) {
        /* Best we can do is copy over the raw mic signal */
        memcpy(aec->out.buf, aec->mic.buf, bytes);
        ALOGV("%s non_zero return value, flushing FIFOs", __func__);
        flush_aec_fifos(aec);
        aec_spk_mic_reset();
    }

    (*ret) = write_to_fifo(&aec->out, aec->out.buf, bytes);

#if DEBUG_AEC
    /* ref data is 32-bit at this point */
    size_t ref_bytes = in_frames*aec->num_reference_channels*sizeof(int32_t);

    FILE *fp_in = fopen("/data/local/traces/aec_in.pcm", "a+");
    if (fp_in) {
        fwrite((char *)aec->mic.buf, 1, bytes, fp_in);
        fclose(fp_in);
    } else {
        ALOGE("AEC debug: Could not open file aec_in.pcm!");
    }
    FILE *fp_out = fopen("/data/local/traces/aec_out.pcm", "a+");
    if (fp_out) {
        fwrite((char *)aec->out.buf, 1, bytes, fp_out);
        fclose(fp_out);
    } else {
        ALOGE("AEC debug: Could not open file aec_out.pcm!");
    }
    FILE *fp_ref = fopen("/data/local/traces/aec_ref.pcm", "a+");
    if (fp_ref) {
        fwrite((char *)aec->reference.buf, 1, ref_bytes, fp_ref);
        fclose(fp_ref);
    } else {
        ALOGE("AEC debug: Could not open file aec_ref.pcm!");
    }
    FILE *fp_ts = fopen("/data/local/traces/aec_timestamps.txt", "a+");
    if (fp_ts) {
        fprintf(fp_ts, "%"PRIu64",%"PRIu64"\n", mic_time, reference_time);
        fclose(fp_ts);
    } else {
        ALOGE("AEC debug: Could not open file aec_timestamps.txt!");
    }
#endif /* #if DEBUG_AEC */

    ALOGV("%s exit", __func__);
    return NULL;
}

static void *run_aec_helper(void *data) {
    ALOGV("%s enter", __func__);

    struct aec_thread_args *args = (struct aec_thread_args *)data;
    struct aec_t *aec = args->aec;

    prctl(PR_SET_NAME, (unsigned long)"run_aec");
    struct sched_param param;
    int policy = SCHED_FIFO;
    param.sched_priority = sched_get_priority_max(policy);
    pthread_setschedparam(aec->run_thread_id, policy, &param);

    aec->running = true;
    while(aec->running) {
        pthread_mutex_lock(&aec->lock);
        pthread_cond_wait(&aec->ready_to_run, &aec->lock);
        run_aec(data);
        pthread_mutex_unlock(&aec->lock);
    }

    ALOGV("%s exit", __func__);
    return NULL;
}

int init_aec_mic_config(struct aec_t *aec, struct alsa_stream_in *in) {
    ALOGV("%s enter", __func__);
#if DEBUG_AEC
    remove("/data/local/traces/aec_in.pcm");
    remove("/data/local/traces/aec_out.pcm");
    remove("/data/local/traces/aec_out_final.pcm");
    remove("/data/local/traces/aec_ref.pcm");
    remove("/data/local/traces/aec_timestamps.txt");
#endif /* #if DEBUG_AEC */

    if (!aec) {
        ALOGE("AEC: No valid interface found!");
        return -EINVAL;
    }

    int ret = 0;
    pthread_mutex_lock(&aec->lock);
    aec->mic.sampling_rate = in->config.rate;
    aec->mic.frame_size_bytes = audio_stream_in_frame_size(&in->stream);
    aec->mic.num_channels = in->config.channels;
    aec->mic.pcm = in->pcm;

    /* MIC INPUT BUFFER (32-bit) */
    aec->mic.buf_size_bytes = in->config.period_size * audio_stream_in_frame_size(&in->stream);
    aec->mic.buf = (int32_t *)malloc(aec->mic.buf_size_bytes);
    if (aec->mic.buf == NULL) {
        ret = -ENOMEM;
        goto exit;
    }
    memset(aec->mic.buf, 0, aec->mic.buf_size_bytes);

    /* REFERENCE INPUT BUFFER (32-bit) */
    /* Reference buffer is the same number of frames as mic,
     * only with a different number of channels in the frame. */
    aec->reference.buf_size_bytes = in->config.period_size * aec->reference.frame_size_bytes;
    aec->reference.buf = (int32_t *)malloc(aec->reference.buf_size_bytes);
    if (aec->reference.buf == NULL) {
        ret = -ENOMEM;
        goto exit_1;
    }
    memset(aec->reference.buf, 0, aec->reference.buf_size_bytes);

    /* AEC OUTPUT BUFFER (32-bit) */
    aec->out.buf_size_bytes = aec->mic.buf_size_bytes;
    aec->out.buf = (int32_t *)malloc(aec->out.buf_size_bytes);
    if (aec->out.buf == NULL) {
        ret = -ENOMEM;
        goto exit_2;
    }
    memset(aec->out.buf, 0, aec->out.buf_size_bytes);

    /* PRE-RESAMPLER REFERENCE BUFFER */
    size_t reference_playback_format_bytes = aec->reference.sampling_rate /
                        aec->mic.sampling_rate * aec->reference.buf_size_bytes;
    aec->reference_playback_format = (int16_t *)malloc(reference_playback_format_bytes);
    if (aec->reference_playback_format == NULL) {
        ret = -ENOMEM;
        goto exit_3;
    }
    memset(aec->reference_playback_format, 0, reference_playback_format_bytes);

    /* POST-RESAMPLER REFERENCE BUFFER (16-bit) */
    /* Resampler is 16-bit */
    aec->reference_resampler_out = (int16_t *)malloc(aec->reference.buf_size_bytes);
    if (aec->reference_resampler_out == NULL) {
        ret = -ENOMEM;
        goto exit_4;
    }
    memset(aec->reference_resampler_out, 0, aec->reference.buf_size_bytes);

    /* REFERENCE RESAMPLER */
    int resampler_ret = create_resampler(
                            aec->reference.sampling_rate,
                            in->config.rate,
                            aec->num_reference_channels,
                            RESAMPLER_QUALITY_MAX - 1, /* MAX - 1 is the real max */
                            NULL, /* resampler_buffer_provider */
                            &aec->reference_resampler);
    if (resampler_ret) {
        ALOGE("AEC: Resampler initialization failed! Error code %d", resampler_ret);
        ret = resampler_ret;
        goto exit_5;
    }

    /* MIC FIFOS */
    aec->mic.audio_fifo = fifo_init(
            FIFO_NUM_ELEMENTS * in->config.period_size *
                audio_stream_in_frame_size(&in->stream),
            false /* reader_throttles_writer */);
    if (aec->mic.audio_fifo == NULL) {
        ALOGE("AEC: Mic FIFO Init failed!");
        ret = -ENOMEM;
        goto exit_6;
    }
    aec->mic.ts_fifo = fifo_init(
            FIFO_NUM_ELEMENTS * sizeof(struct ts_fifo_payload),
            false /* reader_throttles_writer */);
    if (aec->mic.ts_fifo == NULL) {
        ALOGE("AEC: Mic timestamp FIFO Init failed!");
        ret = -EINVAL;
        goto exit_7;
    }

    /* AEC OUTPUT FIFO */
    aec->out.audio_fifo = fifo_init(
            FIFO_NUM_ELEMENTS * in->config.period_size *
                audio_stream_in_frame_size(&in->stream),
            false /* reader_throttles_writer */);
    if (aec->out.audio_fifo == NULL) {
        ALOGE("AEC: Mic FIFO Init failed!");
        ret = -ENOMEM;
        goto exit_8;
    }

    /* Create thread for AEC processing */
    aec->args.aec = aec;
    aec->args.bytes = AEC_PROCESS_FRAME_UNIT * audio_stream_in_frame_size(&in->stream);
    aec->args.ret = -ENOSYS;
    ret = pthread_create(&aec->run_thread_id, NULL, &run_aec_helper, &aec->args);
    if (ret) {
        ALOGE("Could not create AEC thread in %s %d", __func__, __LINE__);
        goto exit_9;
    }

    flush_aec_fifos(aec);
    aec_spk_mic_reset();

exit:
    pthread_mutex_unlock(&aec->lock);
    ALOGV("%s exit", __func__);
    return ret;

exit_9:
    fifo_release(aec->out.audio_fifo);
exit_8:
    fifo_release(aec->mic.ts_fifo);
exit_7:
    fifo_release(aec->mic.audio_fifo);
exit_6:
    release_resampler(aec->reference_resampler);
exit_5:
    free(aec->reference_resampler_out);
exit_4:
    free(aec->reference_playback_format);
exit_3:
    free(aec->out.buf);
exit_2:
    free(aec->reference.buf);
exit_1:
    free(aec->mic.buf);

    pthread_mutex_unlock(&aec->lock);
    ALOGV("%s exit", __func__);
    return ret;
}
