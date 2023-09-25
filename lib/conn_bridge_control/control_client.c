#include <zephyr/device.h>
#include <zephyr/drivers/eeprom.h>
#include <conn_bridge_control/control_client.h>

static const struct device *eeprom;

void conn_bridge_control_client_init(const struct device *dev) {
        eeprom = dev;
}

int conn_bridge_control_get_usb_attached(bool *attached) {
        int err;
        char data;
	err = eeprom_read(eeprom, 0x0, &data, 1);
        *attached = data;
        return err;
}