#ifndef _AVS_STREAMER_H_
#define _AVS_STREAMER_H_

int handle_asr_stream_start(int start_frame, int end_frame);

int handle_asr_read_stream(unsigned char *data, int size);

int handle_asr_stream_stop();
#endif // #ifndef _AVS_STREAMER_H_
