#ifndef __MIXER_UTILS__
#define __MIXER_UTILS__

#include "tinyalsa/asoundlib.h"

/**
 * Initialize the VQ HAL
 *
 * Input  - NA
 * Output - Handle to mixer on success, NULL on failure
 */
struct mixer* open_mixer_ctl();

/**
 * Initialize the VQ HAL
 *
 * Input  - mixer -  mixer handle
 * Output - 0 on success, on failure < 0
 */
void close_mixer_ctl(struct mixer *mixer);

/**
 * Initialize the VQ HAL
 *
 * Input  - mixer - mixer handle
 *        - id - key
 *        - value - value
 * Output - 0 on success, on failure < 0
 */
int set_mixer_ctl_val(struct mixer *mixer, char *id, int value);

/**
 * Initialize the VQ HAL
 *
 * Input  - mixer - mixer handle
 *        - id - key
 *        - string - value string
 * Output - 0 on success, on failure < 0
 */
int set_mixer_ctl_str(struct mixer *mixer, char *id, const char *string);


#endif // #ifndef __MIXER_UTILS__
