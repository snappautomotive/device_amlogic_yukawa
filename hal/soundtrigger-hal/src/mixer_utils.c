#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#include "logger.h"
#include "mixer_utils.h"

#define MAX_SND_CARD    (8)
#define RETRY_NUMBER    (10)
#define RETRY_US        (500000)

#ifdef ANDROID
#define CARD_NAME       "hikey-sndcard"
#else
#define CARD_NAME       "audio-iaxxx"
#endif

static int find_sound_card() {
    int retry_num = 0, snd_card_num = 0, ret = -1;
    const char *snd_card_name;
    struct mixer *mixer = NULL;

    ALOGD("+%s+", __func__);
    while (snd_card_num < MAX_SND_CARD) {
        mixer = mixer_open(snd_card_num);
        while (!mixer && retry_num < RETRY_NUMBER) {
            usleep(RETRY_US);
            mixer = mixer_open(snd_card_num);
            retry_num++;
        }

        if (!mixer) {
            ALOGE("%s: Unable to open the mixer card: %d", __func__,
                    snd_card_num);
            retry_num = 0;
            snd_card_num++;
            continue;
        }
	return snd_card_num;
        snd_card_name = mixer_get_name(mixer);
        ALOGV("%s: snd_card_name: %s", __func__, snd_card_name);
        mixer_close(mixer);

        if(0 == strcmp(CARD_NAME, snd_card_name)){
            ALOGD("Found %s at %d", CARD_NAME, snd_card_num);
            ret = snd_card_num;
            break;
        } else {
            snd_card_num++;
            continue;
        }
    }
    ALOGD("-%s-", __func__);
    return ret;
}

struct mixer* open_mixer_ctl() {
    int card_num = 0;
    struct mixer *hdl = NULL;

    card_num = find_sound_card();
    if (-1 == card_num) {
        ALOGE("%s: Failed to find the sound card number", __func__);
        return hdl;
    }

    hdl = mixer_open(card_num);
    if (NULL == hdl) {
        ALOGE("%s: Error Failed to open mixer for card number %d",
                __func__, card_num);
        return NULL;
    }

    return hdl;
}

void close_mixer_ctl(struct mixer *mixer)
{
    if (mixer) {
        mixer_close(mixer);
    }
}

int set_mixer_ctl_val(struct mixer *mixer, char *id, int value)
{
    struct mixer_ctl *ctl = NULL;
    int err = 0;

    if ((NULL == mixer) || (NULL == id)) {
        ALOGE("%s: ERROR Null argument passed", __func__);
        err = -EINVAL;
        return err;
    }

    ctl = mixer_get_ctl_by_name(mixer, id);
    if (NULL == ctl) {
        ALOGE("%s: ERROR Invalid control name: %s", __func__, id);
        err = -1;
        return err;
    }

    if (mixer_ctl_set_value(ctl, 0, value)) {
        ALOGE("%s: ERROR Invalid value %s", __func__, id);
        err = -1;
        return err;
    }

    return err;
}

int set_mixer_ctl_str(struct mixer *mixer, char *id, const char *string)
{
    struct mixer_ctl *ctl = NULL;
    int err = 0;

    if ((NULL == mixer) || (NULL == id)) {
        ALOGE("%s: ERROR Null argument passed", __func__);
        err = -EINVAL;
        goto exit;
    }

    ctl = mixer_get_ctl_by_name(mixer, id);
    if (NULL == ctl) {
        ALOGE("%s: ERROR Invalid control name: %s", __func__, id);
        err = -1;
        goto exit;
    }

    if (mixer_ctl_set_enum_by_string(ctl, string)) {
        ALOGE("%s: ERROR Invalid string for %s", __func__, id);
        err = -1;
        goto exit;
    }

exit:
    return err;
}

int set_mixer_ctl_array(struct mixer *mixer, char *id, int *value, unsigned int count)
{
    struct mixer_ctl *ctl = NULL;
    int err = 0;
    int i;

    if ((NULL == mixer) || (NULL == id)) {
        ALOGE("%s: ERROR Null argument passed", __func__);
        err = -EINVAL;
        goto exit;
    }

    ctl = mixer_get_ctl_by_name(mixer, id);
    if (NULL == ctl) {
        ALOGE("%s: ERROR Invalid control id: %s", __func__, id);
        err = -1;
        goto exit;
    }

    for (i = 0; i < count; i++) {
        if (mixer_ctl_set_value(ctl, i, value[i])) {
            ALOGE("%s: Error: invalid value for index %d\n", __func__, i);
            err = -EINVAL;
            goto exit;
        }
    }

exit:
    return err;
}

