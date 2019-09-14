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

#define LOG_TAG "audio_hw_aec"
// #define LOG_NDEBUG 0

#include <stdio.h>
#include <inttypes.h>
#include <errno.h>
#include <malloc.h>
#include <sys/time.h>
#include <tinyalsa/asoundlib.h>
#include <log/log.h>
#include "audio_aec.h"
#include "audio_aec_process.h"

#define DEBUG_AEC 0
#define MAX_TIMESTAMP_DIFF_USEC 200000

uint64_t timespec_to_usec(struct timespec ts) {
    return (ts.tv_sec * 1e6L + ts.tv_nsec/1000);
}

void print_queue_status_to_log(struct aec_itfe *aec, bool write_side) {
    ssize_t q1 = fifo_available_to_read(aec->spk_fifo);
    ssize_t q2 = fifo_available_to_read(aec->ts_fifo);

    if (write_side) {
        ALOGV("Queue available (POST-WRITE): Spk %zd (count %zd) TS %zd (count %zd)",
        q1, q1/aec->spk_frame_size_bytes/PLAYBACK_PERIOD_SIZE, q2, q2/sizeof(struct ts_fifo_payload));
    } else {
        ALOGV("Queue available (PRE-READ): Spk %zd (count %zd) TS %zd (count %zd)",
        q1, q1/aec->spk_frame_size_bytes/PLAYBACK_PERIOD_SIZE, q2, q2/sizeof(struct ts_fifo_payload));
    }
}

void flush_aec_fifos(struct aec_itfe *aec) {
    if (aec == NULL) {
        return;
    }
    if (aec->spk_fifo != NULL) {
        ALOGV("Flushing AEC Spk FIFO...");
        fifo_flush(aec->spk_fifo);
    }
    if (aec->ts_fifo != NULL) {
        ALOGV("Flushing AEC Timestamp FIFO...");
        fifo_flush(aec->ts_fifo);
    }
    /* Reset FIFO read-write offset tracker */
    aec->read_write_diff_bytes = 0;
}

int init_aec_interface(struct alsa_audio_device *adev) {
    ALOGV("%s enter", __func__);
    int ret = 0;
    pthread_mutex_lock(&adev->lock);
    if (adev->aec == NULL) {
        adev->aec = (struct aec_itfe *)calloc(1, sizeof(struct aec_itfe));
        if (adev->aec == NULL) {
            ALOGE("Failed to allocate memory for AEC interface!");
            ret = -ENOMEM;
        } else {
            pthread_mutex_init(&adev->aec->lock, NULL);
        }
    }
    pthread_mutex_unlock(&adev->lock);
    ALOGV("%s exit", __func__);
    return ret;
}

int init_aec_spk_config(struct alsa_stream_out *out) {
    ALOGV("%s enter", __func__);
    struct alsa_audio_device *adev = (struct alsa_audio_device *)out->dev;
    int ret = init_aec_interface(adev);
    if (ret) {
        ALOGE("AEC: Interface initialization failed!");
        return ret;
    }

    struct aec_itfe *aec = adev->aec;
    pthread_mutex_lock(&aec->lock);
    aec->spk_fifo = fifo_init(
            PLAYBACK_PERIOD_COUNT*PLAYBACK_PERIOD_SIZE*
                audio_stream_out_frame_size(&out->stream),
            false /* reader_throttles_writer */);
    if (aec->spk_fifo == NULL) {
        ALOGE("AEC: Speaker loopback FIFO Init failed!");
        ret = -EINVAL;
        goto exit;
    }
    aec->ts_fifo = fifo_init(
            PLAYBACK_PERIOD_COUNT*sizeof(struct ts_fifo_payload),
            false /* reader_throttles_writer */);
    if (aec->ts_fifo == NULL) {
        ALOGE("AEC: Speaker timestamp FIFO Init failed!");
        ret = -EINVAL;
        fifo_release(aec->spk_fifo);
        goto exit;
    }

    aec->spk_sampling_rate = PLAYBACK_CODEC_SAMPLING_RATE;
    aec->spk_frame_size_bytes = audio_stream_out_frame_size(&out->stream);
exit:
    pthread_mutex_unlock(&aec->lock);
    ALOGV("%s exit", __func__);
    return ret;
}

