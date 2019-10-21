# Firmware

## Arduino/LogBoard
This is the main firmware of the LogBoard.

## Arduino/SD Card files
- **NewConfig_Example.txt:** At boot, the device checks if a "NewConfig.txt" file is present. If it is, all settings in EEPROM are replaced by the ones in this file. "NewConfig_Example.txt" is a template of the configuration file.
- **NewFirmware.bin:** At boot, the device checks if a "NewFirmware.bin" file is present. If it is, the firmware is updated.

## Google Script/GScriptCode.txt
This code gets the logged data via HTTP post and saves it to a Google Sheets document

## Google Script/HTTPsRedirect.php
This code receives the ESP8266 HTTP post requests and redirects the requests to Google Script via HTTPS. The URL of this script is the HostURL in the Arduino code.

## Firmware Flowchart

![v1](https://raw.githubusercontent.com/duarterr/ESP8266-LogBoard/master/Extras/Flowchart.png)

## License Information

All contents of this repository are released under [Creative Commons Share-alike 4.0](http://creativecommons.org/licenses/by-sa/4.0/).