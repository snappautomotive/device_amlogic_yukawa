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

#define LOG_TAG "knowles_odsp_hw"
#define LOG_NDEBUG 0

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <inttypes.h>
#include <unistd.h>

#include "iaxxx-odsp.h"

#include "iaxxx_odsp_hw.h"
#include "logger.h"

#define DEV_NODE "/dev/iaxxx-odsp-celldrv"
#define FUNCTION_ENTRY_LOG ALOGD("+%s+", __func__);
#define FUNCTION_EXIT_LOG ALOGD("-%s-", __func__);

#define MAX_ACCESS_RETRY  5

struct iaxxx_odsp_hw {
    FILE *dev_node;
};

/**
 * Initialize the ODSP HAL
 *
 * Input  - NA
 * Output - Handle to iaxxx_odsp_hw on success, NULL on failure
 */
struct iaxxx_odsp_hw* iaxxx_odsp_init() {
    struct iaxxx_odsp_hw *ioh;
    int retry = 0, err = 0;

    FUNCTION_ENTRY_LOG;

    ioh = (struct iaxxx_odsp_hw*) malloc(sizeof(struct iaxxx_odsp_hw));
    if (NULL == ioh) {
        ALOGE("%s: ERROR: Failed to allocate memory for iaxxx_odsp_hw",
                __func__);
        return NULL;
    }

    ALOGI("%s: Checking for access", __func__);
    while((err = access(DEV_NODE, F_OK|R_OK|W_OK)) != 0 &&
                                retry < MAX_ACCESS_RETRY) {
        usleep(1000*1000*5);
        retry++;
        ALOGE("%s: Waiting for ODSP device node ....", __func__);
    }

    if (err != 0) {
        ALOGE("%s: ERROR: Failed with error %s", __func__, strerror(errno));
        return NULL;
    }

    ALOGI("%s: Access check passed!", __func__);

    ioh->dev_node = fopen(DEV_NODE, "rw");
    if (NULL == ioh->dev_node) {
        ALOGE("%s: ERROR: Failed to open %s", __func__, DEV_NODE);
        free(ioh);
        return NULL;
    }

    FUNCTION_EXIT_LOG;
    return ioh;
}

/**
 * De-Initialize the ODSP HAL
 *
 * Input  - odsp_hw_hdl - Handle to odsp hw structure
 * Output - 0 on success, on failure < 0
 */
int iaxxx_odsp_deinit(struct iaxxx_odsp_hw *odsp_hw_hdl) {
    FUNCTION_ENTRY_LOG;
    if (NULL == odsp_hw_hdl) {
        ALOGE("%s: ERROR: Invalid handle to iaxxx_odsp_hw", __func__);
        return -1;
    }

    if (odsp_hw_hdl->dev_node) {
        fclose(odsp_hw_hdl->dev_node);
    }

    free(odsp_hw_hdl);

    FUNCTION_EXIT_LOG;
    return 0;
}

/**
 * Load a package
 *
 * Input  - odsp_hw_hdl - Handle to odsp hw structure
 *          pkg_name    - Relative path to the Package binary (Should be placed in
 *                        the firmware location)
 *          pkg_id      - Package ID
 * Output - 0 on success, on failure < 0
 */
int iaxxx_odsp_package_load(struct iaxxx_odsp_hw *odsp_hw_hdl,
                            const char *pkg_name, const uint32_t pkg_id) {
    int err = 0;
    struct iaxxx_pkg_mgmt_info pkg_info;

    FUNCTION_ENTRY_LOG;

    if (NULL == odsp_hw_hdl) {
        ALOGE("%s: ERROR: Invalid handle to iaxxx_odsp_hw", __func__);
        return -1;
    }

    if (NULL == pkg_name) {
        ALOGE("%s: ERROR: Package name cannot be null", __func__);
        return -1;
    }

    ALOGV("%s: Package name %s, package id %u",
                             __func__, pkg_name, pkg_id);

    strcpy(pkg_info.pkg_name, pkg_name);
    pkg_info.pkg_id = pkg_id;
    pkg_info.proc_id = 0;
    err = ioctl(fileno(odsp_hw_hdl->dev_node),
                ODSP_LOAD_PACKAGE, (unsigned long) &pkg_info);
    if (-1 == err) {
        ALOGE("%s: ERROR: Failed with error %s", __func__, strerror(errno));
        return err;
    }

    FUNCTION_EXIT_LOG;
    return err;
}

