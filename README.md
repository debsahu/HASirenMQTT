# ESP8266/32 (Home Assistant 120dB Siren Switch) connected to GPIO5

[![Build Status](https://travis-ci.com/debsahu/HASirenMQTT.svg?branch=master)](https://travis-ci.com/debsahu/HASirenMQTT) [![License: MIT](https://img.shields.io/github/license/debsahu/HASirenMQTT.svg)](https://opensource.org/licenses/MIT) [![version](https://img.shields.io/github/release/debsahu/HASirenMQTT.svg)](https://github.com/debsahu/HASirenMQTT/releases) [![LastCommit](https://img.shields.io/github/last-commit/debsahu/HASirenMQTT.svg?style=social)](https://github.com/debsahu/HASirenMQTT/commits/master)

Home Assistant Switch for 120dB Siren on GPIO5

- Code can compile on ESP8266/32
- Uses WiFiManager to get WiFi/MQTT info from users
- Gets auto-discovered by Home Assistant as a MQTT switch
- Web-Server/WebSockets to control alarm
- REST-API to control alarm
- 120dB Siren is connected to D1/GPIO5
- GPIO0/FLASH_BUTTON acts as "test"

[![HASirenMQTT](https://img.youtube.com/vi/XXXXXXX/0.jpg)](https://www.youtube.com/watch?v=XXXXXXXX)

## Libraries Needed

[platformio.ini](https://github.com/debsahu/HASirenMQTT/blob/master/platformio.ini) is included, use [PlatformIO](https://platformio.org/platformio-ide) and it will take care of installing the following libraries.

| Library                     | Link                                                                                              |
|-----------------------------|---------------------------------------------------------------------------------------------------|
|MQTT                         |https://github.com/256dpi/arduino-mqtt                                                             |
|Bounce 2                     |https://github.com/thomasfredericks/Bounce2                                                        |
|ArduinoJson                  |https://github.com/bblanchon/ArduinoJson                                                           |
|WiFiManager Develop          |https://github.com/tzapu/WiFiManager/archive/development.zip                                       |
|Arduino WebSockets           |https://github.com/Links2004/arduinoWebSockets                                                     |
|UpdateUploadServer (ESP8266) |https://github.com/debsahu/DDUpdateUploadServer                                                    |

## Hardware

### Items

1. ESP8266 ([aliexpress](https://www.aliexpress.com/item/ESP8266-CH340G-CH340-G-NodeMcu-V3-Lua-Wireless-WIFI-Module-Micro-USB-Connector-Development-Board-CP2102/32965931916.html)) / ESP32 ([aliexpress](https://www.aliexpress.com/item/32808551116.html))
2. 120dB 12V Siren ([amazon](https://www.amazon.com/gp/product/B07P1FNJTG))
3. IRLB8721 N-channel MOSFET ([adafruit](https://www.adafruit.com/product/355))
4. 3S Battery ([amazon](https://www.amazon.com/gp/product/B07GF63645))
5. B3 Charger ([aliexpress](https://www.aliexpress.com/item/32827966737.html))
6. Project Box ([amazon](https://www.amazon.com/gp/product/B0002BSRIO))

### Instructions

- Use N-channel MOSFET, exact one used here is IRLB8721: works with 12V, gate can be controlled by 3.3V signal
- Connect the gate of MOSFET to D1/GPIO5, source to GND, drain to -ve of siren, +ve of battery to +ve of siren
- Use 300k and 100k voltage divider for checking battery voltage of 3S battery

![schematic](https://github.com/debsahu/HASirenMQTT/blob/master/doc/schematic.png)
