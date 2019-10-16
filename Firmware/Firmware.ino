/* ------------------------------------------------------------------------------------------- */
// Logger - Data logger
// Author:  Renan R. Duarte
// E-mail:  duarte.renan@hotmail.com
// Date:    October 14, 2019
//
// Released into the public domain
/* ------------------------------------------------------------------------------------------- */

/* ------------------------------------------------------------------------------------------- */
// Libraries
/* ------------------------------------------------------------------------------------------- */

#include <DS1722_SPI.h> // https://github.com/duarterr/Arduino-DS1722-SPI
#include <DS1390_SPI.h> // https://github.com/duarterr/Arduino-DS1390-SPI

#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>

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
// HTTPS client
WiFiClientSecure LogClient;
#endif

/* ------------------------------------------------------------------------------------------- */
// Global variables
/* ------------------------------------------------------------------------------------------- */

#if USE_WIFI    
// Wifi settings
const char *WifiSSID = "Aline e Renan 2.4GHz";
const char *WifiPsw = "luiz_priscilo";

// Google script settings
const char* LogHostUrl = "script.google.com";
const int LogHostUrlPort = 443;
const char LogFingerprint[] PROGMEM = "d1 5a f8 09 73 6d 00 b0 65 10 1b a4 f3 56 3a f1 69 1d ea 53";
const char* LogScriptID = "AKfycbyg0E05h6GBNsm6fbYCDYqk5fxI9bxTghrIhBhSW9CdWW3HI43l";
#endif

// DS1722 temperature
float SensorTemperature = 0;

// ADC voltage
float BatteryVoltage = 0;

// Date and time
DS1390DateTime Time;

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

  Serial.println(); 
  Serial.print("Compilation date: ");
  Serial.print(__DATE__);
  Serial.print(" - ");
  Serial.println(__TIME__); 
#endif

  /* ----------------------------------------------------------------------------------------- */

  // Set LED pin as digital output
  pinMode(PIN_LED, OUTPUT);

  // LED off
  digitalWrite (PIN_LED, HIGH);
    
  /* ----------------------------------------------------------------------------------------- */  

  // Calculate battery voltage
  BatteryVoltage = analogRead(PIN_ADC)*ADC_GAIN;  

  // Check for low voltage
  if (BatteryVoltage < 3)
  {
#if DEBUG_SERIAL 
    Serial.println("Low battery!");
    Serial.println("Shuting down...");
#endif  

    // Flash LED to inform user
    LED_Flash ();
    
    // Enter deep sleep forever
    ESP.deepSleep(0);  
  }

  /* ----------------------------------------------------------------------------------------- */ 
   
#if DEBUG_SERIAL 
  Serial.println("Initializing SD card...");
#endif

  // Card detect pin
  pinMode(PIN_SD_CD, INPUT_PULLUP);

  // Check for SD card
  if (digitalRead(PIN_SD_CD))
  {
#if DEBUG_SERIAL   
    Serial.println("SD card not detected!");
#endif         

    // Call error function
    RunOnError ();
  }

  // Initialize SD card
  if (!SD.begin(PIN_SD_CS)) 
  {
#if DEBUG_SERIAL     
    Serial.println("Initialization failed!"); 
#endif         

    // Call error function
    RunOnError (); 
  }

#if DEBUG_SERIAL   
  Serial.println("Done.");
#endif 

  /* ----------------------------------------------------------------------------------------- */

#if DEBUG_SERIAL 
  Serial.println("Configuring DS1722...");
#endif

  // Set DS1722 conversion mode
  TS.setMode (DS1722_MODE_ONESHOT);

  // Set DS1722 resolution
  TS.setResolution (12);  

  // Request a temperature conversion - Will be ready in 1.2s @ 12bits
  TS.requestConversion ();

#if DEBUG_SERIAL 
  Serial.println("Done.");
#endif  

  /* ----------------------------------------------------------------------------------------- */

#if DEBUG_SERIAL 
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
    Serial.println ("Memory content was recently lost! Please update date and time values.");  
#endif      
    // Set DS1390 trickle charger mode (250 ohms without series diode)
    RTC.setTrickleChargerMode (DS1390_TCH_250_NO_D);    
  }

#if DEBUG_SERIAL        
  else
    Serial.println ("Memory content is valid.");
