.. _protectedmem_periphconf_sample:

.. ncs-sample::
   :title: Protected Memory with PERIPHCONF Partition

   This sample demonstrates how to protect the PERIPHCONF partition using UICR.PROTECTEDMEM.

Requirements
************

The sample supports the following development kits:

.. table-from-sample-yaml::

Overview
********

The sample relocates the ``periphconf_partition`` right after ``cpuapp_boot_partition`` and configures PROTECTEDMEM to cover both partitions.
When protected memory is modified, the integrity check fails on the next boot, causing |ISE| to boot the secondary firmware instead of the main application.

PROTECTEDMEM detects modification of the protected region, it does not prevent it.
Writes to the region from the application still succeed, and are only reported at the next boot.

Building and running
*********************

.. |sample path| replace:: :file:`samples/ironside_se/protectedmem_periphconf`

.. include:: /includes/build_and_run.txt

Testing
*******

After programming the sample to your development kit, complete the following steps to test it:

1. |connect_terminal|
#. Reset the development kit.

The application writes a test pattern to protected memory and then reboots.
On the next boot, if protection works correctly, the secondary firmware boots instead of the main application, indicating that the integrity check detected the modification.

The device keeps booting the secondary firmware after this, because |ISE| calculates the integrity check value once, on the first reset after the UICR fields are written.
Rewriting the protected region is not enough to clear the failure, and neither is ``west flash``, which only programs the addresses present in the image.
Use ``nrfutil device recover`` before reprogramming to return the device to the main application.

Configuration
*************

The sample uses the following key configurations:

Device Tree Overlay
  The overlay in the :file:`boards` directory relocates the ``periphconf_partition`` to be placed right after ``cpuapp_boot_partition``, and moves ``cpuapp_slot0_partition`` out of the way.
  The partition layout differs per SoC, so there is one overlay per board target: offset 0x40000 on the nRF54H20 DK and 0x322000 on the nRF9251 DK.
  On the nRF9251 DK the overlay also adds ``secondary_partition`` and ``secondary_periphconf_partition``, which that memory map does not yet define.
  The ``sysbuild.cmake`` file applies the same overlay to the UICR and secondary images, so every image in the build sees the same partition layout.

Kconfig Configuration
  The ``sysbuild/uicr.conf`` file configures the PROTECTEDMEM size to 72KB (73728 bytes) to cover both ``cpuapp_boot_partition`` (64KB) and ``periphconf_partition`` (8KB).
  The protected region always starts at the lowest application-owned MRAM address, so its size must reach the end of the last partition you want covered.
  A size that is too small protects only part of the region and is not reported as an error, so check it against the partition layout when you change either.

Secondary Firmware
  The sample includes a secondary firmware (in the ``secondary/`` directory) that boots automatically when the PROTECTEDMEM integrity check fails.
  The secondary firmware is enabled via ``CONFIG_GEN_UICR_SECONDARY=y`` in ``sysbuild/uicr.conf`` and is built as part of the sysbuild process (configured in ``sysbuild.cmake``).

  Shipping a secondary firmware is what makes a failed integrity check survivable, and it is a design decision to make deliberately.
  With one, |ISE| boots it instead of the main application, so the device stays alive and can report the fault.
  Without one, |ISE| leaves the application core halted, and recovery requires a debugger.
  Note also that a secondary boot does not identify the fault: |ISE| boots the secondary firmware on any boot failure, not only on a failed integrity check.

Dependencies
************

This sample uses the following |NCS| subsystems:

* UICR generation - Configures UICR.PROTECTEDMEM to protect the memory region and UICR.SECONDARY to enable secondary firmware boot
* Sysbuild - Enables building the UICR image and secondary firmware with the protected memory configuration

In addition, it uses the following Zephyr subsystems:

* :ref:`Kernel <kernel>` - Provides basic system functionality and threading
* :ref:`Console <console>` - Enables UART console output for debugging and user interaction
* Devicetree - Defines the partition layout
