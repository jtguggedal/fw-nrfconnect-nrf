#include <nrfx_twis.h>
#include <nrfx_twim.h>

#include <zephyr/kernel.h>
#include <zephyr/pm/device.h>
#include <zephyr/irq.h>
#include <zephyr/sys/util.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/gpio.h>

#include <drivers/virtual_eeprom.h>

LOG_MODULE_REGISTER(virtual_eeprom, CONFIG_I2C_VIRTUAL_EEPROM_DRIVER_LOG_LEVEL);

#define DT_DRV_COMPAT nordic_i2c_virtual_eeprom

#define GPIO_PIN_MAP(port, pin) (port * 32 + pin)

enum virtual_eeprom_state {
        STATE_IDLE,
        STATE_OPERATION,
};

struct virtual_eeprom_dev_cfg {
        nrfx_twis_t twis;
        void (*irq_connect)(void);
        void (*twis_handler)(nrfx_twis_evt_t const *p_event);
        struct gpio_dt_spec sda_gpio;
        uint32_t sda_gpio_port; 
	struct gpio_dt_spec scl_gpio;
        uint32_t scl_gpio_port;
        uint32_t address;
        size_t buf_data_size;
        size_t buf_scratch_size;
};

struct virtual_eeprom_dev_data {
        enum virtual_eeprom_state state;
        struct k_mutex lock;
        virtual_eeprom_evt_handler_t evt_handler;
        uint16_t address;
        uint8_t* buf_scratch;
        uint8_t* buf_data;
};

int virtual_eeprom_write(const struct device *dev, uint16_t address, uint8_t *src, uint16_t len) {

        if (dev == NULL || src == NULL) {
                LOG_ERR("Null pointer provided");
                return -EINVAL;
        }

        const struct virtual_eeprom_dev_cfg *cfg = dev->config;
        struct virtual_eeprom_dev_data *data = dev->data;

        if (address + len >= cfg->buf_data_size) {
                LOG_ERR("Data out of bounds");
                return -EFAULT;
        }

        k_mutex_lock(&data->lock, K_FOREVER);
        memcpy(&data->buf_data[address], src, len);
        k_mutex_unlock(&data->lock);

        LOG_DBG("Internally written %x bytes to %x", src[0], address);

        return 0;
}

int virtual_eeprom_read(const struct device *dev, uint16_t address, uint8_t *dest, uint16_t len) {

        if (dev == NULL || dest == NULL) {
                LOG_ERR("Null pointer provided");
                return -EINVAL;
        }

        const struct virtual_eeprom_dev_cfg *cfg = dev->config;
        struct virtual_eeprom_dev_data *data = dev->data;

        if (address + len >= cfg->buf_data_size) {
                LOG_ERR("Data out of bounds");
                return -EFAULT;
        }

        k_mutex_lock(&data->lock, K_FOREVER);
        memcpy(dest, &data->buf_data[address], len);
        k_mutex_unlock(&data->lock);


        return 0;
}

void virtual_eeprom_set_evt_handler(const struct device *dev, 
                                    virtual_eeprom_evt_handler_t handler) {
        struct virtual_eeprom_dev_data *data = dev->data;
        data->evt_handler = handler;
}

static void twis_handler(const struct device *dev, nrfx_twis_evt_t const *p_event) {
        nrfx_err_t status;
        (void)status;

        const struct virtual_eeprom_dev_cfg *cfg = dev->config;
        struct virtual_eeprom_dev_data *data = dev->data;

        switch (p_event->type)
        {
        case NRFX_TWIS_EVT_WRITE_DONE:
                size_t write_len = p_event->data.rx_amount;
                size_t addr_len = sizeof(data->address);

                // First 2 bytes written is always the address
                uint16_t new_address = *((uint16_t*)data->buf_scratch);

                if (new_address >= cfg->buf_data_size) {
                        LOG_ERR("Address %x exceeds size %x", new_address, cfg->buf_data_size);
                        break;
                }

                data->address = new_address;
                LOG_DBG("Set address to %x", data->address);

                // Anything else is supposed to be written to that address
                if (write_len > addr_len) {

                        // Determine maximum copy length and drop remaining bytes
                        size_t data_len = MIN(write_len - addr_len, 
                                              cfg->buf_data_size - data->address);

                        k_mutex_lock(&data->lock, K_FOREVER);
                        memcpy(&data->buf_data[data->address], 
                               &data->buf_scratch[addr_len], 
                               data_len);
                        k_mutex_unlock(&data->lock);

                        LOG_DBG("Written %d bytes @ %x", data_len, data->address);

                        // Call external event handler if present
                        if (data->evt_handler != NULL) {
                                data->evt_handler(VIRTUAL_EEPROM_WRITE, data->address, data_len);
                        }

                        data->address += data_len; 
                }
                break;

        case NRFX_TWIS_EVT_READ_DONE:
                LOG_DBG("Read %d bytes @ %x", p_event->data.tx_amount, data->address);

                // Call external event handler if present
                if (data->evt_handler != NULL) {
                        data->evt_handler(VIRTUAL_EEPROM_READ, 
                                          data->address, 
                                          p_event->data.tx_amount);
                }

                data->address += p_event->data.tx_amount;
                data->state = STATE_IDLE;
                break;

        case NRFX_TWIS_EVT_WRITE_REQ:
                status = nrfx_twis_rx_prepare(&cfg->twis, 
                                              data->buf_scratch, 
                                              cfg->buf_scratch_size);
                break;

        case NRFX_TWIS_EVT_READ_REQ:
                size_t max_read_len = cfg->buf_scratch_size - data->address;

                k_mutex_lock(&data->lock, K_FOREVER);
                memcpy(data->buf_scratch, &data->buf_data[data->address], max_read_len);
                k_mutex_unlock(&data->lock);

                status = nrfx_twis_tx_prepare(&cfg->twis, data->buf_scratch, max_read_len);
                break;
        case NRFX_TWIS_EVT_READ_ERROR:
                LOG_ERR("Read error");
                break;
        case NRFX_TWIS_EVT_WRITE_ERROR:
                LOG_ERR("Write error");
                break;
        case NRFX_TWIS_EVT_GENERAL_ERROR:
                LOG_ERR("General error");
                break;
        default:
                LOG_DBG("Event: %d.", p_event->type);
        }
}

