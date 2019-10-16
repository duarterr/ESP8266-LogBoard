/* ------------------------------------------------------------------------------------------- */
// Logger - Data logger
// Author:  Renan R. Duarte
// E-mail:  duarte.renan@hotmail.com
// Date:    October 14, 2019
//
// Notes:   Sending data to Google Sheets (via Google Script) can be done directly using the
//          WiFiClientSecure library. However, HTTPS requests require more heap memory than
//          the device has available in this code when the HTTPS connection is made.
//          As a solution, this code sends an HTTP request to a server where a HTTPs redirect
//          is performed using cURL.
//
// Released into the public domain
/* ------------------------------------------------------------------------------------------- */

/* ------------------------------------------------------------------------------------------- */
// Libraries
/* ------------------------------------------------------------------------------------------- */

// EEPROM module related functions
#include <EEPROM.h>

/* ------------------------------------------------------------------------------------------- */
// Software defines
/* ------------------------------------------------------------------------------------------- */

// EEPROM addresses
#define ADDR_FILENAME           0                     // 64 bytes max (+ '\n')
#define ADDR_HOSTURL            ADDR_FILENAME + 64    // 64 bytes max (+ '\n')
#define ADDR_SCRIPTID           ADDR_HOSTURL + 64     // 64 bytes max (+ '\n')
#define ADDR_TIMEZONE           ADDR_SCRIPTID + 64    // 2 bytes max
#define ADDR_WIFI_SSID          ADDR_TIMEZONE + 2     // 32 bytes max (+ '\n')
#define ADDR_WIFI_PSW           ADDR_WIFI_SSID + 32   // 32 bytes max (+ '\n')
#define ADDR_TIME_SAMPLE        ADDR_WIFI_PSW + 32    // 2 bytes max 
#define ADDR_TIME_ERROR         ADDR_TIME_SAMPLE + 2  // 2 bytes max 

/* ------------------------------------------------------------------------------------------- */
// Name:        setup
// Description: This function runs on boot
// Arguments:   None
// Returns:     None
/* ------------------------------------------------------------------------------------------- */

void setup() 
{ 
  // Init EEPROM
  EEPROM.begin(ADDR_TIME_ERROR + 2);

  EEPROMSetLogFilename ("Log.csv");
  EEPROMSetLogHost ("http://hosturl.com/");
  EEPROMSetScriptID ("ScriptID");
  EEPROMSetWifiSSID ("ssid");
  EEPROMSetWifiPsw ("psw");
  EEPROMSetTimezone (-3);
  EEPROMSetSampleInterval (30);
  EEPROMSetErrorInterval (30);  
}

/* ------------------------------------------------------------------------------------------- */
// Name:        loop
// Description: This function runs in a loop after 'setup' finishes
// Arguments:   None
// Returns:     None
/* ------------------------------------------------------------------------------------------- */

void loop () 
{
}

/* ------------------------------------------------------------------------------------------- */
// Name:        EEPROMSetLogFilename
// Description: Writes the log filename in EEPROM
// Arguments:   Buffer - Pointer to the string to be written (64 bytes)
// Returns:     None
/* ------------------------------------------------------------------------------------------- */

void EEPROMSetLogFilename (const char *Buffer)
{
  unsigned int Addr = ADDR_FILENAME;

  // Write all chars
  while (*Buffer)
    EEPROM.write(Addr++, *Buffer++);

  // Write carriage return
  EEPROM.write(Addr, '\n');

  // Apply changes in EEPROM
  EEPROM.commit();
}

/* ------------------------------------------------------------------------------------------- */
// Name:        EEPROMSetLogHost
// Description: Writes the host url in EEPROM
// Arguments:   Buffer - Pointer to the string to be written (64 bytes)
// Returns:     None
/* ------------------------------------------------------------------------------------------- */

void EEPROMSetLogHost (const char *Buffer)
{
  unsigned int Addr = ADDR_HOSTURL;

  // Write all chars
  while (*Buffer)
    EEPROM.write(Addr++, *Buffer++);

  // Write carriage return
  EEPROM.write(Addr, '\n');

  // Apply changes in EEPROM
  EEPROM.commit();
}

