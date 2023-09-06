=====================
  Nordic Thingy:91 X  
=====================

Full Nordic Thingy:91 documentation can be found here:
https://nordicsemi.com/thingy91x

This USB interface has the following functions:
- Disk drive containing this file and others
- COM ports for nRF91 debug, trace, and firmware update
- CMSIS-DAP 2.0 compatible debug probe interface

COM Ports
====================
This USB interface exposes two COM ports mapped to the physical UART interfaces between the nRF91 and nRF5340 devices.
When opening these ports manually (without using LTE Link Monitor) be aware that the USB COM port baud rate selection is applied to the UART.

BLE UART Service
====================
This device will advertise as "Thingy:91 X UART".
Connect using a BLE Central device, for example a phone running the nRF Connect app:
https://www.nordicsemi.com/Software-and-tools/Development-Tools/nRF-Connect-for-mobile/

NOTE: The BLE interface is unencrypted and intended to be used during debugging.
      By default the BLE interface is disabled.
      Enable it by setting the appropriate option in Config.txt.
