.. _asset_tracker_v2:

nRF9160: Asset Tracker v2
#########################

.. contents::
   :local:
   :depth: 2

The Asset Tracker v2 is a real-time configurable ultra-low-power capable application firmware for the nRF9160 System in Package.
Its overall purpose is to sample sensor data and publish the data to a connected cloud service over TCP/IP over LTE.

Data types
**********
Data from multiple sensor sources are collected to provide information about the nRF9160s location, environment, and health.
The application supports the following data types.

.. table:: Sampled data
   :widths: auto
   :align: center

   +---------------+----------------------------+-----------------------------------------------+
   | **Data type** | **Description**            | **Identifiers**                               |
   +===============+============================+===============================================+
   | Location      | GNSS coordinates           | APP_DATA_GNSS                                 |
   +---------------+----------------------------+-----------------------------------------------+
   | Environmental | Temperature, humidity      | APP_DATA_ENVIRONMENTAL                        |
   +---------------+----------------------------+-----------------------------------------------+
   | Movement      | Acceleration               | APP_DATA_MOVEMENT                             |
   +---------------+----------------------------+-----------------------------------------------+
   | Modem         | LTE link data, device data | APP_DATA_MODEM_DYNAMIC, APP_DATA_MODEM_STATIC |
   +---------------+----------------------------+-----------------------------------------------+
   | Battery       | Voltage                    | APP_DATA_BATTERY                              |
   +---------------+----------------------------+-----------------------------------------------+

All sensor data published to the cloud service have relative timestamps that originate from the time of sampling. For more
information on timestamp handling in the application, see <link-to-timestamp-section-protocol>.

Multiple cloud services
***********************
The application supports communication with multiple cloud services, but only a single service at a time.
To configure the application to use a specific service, see section <link here>.
The following table lists the currently supported cloud vendor services and technologies supported in the cloud connection.

.. table:: Supported cloud services
   :widths: auto
   :align: center

   +---------------------+--------------+--------------+
   | Cloud vendor        | Service      | Technologies |
   +=====================+==============+==============+
   | Amazon Web Services | AWS IoT Core |  * MQTT      |
   |                     |              |  * TLS       |
   |                     |              |  * FOTA      |
   +---------------------+--------------+--------------+

Device confgurations
********************
The application can either be in an active or passive state depending on the applied device mode.
The device mode is a part of the application's real-time configurations listed in the following table.

.. table:: Real-time configurations
   :widths: auto

   +-------------------------------------+---------------------------------------------------------------------------------------------------------------------------------+----------------+
   | Configurations                      | Description                                                                                                                     | Default values |
   +=====================================+=================================================================================================================================+================+
   | Device mode                         | Either in *active* or *passive* mode.                                                                                           | Active         |
   +---------------+---------------------+---------------------------------------------------------------------------------------------------------------------------------+----------------+
   | *Active*      |                     | Sample and publish data at regular intervals.                                                                                   |                |
   |               +---------------------+---------------------------------------------------------------------------------------------------------------------------------+----------------+
   |               | Active wait time    | Amount of seconds between each sampling/publication.                                                                            | 120 seconds    |
   +---------------+---------------------+---------------------------------------------------------------------------------------------------------------------------------+----------------+
   | *Passive*     |                     | Sample and publish data only if movement has been detected.                                                                     |                |
   |               +---------------------+---------------------------------------------------------------------------------------------------------------------------------+----------------+
   |               | Movement resolution | After detecting movement sample and publish data and wait this amount of time until movement again can trigger the next update. | 120 seconds    |
   |               +---------------------+---------------------------------------------------------------------------------------------------------------------------------+----------------+
   |               | Movement timeout    | Sample/publish data at least this often. Not dependent on movement.                                                             | 3600 seconds   |
   +---------------+---------------------+---------------------------------------------------------------------------------------------------------------------------------+----------------+
   | GPS timeout                         | Timeout for acquiring a GPS fix during sampling of data.                                                                        | 60 seconds     |
   +-------------------------------------+---------------------------------------------------------------------------------------------------------------------------------+----------------+
   | Accelerometer threshold             | Accelerometer threshold in m/s²: Minimal absolute value in m/s² for the accelerometer readings to be considered movement.       | 10 m/s²        |
   +-------------------------------------+---------------------------------------------------------------------------------------------------------------------------------+----------------+

.. note::
   Utilized configurations depend on the application state. For instance, in *active* mode, neither *movement resolution* nor *movement timeout* is used.

The following flow charts show how the application acts in its active and passive states, illustrating the relationship between data sampling, publishing, and device configurations.
Non-essential configurations to this relationship are abstracted away for simplicity.