/* ------------------------------------------------------------------------------------------- */
// Name:        EEPROMSetScriptID
// Description: Writes Google script ID in EEPROM
// Arguments:   Buffer - Pointer to the string to be written (64 bytes)
// Returns:     None
/* ------------------------------------------------------------------------------------------- */

void EEPROMSetScriptID (const char *Buffer)
{
  unsigned int Addr = ADDR_SCRIPTID;

  // Write all chars
  while (*Buffer)
    EEPROM.write(Addr++, *Buffer++);

  // Write carriage return
  EEPROM.write(Addr, '\n');

  // Apply changes in EEPROM
  EEPROM.commit();
}

/* ------------------------------------------------------------------------------------------- */
// Name:        EEPROMSetWifiSSID
// Description: Writes the WiFi SSID in EEPROM
// Arguments:   Buffer - Pointer to the string to be written (32 bytes)
// Returns:     None
/* ------------------------------------------------------------------------------------------- */

void EEPROMSetWifiSSID (const char *Buffer)
{
  unsigned int Addr = ADDR_WIFI_SSID;

  // Write all chars
  while (*Buffer)
    EEPROM.write(Addr++, *Buffer++);

  // Write carriage return
  EEPROM.write(Addr, '\n');

  // Apply changes in EEPROM
  EEPROM.commit();
}

/* ------------------------------------------------------------------------------------------- */
// Name:        EEPROMSetWifiPsw
// Description: Writes the WiFi password in EEPROM
// Arguments:   Buffer - Pointer to the string to be written (32 bytes)
// Returns:     None
/* ------------------------------------------------------------------------------------------- */

void EEPROMSetWifiPsw (const char *Buffer)
{
  unsigned int Addr = ADDR_WIFI_PSW;

  // Write all chars
  while (*Buffer)
    EEPROM.write(Addr++, *Buffer++);

  // Write carriage return
  EEPROM.write(Addr, '\n');

  // Apply changes in EEPROM
  EEPROM.commit();
}

/* ----------------------------------------------------------------------------------------------------------------------- */
// Name:        EEPROMSetTimezone
// Description: Writes the log timezone value in EEPROM
// Arguments:   Timezone - Value to be written (int)
// Returns:     None
/* ----------------------------------------------------------------------------------------------------------------------- */

void EEPROMSetTimezone (int Timezone)
{
  // Write upper and lower bytes of Timezone
  EEPROM.write (ADDR_TIMEZONE, Timezone >> 8);
  EEPROM.write (ADDR_TIMEZONE + 1, Timezone & 0xFF);

  // Apply changes in EEPROM
  EEPROM.commit();
}

/* ----------------------------------------------------------------------------------------------------------------------- */
// Name:        EEPROMSetSampleInterval
// Description: Writes the Log sample interval value in EEPROM
// Arguments:   Interval - Value to be written (unsigned short)
// Returns:     None
/* ----------------------------------------------------------------------------------------------------------------------- */

void EEPROMSetSampleInterval (unsigned short Interval)
{
  // Write upper and lower bytes of Interval
  EEPROM.write (ADDR_TIME_SAMPLE, Interval >> 8);
  EEPROM.write (ADDR_TIME_SAMPLE + 1, Interval & 0xFF);

  // Apply changes in EEPROM
  EEPROM.commit();
}

/* ----------------------------------------------------------------------------------------------------------------------- */
// Name:        EEPROMSetErrorInterval
// Description: Writes the log error interval value in EEPROM
// Arguments:   Interval - Value to be written (unsigned int)
// Returns:     None
/* ----------------------------------------------------------------------------------------------------------------------- */

void EEPROMSetErrorInterval (unsigned int Interval)
{
  // Write upper and lower bytes of Interval
  EEPROM.write (ADDR_TIME_ERROR, Interval >> 8);
  EEPROM.write (ADDR_TIME_ERROR + 1, Interval & 0xFF);

  // Apply changes in EEPROM
  EEPROM.commit();
}

/* ------------------------------------------------------------------------------------------- */
// End of code
/* ------------------------------------------------------------------------------------------- */