/**
 * Unload a package
 *
 * Input  - odsp_hw_hdl - Handle to odsp hw structure
 *          pkg_id      - Package ID
 * Output - 0 on success, on failure < 0
 */
int iaxxx_odsp_package_unload(struct iaxxx_odsp_hw *odsp_hw_hdl,
                              const uint32_t pkg_id) {
    int err = 0;
    struct iaxxx_pkg_mgmt_info pkg_info;

    FUNCTION_ENTRY_LOG;

    if (NULL == odsp_hw_hdl) {
        ALOGE("%s: ERROR: Invalid handle to iaxxx_odsp_hw", __func__);
        return -1;
    }

    ALOGV("%s: Package id %u", __func__, pkg_id);

    pkg_info.pkg_id = pkg_id;
    err = ioctl(fileno(odsp_hw_hdl->dev_node),
                ODSP_UNLOAD_PACKAGE, (unsigned long) &pkg_info);
    if (-1 == err) {
        ALOGE("%s: ERROR: Failed with error %s", __func__, strerror(errno));
        return err;
    }

    FUNCTION_EXIT_LOG;
    return err;
}

/**
 * Create a plugin
 *
 * Input  - odsp_hw_hdl - Handle to odsp hw structure
 *          plg_idx     - Plugin Index
 *          pkg_id      - Package ID
 *          block_id    - Block ID
 *          inst_id     - Instance ID
 *          priority    - Priority of the plugin
 * Output - 0 on success, on failure < 0
 */
int iaxxx_odsp_plugin_create(struct iaxxx_odsp_hw *odsp_hw_hdl,
                             const uint32_t plg_idx, const uint32_t pkg_id,
                             const uint32_t block_id, const uint32_t inst_id,
                             const uint32_t priority) {
    int err = 0;
    struct iaxxx_plugin_info pi;

    FUNCTION_ENTRY_LOG;

    if (NULL == odsp_hw_hdl) {
        ALOGE("%s: ERROR: Invalid handle to iaxxx_odsp_hw", __func__);
        return -1;
    }

    ALOGV("%s: Plugin index %u, package id %u, block id %u, instance id %u\
         priority %u", __func__, plg_idx, pkg_id, block_id, inst_id, priority);

    pi.plg_idx  = plg_idx;
    pi.pkg_id   = pkg_id;
    pi.block_id = block_id;
    pi.inst_id  = inst_id;
    pi.priority = priority;
    pi.config_id = 0;
    err = ioctl(fileno(odsp_hw_hdl->dev_node),
                ODSP_PLG_CREATE, (unsigned long) &pi);
    if (-1 == err) {
        ALOGE("%s: ERROR: Failed with error %s", __func__, strerror(errno));
        return err;
    }

    FUNCTION_EXIT_LOG;
    return err;
}

/**
 * Set the creation configuration on a plugin
 *
 * Input  - odsp_hw_hdl - Handle to odsp hw structure
 *          inst_id     - Instance ID
 *          block_id    - Block ID
 *          cdata       - Creation configuration data
 * Output - 0 on success, on failure < 0
 */
