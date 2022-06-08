#ifndef _IAXXX_MIXER_CTRL_H
#define _IAXXX_MIXER_CTRL_H

struct mixer* open_mixer_ctl();

void close_mixer_ctl(struct mixer *mixer);

int set_mixer_ctl_str(struct mixer *mixer, char *id, const char *string);

int set_mixer_ctl_val(struct mixer *mixer, char *id, int value) ;

#endif
