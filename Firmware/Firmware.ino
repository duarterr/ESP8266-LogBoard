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
// Software defines
/* ------------------------------------------------------------------------------------------- */

// Code debug with serial port
#define DEBUG_SERIAL            true

// Use WiFi to post data online
#define USE_WIFI                true

// Time between samples (seconds) 
#define TIME_SLEEP_SAMPLES      30

// Deep sleep time if error (seconds)
#define TIME_SLEEP_ERROR        30

#if USE_WIFI    
// WiFi connection timeout (seconds)
#define WIFI_TIMEOUT            5
#endif

// Log file
#define LOG_FILE                "Log.csv"

// Timezone (-12 +12)
#define TIMEZONE                -3

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
// Constructors
/* ------------------------------------------------------------------------------------------- */

// Temperature sensor
DS1722 TS (PIN_TS_CS);

// Real time clock
DS1390 RTC (PIN_RTC_CS);

// SD card
SdFat SD;

// Log file
File Log;

#if USE_WIFI    
// HTTP client
HTTPClient LogClient;
#endif

/* ------------------------------------------------------------------------------------------- */
// Global variables
/* ------------------------------------------------------------------------------------------- */

#if USE_WIFI    
// Wifi settings
const char *WifiSSID = "Aline e Renan 2.4GHz";
const char *WifiPsw = "luiz_priscilo";

// Google script settings
const char* LogHostUrl = "http://coral.ufsm.br/gedre/accesscontrol/";
const char* LogScriptID = "AKfycbyg0E05h6GBNsm6fbYCDYqk5fxI9bxTghrIhBhSW9CdWW3HI43l";
#endif

// DS1722 temperature
float SensorTemperature = 0;

// ADC voltage
float BatteryVoltage = 0;

// Date and time - Epoch format
unsigned long EpochTime = 0;

// RTC data valid flag
bool RTCDataValid = false;

// Log string
char Buffer[200] = {0};

#if USE_WIFI    
// WiFi connection counter
unsigned char ConnectionCounter = 0;
#endif

/* ------------------------------------------------------------------------------------------- */
// LED flash function
/* ------------------------------------------------------------------------------------------- */

void LED_Flash ()
{
  digitalWrite (PIN_LED, LOW);
  delay(200);
  digitalWrite (PIN_LED, HIGH);
  delay(200);
  digitalWrite (PIN_LED, LOW); 
  delay(200);
  digitalWrite (PIN_LED, HIGH);  
}

/* ------------------------------------------------------------------------------------------- */
// Initialization function
/* ------------------------------------------------------------------------------------------- */