int init_aec_mic_config(struct alsa_stream_in *in) {
    ALOGV("%s enter", __func__);
#if DEBUG_AEC
    remove("/data/local/traces/aec_in.pcm");
    remove("/data/local/traces/aec_out.pcm");
    remove("/data/local/traces/aec_ref.pcm");
    remove("/data/local/traces/aec_timestamps.txt");
#endif /* #if DEBUG_AEC */
    struct alsa_audio_device *adev = (struct alsa_audio_device *)in->dev;
    int ret = init_aec_interface(adev);
    if (ret) {
        ALOGE("AEC: Interface initialization failed!");
        return ret;
    }

    struct aec_itfe *aec = adev->aec;
    pthread_mutex_lock(&aec->lock);
    aec->mic_sampling_rate = CAPTURE_CODEC_SAMPLING_RATE;
    aec->mic_frame_size_bytes = audio_stream_in_frame_size(&in->stream);

    aec->mic_buf_size_bytes = CAPTURE_PERIOD_SIZE*audio_stream_in_frame_size(&in->stream);
    aec->mic_buf = (int32_t *)malloc(aec->mic_buf_size_bytes);
    if (aec->mic_buf == NULL) {
        ret = -ENOMEM;
        goto exit;
    }
    memset(aec->mic_buf, 0, aec->mic_buf_size_bytes);
    aec->spk_buf_size_bytes = aec->mic_buf_size_bytes * NUM_LOUDSPEAKER_FEEDS /
                                    in->config.channels;
    aec->spk_buf = (int32_t *)malloc(aec->spk_buf_size_bytes);
    if (aec->spk_buf == NULL) {
        ret = -ENOMEM;
        goto exit_1;
    }
    memset(aec->spk_buf, 0, aec->spk_buf_size_bytes);

    /* There may not be an open output stream at this point, so we'll have to statically allocate.
     * Echo reference (spk_buf) is 1-ch, 32 bit (4 bytes/frame),
     * while output is 2-ch 16-bit frames (4 bytes/frame). */
    ssize_t spk_frame_out_format_bytes = PLAYBACK_CODEC_SAMPLING_RATE / aec->mic_sampling_rate *
                                            aec->spk_buf_size_bytes;
    aec->spk_buf_playback_format = (int16_t *)malloc(spk_frame_out_format_bytes);
    if (aec->spk_buf_playback_format == NULL) {
        ret = -ENOMEM;
        goto exit_2;
    }
    aec->spk_buf_resampler_out = (int16_t *)malloc(aec->spk_buf_size_bytes); /* Resampler is 16-bit */
    if (aec->spk_buf_resampler_out == NULL) {
        ret = -ENOMEM;
        goto exit_3;
    }

    int resampler_ret = create_resampler(
                            PLAYBACK_CODEC_SAMPLING_RATE,
                            CAPTURE_CODEC_SAMPLING_RATE,
                            NUM_LOUDSPEAKER_FEEDS,
                            RESAMPLER_QUALITY_MAX - 1, /* MAX - 1 is the real max */
                            NULL, /* resampler_buffer_provider */
                            &aec->spk_resampler);
    if (resampler_ret) {
        ALOGE("AEC: Resampler initialization failed! Error code %d", resampler_ret);
        ret = resampler_ret;
        goto exit_4;
    }

    flush_aec_fifos(aec);

    int aec_ret = aec_spk_mic_init(
            CAPTURE_CODEC_SAMPLING_RATE,
            NUM_LOUDSPEAKER_FEEDS, /* num_loudspeaker_feeds */
            in->config.channels);

    if (aec_ret) {
        ALOGE("AEC object failed to initialize!");
        ret = -EINVAL;
        goto exit_5;
    }

exit:
    pthread_mutex_unlock(&aec->lock);
    ALOGV("%s exit", __func__);
    return ret;

exit_5:
    release_resampler(aec->spk_resampler);
exit_4:
    free(aec->spk_buf_resampler_out);
exit_3:
    free(aec->spk_buf_playback_format);
exit_2:
    free(aec->spk_buf);
exit_1:
    free(aec->mic_buf);
    pthread_mutex_unlock(&aec->lock);
    ALOGV("%s exit", __func__);
    return ret;
}