int iaxxx_odsp_plugin_set_creation_config(struct iaxxx_odsp_hw *odsp_hw_hdl,
                                          const uint32_t inst_id,
                                          const uint32_t block_id,
                                        struct iaxxx_create_config_data cdata) {
    int err = 0;
    struct iaxxx_plugin_create_cfg pcc;

    FUNCTION_ENTRY_LOG;

    if (NULL == odsp_hw_hdl) {
        ALOGE("%s: ERROR: Invalid handle to iaxxx_odsp_hw", __func__);
        return -1;
    }

    pcc.inst_id = inst_id;
    pcc.block_id = block_id;
    switch (cdata.type) {
    case CONFIG_FILE:
        pcc.cfg_size = 0;
        strcpy(pcc.file_name, cdata.data.fdata.filename);
        ALOGV("%s: Configuration file name %s", __func__, pcc.file_name);
    break;
    case CONFIG_VALUE:
        pcc.cfg_size = cdata.data.vdata.config_val_sz;
        pcc.cfg_val = cdata.data.vdata.config_val;
        ALOGV("%s: Configuration value %"PRId64, __func__, pcc.cfg_val);
    break;
    default:
        ALOGE("%s: ERROR: Invalid type of configuration type", __func__);
        return -1;
    break;
    }

    ALOGV("%s: Instance id %u, block id %u", __func__, inst_id, block_id);

    err = ioctl(fileno(odsp_hw_hdl->dev_node),
                ODSP_PLG_SET_CREATE_CFG, (unsigned long) &pcc);
    if (-1 == err) {
        ALOGE("%s: ERROR: Failed with error %s", __func__, strerror(errno));
        return err;
    }

    FUNCTION_EXIT_LOG;
    return err;
}

/**
 * Destroy the plugin
 *
 * Input  - odsp_hw_hdl - Handle to odsp hw structure
 *          block_id    - Block ID
 *          inst_id     - Instance ID
 * Output - 0 on success, on failure < 0
 */
int iaxxx_odsp_plugin_destroy(struct iaxxx_odsp_hw *odsp_hw_hdl,
                              const uint32_t block_id, const uint32_t inst_id) {
    int err = 0;
    struct iaxxx_plugin_info pi;

    FUNCTION_ENTRY_LOG;

    if (NULL == odsp_hw_hdl) {
        ALOGE("%s: ERROR: Invalid handle to iaxxx_odsp_hw", __func__);
        return -1;
    }

    ALOGV("%s: block id %u, instance id %u", __func__, block_id, inst_id);

    pi.block_id = block_id;
    pi.inst_id  = inst_id;
    err = ioctl(fileno(odsp_hw_hdl->dev_node),
                ODSP_PLG_DESTROY, (unsigned long) &pi);
    if (-1 == err) {
        ALOGE("%s: ERROR: Failed with error %s", __func__, strerror(errno));
        return err;
    }

    FUNCTION_EXIT_LOG;
    return err;
}

/**
 * Enable the plugin
 *
 * Input  - odsp_hw_hdl - Handle to odsp hw structure
 *          block_id    - Block ID
 *          inst_id     - Instance ID
 * Output - 0 on success, on failure < 0
 */
int iaxxx_odsp_plugin_enable(struct iaxxx_odsp_hw *odsp_hw_hdl,
                             const uint32_t block_id, const uint32_t inst_id) {
    int err = 0;
    struct iaxxx_plugin_info pi;

    FUNCTION_ENTRY_LOG;

    if (NULL == odsp_hw_hdl) {
        ALOGE("%s: ERROR: Invalid handle to iaxxx_odsp_hw", __func__);
        return -1;
    }

    ALOGV("%s: block id %u, instance id %u", __func__, block_id, inst_id);

    pi.block_id = block_id;
    pi.inst_id  = inst_id;
    err = ioctl(fileno(odsp_hw_hdl->dev_node),
                ODSP_PLG_ENABLE, (unsigned long) &pi);
    if (-1 == err) {
        ALOGE("%s: ERROR: Failed with error %s", __func__, strerror(errno));
        return err;
    }

    FUNCTION_EXIT_LOG;
    return err;
}

/**
 * Disable the plugin
 *
 * Input  - odsp_hw_hdl - Handle to odsp hw structure
 *          block_id    - Block ID
 *          inst_id     - Instance ID
 * Output - 0 on success, on failure < 0
 */
