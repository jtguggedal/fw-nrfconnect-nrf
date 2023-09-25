#include <zephyr/device.h>
#include <drivers/virtual_eeprom.h>
#include <conn_bridge_control/control_host.h>

static const struct device *eeprom;

void conn_bridge_control_host_init(const struct device *dev) {
        eeprom = dev;
}

void conn_bridge_control_set_usb_attached(bool attached) {
        virtual_eeprom_write(eeprom, 0x0, &attached, 1);
}