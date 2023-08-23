
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/usb/usb_device.h>
#include <zephyr/usb/bos.h>
#include <zephyr/net/buf.h>
#include <usb_descriptor.h>
#include <cmsis_dap.h>
#include <zephyr/usb/msos_desc.h>

#include "cmsis_dap_usb.h"

uint8_t rx_buf[CONFIG_CMSIS_DAP_PACKET_SIZE];
uint8_t tx_buf[CONFIG_CMSIS_DAP_PACKET_SIZE];

NET_BUF_POOL_FIXED_DEFINE(ep_out_pool, CONFIG_CMSIS_DAP_PACKET_COUNT,
			  CONFIG_CMSIS_DAP_PACKET_SIZE, 0, NULL);

K_FIFO_DEFINE(cmsis_dap_input_queue);

#define WEBUSB_VENDOR_CODE     0x21
#define WINUSB_VENDOR_CODE     0x20

#define BULK_INTF_STRDESC		"CMSIS-DAP v2"

#define LOG_LEVEL CONFIG_DAP_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(cmsis_dap);

/* {CDB3B5AD-293B-4663-AA36-1AAE46463776} */
#define CMSIS_DAP_V2_DEVICE_INTERFACE_GUID \
	'{', 0x00, 'C', 0x00, 'D', 0x00, 'B', 0x00, '3', 0x00, 'B', 0x00, \
	'5', 0x00, 'A', 0x00, 'D', 0x00, '-', 0x00, '2', 0x00, '9', 0x00, \
	'3', 0x00, 'B', 0x00, '-', 0x00, '4', 0x00, '6', 0x00, '6', 0x00, \
	'3', 0x00, '-', 0x00, 'A', 0x00, 'A', 0x00, '3', 0x00, '6', 0x00, \
	'-', 0x00, '1', 0x00, 'A', 0x00, 'A', 0x00, 'E', 0x00, '4', 0x00, \
	'6', 0x00, '4', 0x00, '6', 0x00, '3', 0x00, '7', 0x00, '7', 0x00, \
	'6', 0x00, '}', 0x00, 0x00, 0x00, 0x00, 0x00

#define COMPATIBLE_ID_WINUSB \
	'W', 'I', 'N', 'U', 'S', 'B', 0x00, 0x00

static struct msosv2_descriptor_t {
	struct msosv2_descriptor_set_header header;
#if defined(CONFIG_USB_COMPOSITE_DEVICE)
	struct msosv2_function_subset_header cmsis_dap_v2_subset_header;
#endif
	struct msosv2_compatible_id cmsis_dap_v2_compatible_id;
	struct msosv2_guids_property cmsis_dap_v2_guids_property;
} __packed msosv2_descriptor = {
	/* Microsoft OS 2.0 descriptor set
	 * This tells Windows what kind of device this is and to install the WinUSB driver.
	 */
	.header = {
		.wLength = sizeof(struct msosv2_descriptor_set_header),
		.wDescriptorType = MS_OS_20_SET_HEADER_DESCRIPTOR,
		.dwWindowsVersion = 0x06030000,
		.wTotalLength = sizeof(struct msosv2_descriptor_t),
	},
#if defined(CONFIG_USB_COMPOSITE_DEVICE)
	.cmsis_dap_v2_subset_header = {
		.wLength = sizeof(struct msosv2_function_subset_header),
		.wDescriptorType = MS_OS_20_SUBSET_HEADER_FUNCTION,
		.wSubsetLength = sizeof(struct msosv2_function_subset_header)
			+ sizeof(struct msosv2_compatible_id)
			+ sizeof(struct msosv2_guids_property),
	},
#endif
	.cmsis_dap_v2_compatible_id = {
		.wLength = sizeof(struct msosv2_compatible_id),
		.wDescriptorType = MS_OS_20_FEATURE_COMPATIBLE_ID,
		.CompatibleID = {COMPATIBLE_ID_WINUSB},
	},
	.cmsis_dap_v2_guids_property = {
		.wLength = sizeof(struct msosv2_guids_property),
		.wDescriptorType = MS_OS_20_FEATURE_REG_PROPERTY,
		.wPropertyDataType = MS_OS_20_PROPERTY_DATA_REG_MULTI_SZ,
		.wPropertyNameLength = 42,
		.PropertyName = {DEVICE_INTERFACE_GUIDS_PROPERTY_NAME},
		.wPropertyDataLength = 80,
		.bPropertyData = {CMSIS_DAP_V2_DEVICE_INTERFACE_GUID},
	},
};