.. figure:: /images/asset_tracker_v2_active_state.svg
    :alt: Active state flow chart

    Active state flow chart

In the active state, the application samples and published data at regular intervals set by the *Active wait timeout* configuration.

.. figure:: /images/asset_tracker_v2_passive_state.svg
    :alt: Passive state flow chart

    Passive state flow chart

In the passive state, the application will only sample and publish upon movement. The reason behind this is to save data transferred over the air and "abundant" processing cycles.
Not covered by this flow chart is the timer that acts on the *Movement timeout* configuration.
This timer is enabled when the application enters the passive state, and when the timer expires, the application will initiate data sampling and publishing.
This timer ensures that given no movement, the application still sends sub-regular updates to the cloud service.
This timeout acts as a failsafe if,  in case, the asset wearing the tracker is not moving for a very long time. Ideally, the *Movement timeout* should be set much higher than the *Movement resolution*.

The device retrieves its real-time configurations from the cloud service in one of two ways.
Upon every established connection to the cloud service, the application will always request its cloud-side device state containing the latest real-time configurations.
When the device exits <linkPSM> to publish data and the cloud-side device configuration has been updated.
The application will always acknowledge newly applied device configurations back to the cloud service.

.. note::
   When the application gets a new configuration from the cloud service, it will always store it to flash.
   If an unexpected reboot occurs, with areas without LTE coverage, the application will have access to the latest applied configuration.

Data buffers
************
Data sampled from the onboard modem and external sensors get stored in ring-buffers. Newer data related to the last data sampling is always published first before older buffered data get published.

The application has LTE and cloud connection awareness.
Upon a disconnect from the cloud service, it will keep buffered sensor data and empty the buffers in batch messages when the application reconnects to the cloud service.

User Interface
**************
The application supports button one on the Thingy91 and button one and two on the nRF9160DK. Additionally, the application displays LED behavior that corresponds to what task the application is doing.
The following tables describe LED behavior and the purpose of each supported button.

.. table:: Buttons
   :widths: auto

   +--------+-----------------------------------+--------------------------------------------------------------------------------------------------------------+
   | Button | Thingy91                          | nRF9160DK                                                                                                    |
   +========+===================================+==============================================================================================================+
   | 1      | Send message to the cloud service | Send message to the cloud service                                                                            |
   +--------+-----------------------------------+--------------------------------------------------------------------------------------------------------------+
   | 2      | NA                                |  * Send message to the cloud service                                                                         |
   |        |                                   |  * Fake movement. The nRF9160DK does not have an external accelerometer to trigger movement in passive mode. |
   +--------+-----------------------------------+--------------------------------------------------------------------------------------------------------------+

.. table:: LED behaviour
   :widths: auto

   +----------------------------+-------------------------+----------------------+
   | State                      | RGB LED Thingy91        | Solid LEDs nRF9160DK |
   +============================+=========================+======================+
   | LTE connection search      | Yellow flashing         | LED1 flashing        |
   +----------------------------+-------------------------+----------------------+
   | GPS fix search             | Purple flashing         | LED2 flashing        |
   +----------------------------+-------------------------+----------------------+
   | Publishing data            | Green flashing          | LED3 flashing        |
   +----------------------------+-------------------------+----------------------+
   | Active mode                | Light blue flashing     | LED4 flashing        |
   +----------------------------+-------------------------+----------------------+
   | Passive mode               | Dark blue slow flashing | LED4 slow flashing   |
   |                            |                         |                      |
   |                            | (short on, long off)    | (short on, long off) |
   +----------------------------+-------------------------+----------------------+
   | Error                      | Red solid               | all 4 LEDs flashing  |
   +----------------------------+-------------------------+----------------------+


Requirements
************

The application supports the following development kits:

.. table-from-rows:: /includes/sample_board_rows.txt
   :header: heading
   :rows: thingy91_nrf9160ns, nrf9160dk_nrf9160ns

.. include:: /includes/spm.txt

Firmware architecture
*********************

The Asset Tracker v2 has a modular structure, where each module has a defined scope of responsibility.
It makes use of the Event Manager to distribute events between modules in the system.
All communication between modules happens using the event manager.
A module module converts incoming events to messages and processes them in a FIFO manner.
The processing happens either in a dedicated processing thread in the module, or directly in the event manager callback.

The figure below shows the relationship between modules and the event manager, and also shows which modules have thread and which don't.

.. figure:: /images/asset_tracker_v2_module_hierarchy.svg
    :alt: Module hierarchy

    Relationship between modules and the event manager.


