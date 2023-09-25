/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef _VIRTUAL_EEPROM_H_
#define _VIRTUAL_EEPROM_H_

/**
 * @file virtual_eeprom.h
 *
 * @brief Public API for the virtual_eeprom driver.
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <zephyr/types.h>

enum virtual_eeprom_evt {
        VIRTUAL_EEPROM_READ,
        VIRTUAL_EEPROM_WRITE
};

typedef void(*virtual_eeprom_evt_handler_t)(enum virtual_eeprom_evt event, 
                                            uint16_t address, 
                                            uint16_t len);

/**
 * @brief Write to virtual EEPROM memory
 *
 * @param dev Device handle
 * @param address EEPROM memory address to write to
 * @param src Pointer to the data to write
 * @param len Length of the data to write
 * @return 0 on success, error code otherwise.
 */
int virtual_eeprom_write(const struct device *dev, uint16_t address, uint8_t *src, uint16_t len);

/**
 * @brief Read from virtual EEPROM memory
 *
 * @param dev   Device handler.
 * @param address EEPROM memory address to read from
 * @param data Pointer to the memory location to copy the data into
 * @param len Length of the data to read
 * @return 0 on success, error code otherwise
 */
int virtual_eeprom_read(const struct device *dev, uint16_t address, uint8_t *dest, uint16_t len);

/**
 * @brief Set event handler callback for external virtual EEPROM reads and writes
 * 
 * @param handler Pointer to the handler function
 */
void virtual_eeprom_set_evt_handler(const struct device *dev, 
                                    virtual_eeprom_evt_handler_t handler);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* _VIRTUAL_EEPROM_H_ */