#endif  
  
#if DEBUG_SERIAL 
  Serial.println("Done.");
#endif  

  /* ----------------------------------------------------------------------------------------- */  

  // LED on
  digitalWrite (PIN_LED, LOW);

  // Get current time
  Time.Second = RTC.getDateTimeSeconds ();
  Time.Minute = RTC.getDateTimeMinutes ();  
  Time.Hour = RTC.getDateTimeHours ();      
  Time.Day = RTC.getDateTimeDay ();
  Time.Month = RTC.getDateTimeMonth ();
  Time.Year = RTC.getDateTimeYear ();
  
  // Get DS172 temperature
  SensorTemperature = TS.getTemperature();

  // Prepare log string
  snprintf (Buffer, sizeof(Buffer), "%02d/%02d/20%02d - %02d:%02d:%02d; %.4f; %.4f; %s", 
      Time.Day, Time.Month, Time.Year, Time.Hour, Time.Minute, Time.Second, BatteryVoltage, 
      SensorTemperature, RTCDataValid ? "Valid" : "Invalid");

#if DEBUG_SERIAL
  Serial.printf ("Log string: ");
  Serial.println(Buffer);
#endif

  // LED off
  digitalWrite (PIN_LED, HIGH);

  /* ----------------------------------------------------------------------------------------- */  

  // Check for SD card
  if (digitalRead(PIN_SD_CD))
  {
#if DEBUG_SERIAL   
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
    Serial.printf ("Writing to \'%s\'... \n", LOG_FILE);
#endif    

    // Write to file
    Log.println(Buffer);
    
    // Close the file
    Log.close();
    
#if DEBUG_SERIAL     
    Serial.println("Done.");
#endif      
  } 

  else 
  {
#if DEBUG_SERIAL       
    // Print an error if the file didn't open
    Serial.printf ("Error opening \'%s\'! \n", LOG_FILE);
#endif

    // Call error function
    RunOnError ();  
  }

  /* ----------------------------------------------------------------------------------------- */  

#if USE_WIFI    
#if DEBUG_SERIAL  
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
  Serial.println("Connected.");
#endif
#endif

  /* ----------------------------------------------------------------------------------------- */  

#if USE_WIFI   
#if DEBUG_SERIAL
  Serial.printf("Connecting to %s:%d using fingerprint \'%s\' \n", LogHostUrl, LogHostUrlPort, LogFingerprint);
#endif

  // Set certificate fingerprint
  LogClient.setFingerprint (LogFingerprint);

  Serial.println (ESP.getFreeHeap());
  
  // Start HTTPs client connection to host
  if (!LogClient.connect (LogHostUrl, LogHostUrlPort)) 
  {
#if DEBUG_SERIAL    
    Serial.println ("Connection failed!");
#endif         
    Serial.println (ESP.getFreeHeap());
    // Call error function
    RunOnError ();
  }
    
#if DEBUG_SERIAL  
  Serial.println("Done.");   
#endif 

  // Prepare GET string
  //String url = "/macros/s/" + (String)LogScriptID + "/exec?value=2";
  //Serial.println(url);
  
  // Prepare log string
  snprintf (Buffer, sizeof(Buffer), "/macros/s/%s/exec?value=%02d/%02d/20%02d - %02d:%02d:%02d; %.4f; %.4f; %s", 
      LogScriptID, Time.Day, Time.Month, Time.Year, Time.Hour, Time.Minute, Time.Second, BatteryVoltage, 
      SensorTemperature, RTCDataValid ? "Valid" : "Invalid");

#if DEBUG_SERIAL  
  Serial.println (Buffer);
#endif

//  LogClient.print(String("GET ") + (String)Buffer + " HTTP/1.1\r\n" +
//               "Host: " + LogHostUrl + "\r\n" +
//               "User-Agent: ESP8266LogBoard\r\n" +
//               "Connection: close\r\n\r\n");

      
  while(LogClient.connected()) 
  {
     while(LogClient.available()) 
     {
        Serial.write (LogClient.read());
     }
  }
  
  LogClient.stop();
  
//#if DEBUG_SERIAL    
//  Serial.print("HTTP response code: ");
//  Serial.println(httpCode);
//#endif
#endif

  /* ----------------------------------------------------------------------------------------- */  

#if DEBUG_SERIAL    
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
