#define LOG_TAG "knowles_util_mixer"

#include <cutils/log.h>
#include <cutils/trace.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <tinyalsa/asoundlib.h>
#include "mixer_ctl.h"

#define FUNCTION_ENTRY_LOG ALOGD("+%s+", __func__);
#define FUNCTION_EXIT_LOG ALOGD("-%s-", __func__);

#define CARD_NUM 0
#define CVQ_BUFFER 1

struct mixer* open_mixer_ctl() {
    FUNCTION_ENTRY_LOG
    return mixer_open(CARD_NUM);
}

void close_mixer_ctl(struct mixer *mixer) {
    FUNCTION_ENTRY_LOG
    if (mixer) {
        mixer_close(mixer);
    }
    FUNCTION_EXIT_LOG
}

int set_mixer_ctl_str(struct mixer *mixer, char *id, const char *string) {
    struct mixer_ctl *ctl = NULL;
    int err = 0;
    FUNCTION_ENTRY_LOG

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
    FUNCTION_EXIT_LOG
    return err;
}

int set_mixer_ctl_val(struct mixer *mixer, char *id, int value) {
    struct mixer_ctl *ctl = NULL;
    int err = 0;
    FUNCTION_ENTRY_LOG

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

    if (mixer_ctl_set_value(ctl, 0, value)) {
        ALOGE("%s: ERROR Invalid value for %s", __func__, id);
        err = -1;
        goto exit;
    }

exit:
    FUNCTION_EXIT_LOG
    return err;
}
