#!/usr/bin/env python3

import sys
import pyocd.probe.pydapaccess.interface.pyusb_v2_backend as pyusb_v2_backend

if __name__ == "__main__":
    probes = [
        x
        for x in pyusb_v2_backend.PyUSBv2.get_all_connected_interfaces()
        if x.product_name == "Thingy:91 X UART"
    ]
    if len(probes) == 0:
        print("no devices found")
        sys.exit(1)
        
    probe = probes[0]
    print(f"trying to put {probe.serial_number} in bootloader mode...")
    probe.open()
    probe.write([0x80 + 14]) # trigger vendor command 14 - BOOTLOADER