int iaxxx_odsp_plugin_disable(struct iaxxx_odsp_hw *odsp_hw_hdl,
                              const uint32_t block_id, const uint32_t inst_id) {
    int err = 0;
    struct iaxxx_plugin_info pi;

    FUNCTION_ENTRY_LOG;

    if (NULL == odsp_hw_hdl) {
        ALOGE("%s: ERROR: Invalid handle to iaxxx_odsp_hw", __func__);
        return -1;
    }

    ALOGV("%s: block id %u, instance id %u", __func__, block_id, inst_id);

    pi.block_id = block_id;
    pi.inst_id  = inst_id;
    err = ioctl(fileno(odsp_hw_hdl->dev_node),
                ODSP_PLG_DISABLE, (unsigned long) &pi);
    if (-1 == err) {
        ALOGE("%s: ERROR: Failed with error %s", __func__, strerror(errno));
        return err;
    }

    FUNCTION_EXIT_LOG;
    return err;
}

/**
 * Reset the plugin
 *
 * Input  - odsp_hw_hdl - Handle to odsp hw structure
 *          block_id    - Block ID
 *          inst_id     - Instance ID
 * Output - 0 on success, on failure < 0
 */
int iaxxx_odsp_plugin_reset(struct iaxxx_odsp_hw *odsp_hw_hdl,
                            const uint32_t block_id, const uint32_t inst_id) {
    int err = 0;
    struct iaxxx_plugin_info pi;

    FUNCTION_ENTRY_LOG;

    if (NULL == odsp_hw_hdl) {
        ALOGE("%s: ERROR: Invalid handle to iaxxx_odsp_hw", __func__);
        return -1;
    }

    ALOGV("%s: block id %u, instance id %u", __func__, block_id, inst_id);

    pi.block_id = block_id;
    pi.inst_id  = inst_id;
    err = ioctl(fileno(odsp_hw_hdl->dev_node),
                ODSP_PLG_RESET, (unsigned long) &pi);
    if (-1 == err) {
        ALOGE("%s: ERROR: Failed with error %s", __func__, strerror(errno));
        return err;
    }

    FUNCTION_EXIT_LOG;
    return err;
}

/**
 * Set a parameter on a plugin
 *
 * Input  - odsp_hw_hdl - Handle to odsp hw structure
 *          inst_id     - Instance ID
 *          block_id    - Block ID
 *          param_id    - Parameter ID
 *          param_val   - Parameter Value
 * Output - 0 on success, on failure < 0
 */
int iaxxx_odsp_plugin_set_parameter(struct iaxxx_odsp_hw *odsp_hw_hdl,
                                    const uint32_t inst_id,
                                    const uint32_t block_id,
                                    const uint32_t param_id,
                                    const uint32_t param_val) {
    int err = 0;
    struct iaxxx_plugin_param pp;

    FUNCTION_ENTRY_LOG;

    if (NULL == odsp_hw_hdl) {
        ALOGE("%s: ERROR: Invalid handle to iaxxx_odsp_hw", __func__);
        return -1;
    }

    ALOGV("%s: Instance id %u, block id %u param_id %u param_val %u", __func__,
                inst_id, block_id, param_id, param_val);

    pp.inst_id   = inst_id;
    pp.block_id  = block_id;
    pp.param_id  = param_id;
    pp.param_val = param_val;
    err = ioctl(fileno(odsp_hw_hdl->dev_node),
                ODSP_PLG_SET_PARAM, (unsigned long) &pp);
    if (-1 == err) {
        ALOGE("%s: ERROR: Failed with error %s", __func__, strerror(errno));
        return err;
    }

    FUNCTION_EXIT_LOG;
    return 0;
}

/**
 * Get the value of parameter on a plugin
 *
 * Input  - odsp_hw_hdl - Handle to odsp hw structure
 *          inst_id     - Instance ID
 *          block_id    - Block ID
 *          param_id    - Parameter ID
 *          param_val   - Parameter Value
 * Output - 0 on success, on failure < 0
 */