Modules
=======

The application has two types of modules: with dedicated thread or without thread.
Every module has an event manager handler function, and subscribes to one or more event types.
When an event is sent from a module, all subscribers receives that event in its handler, where it's converted to a message and either processed directly or queued.

Modules may also receive events from other sources, such as drivers and libraries.
The cloud module will for instance also receive events from the configured cloud backend.
These events will also be converted to messages and either queued in the case of the cloud module, or processed directly for modules that do not have a processing thread.

.. figure:: /images/asset_tracker_v2_module_structure.svg
    :alt: Event handling in modules

    Event handling in modules

Thread usage
============

In addition to system threads, some modules have dedicated threads to process messages.
Modules that have threads perform operations that may potentially take an extended mount of time, and is therefore not suitable to be processed directly in the event handler callbacks that run on the system workqueue.

Application-specific threads
        - Main thread (app module)
        - Data management module
        - Cloud module
        - Sensor module
        - Modem module

Modules that do not have dedicated threads, process events in the context of system workqueue in the event manager callback.
Therefore, their workloads should be light and non-blocking.

All module threads have the same properties by default:
        - Thread is initialized at boot
        - Thread is preemptive
        - Priority is set to lowest application priority in the system, which is defined as CONFIG_NUM_PREEMPT_PRIORITIES - 1
        - Thread is started without delay, which means it will start running immediately after it's initialized in the boot sequende

The basic structure of the threads are also the same and is shown below.

.. code-block:: c

   static void module_thread_fn(void)
   {
           struct module_specific msg;

           self.thread_id = k_current_get();
           module_start(&self);

           /* Module specific setup */

           state_set(STATE_DISCONNECTED);

           while (true) {
                   module_get_next_msg(&self, &msg);

                   switch (state) {
                   case STATE_DISCONNECTED:
                           on_tate_disconnected(msg);
                           break;
                   case STATE_CONNECTED:
                           on_state_connected(msg);
                           break;
                   default:
                           LOG_WRN("Unknown state");
                           break;
                   }

                   on_all_states(msg);
           }
   }


Memory allocation
=================

For the most part modules use statically allocated memory.
Some features rely on dynamically allocated memory, using the Zephyr heap memory pool implementation:
        - Event manager events
        - Encoding of data that will be sent to cloud.

The heap is configured using the following Kconfig options:

* :option:`CONFIG_HEAP_MEM_POOL_SIZE` - The biggest consumer of heap is the data management module that encodes data destined for cloud, and care must therefore be taken when asjusting buffer sizes in that module so that the heap is also adjusted accordingly to avoid running out of heap in worst-case scenarios.
* :option:`CONFIG_HEAP_MEM_POOL_MIN_SIZE` - Adjusts the smallest block that can be allocated on the heap.


Cloud Setup
###########

This application is compatible with the <link-asset-tracker-cloud-solution>.
An open-source, serverless cloud backend and Web UI framework designed to store and graphically represent data sent from the application firmware.
The <link-asset-tracker-web-ui> supports the manipulation of the application's real-time configurations.

To set up the application to work with the <asset-tracker-cloud-solution>, see the following link <link-to-asset-tracker-v2-firmware-setup>.

Cloud-related Configurations
############################

To set up the application firmware to work with the <link-asset-tracker-cloud-solution> follow the steps in <link-asset-tracker-firmware-setup>.
If you want to implement the application firmware to work with your cloud solution using a specific supported vendor, follow this section.

To get the application to communicate with a specified cloud service, configure the Kconfigurations specific to each *cloud library*.
Every cloud service supported in NCS has a corresponding *cloud library* that needs to be selected and properly configured.
The following table lists supported cloud libraries, needed compile-time configurations, and links to each cloud library documentation.
Please read the documentation for the respective cloud library before trying to set up a connection.

.. table:: Cloud library related configurations
   :widths: auto
   :align: center

   +---------------+---------------+-------------------------------------------+-------------------------+
   | Cloud service | NCS library   | Configurations                            | Documentation link      |
   +===============+===============+===========================================+=========================+
   | AWS IoT Core  | 'lib_aws_iot' | :option:`CONFIG_AWS_IOT_BROKER_HOST_NAME` | :ref:`lib_aws_iot`      |
   |               |               | :option:`CONFIG_AWS_IOT_SEC_TAG`          |                         |
   +---------------+---------------+-------------------------------------------+-------------------------+

.. note::
   By default, the application uses the nRF9160s IMEI as the client ID in the cloud connection.


Building and running
********************

.. |sample path| replace:: :file:`applications/asset_tracker_v2`

