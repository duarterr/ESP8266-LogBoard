# ESP8266 LogBoard

A development platform for the ESP8266 with data logger capabilities. Fully compatible with the Arduino IDE.

![v1](https://raw.githubusercontent.com/duarterr/ESP8266-LogBoard/master/Extras/v1_500.png)

Author: Renan R. Duarte - <duarte.renan@hotmail.com>

## Features
 - **ESP8266 (ESP12E) microcontroller:** The board is based on the ESP12E module, featuring the ESP8266 microcontroller. The module is configured in DIO SPI mode, so two extras GPIO pins can be used by the aplication (13 GPIOs in total).
 - **MicroSD card:** Data can be easily stored in a stardard microSD card.
 - **Temperature sensor:** The Maxim DS1722 SPI temperature sensor can be used to measure the temperature.
 - **Real time clock:** A Maxim DS1391 battery-backed SPI real time clock provides timming capabilities without needing the internet.
 - **Onboard battery charger:** The board can be powered directly from the USB port or by a lithium battery. The onboard battery charger can be configure to fully charge the battery (4.2V) for ciclic use or float charge with a lower voltage (4.1V) to extend battery life in applications where the USB power is always connected.
 - **Switched mode power supply:** A Maxim MAX8625 buck boost controller IC provides a high efficiency continuous 3.3V bus to the system.
 - **Onboard programmer:** The device can be programmed directly by the USB port using the onboard USB-Serial bridge (FTDI FT232RQ). This device also enable serial communications between the microcontroller and a PC.

## Repository Contents

* **/Hardware** - PCB related files (EAGLE v9.5.5 and Sketchup 2018).
* **/Firmware** - Arduino example sketches to make use of it. 
* **/Extras** - Images and other stuff. 


## License Information

All contents of this repository are released under [Creative Commons Share-alike 4.0](http://creativecommons.org/licenses/by-sa/4.0/).