int iaxxx_odsp_plugin_get_parameter(struct iaxxx_odsp_hw *odsp_hw_hdl,
                                    const uint32_t inst_id,
                                    const uint32_t block_id,
                                    const uint32_t param_id,
                                    uint32_t *param_val) {
    int err = 0;
    struct iaxxx_plugin_param pp;

    FUNCTION_ENTRY_LOG;

    if (NULL == odsp_hw_hdl) {
        ALOGE("%s: ERROR: Invalid handle to iaxxx_odsp_hw", __func__);
        return -1;
    }

    pp.inst_id   = inst_id;
    pp.block_id  = block_id;
    pp.param_id  = param_id;
    pp.param_val = 0;
    err = ioctl(fileno(odsp_hw_hdl->dev_node),
                ODSP_PLG_GET_PARAM, (unsigned long) &pp);
    if (-1 == err) {
        ALOGE("%s: ERROR: Failed with error %s", __func__, strerror(errno));
        return err;
    }

    *param_val = pp.param_val;

    ALOGV("%s: Instance id %u, block id %u param_id %u param_val %u", __func__,
                inst_id, block_id, param_id, *param_val);


    FUNCTION_EXIT_LOG;
    return 0;
}

/**
 * Set a parameter block on a plugin
 *
 * Input  - odsp_hw_hdl  - Handle to odsp hw structure
 *          inst_id      - Instance ID
 *          block_id     - Block ID
 *          param_buf    - Pointer to the parameter block
 *          param_buf_sz - Parameter block size
 *          id           - Parameter block id
 * Output - 0 on success, on failure < 0
 */
int iaxxx_odsp_plugin_set_parameter_blk(struct iaxxx_odsp_hw *odsp_hw_hdl,
                                        const uint32_t inst_id,
                                        const uint32_t block_id,
                                        const void *param_buf,
                                        const uint32_t param_buf_sz,
                                        const uint32_t id) {
    int err = 0;
    struct iaxxx_plugin_param_blk ppb;

    FUNCTION_ENTRY_LOG;

    if (NULL == odsp_hw_hdl) {
        ALOGE("%s: ERROR: Invalid handle to iaxxx_odsp_hw", __func__);
        return -1;
    }

    ALOGV("%s: Instance id %u, block id %u, param_buf_sz %u, id %u", __func__,
                inst_id, block_id, param_buf_sz, id);

    ppb.inst_id    = inst_id;
    ppb.block_id   = block_id;
    ppb.param_size = param_buf_sz;
    ppb.param_blk  = (uintptr_t)param_buf;
    ppb.id         = id;
    ppb.file_name[0] = '\0';
    err = ioctl(fileno(odsp_hw_hdl->dev_node),
                ODSP_PLG_SET_PARAM_BLK, (unsigned long) &ppb);
    if (-1 == err) {
        ALOGE("%s: ERROR: Failed with error %s", __func__, strerror(errno));
        return err;
    }

    FUNCTION_EXIT_LOG;
    return 0;
}

/**
 * Set a parameter block on a plugin
 *
 * Input  - odsp_hw_hdl  - Handle to odsp hw structure
 *          inst_id      - Instance ID
 *          param_blk_id - Parameter block id
 *          block_id     - Block ID
 *          file_name    - Relative path to the Parameter File(Should be placed in
 *                         the firmware location)
 * Output - 0 on success, on failure < 0
 */