USB_DEVICE_BOS_DESC_DEFINE_CAP struct usb_bos_msosv2_desc {
	struct usb_bos_platform_descriptor platform;
	struct usb_bos_capability_msos cap;
} __packed bos_cap_msosv2 = {
	/* Microsoft OS 2.0 Platform Capability Descriptor
	 * See https://docs.microsoft.com/en-us/windows-hardware/drivers/usbcon/
	 * microsoft-defined-usb-descriptors
	 * Adapted from the source:
	 * https://github.com/sowbug/weblight/blob/master/firmware/webusb.c
	 * (BSD-2) Thanks http://janaxelson.com/files/ms_os_20_descriptors.c
	 */
	.platform = {
		.bLength = sizeof(struct usb_bos_platform_descriptor)
			+ sizeof(struct usb_bos_capability_msos),
		.bDescriptorType = USB_DESC_DEVICE_CAPABILITY,
		.bDevCapabilityType = USB_BOS_CAPABILITY_PLATFORM,
		.bReserved = 0,
		.PlatformCapabilityUUID = {
			/**
			 * MS OS 2.0 Platform Capability ID
			 * D8DD60DF-4589-4CC7-9CD2-659D9E648A9F
			 */
			0xDF, 0x60, 0xDD, 0xD8,
			0x89, 0x45,
			0xC7, 0x4C,
			0x9C, 0xD2,
			0x65, 0x9D, 0x9E, 0x64, 0x8A, 0x9F,
		},
	},
	.cap = {
		/* Windows version (8.1) (0x06030000) */
		.dwWindowsVersion = sys_cpu_to_le32(0x06030000),
		.wMSOSDescriptorSetTotalLength =
			sys_cpu_to_le16(sizeof(msosv2_descriptor)),
		.bMS_VendorCode = WINUSB_VENDOR_CODE,
		.bAltEnumCode = 0x00
	},
};

USB_DEVICE_BOS_DESC_DEFINE_CAP struct usb_bos_webusb_desc {
	struct usb_bos_platform_descriptor platform;
	struct usb_bos_capability_webusb cap;
} __packed bos_cap_webusb = {
	/* WebUSB Platform Capability Descriptor:
	 * https://wicg.github.io/webusb/#webusb-platform-capability-descriptor
	 */
	.platform = {
		.bLength = sizeof(struct usb_bos_platform_descriptor)
			+ sizeof(struct usb_bos_capability_webusb),
		.bDescriptorType = USB_DESC_DEVICE_CAPABILITY,
		.bDevCapabilityType = USB_BOS_CAPABILITY_PLATFORM,
		.bReserved = 0,
		/* WebUSB Platform Capability UUID
		 * 3408b638-09a9-47a0-8bfd-a0768815b665
		 */
		.PlatformCapabilityUUID = {
			0x38, 0xB6, 0x08, 0x34,
			0xA9, 0x09,
			0xA0, 0x47,
			0x8B, 0xFD,
			0xA0, 0x76, 0x88, 0x15, 0xB6, 0x65,
		},
	},
	.cap = {
		.bcdVersion = sys_cpu_to_le16(0x0100),
		.bVendorCode = WEBUSB_VENDOR_CODE,
		.iLandingPage = 0x01
	}
};

/* URL Descriptor: https://wicg.github.io/webusb/#url-descriptor */
static const uint8_t webusb_origin_url[] = {
	/* Length, DescriptorType, Scheme */
	0x0F, 0x03, 0x01,
	'o', 's', '.', 'm', 'b', 'e', 'd', '.', 'c', 'o', 'm', '/',
};


