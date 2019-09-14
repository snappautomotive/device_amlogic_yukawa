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

#ifndef _AUDIO_FIFO_WRAPPER_H_
#define _AUDIO_FIFO_WRAPPER_H_

#ifdef __cplusplus
extern "C" {
#endif

struct audio_fifo_itfe {
	void *p_fifo;
	void *p_fifo_reader;
	void *p_fifo_writer;
	int8_t *p_buffer;
};

int fifo_init(struct audio_fifo_itfe **p_fifo_itfe, uint32_t bytes, bool reader_throttles_writer);
void fifo_release(struct audio_fifo_itfe *fifo_itfe);
ssize_t fifo_read(struct audio_fifo_itfe *fifo_itfe, void *buffer, size_t bytes);
ssize_t fifo_write(struct audio_fifo_itfe *fifo_itfe, void *buffer, size_t bytes);
ssize_t fifo_available_to_read(struct audio_fifo_itfe *fifo_itfe);
ssize_t fifo_available_to_write(struct audio_fifo_itfe *fifo_itfe);
ssize_t fifo_flush(struct audio_fifo_itfe *fifo_itfe);

#ifdef __cplusplus
}
#endif
#endif /* #ifndef _AUDIO_FIFO_WRAPPER_H_ */
