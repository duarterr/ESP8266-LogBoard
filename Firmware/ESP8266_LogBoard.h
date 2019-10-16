/* ------------------------------------------------------------------------------------------- */
// ESP8266 LogBoard
// Version: 1.0
// Author:  Renan R. Duarte
// E-mail:  duarte.renan@hotmail.com
// Date:    October 16, 2019
//
// Released into the public domain
/* ------------------------------------------------------------------------------------------- */

#ifndef ESP8266_LOGBOARD_H
#define ESP8266_LOGBOARD_H

/* ------------------------------------------------------------------------------------------- */
// Includes
/* ------------------------------------------------------------------------------------------- */

// DS1722 SPI thermometer related functions
#include <DS1722_SPI.h> // https://github.com/duarterr/Arduino-DS1722-SPI

// DS1390 SPI real time clock related functions
#include <DS1390_SPI.h> // https://github.com/duarterr/Arduino-DS1390-SPI

// ESP8266 WiFi related functions
#include <ESP8266WiFi.h>

// ESP8266 HTTP client related functions
#include <ESP8266HTTPClient.h>

// SD card related functions
#include <SdFat.h>
using namespace sdfat;

/* ------------------------------------------------------------------------------------------- */
// Hardware defines
/* ------------------------------------------------------------------------------------------- */

// Peripheral pins
#define PIN_SD_CS               9
#define PIN_SD_CD               4
#define PIN_RTC_CS              10
#define PIN_TS_CS               5
#define PIN_LED                 2
#define PIN_ADC                 A0

// ADC gain
#define ADC_GAIN                0.005311198

/* ------------------------------------------------------------------------------------------- */
// Software defines
/* ------------------------------------------------------------------------------------------- */

// Code debug with serial port
#define DEBUG_SERIAL            true

// Time between samples (seconds) 
#define TIME_SLEEP_SAMPLES      30

// Deep sleep time if error (seconds)
#define TIME_SLEEP_ERROR        30
   
// WiFi connection timeout (seconds)
#define WIFI_TIMEOUT            5

// Timezone (-12 +12)
#define TIMEZONE                -3

/* ------------------------------------------------------------------------------------------- */
// Structures
/* ------------------------------------------------------------------------------------------- */

/* ------------------------------------------------------------------------------------------- */
// Functions
/* ------------------------------------------------------------------------------------------- */



#endif

/* ------------------------------------------------------------------------------------------- */
// End of code
/* ------------------------------------------------------------------------------------------- */