/**
 * @brief Handler called for vendor specific commands. This includes
 *        WebUSB GET_URL and MS OS 2.0 descriptors.
 *
 * @param pSetup    Information about the request to execute.
 * @param len       Size of the buffer.
 * @param data      Buffer containing the request result.
 *
 * @return  0 on success, negative errno code on fail.
 */
static int msosv2_vendor_handle_req(struct usb_setup_packet *pSetup, int32_t *len, uint8_t **data)
{
	if (usb_reqtype_is_to_device(pSetup)) {
		return -ENOTSUP;
	}

	if (pSetup->bRequest == WEBUSB_VENDOR_CODE && pSetup->wIndex == 0x02) {
		/* WebUSB: Get URL request */
		*data = (uint8_t *)(&webusb_origin_url);
		*len = sizeof(webusb_origin_url);

		LOG_DBG("Get webusb_origin_url");

		return 0;
	} else if (pSetup->bRequest == WINUSB_VENDOR_CODE && pSetup->wIndex == 0x07) {
		/* Get MS OS 2.0 Descriptors request */
		/* 0x07 means "MS_OS_20_DESCRIPTOR_INDEX" */
		*data = (uint8_t *)(&msosv2_descriptor);
		*len = sizeof(msosv2_descriptor);

		LOG_DBG("Get MS OS Descriptors v2");

		return 0;
	}
	return -ENOTSUP;
}

static void bulk_read_cb(uint8_t ep, int size, void *priv)
{
	struct net_buf *buf;
	struct usb_cfg_data *cfg = priv;

	LOG_DBG("cfg %p ep %x size %u", cfg, ep, size);

	if (size <= 0) {
		goto done;
	}

	buf = net_buf_alloc(&ep_out_pool, K_FOREVER);
	net_buf_add_mem(buf, rx_buf, MIN(size, CONFIG_CMSIS_DAP_PACKET_SIZE));
	k_fifo_put(&cmsis_dap_input_queue, buf);
done:
	/* keep on reading */
	usb_transfer(ep, rx_buf, sizeof(rx_buf), USB_TRANS_READ, bulk_read_cb, cfg);
}

#define INITIALIZER_IF(num_ep, iface_class)				\
	{								\
		.bLength = sizeof(struct usb_if_descriptor),		\
		.bDescriptorType = USB_DESC_INTERFACE,			\
		.bInterfaceNumber = 0,					\
		.bAlternateSetting = 0,					\
		.bNumEndpoints = num_ep,				\
		.bInterfaceClass = iface_class,				\
		.bInterfaceSubClass = 0,				\
		.bInterfaceProtocol = 0,				\
		.iInterface = 0,					\
	}

#define INITIALIZER_IF_EP(addr, attr, mps, interval)			\
	{								\
		.bLength = sizeof(struct usb_ep_descriptor),		\
		.bDescriptorType = USB_DESC_ENDPOINT,			\
		.bEndpointAddress = addr,				\
		.bmAttributes = attr,					\
		.wMaxPacketSize = sys_cpu_to_le16(mps),			\
		.bInterval = interval,					\
	}

USBD_CLASS_DESCR_DEFINE(primary, 0) struct {
	struct usb_if_descriptor if0;
	struct usb_ep_descriptor if0_out_ep;
	struct usb_ep_descriptor if0_in_ep;
} __packed dapusb_desc = {
	.if0 = INITIALIZER_IF(2, USB_BCC_VENDOR),
	.if0_out_ep = INITIALIZER_IF_EP(AUTO_EP_OUT, USB_DC_EP_BULK,
					CONFIG_CMSIS_DAP_PACKET_SIZE, 0),
	.if0_in_ep = INITIALIZER_IF_EP(AUTO_EP_IN, USB_DC_EP_BULK,
				       CONFIG_CMSIS_DAP_PACKET_SIZE, 0),
};

/* Describe EndPoints configuration */
static struct usb_ep_cfg_data dapusb_ep_data[] = {
	{
		.ep_cb	= usb_transfer_ep_callback,
		.ep_addr = AUTO_EP_OUT
	},
	{
		.ep_cb = usb_transfer_ep_callback,
		.ep_addr = AUTO_EP_IN
	}
};