int iaxxx_odsp_plugin_set_parameter_blk_from_file(
                                        struct iaxxx_odsp_hw *odsp_hw_hdl,
                                        const uint32_t inst_id,
                                        const uint32_t param_blk_id,
                                        const uint32_t block_id,
                                        const char *file_name
                                        ) {
    int err = 0;
    struct iaxxx_plugin_param_blk ppb;

    FUNCTION_ENTRY_LOG;

    if (NULL == odsp_hw_hdl) {
        ALOGE("%s: ERROR: Invalid handle to iaxxx_odsp_hw", __func__);
        err = -1;
        goto func_exit;
    }

    ALOGD("%s: Instance id %u, block id %u, file_name %s, parm_blk_id %u", __func__,
                inst_id, block_id, file_name, param_blk_id);

    ppb.param_size = 0;
    ppb.param_blk  = (uintptr_t) NULL;
    ppb.inst_id    = inst_id;
    ppb.block_id   = block_id;
    ppb.id         = param_blk_id;
    strcpy(ppb.file_name, file_name);
    err = ioctl(fileno(odsp_hw_hdl->dev_node),
                ODSP_PLG_SET_PARAM_BLK, (unsigned long) &ppb);
    if (err < 0) {
        ALOGE("%s: ERROR: Failed with error %s", __func__, strerror(errno));
    }

func_exit:
    FUNCTION_EXIT_LOG;
    return err;
}

/**
 * Set Event for the plugin
 *
 * Input  - odsp_hw_hdl     - Handle to odsp hw structure
 *          inst_id         - Instance ID
 *          eventEnableMask - event Mask
 *          block_id        - Block ID
 *
 * Output - 0 on success, on failure < 0
 */
int iaxxx_odsp_plugin_setevent(struct iaxxx_odsp_hw *odsp_hw_hdl,
                              const uint32_t inst_id,
                              const uint32_t eventEnableMask,
                              const uint32_t block_id)
{
    int err = 0;
    struct iaxxx_set_event se;

    FUNCTION_ENTRY_LOG;

    if (NULL == odsp_hw_hdl) {
        ALOGE("%s: ERROR: Invalid handle to iaxxx_odsp_hw", __func__);
        err = -1;
        goto func_exit;
    }

    ALOGV("%s: instance id %u, eventEnableMask %x, block id %u", __func__, inst_id, eventEnableMask, block_id);

    se.block_id = block_id;
    se.event_enable_mask = eventEnableMask;
    se.inst_id  = inst_id;
    err = ioctl(fileno(odsp_hw_hdl->dev_node),
                ODSP_PLG_SET_EVENT, (unsigned long) &se);
    if (-1 == err) {
        ALOGE("%s: ERROR: Failed with error %s", __func__, strerror(errno));
        return err;
    }

func_exit:
    FUNCTION_EXIT_LOG;
    return err;
}

/**
 * Subscribe to an event
 *
 * Input  - odsp_hw_hdl - Handle to odsp hw structure
 *          src_id      - System Id of event source
 *          event_id    - Event Id
 *          dst_id      - System Id of event destination
 *          dst_opaque  - Info sought by destination task when event occurs
 *
 * Output - 0 on success, on failure < 0
 */
int iaxxx_odsp_evt_subscribe(struct iaxxx_odsp_hw *odsp_hw_hdl,
                                const uint16_t src_id,
                                const uint16_t event_id,
                                const uint16_t dst_id,
                                const uint32_t dst_opaque)
{
    int err = 0;
    struct iaxxx_evt_info ei;

    FUNCTION_ENTRY_LOG;

    if (NULL == odsp_hw_hdl) {
        ALOGE("%s: ERROR: Invalid handle to iaxxx_odsp_hw", __func__);
        err = -1;
        goto func_exit;
    }

    ALOGV("%s: src id %u, event id %u, dst_id %u dst_opq %u",
            __func__, src_id, event_id, dst_id, dst_opaque);

    ei.src_id       = src_id;
    ei.event_id     = event_id;
    ei.dst_id       = dst_id;
    ei.dst_opaque   = dst_opaque;

    err = ioctl(fileno(odsp_hw_hdl->dev_node),
            ODSP_EVENT_SUBSCRIBE, (unsigned long) &ei);
    if (err < 0) {
        ALOGE("%s: ERROR: Failed with error %s", __func__, strerror(errno));
    }

func_exit:
    FUNCTION_EXIT_LOG;
    return err;
}

