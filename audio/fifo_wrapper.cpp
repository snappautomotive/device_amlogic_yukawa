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

#define LOG_TAG "audio_utils_fifo_wrapper"
// #define LOG_NDEBUG 0

#include <stdint.h>
#include <errno.h>
#include <log/log.h>
#include <audio_utils/fifo.h>
#include "fifo_wrapper.h"

int fifo_init(struct audio_fifo_itfe **p_fifo_itfe, uint32_t bytes, bool reader_throttles_writer) {
	struct audio_fifo_itfe *fifo_itfe = new struct audio_fifo_itfe;
	fifo_itfe->p_buffer = new int8_t[bytes];
	if (fifo_itfe->p_buffer == NULL) {
		ALOGE("Failed to allocate fifo buffer!");
		return -ENOMEM;
	}
	audio_utils_fifo *p_fifo = new audio_utils_fifo(bytes, 1, fifo_itfe->p_buffer, reader_throttles_writer);
	fifo_itfe->p_fifo = static_cast<void*>(p_fifo);
	fifo_itfe->p_fifo_writer = static_cast<void*>(new audio_utils_fifo_writer(*p_fifo));
	fifo_itfe->p_fifo_reader = static_cast<void*>(new audio_utils_fifo_reader(*p_fifo));

	*p_fifo_itfe = fifo_itfe;

	return 0;
}

void fifo_release(struct audio_fifo_itfe *fifo_itfe) {
	delete static_cast<audio_utils_fifo_writer *>(fifo_itfe->p_fifo_writer);
	delete static_cast<audio_utils_fifo_reader *>(fifo_itfe->p_fifo_reader);
	delete static_cast<audio_utils_fifo *>(fifo_itfe->p_fifo);
	delete[] fifo_itfe->p_buffer;
	delete fifo_itfe;
}

ssize_t fifo_read(struct audio_fifo_itfe *fifo_itfe, void *buffer, size_t bytes) {
	audio_utils_fifo_reader *p_fifo_reader = static_cast<audio_utils_fifo_reader *>(fifo_itfe->p_fifo_reader);
	return p_fifo_reader->read(buffer, bytes);
}

ssize_t fifo_write(struct audio_fifo_itfe *fifo_itfe, void *buffer, size_t bytes) {
	audio_utils_fifo_writer *p_fifo_writer = static_cast<audio_utils_fifo_writer *>(fifo_itfe->p_fifo_writer);
	return p_fifo_writer->write(buffer, bytes);
}

ssize_t fifo_available_to_read(struct audio_fifo_itfe *fifo_itfe) {
	audio_utils_fifo_reader *p_fifo_reader = static_cast<audio_utils_fifo_reader *>(fifo_itfe->p_fifo_reader);
	return p_fifo_reader->available();
}

ssize_t fifo_available_to_write(struct audio_fifo_itfe *fifo_itfe) {
	audio_utils_fifo_writer *p_fifo_writer = static_cast<audio_utils_fifo_writer *>(fifo_itfe->p_fifo_writer);
	return p_fifo_writer->available();
}

ssize_t fifo_flush(struct audio_fifo_itfe *fifo_itfe) {
	audio_utils_fifo_reader *p_fifo_reader = static_cast<audio_utils_fifo_reader *>(fifo_itfe->p_fifo_reader);
	return p_fifo_reader->flush();
}