static int virtual_eeprom_init(const struct device *dev) {
        int err;
        LOG_DBG("Initializing eeprom slave. Device %p", dev);

	const struct virtual_eeprom_dev_cfg *cfg = dev->config;
	struct virtual_eeprom_dev_data *data = dev->data;

        cfg->irq_connect();

        k_mutex_init(&data->lock);

        nrfx_twis_config_t twis_config = {
                .addr = { cfg->address },
                .scl_pin = GPIO_PIN_MAP(cfg->scl_gpio_port, cfg->scl_gpio.pin),
                .sda_pin = GPIO_PIN_MAP(cfg->sda_gpio_port, cfg->sda_gpio.pin),
                .skip_gpio_cfg = false,
                .skip_psel_cfg = false
        };

        err = nrfx_twis_init(&cfg->twis, &twis_config, cfg->twis_handler);
        if (err != NRFX_SUCCESS) {
                LOG_ERR("Error initializing TWIS: %d", err);
                return -1;
        }
        nrfx_twis_enable(&cfg->twis);
        return 0;
}

#define EEPROM_I2C_BUS_IDX(n)                                                   \
        DT_INST_PROP(n, i2c_bus_idx)

#define EEPROM_I2C_NODE(n) \
        DT_NODELABEL(i2c2)

#define EEPROM_GPIOS_GET_PORT(n, gpios)						\
	DT_PROP_OR(DT_PHANDLE(DT_DRV_INST(n), gpios), port, 0)

#define EEPROM_DEVICE_DEFINE(n)                                                 \
        static void irq_connect_##n(void) {                                     \
                IRQ_DIRECT_CONNECT(DT_IRQN(EEPROM_I2C_NODE(n)),                 \
                        DT_IRQ(EEPROM_I2C_NODE(n), priority),                   \
                        NRFX_TWIS_INST_HANDLER_GET(EEPROM_I2C_BUS_IDX(n)),      \
                        0);                                                     \
        }                                                                       \
                                                                                \
        static void twis_handler_forward_##n(nrfx_twis_evt_t const *p_event) {  \
                twis_handler(DEVICE_DT_INST_GET(n), p_event);                   \
        }                                                                       \
                                                                                \
        struct virtual_eeprom_dev_cfg virtual_eeprom_cfg_##n = {                \
                .twis = NRFX_TWIS_INSTANCE(EEPROM_I2C_BUS_IDX(n)),              \
                .irq_connect = irq_connect_##n,                                 \
                .twis_handler = twis_handler_forward_##n,                       \
                .sda_gpio = GPIO_DT_SPEC_INST_GET(n, sda_gpios),                \
                .sda_gpio_port = EEPROM_GPIOS_GET_PORT(n, sda_gpios),           \
                .scl_gpio = GPIO_DT_SPEC_INST_GET(n, scl_gpios),                \
                .scl_gpio_port = EEPROM_GPIOS_GET_PORT(n, scl_gpios),           \
                .address = DT_INST_PROP(n, address),                            \
                .buf_data_size = DT_INST_PROP(n, size),                         \
                .buf_scratch_size = DT_INST_PROP(n, size)                       \
        };                                                                      \
                                                                                \
        uint8_t virtual_eeprom_##n##_buf_data[DT_INST_PROP(n, size)];           \
        uint8_t virtual_eeprom_##n##_buf_scratch[DT_INST_PROP(n, size)];        \
                                                                                \
        struct virtual_eeprom_dev_data virtual_eeprom_data_##n = {              \
                .buf_data = virtual_eeprom_##n##_buf_data,                      \
                .buf_scratch = virtual_eeprom_##n##_buf_scratch,                \
                .state = STATE_IDLE,                                            \
                .address = 0x0,                                                 \
                .evt_handler = NULL                                             \
        };                                                                      \
                                                                                \
        DEVICE_DT_INST_DEFINE(n, virtual_eeprom_init, NULL,			\
                        &virtual_eeprom_data_##n, &virtual_eeprom_cfg_##n,	\
                        POST_KERNEL, 80, NULL);                                 \


DT_INST_FOREACH_STATUS_OKAY(EEPROM_DEVICE_DEFINE)