/**
 * UnSubscribe to an event
 *
 * Input  - odsp_hw_hdl - Handle to odsp hw structure
 *          src_id      - System Id of event source
 *          event_id    - Event Id
 *          dst_id      - System Id of event destination
 *          dst_opaque  - Info sought by destination task when event occurs
 *
 * Output - 0 on success, on failure < 0
 */
int iaxxx_odsp_evt_unsubscribe(struct iaxxx_odsp_hw *odsp_hw_hdl,
                                const uint16_t src_id,
                                const uint16_t event_id,
                                const uint16_t dst_id)
{
    int err = 0;
    struct iaxxx_evt_info ei;

    FUNCTION_ENTRY_LOG;

    if (NULL == odsp_hw_hdl) {
        ALOGE("%s: ERROR: Invalid handle to iaxxx_odsp_hw", __func__);
        err = -1;
        goto func_exit;
    }

    ALOGV("%s: src id %u, event id %u, dst_id %u",
            __func__, src_id, event_id, dst_id);

    ei.src_id       = src_id;
    ei.event_id     = event_id;
    ei.dst_id       = dst_id;

    err = ioctl(fileno(odsp_hw_hdl->dev_node),
            ODSP_EVENT_UNSUBSCRIBE, (unsigned long) &ei);
    if (err < 0) {
        ALOGE("%s: ERROR: Failed with error %s", __func__, strerror(errno));
    }

func_exit:
    FUNCTION_EXIT_LOG;
    return err;
}

/**
 * Retrieve an event
 *
 * Input  - odsp_hw_hdl - Handle to odsp hw structure
 *          event_info  - Struct to return event info
 *
 * Output - 0 on success, on failure < 0
 */
int iaxxx_odsp_evt_getevent( struct iaxxx_odsp_hw *odsp_hw_hdl,
                             struct iaxxx_get_event_info *event_info)
{
    int err = 0;
    struct iaxxx_get_event ei;

    FUNCTION_ENTRY_LOG;

    if (NULL == odsp_hw_hdl) {
        ALOGE("%s: ERROR: Invalid handle to iaxxx_odsp_hw", __func__);
        err = -1;
        goto func_exit;
    }

    err = ioctl(fileno(odsp_hw_hdl->dev_node),
            ODSP_GET_EVENT, (unsigned long) &ei);
    if (err < 0) {
        ALOGE("%s: ERROR: Failed with error %s", __func__, strerror(errno));
    }

    ALOGV("%s: event id %u, data %u",
            __func__, ei.event_id, ei.data);
    event_info->event_id = ei.event_id;
    event_info->data     = ei.data;

func_exit:
    FUNCTION_EXIT_LOG;
    return err;
}

/**
 * Retrieve fw state
 *
 * Input  - odsp_hw_hdl - Handle to odsp hw structure
 *          fw_state    - unsigned int to return event info
 *
 * Output - 0 on success, on failure < 0
 */
int iaxxx_odsp_get_fwstate( struct iaxxx_odsp_hw *odsp_hw_hdl,
                             unsigned int *fw_state) {
    int err = 0;

    FUNCTION_ENTRY_LOG;

    if (NULL == odsp_hw_hdl) {
        ALOGE("%s: ERROR: Invalid handle to iaxxx_odsp_hw", __func__);
        err = -1;
        goto func_exit;
    }

    err = ioctl(fileno(odsp_hw_hdl->dev_node),
            ODSP_GET_FW_STATE, (unsigned int) fw_state);
    if (err < 0) {
        ALOGE("%s: ERROR: Failed with error %s", __func__, strerror(errno));
    }
func_exit:
    FUNCTION_EXIT_LOG;
    return err;
}

/**
 * Set channel gain
 *
 * Input  - odsp_hw_hdl - Handle to odsp hw structure
 *          channel_id  - rx (0 to 7) and tx (8 to 15)
 *          target_gain - singed int (-128 to 127)
 *          gain_ramp   - unsigned short (0 to 65536)
 *          block_id    - Block ID
 *
 * Output - 0 on success, on failure < 0
 */
