# electrodacus-esp32

Firmware for the esp32 on the WiFi-addon of the [electrodacus BMS](http://www.electrodacus.com/).

Built with **ESP-IDF v5.3** only (no Arduino framework). It can be developed and
maintained from VS Code with the official
[Espressif IDF extension](https://marketplace.visualstudio.com/items?itemName=espressif.esp-idf-extension),
or from the command line with `idf.py`.

## Current features

* Handle wifi connection to access point, fallback to provide own WiFi network in case no connection could be made.
* Vue.js based Webinterface for configuration
* Hosts unaltered electrodacus HTML file (apart from data URL)
* Provides raw data as read by HTML file (you can still use any local HTML file, just change the data URL to `http://[the IP of the device]/rawData`)
* Receiving and caching data from SBMS with unaltered firmware. (ignores AT commands)
* Parsing data from SBMS, usable by Consumers like the MQTT client. (currently only live data)
* MQTT client: publish live data in JSON format whenever it is received from the SBMS main board
* Browser-based OTA updates for both firmware and filesystem at `/update`


## Planned features

Not necessarily in that order.

* More configuration options (especially for MQTT and data rates)
* InfluxDB client: create InfluxDB Line-format and send to configurable Server
* NTP Timesync: add reliable timestamps to the data (in case of permanent internet connection)
* TLS encryption: add certificate based authentication and encryption for MQTT and possibly Influxdb endpoints

Please feel free to create issues for any suggestions. I've also added my plans there.

## Installation

This section describes how to update your SBMS' WiFi module to the latest version.

### current state

There will be browser based OTA updates and binary releases very soon. For now, refer to the instructions below.

### Requirements

If your SBMS does not come pre-flashed with an OTA capable firmware version yet, you need a USB to Serial/UART converter with an interface level of 3.3V. **5V will destroy your ESP32.** You also need a "PC" (Linux, macOS or Windows) with [ESP-IDF v5.3](https://docs.espressif.com/projects/esp-idf/en/v5.3.2/esp32/get-started/index.html) installed — either the standalone tools (`install.sh` / `install.bat`) or the VS Code Espressif IDF extension.

### Wired Procedure

![Connections](documentation/connections1.png)

The basic procedure is as follows:
1. Disconnect the WiFi shield with the ESP32 module from your SBMS. To be safe, unplug the SBMS completely before doing that.
2. Connect the pins RX,TX,GND and 3.3V as shown in the illustration. Your USB to Serial converter will probably already provide 3.3V and enough power, so you can use that. The RX of your converter must be connected to the pin labeled TX and vice versa. It is probably easiest to use female jumper wires.
3. Make a temporary connection (hold a male jumper wire onto it) from the pin labelled IO0 to GND. This will bring the ESP32 into bootloader mode so you can flash the new firmware.
4. Only now plug in your USB to Serial converter to your USB port (or power up your dedicated 3.3V source). You can release the connection to IO0 now.
5. Flash the firmware (see next section)
6. Verify you can reach the device via WiFi. It should create an Access Point with a name starting with SBMS, followed by 12 characters representing a MAC-Address. The default password for that AP is "electrodacus" (without the quotes). Connect to it by going to `192.168.4.1` in your browser.
7. Disconnect everything you just connected before plugging the WiFi module back onto your SBMS.

### Over-the-air updates

Once a version with OTA support is running, further updates go through the web
interface: browse to `http://[IP of the device]/update`, pick a firmware
`electrodacus-esp32.bin` or a filesystem `spiffs.bin`, and upload. Uploading the
filesystem image reboots the device when done; upload firmware first, then the
filesystem, if you want to update both in one go. By default OTA is only accepted
in the first 5 minutes after boot (configurable in Settings).

### Installation / Flashing

Open a terminal.

1. Clone the repository:
```
git clone https://github.com/armageddon421/electrodacus-esp32.git
cd electrodacus-esp32
```
2. Activate your ESP-IDF environment (skip if you use the VS Code extension):
```
. $HOME/esp/esp-idf/export.sh
```
3. Set the target and build. The managed dependency (ArduinoJson) is fetched
   automatically. The `data/` directory is built into `spiffs.bin`.
```
idf.py set-target esp32
idf.py build
```
   A successful build ends with a line like:
```
electrodacus-esp32.bin binary size 0xcc5b0 bytes. Smallest app partition is 0x140000 bytes. 0x73a50 bytes (36%) free.
```
4. Flash firmware, partition table, bootloader and filesystem in one step
   (PlatformIO auto-detected the port; with `idf.py` pass it explicitly if
   needed with `-p`):
```
idf.py -p /dev/ttyUSB0 flash
```
5. Because the build sets "no reset after flashing", replug your USB (or power
   supply) to reboot the ESP32. Keep GPIO0 tied to GND if you want to flash again.
6. To watch the (normally silent) console or debug, use `idf.py -p /dev/ttyUSB0 monitor`.
   Note UART0 is shared with the SBMS link, so keep IDF logging disabled.
7. Disconnect GPIO0 and replug power. That should be it.

From the VS Code Espressif IDF extension the same steps are the "Set Espressif
target", "Build", and "Flash" buttons on the status bar.

## Frequently asked Questions (probably)

#### Why are there no binary releases?
There will be binary releases with OTA update via web interface very soon.

#### What is MQTT?
[MQTT](http://mqtt.org/) is a publisher-subscriber based message passing protocol that uses a central server called "broker" to relay the messages. Many IoT applications nowadays support MQTT, devices as well as dashboard and home automation solutions. It is easy to locally host your own local MQTT-Broker (for example [Mosquitto](https://mosquitto.org/)) for your home automation needs, a raspberry pi is already enough.
I personally use [node-red](https://nodered.org/) as "glue logic" to connect all my sensors and actors, feed my databases and for creating logic.