void aec_set_spk_running(struct alsa_stream_out *out, bool state) {
    ALOGV("%s enter", __func__);
    struct alsa_audio_device *adev = out->dev;
    pthread_mutex_lock(&adev->aec->lock);
    adev->aec->spk_running = state;
    pthread_mutex_unlock(&adev->aec->lock);
    ALOGV("%s exit", __func__);
}

bool aec_get_spk_running(struct aec_itfe *aec) {
    ALOGV("%s enter", __func__);
    pthread_mutex_lock(&aec->lock);
    bool state = aec->spk_running;
    pthread_mutex_unlock(&aec->lock);
    ALOGV("%s exit", __func__);
    return state;
}

void destroy_aec_spk_config(struct alsa_stream_out *out) {
    ALOGV("%s enter", __func__);
    struct aec_itfe *aec = (struct aec_itfe *)out->dev->aec;
    if (aec == NULL) {
        ALOGV("%s exit", __func__);
        return;
    }
    pthread_mutex_lock(&aec->lock);
    aec_set_spk_running(out, false);
    fifo_release(aec->spk_fifo);
    fifo_release(aec->ts_fifo);
    memset(&aec->last_spk_ts, 0, sizeof(struct ts_fifo_payload));
    pthread_mutex_unlock(&aec->lock);
    ALOGV("%s exit", __func__);
}

void destroy_aec_mic_config(struct alsa_stream_in *in) {
    ALOGV("%s enter", __func__);
    struct aec_itfe *aec = (struct aec_itfe *)in->dev->aec;
    if (aec == NULL) {
        ALOGV("%s exit", __func__);
        return;
    }
    pthread_mutex_lock(&aec->lock);
    aec_spk_mic_release();
    release_resampler(aec->spk_resampler);
    free(aec->mic_buf);
    free(aec->spk_buf);
    free(aec->spk_buf_playback_format);
    free(aec->spk_buf_resampler_out);
    memset(&aec->last_mic_ts, 0, sizeof(struct ts_fifo_payload));
    pthread_mutex_unlock(&aec->lock);
    ALOGV("%s exit", __func__);
}

int write_to_spk_fifo(struct alsa_stream_out *out, void *buffer, size_t bytes) {
    ALOGV("%s enter", __func__);
    int ret = 0;
    struct alsa_audio_device *adev = (struct alsa_audio_device *)out->dev;

    /* Write audio samples to FIFO */
    ssize_t written_bytes = fifo_write(adev->aec->spk_fifo, buffer, bytes);
    if (written_bytes != bytes) {
        ALOGE("Could only write %zu of %zu bytes", written_bytes, bytes);
        ret = -ENOMEM;
    }

    /* Get current timestamp and write to FIFO */
    struct ts_fifo_payload spk_ts;
    pcm_get_htimestamp(out->pcm, &spk_ts.available, &spk_ts.timestamp);
    spk_ts.bytes = written_bytes;
    ALOGV("Speaker timestamp: %ld s, %ld nsec", spk_ts.timestamp.tv_sec, spk_ts.timestamp.tv_nsec);
    ssize_t ts_bytes = fifo_write(adev->aec->ts_fifo, &spk_ts, sizeof(struct ts_fifo_payload));
    ALOGV("Wrote TS bytes: %zu", ts_bytes);
    print_queue_status_to_log(adev->aec, true);
    ALOGV("%s exit", __func__);
    return ret;
}