int iaxxx_odsp_set_gain( struct iaxxx_odsp_hw *odsp_hw_hdl,
                          const uint8_t channel_id, const int8_t target_gain,
                          const uint16_t gain_ramp, const uint8_t block_id) {
    int err = 0;
    struct iaxxx_set_chan_gain scg;

    FUNCTION_ENTRY_LOG;

    if (NULL == odsp_hw_hdl) {
        ALOGE("%s: ERROR: Invalid handle to iaxxx_odsp_hw", __func__);
        err = -1;
        goto func_exit;
    }

    scg.id = channel_id;
    scg.target_gain = target_gain;
    scg.gain_ramp = gain_ramp;
    scg.block_id = block_id;

    ALOGV("%s: ch id %u, target gain %u, gain ramp %u block id %u",
            __func__, channel_id, target_gain, gain_ramp, block_id);

    err = ioctl(fileno(odsp_hw_hdl->dev_node),
            ODSP_SET_CHAN_GAIN, (unsigned long) &scg);

    if (err < 0) {
        ALOGE("%s: ERROR: Failed with error %s", __func__, strerror(errno));
    }
func_exit:
    FUNCTION_EXIT_LOG;
    return err;
}

/**
 * Trigger an event. This may be most useful when debugging the system,
 * but can also be used to trigger simultaneous behavior in entities which
 * have subscribed, or to simply provide notifications regarding host status:
 *
 * Input  - odsp_hw_hdl - Handle to odsp hw structure
 *          src_id      - SystemId of event source
 *          evt_id      - Id of event
 *          src_opaque  - Source opaque to pass with event notification
 *
 * Output - 0 on success, on failure < 0
 */
int iaxxx_odsp_evt_trigger(struct iaxxx_odsp_hw *odsp_hw_hdl,
                            uint16_t src_id,
                            uint16_t evt_id,
                            uint32_t src_opaque)
{
    int err = 0;
    struct iaxxx_evt_trigger et;

    FUNCTION_ENTRY_LOG;

    if (NULL == odsp_hw_hdl) {
        ALOGE("%s: ERROR: Invalid handle to iaxxx_odsp_hw", __func__);
        err = -1;
        goto func_exit;
    }

    ALOGV("%s: src_id=%u, evt_id=%u, src_opaque=%u", __func__,
                                                src_id, evt_id, src_opaque);

    et.src_id = src_id;
    et.evt_id = evt_id;
    et.src_opaque = src_opaque;
    err = ioctl(fileno(odsp_hw_hdl->dev_node), ODSP_EVENT_TRIGGER,
                                                        (unsigned long)&et);
    if (-1 == err) {
        ALOGE("%s: ERROR: Failed with error %s", __func__, strerror(errno));
    }

func_exit:
    FUNCTION_EXIT_LOG;
    return err;
}

/**
 * Trigger an script
 * This function is used to trigger particular script using
 * script Id.
 *
 * Input  - odsp_hw_hdl - Handle to odsp hw structure
 *          script_id   - unsigned int 16 to pass script id
 *
 * Output - 0 on success, on failure < 0
 */
int iaxxx_odsp_script_trigger( struct iaxxx_odsp_hw *odsp_hw_hdl,
                               uint16_t script_id) {
    int err = 0;

    FUNCTION_ENTRY_LOG

    if (NULL == odsp_hw_hdl) {
        ALOGE("%s: ERROR: Invalid handle to iaxxx_odsp_hw", __func__);
        err = -EINVAL;
        goto func_exit;
    }

    err = ioctl(fileno(odsp_hw_hdl->dev_node), ODSP_SCRIPT_TRIGGER,
                                                                   &script_id);
    if (err < 0) {
        ALOGE("%s: ERROR: Failed with error %s", __func__, strerror(errno));
}

func_exit:
    FUNCTION_EXIT_LOG

    return err;
}