#define BULK_OUT_EP_IDX		0
#define BULK_IN_EP_IDX		1

static void dapusb_dev_status_cb(struct usb_cfg_data *cfg,
				 enum usb_dc_status_code status,
				 const uint8_t *param)
{
	ARG_UNUSED(param);

	if (status == USB_DC_CONFIGURED) {
		bulk_read_cb(cfg->endpoint[BULK_OUT_EP_IDX].ep_addr, 0, cfg);
	}
}

static void dapusb_interface_config(struct usb_desc_header *head,
				 uint8_t bInterfaceNumber)
{
	ARG_UNUSED(head);

	dapusb_desc.if0.bInterfaceNumber = bInterfaceNumber;
#if defined(CONFIG_USB_COMPOSITE_DEVICE)
	msosv2_descriptor.cmsis_dap_v2_subset_header.bFirstInterface = bInterfaceNumber;
#endif
}
USBD_DEFINE_CFG_DATA(dapusb_config) = {
	.usb_device_description = NULL,
	.interface_config = dapusb_interface_config,
	.interface_descriptor = &dapusb_desc.if0,
	.cb_usb_status = dapusb_dev_status_cb,
	.interface = {
		.class_handler = NULL,
		.custom_handler = NULL,
		.vendor_handler = msosv2_vendor_handle_req,
	},
	.num_endpoints = ARRAY_SIZE(dapusb_ep_data),
	.endpoint = dapusb_ep_data
};

struct bulk_iface_descriptor {
	uint8_t bLength;
	uint8_t bDescriptorType;
	uint8_t bString[USB_BSTRING_LENGTH(BULK_INTF_STRDESC)];
} __packed;

USBD_STRING_DESCR_USER_DEFINE(primary) struct bulk_iface_descriptor bulk_string_descr = {
	.bLength = USB_STRING_DESCRIPTOR_LENGTH(BULK_INTF_STRDESC),
	.bDescriptorType = USB_DESC_STRING,
	.bString = BULK_INTF_STRDESC
};

/**
 * @brief Set string descriptor index for bulk interface
 * 
 * @param bulk_cfg USB config struct of bulk interface
 */
static void bulk_str_descr_init(struct usb_cfg_data *bulk_cfg)
{
	int idx = usb_get_str_descriptor_idx(&bulk_string_descr);
	if (idx != 0) {
		struct usb_if_descriptor *bulk_if = bulk_cfg->interface_descriptor;
		bulk_if->iInterface = idx;
	}
}

int cmsis_dap_usb_init(const struct device *swd_dev)
{
	int ret;

	/* add MS OS 2.0 BOS descriptor to BOS structure */
	usb_bos_register_cap((void *)&bos_cap_msosv2);
	/* point interface index to string descriptor */
	bulk_str_descr_init(&dapusb_config);

	/* set up the underlaying DAP device that will process the commands */
	if (!device_is_ready(swd_dev)) {
		LOG_ERR("SWD device is not ready");
		return -ENODEV;
	}
	ret = dap_setup(swd_dev);
	if (ret) {
		LOG_ERR("Failed to initialize DAP controller (%d)", ret);
		return ret;
	}

	return 0;
}

void cmsis_dap_usb_process(k_timeout_t timeout)
{
	struct net_buf *buf;
	size_t len;
	int ret;

	buf = k_fifo_get(&cmsis_dap_input_queue, timeout);

	len = dap_execute_cmd(buf->data, tx_buf);
	LOG_DBG("response length %u, starting with [0x%02X, 0x%02X]",
		len, tx_buf[0], tx_buf[1]);
	net_buf_unref(buf);

send:
	ret = usb_transfer_sync(dapusb_config.endpoint[BULK_IN_EP_IDX].ep_addr, tx_buf, len,
		USB_TRANS_WRITE | USB_TRANS_NO_ZLP);
	if (ret == -EAGAIN || ret == -ENOMEM) {
		LOG_WRN("got EAGAIN, retransmitting");
		k_sleep(K_MSEC(1));
		goto send;
	} else if (ret != len) {
		LOG_ERR("usb_transfer_sync failed: %d", ret);
	}
}