void update_spk_timestamp(struct aec_itfe *aec, ssize_t read_bytes, uint64_t *spk_time) {
    *spk_time = 0;
    uint64_t spk_time_offset = 0;
    float usec_per_byte = 1E6 / ((float)(aec->spk_frame_size_bytes * aec->spk_sampling_rate));
    if (aec->read_write_diff_bytes < 0) {
        /* We're still reading a previous write packet. (We only need the first sample's timestamp,
         * so even if we straddle packets we only care about the first one)
         * So we just use the previous timestamp, with an appropriate offset
         * based on the number of bytes remaining to be read from that write packet. */
        spk_time_offset = (aec->last_spk_ts.bytes + aec->read_write_diff_bytes) * usec_per_byte;
        ALOGV("Reusing previous timestamp, calculated offset (usec) %"PRIu64, spk_time_offset);
    } else {
        /* If read_write_diff_bytes > 0, there are no new writes, so there won't be timestamps in the FIFO,
         * and the check below will fail. */
        if (!fifo_available_to_read(aec->ts_fifo)) {
            ALOGE("Timestamp error: no new timestamps!");
            return;
        }
        /* We just read valid data, so if we're here, we should have a valid timestamp to use. */
        ssize_t ts_bytes = fifo_read(aec->ts_fifo, &aec->last_spk_ts, sizeof(struct ts_fifo_payload));
        ALOGV("Read TS bytes: %zd, expected %zu", ts_bytes, sizeof(struct ts_fifo_payload));
        aec->read_write_diff_bytes -= aec->last_spk_ts.bytes;
    }

    *spk_time = timespec_to_usec(aec->last_spk_ts.timestamp) + spk_time_offset;

    aec->read_write_diff_bytes += read_bytes;
    struct ts_fifo_payload spk_ts = aec->last_spk_ts;
    while (aec->read_write_diff_bytes > 0) {
        /* If read_write_diff_bytes > 0, it means that there are more write packet timestamps in FIFO
         * (since there we read more valid data the size of the current timestamp's packet).
         * Keep reading timestamps from FIFO to get to the most recent one. */
        if (!fifo_available_to_read(aec->ts_fifo)) {
            /* There are no more timestamps, we have the most recent one. */
            ALOGV("At the end of timestamp FIFO, breaking...");
            break;
        }
        fifo_read(aec->ts_fifo, &spk_ts, sizeof(struct ts_fifo_payload));
        ALOGV("Fast-forwarded timestamp by %zd bytes, remaining bytes: %zd, new timestamp (usec) %"PRIu64,
            spk_ts.bytes, aec->read_write_diff_bytes, timespec_to_usec(spk_ts.timestamp));
        aec->read_write_diff_bytes -= spk_ts.bytes;
    }
    aec->last_spk_ts = spk_ts;
}

