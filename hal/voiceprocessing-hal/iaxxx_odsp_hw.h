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

#ifndef _IAXXX_ODSP_HW_H_
#define _IAXXX_ODSP_HW_H_

#if __cplusplus
extern "C"
{
#endif

struct iaxxx_odsp_hw;

struct iaxxx_config_file {
    const char *filename;
};

struct iaxxx_config_value {
    uint64_t config_val;
    uint32_t config_val_sz;
};

union iaxxx_config_data {
    struct iaxxx_config_file fdata;
    struct iaxxx_config_value vdata;
};

enum iaxxx_config_type {
    CONFIG_FILE,
    CONFIG_VALUE
};

struct iaxxx_create_config_data {
    enum iaxxx_config_type type;
    union iaxxx_config_data data;
};

struct iaxxx_get_event_info {
  uint16_t event_id;
  uint32_t data;
};

/**
 * Initialize the ODSP HAL
 *
 * Input  - NA
 * Output - Handle to iaxxx_odsp_hw on success, NULL on failure
 */
struct iaxxx_odsp_hw* iaxxx_odsp_init();

/**
 * De-Initialize the ODSP HAL
 *
 * Input  - odsp_hw_hdl - Handle to odsp hw structure
 * Output - 0 on success, on failure < 0
 */
int iaxxx_odsp_deinit(struct iaxxx_odsp_hw *odsp_hw_hdl);

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
                            const char *pkg_name, const uint32_t pkg_id);

/**
 * Unload a package
 *
 * Input  - odsp_hw_hdl - Handle to odsp hw structure
 *          pkg_id      - Package ID
 * Output - 0 on success, on failure < 0
 */
int iaxxx_odsp_package_unload(struct iaxxx_odsp_hw *odsp_hw_hdl,
                              const uint32_t pkg_id);

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
                             const uint32_t priority);

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
                                          struct iaxxx_create_config_data cdata);

/**
 * Destroy the plugin
 *
 * Input  - odsp_hw_hdl - Handle to odsp hw structure
 *          block_id    - Block ID
 *          inst_id     - Instance ID
 * Output - 0 on success, on failure < 0
 */
int iaxxx_odsp_plugin_destroy(struct iaxxx_odsp_hw *odsp_hw_hdl,
                              const uint32_t block_id, const uint32_t inst_id);

/**
 * Enable the plugin
 *
 * Input  - odsp_hw_hdl - Handle to odsp hw structure
 *          block_id    - Block ID
 *          inst_id     - Instance ID
 * Output - 0 on success, on failure < 0
 */
int iaxxx_odsp_plugin_enable(struct iaxxx_odsp_hw *odsp_hw_hdl,
                             const uint32_t block_id, const uint32_t inst_id);

/**
 * Disable the plugin
 *
 * Input  - odsp_hw_hdl - Handle to odsp hw structure
 *          block_id    - Block ID
 *          inst_id     - Instance ID
 * Output - 0 on success, on failure < 0
 */
int iaxxx_odsp_plugin_disable(struct iaxxx_odsp_hw *odsp_hw_hdl,
                              const uint32_t block_id, const uint32_t inst_id);

/**
 * Reset the plugin
 *
 * Input  - odsp_hw_hdl - Handle to odsp hw structure
 *          block_id    - Block ID
 *          inst_id     - Instance ID
 * Output - 0 on success, on failure < 0
 */
int iaxxx_odsp_plugin_reset(struct iaxxx_odsp_hw *odsp_hw_hdl,
                            const uint32_t block_id, const uint32_t inst_id);

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
                                    const uint32_t param_val);

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
                                    uint32_t *param_val);

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
                                        const uint32_t id);


/**
 * Set a parameter block on a plugin
 *
 * Input  - odsp_hw_hdl  - Handle to odsp hw structure
 *          inst_id      - Instance ID
 *          param_blk_id - Parameter block id
 *          block_id     - Block ID
 *          file_name    - Relative path to the Parameter File(Should be placed in
 *                          the firmware location)
 * Output - 0 on success, on failure < 0
 */
int iaxxx_odsp_plugin_set_parameter_blk_from_file(
                                        struct iaxxx_odsp_hw *odsp_hw_hdl,
                                        const uint32_t inst_id,
                                        const uint32_t param_blk_id,
                                        const uint32_t block_id,
                                        const char *file_name);

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
                              const uint32_t block_id);

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
                                const uint32_t dst_opaque);

/**
 * Un-Subscribe to an event
 *
 * Input  - odsp_hw_hdl - Handle to odsp hw structure
 *          src_id      - System Id of event source
 *          event_id    - Event Id
 *          dst_id      - System Id of event destination
 *
 * Output - 0 on success, on failure < 0
 */
int iaxxx_odsp_evt_unsubscribe(struct iaxxx_odsp_hw *odsp_hw_hdl,
                                const uint16_t src_id,
                                const uint16_t event_id,
                                const uint16_t dst_id);

/**
 * Retrieve an event
 *
 * Input  - odsp_hw_hdl - Handle to odsp hw structure
 *          event_info  - Struct to return event info
 *
 * Output - 0 on success, on failure < 0
 */
int iaxxx_odsp_evt_getevent( struct iaxxx_odsp_hw *odsp_hw_hdl,
                             struct iaxxx_get_event_info *event_info);

/**
 * Retrieve fw state
 *
 * Input  - odsp_hw_hdl - Handle to odsp hw structure
 *          fw_state    - unsigned int to return event info
 *
 * Output - 0 on success, on failure < 0
 */
int iaxxx_odsp_get_fwstate( struct iaxxx_odsp_hw *odsp_hw_hdl,
                             unsigned int *fw_state);

/**
 * Set channel gain
 *
 * Input  - odsp_hw_hdl - Handle to odsp hw structure
 *          channel_id  - rx (0 to 7) tx (8 to 15)
 *          target_gain - singed int (-128 to 127)
 *          gain_ramp   - unsigned short (0 to 65536)
 *          block_id    - Block ID
 *
 * Output - 0 on success, on failure < 0
 */
int iaxxx_odsp_set_gain( struct iaxxx_odsp_hw *odsp_hw_hdl,
                          const uint8_t channel_id, const int8_t target_gain,
                          const uint16_t gain_ramp, const uint8_t block_id);

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
                            uint32_t src_opaque);
/**
 * Trigger an script.
 * This is used to trigger particular script using script id.
 *
 * Input  - odsp_hw_hdl - Handle to odsp hw structure
 *          script_id   - unsigned int 16 to pass script id
 *
 * Output - 0 on success, on failure < 0
 */
int iaxxx_odsp_script_trigger( struct iaxxx_odsp_hw *odsp_hw_hdl,
                               uint16_t script_id);


#if __cplusplus
} // extern "C"
#endif

#endif // #ifndef _IAXXX_ODSP_HW_H_
