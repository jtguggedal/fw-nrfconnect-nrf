/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef _CONN_BRIDGE_CONTROL_COMMON_H_
#define _CONN_BRIDGE_CONTROL_COMMON_H_

/**
 * @file control_common.h
 *
 * @brief Common header for connectivity bridge control registers
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <zephyr/types.h>

struct conn_bridge_ctrl_reg_definition {
        uint16_t address;
};

#define DEFINE_CONN_BRIDGE_CTRL_REG(idx, name)                                                  \
struct conn_bridge_ctrl_reg_definition conn_bridge_ctrl_reg_definition_##name = {               \
        .address = address * sizeof(uint32_t),                                                  \
};                                                                                              \

#define GEN_CONN_BRIDGE_CTRL_HOST_GETSET(name)                                                  \
void conn_bridge_ctrl_set_##name(uint32_t input) {                                              \
        virtual_eeprom_write(eeprom, conn_bridge_ctrl_reg_definition_##name.address,            \
                             &input, sizeof(input));                                            \
}                                                                                               \
void conn_bridge_ctrl_get_##name(uint32_t *output) {                                            \
        virtual_eeprom_read(eeprom, conn_bridge_ctrl_reg_definition_##name.address,             \
                             &output, sizeof(output));                                          \
}                                                                                               \


/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* _CONN_BRIDGE_CONTROL_COMMON_H_ */