int process_aec(struct alsa_stream_in *in, void* buffer, size_t bytes) {
    ALOGV("%s enter", __func__);
    int ret = 0;
    struct alsa_audio_device *adev = (struct alsa_audio_device *)in->dev;
    struct aec_itfe *aec = adev->aec;
    size_t frame_size = audio_stream_in_frame_size((const struct audio_stream_in *)&in->stream);
    size_t in_frames = bytes / frame_size;

    if (aec == NULL) {
        ALOGE("AEC: Interface uninitialized! Cannot process.");
        return -EINVAL;
    }

    /* Copy raw mic samples to AEC input buffer */
    memcpy(aec->mic_buf, buffer, bytes);

    bool spk_running = aec_get_spk_running(aec);

    pcm_get_htimestamp(in->pcm, &aec->last_mic_ts.available, &aec->last_mic_ts.timestamp);
    uint64_t mic_time = timespec_to_usec(aec->last_mic_ts.timestamp);
    uint64_t spk_time = 0;

    /* Only run AEC if there is speaker playback.
     * The first time speaker state changes to running, flush FIFOs, so we're not stuck
     * processing stale reference input. */
    if (spk_running) {
        ssize_t spk_frame_size_bytes = aec->spk_frame_size_bytes;
        ssize_t sample_rate_ratio = aec->spk_sampling_rate / aec->mic_sampling_rate;
        size_t resampler_in_frames = in_frames * sample_rate_ratio;
        int frame, ch;
        ssize_t req_bytes = resampler_in_frames * spk_frame_size_bytes;

        if (!aec->prev_spk_running) {
            flush_aec_fifos(aec);
        }

        if (fifo_available_to_read(aec->spk_fifo) <= 0) {
            ALOGV("Echo reference buffer empty, zeroing reference....");
            memset(aec->spk_buf, 0, req_bytes);
        } else {
            print_queue_status_to_log(aec, false);
            /* Read from FIFO */
            ssize_t read_bytes = fifo_read(aec->spk_fifo, aec->spk_buf_playback_format, req_bytes);
            update_spk_timestamp(aec, read_bytes, &spk_time);

            if (read_bytes < req_bytes) {
                ALOGI("Could only read %zu of %zu bytes", read_bytes, req_bytes);
                if (read_bytes > 0) {
                    memmove(aec->spk_buf_playback_format + req_bytes - read_bytes, aec->spk_buf_playback_format, read_bytes);
                    memset(aec->spk_buf_playback_format, 0, req_bytes - read_bytes);
                } else {
                    ALOGE("Fifo read returned code %zd ", read_bytes);
                    ret = -ENOMEM;
                    goto exit;
                }
            }

            /* Only 1 channel required, convert in-place */
            int16_t *src_Nch = &aec->spk_buf_playback_format[CHANNEL_STEREO];
            int16_t *dst_1ch = &aec->spk_buf_playback_format[1];
            for (frame = 1; frame < resampler_in_frames; frame++) {
                *dst_1ch = *src_Nch;
                dst_1ch++;
                src_Nch += CHANNEL_STEREO;
            }

            /* Resample to mic sampling rate (16-bit resampler) */
            size_t in_frame_count = resampler_in_frames;
            size_t out_frame_count = in_frames;
            aec->spk_resampler->resample_from_input(
                                    aec->spk_resampler,
                                    aec->spk_buf_playback_format,
                                    &in_frame_count,
                                    aec->spk_buf_resampler_out,
                                    &out_frame_count);

            /* Convert to 32 bit */
            int16_t *src16 = aec->spk_buf_resampler_out;
            int32_t *dst32 = aec->spk_buf;
            for (frame = 0; frame < in_frames; frame++) {
                for (ch = 0; ch < NUM_LOUDSPEAKER_FEEDS; ch++) {
                   *dst32++ = ((int32_t)*src16++) << 16;
                }
            }
        }
    } else {
        /* No new playback samples, so don't run AEC.
         * 'buffer' already contains input samples. */
        ALOGV("Speaker not running, skipping AEC..");
        goto exit;
    }

    int64_t time_diff = (mic_time > spk_time) ? (mic_time - spk_time) : (spk_time - mic_time);
    if ((spk_time == 0) || (mic_time == 0) || (time_diff > MAX_TIMESTAMP_DIFF_USEC)) {
        ALOGV("Speaker-mic timestamps diverged, skipping AEC");
        flush_aec_fifos(aec);
        aec_spk_mic_reset();
        goto exit;
    }

    ALOGV("Mic time: %"PRIu64", spk time: %"PRIu64, mic_time, spk_time);
    /* AEC processing call - output stored at 'buffer' */
    int32_t aec_status = aec_spk_mic_process(
        aec->spk_buf, spk_time,
        aec->mic_buf, mic_time,
        in_frames,
        buffer);

    if (!aec_status) {
        ALOGE("AEC processing failed!");
        ret = -EINVAL;
    }

exit:
    aec->prev_spk_running = spk_running;
    ALOGV("Mic time: %"PRIu64", spk time: %"PRIu64, mic_time, spk_time);
    if (ret) {
        /* Best we can do is copy over the raw mic signal */
        memcpy(buffer, aec->mic_buf, bytes);
        flush_aec_fifos(aec);
        aec_spk_mic_reset();
    }

#if DEBUG_AEC
    ssize_t ref_bytes = in_frames*NUM_LOUDSPEAKER_FEEDS*4; /* ref data is 32-bit at this point */

    FILE *fp_in = fopen("/data/local/traces/aec_in.pcm", "a+");
    if (fp_in) {
        fwrite((char *)aec->mic_buf, 1, bytes, fp_in);
        fclose(fp_in);
    } else {
        ALOGE("AEC debug: Could not open file aec_in.pcm!");
    }
    FILE *fp_out = fopen("/data/local/traces/aec_out.pcm", "a+");
    if (fp_out) {
        fwrite((char *)buffer, 1, bytes, fp_out);
        fclose(fp_out);
    } else {
        ALOGE("AEC debug: Could not open file aec_out.pcm!");
    }
    FILE *fp_ref = fopen("/data/local/traces/aec_ref.pcm", "a+");
    if (fp_ref) {
        fwrite((char *)aec->spk_buf, 1, ref_bytes, fp_ref);
        fclose(fp_ref);
    } else {
        ALOGE("AEC debug: Could not open file aec_ref.pcm!");
    }
    FILE *fp_ts = fopen("/data/local/traces/aec_timestamps.txt", "a+");
    if (fp_ts) {
        fprintf(fp_ts, "%"PRIu64",%"PRIu64"\n", mic_time, spk_time);
        fclose(fp_ts);
    } else {
        ALOGE("AEC debug: Could not open file aec_timestamps.txt!");
    }
#endif /* #if DEBUG_AEC */
    ALOGV("%s exit", __func__);
    return ret;
}