void setup() 
{
#if DEBUG_SERIAL
  Serial.begin(74880);
  while (!Serial);  
#endif

  /* ----------------------------------------------------------------------------------------- */

  // Set LED pin as digital output
  pinMode(PIN_LED, OUTPUT);

  // LED off
  digitalWrite (PIN_LED, HIGH);
    
  /* ----------------------------------------------------------------------------------------- */ 
   
#if DEBUG_SERIAL 
  Serial.printf ("[%05d] ", millis());
  Serial.println("Initializing SD card...");
#endif

  // Card detect pin
  pinMode(PIN_SD_CD, INPUT_PULLUP);

  // Check for SD card
  if (digitalRead(PIN_SD_CD))
  {
#if DEBUG_SERIAL   
    Serial.printf ("[%05d] ", millis());
    Serial.println("SD card not detected!");
#endif         

    // Call error function
    RunOnError ();
  }

  // Initialize SD card
  if (!SD.begin(PIN_SD_CS)) 
  {
#if DEBUG_SERIAL     
    Serial.printf ("[%05d] ", millis());
    Serial.println("Initialization failed!"); 
#endif         

    // Call error function
    RunOnError (); 
  }

#if DEBUG_SERIAL
  Serial.printf ("[%05d] ", millis());
  Serial.println("Done.");
#endif 

  /* ----------------------------------------------------------------------------------------- */

#if DEBUG_SERIAL 
  Serial.printf ("[%05d] ", millis());
  Serial.println("Configuring DS1722...");
#endif

  // Set DS1722 conversion mode
  TS.setMode (DS1722_MODE_ONESHOT);

  // Set DS1722 resolution
  TS.setResolution (12);  

  // Request a temperature conversion - Will be ready in 1.2s @ 12bits
  TS.requestConversion ();

#if DEBUG_SERIAL 
  Serial.printf ("[%05d] ", millis());
  Serial.println("Done.");
#endif  

  /* ----------------------------------------------------------------------------------------- */

#if DEBUG_SERIAL 
  Serial.printf ("[%05d] ", millis());
  Serial.println("Configuring DS1390...");
#endif

  // Delay for DS1390 boot (mandatory)
  delay (200);

  // Check if memory was lost recently
  RTCDataValid = RTC.getValidation ();

  // Invalid data - Proceed anyway and configure trickle charger
  if (RTCDataValid == false)
  {
#if DEBUG_SERIAL  
    Serial.printf ("[%05d] ", millis());
    Serial.println ("Memory content was recently lost! Please update date and time values.");  
#endif      
    // Set DS1390 trickle charger mode (250 ohms with one series diode)
    RTC.setTrickleChargerMode (DS1390_TCH_250_D);    
  }

#if DEBUG_SERIAL        
  else
    Serial.printf ("[%05d] ", millis());
    Serial.println ("Memory content is valid.");
#endif  
  
#if DEBUG_SERIAL 
  Serial.printf ("[%05d] ", millis());
  Serial.println("Done.");
#endif  

  /* ----------------------------------------------------------------------------------------- */  

  // LED on
  digitalWrite (PIN_LED, LOW);

  // Calculate battery voltage
  BatteryVoltage = analogRead(PIN_ADC)*ADC_GAIN;   

  // Get current time
  EpochTime = RTC.getDateTimeEpoch (TIMEZONE);
  
  // Get DS172 temperature
  SensorTemperature = TS.getTemperature();

  // Prepare log string
  snprintf (Buffer, sizeof(Buffer), "%010d; %.4f; %.4f; %s", 
      EpochTime, BatteryVoltage, SensorTemperature, RTCDataValid ? "Valid" : "Invalid");

#if DEBUG_SERIAL
  Serial.printf ("[%05d] ", millis());
  Serial.printf ("Log payload: %s \n", Buffer);
#endif

  // LED off
  digitalWrite (PIN_LED, HIGH);

  /* ----------------------------------------------------------------------------------------- */  

  // Check for SD card
  if (digitalRead(PIN_SD_CD))
  {
#if DEBUG_SERIAL   
    Serial.printf ("[%05d] ", millis());
    Serial.println("SD card not detected!");
#endif         

    // Call error function
    RunOnError ();    
  }
  
  // Open the log file
  Log = SD.open(LOG_FILE, FILE_WRITE);

  // If the file opened okay, write to it
  if (Log) 
  {
#if DEBUG_SERIAL    
    Serial.printf ("[%05d] ", millis());
    Serial.printf ("Writing to \'%s\'... \n", LOG_FILE);
#endif    

    // Write to file
    Log.println(Buffer);
    
    // Close the file
    Log.close();
    
#if DEBUG_SERIAL     
    Serial.printf ("[%05d] ", millis());
    Serial.println("Done.");
#endif      
  } 
  
  // Error opening the file
  else 
  {
#if DEBUG_SERIAL       
    Serial.printf ("[%05d] ", millis());
    Serial.printf ("Error opening \'%s\'! \n", LOG_FILE);
#endif

    // Call error function
    RunOnError ();  
  }

  /* ----------------------------------------------------------------------------------------- */  

#if USE_WIFI    
#if DEBUG_SERIAL 
  Serial.printf ("[%05d] ", millis()); 
  Serial.print("Connecting to WiFi...");
#endif

  // Start WiFi connection
  WiFi.begin(WifiSSID, WifiPsw);

  // Check connection status
  while(WiFi.status() != WL_CONNECTED)
  {
    if (ConnectionCounter >= WIFI_TIMEOUT)
    {
#if DEBUG_SERIAL   
      Serial.println();
      Serial.printf ("[%05d] ", millis());      
      Serial.println("Connection timed out!");
#endif         

      // Call error function
      RunOnError ();         
    }

    // 1 second delay
    delay(1000);

    // Increase counter
    ConnectionCounter++;    
  }

#if DEBUG_SERIAL
  Serial.println();
  Serial.printf ("[%05d] ", millis());  
  Serial.println("Connected.");
#endif
#endif

  /* ----------------------------------------------------------------------------------------- */  

#if USE_WIFI   
#if DEBUG_SERIAL
  Serial.printf ("[%05d] ", millis());
  Serial.printf ("Connecting to \'%s\'... \n", LogHostUrl);
#endif

  // Start HTTP client connection to host
  LogClient.begin ((String)LogHostUrl);

  // Define request timeout
  LogClient.setTimeout(2000);

  // Define request content type - Required to perform posts
  LogClient.addHeader("Content-Type", "application/x-www-form-urlencoded");

  // Delay to establish connection
  delay (100);
  
#if DEBUG_SERIAL
  Serial.printf ("[%05d] ", millis());  
  Serial.println("Done.");   
#endif
 
  // Prepare log string
  snprintf (Buffer, sizeof(Buffer), "id=%s&log=%010d;%.4f;%.4f;%s", 
      LogScriptID, EpochTime, BatteryVoltage, SensorTemperature, RTCDataValid ? "Valid" : "Invalid");     

#if DEBUG_SERIAL
  Serial.printf ("[%05d] ", millis());  
  Serial.printf("POST Payload: %s \n", Buffer);  
#endif

#if DEBUG_SERIAL
  Serial.printf ("[%05d] ", millis());  
  Serial.println ("Posting...");  
#endif

  // HTTP post request to host
  short HTTPCode = LogClient.POST(Buffer); 
  
#if DEBUG_SERIAL 
  Serial.printf ("[%05d] ", millis());   
  Serial.printf ("HTTP response code: %d \n", HTTPCode);

//  Serial.printf ("[%05d] ", millis());   
//  Serial.print (LogClient.getString());
#endif

  // Close client connection
  LogClient.end();

#endif

  /* ----------------------------------------------------------------------------------------- */   

  // Check for low voltage
  if (BatteryVoltage < 3)
  {
#if DEBUG_SERIAL 
    Serial.printf ("[%05d] ", millis());
    Serial.println ("Low battery!");
    Serial.printf ("[%05d] ", millis());
    Serial.println ("Shuting down...");
#endif  

    // Flash LED to inform user
    LED_Flash ();
    
    // Enter deep sleep forever
    ESP.deepSleep(0);  
  }
  
  /* ----------------------------------------------------------------------------------------- */  

#if DEBUG_SERIAL
  Serial.printf ("[%05d] ", millis());    
  Serial.printf("Going run again in %d seconds... \n", TIME_SLEEP_SAMPLES);
#endif

  // Enter deep sleep
  ESP.deepSleep(TIME_SLEEP_SAMPLES*1e6); 
}

/* ------------------------------------------------------------------------------------------- */
// Loop function
/* ------------------------------------------------------------------------------------------- */

void loop() 
{
}

/* ------------------------------------------------------------------------------------------- */
// Error function
/* ------------------------------------------------------------------------------------------- */

void RunOnError () 
{
#if DEBUG_SERIAL 
  Serial.printf ("[%05d] ", millis());  
  Serial.printf("Going to sleep for %d seconds... \n", TIME_SLEEP_ERROR);
#endif     

  // Flash LED to inform user
  LED_Flash ();
  
  // Enter deep sleep
  ESP.deepSleep(TIME_SLEEP_ERROR*1e6);    
}

/* ------------------------------------------------------------------------------------------- */
// End of code
/* ------------------------------------------------------------------------------------------- */
