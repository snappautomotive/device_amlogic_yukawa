/*
 * Copyright (C) 2018 Knowles Electronics
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

#define LOG_TAG "ia_tunneling_hal"
#define LOG_NDEBUG 0

#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include <string.h>
#include <sys/ioctl.h>
#include <stdint.h>
#include <limits.h>

#include "iaxxx-tunnel-intf.h"
#include "iaxxx-system-identifiers.h"

#include "logger.h"
#include "tunnel.h"

#define TUNNELING_DEVICE "/dev/tunnel0"
#define FUNCTION_ENTRY_LOG ALOGV("Entering %s", __func__);
#define FUNCTION_EXIT_LOG ALOGV("Exiting %s", __func__);

struct ia_tunneling_hal {
    FILE *tunnel_dev;
};

struct ia_tunneling_hal* ia_start_tunneling(int buffering_size)
{
    struct ia_tunneling_hal *thdl;
    int err;

    FUNCTION_ENTRY_LOG;

    thdl = (struct ia_tunneling_hal*) malloc(sizeof(struct ia_tunneling_hal));
    if (NULL == thdl) {
        ALOGE("%s: ERROR Failed to allocate memory of ia_tunneling_hal",
            __func__);
        return NULL;
    }

    thdl->tunnel_dev = fopen(TUNNELING_DEVICE, "r");
    if (NULL == thdl->tunnel_dev) {
        ALOGE("%s: ERROR Failed to open the tunneling device - %s",
            __func__, strerror(errno));
        free(thdl);
        return NULL;
    }

    if (buffering_size) {
        struct iaxxx_tunnel_output_buffer tb;

        tb.size = buffering_size;
        err = ioctl(fileno(thdl->tunnel_dev), TUNNEL_SET_OUTPUT_BUF_SIZE, &tb);
        if (err == -1) {
            ALOGE("%s: ERROR Tunnel Set Output buffer size failed %s",
                    __func__, strerror(errno));
        }
    }

    return thdl;
}

int ia_stop_tunneling(struct ia_tunneling_hal *thdl)
{
    FUNCTION_ENTRY_LOG;

    if (thdl->tunnel_dev) {
        fclose(thdl->tunnel_dev);
        thdl->tunnel_dev = NULL;
    }

    free(thdl);

    return 0;
}

int ia_enable_tunneling_source(struct ia_tunneling_hal *thdl,
                                          unsigned int src_id,
                                          unsigned int tnl_mode,
                                          unsigned int tnl_encode)
{
    FUNCTION_ENTRY_LOG;
    struct tunlMsg tm;
    int err = 0;

    if (NULL == thdl->tunnel_dev) {
        ALOGE("%s: ERROR Tunneling device is not opened", __func__);
        err = -EIO;
        goto exit;
    }

    tm.tunlSrc = src_id;
    tm.tunlMode = tnl_mode;
    tm.tunlEncode = tnl_encode;
    err = ioctl(fileno(thdl->tunnel_dev), TUNNEL_SETUP, &tm);
    if (err == -1) {
        ALOGE("%s: ERROR Tunnel setup failed %s", __func__, strerror(errno));
    }

exit:
    return err;
}

int ia_disable_tunneling_source(struct ia_tunneling_hal *thdl,
                                            unsigned int src_id,
                                            unsigned int tunl_mode,
                                            unsigned int tunl_encode)
{
    FUNCTION_ENTRY_LOG;
    struct tunlMsg tm;
    int err = 0;

    if (NULL == thdl->tunnel_dev) {
        ALOGE("%s: ERROR Tunneling devices is not opened", __func__);
        err = -EIO;
        goto exit;
    }

    tm.tunlSrc = src_id;
    tm.tunlMode = tunl_mode;
    tm.tunlEncode = tunl_encode;
    err = ioctl(fileno(thdl->tunnel_dev), TUNNEL_TERMINATE, &tm);
    if (err == -1) {
        ALOGE("%s: ERROR Tunnel terminate failed %s",
            __func__, strerror(errno));
    }

exit:
    return err;
}

int ia_read_tunnel_data(struct ia_tunneling_hal *thdl,
                                void *buf,
                                int buf_sz)
{
    //FUNCTION_ENTRY_LOG;
    int read_bytes;

    if ((NULL == buf) || (buf_sz <= 0)) {
        ALOGE("%s: ERROR Invalid buffer or buffer size", __func__);
        return -EINVAL;
    }

    read_bytes = fread(buf, 1, buf_sz, thdl->tunnel_dev);
    if (0 == read_bytes) {
        ALOGE("%s: Warning zero bytes read from tunneling device, "
              "trying again..", __func__);
    }

    return read_bytes;
}