.. include:: /includes/build_and_run_nrf9160.txt

The application has a Kconfig file with options that are specitic to the Asset Tracker v2, where each of the modules has a separet submenu.
These options can be used to enable and disable modules and modify their behavior and properties.
In |SES|, select :guilabel:`Project` -> :guilabel:`Configure nRF Connect SDK project` to browse and configure the options.
Alternatively, use the command line tool ``menuconfig`` or configure the options directly in :file:`prj.conf`.

.. external_antenna_note_start

.. note::
   For nRF9160 DK v0.15.0 and later, set the :option:`CONFIG_NRF9160_GPS_ANTENNA_EXTERNAL` option to ``y`` when building the application to achieve the best external antenna performance.

.. external_antenna_note_end

This application supports the |NCS| :ref:`ug_bootloader`, but it is disabled by default.
To enable the immutable bootloader, set ``CONFIG_SECURE_BOOT=y``.


Building with overlays
======================

It's possible to build the application with overlay files for both DTS and Kconfig to override the default values for the board.
The application contains examples of both DTS and Kconfig overlays.
DTS overlay files are named the same as the build target, and use the file extension ``.overlay``.
When the DTS overlay filename matches the build target, the overlay is automatically picked up and applied by the build system.

Kconfig overlays have a ``overlay-`` prefix and a ``.conf`` extension.
To build with Kconfig overlay, it has to be bassed to the build system, for instance like this:

    ``west build -b nrf9160dk_nrf9160ns -- -DOVERLAY_CONFIG=overlay-low-power.conf``

The above command will build for nRF9160-DK and use the configurations found in ``overlay-low-power.conf`` in addition to the configurations found in ``prj_nrf9160dk_nrf9160ns.conf``.
If some options are defined in both files, the options set in the overlay takes precedence.


Testing
=======

.. note::
   The cloud side must be set up before one can expect the application to behave as described below.
   The device must also be provisioned and configured with certificates according to the instructions for the cloud connection attempt to succeed.

After programming the application and all prerequisites to your board, test the Asset Tracker v2 application by performing the following steps:

1. Connect the board to the computer using a USB cable.
   The board is assigned a COM port (Windows) or ttyACM device (Linux), which is visible in the Device Manager.
#. Connect to the board with a terminal emulator, for example, LTE Link Monitor.
#. Reset the board.
#. Observe in the terminal window that the board starts up in the Secure Partition Manager and that the application starts.
   This is indicated by output similar to the following lines::

      *** Booting Zephyr OS build v2.4.0-ncs1-2616-g3420cde0e37b  ***
      <inf> event_manager: APP_EVT_START

#. Observe in the terminal window that LTE connection is established, indicated by::

     <inf> event_manager: MODEM_EVT_LTE_CONNECTING
     ...
     <inf> event_manager: MODEM_EVT_LTE_CONNECTED

#. Observe that the device establishes connection to cloud::

    <inf> event_manager: CLOUD_EVT_CONNECTING
    ...
    <inf> event_manager: CLOUD_EVT_CONNECTED

#. Observe periodic data sampling and sending to cloud::

    <inf> event_manager: APP_EVT_DATA_GET_ALL
    <inf> event_manager: APP_EVT_DATA_GET - Requested data types (MOD_DYN, BAT, ENV, GNSS)
    <inf> event_manager: GPS_EVT_ACTIVE
    <inf> event_manager: SENSOR_EVT_ENVIRONMENTAL_NOT_SUPPORTED
    <inf> event_manager: MODEM_EVT_MODEM_DYNAMIC_DATA_READY
    <inf> event_manager: MODEM_EVT_BATTERY_DATA_READY
    <inf> event_manager: GPS_EVT_DATA_READY
    <inf> event_manager: DATA_EVT_DATA_READY
    <inf> event_manager: GPS_EVT_INACTIVE
    <inf> event_manager: DATA_EVT_DATA_SEND
    <wrn> data_module: No batch data to encode, ringbuffers empty
    <inf> event_manager: CLOUD_EVT_DATA_ACK


Dependencies
************

This application uses the following |NCS| libraries and drivers:

* :ref:`event_manager`
* :ref:`lib_aws_iot`
* :ref:`lib_date_time`
* :ref:`lte_lc_readme`
* :ref:`modem_info_readme`
* :ref:`lib_download_client`
* :ref:`lib_fota_download`

It uses the following `sdk-nrfxlib`_ library:

* :ref:`nrfxlib:nrf_modem`

In addition, it uses the following sample:

* :ref:`secure_partition_manager`


