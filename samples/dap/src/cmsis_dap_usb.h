#include <zephyr/kernel.h>
#include <zephyr/device.h>

/**
 * @brief Initialize the CMSIS-DAP probe including USB setup.
 * 
 * @param swd_dev underlying SWD device
 * @return int 0 on success, negative number on error
 */
int cmsis_dap_usb_init(const struct device *swd_dev);

/**
 * @brief Process DAP commands.
 * 
 * This needs to be called continuously, but is exported so you can choose where.
 * 
 * @param timeout 
 */
void cmsis_dap_usb_process(k_timeout_t timeout);
