# Arduino sketches and related files

## LogBoard
This is the main firmware of the LogBoard.

## SD Card files
- **NewConfig_Example.txt:** At boot, the device checks if a "NewConfig.txt" file is present. If it is, all settings in EEPROM are replaced by the ones in this file. "NewConfig_Example.txt" is a template of the configuration file.
- **NewFirmware.bin:** At boot, the device checks if a "NewFirmware.bin" file is present. If it is, the firmware is updated.

## License Information

All contents of this repository are released under [Creative Commons Share-alike 4.0](http://creativecommons.org/licenses/by-sa/4.0/).