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
#include <ESP8266HTTPClient.h>

#include <SdFat.h>

using namespace sdfat;

/* ------------------------------------------------------------------------------------------- */
// Software defines
/* ------------------------------------------------------------------------------------------- */

// Code debug with serial port
#define DEBUG_SERIAL            true

// Use WiFi to post data online
#define USE_WIFI                false

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
// HTTP client
HTTPClient Log_Client;
#endif

/* ------------------------------------------------------------------------------------------- */
// Global variables
/* ------------------------------------------------------------------------------------------- */

#if USE_WIFI    
// Wifi settings
const char *SERVER_WIFI_SSID = "Aline e Renan 2.4GHz";
const char *SERVER_WIFI_PASS = "luiz_priscilo";
const char* fingerprint = "C6 69 4A 9D 71 F5 EA A5 EE 3D 63 EE 9F BC C6 27 F7 C3 A8 A6";
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
char Buffer[50] = {0};

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
    Serial.printf("Going to sleep for %d seconds... \n", TIME_SLEEP_ERROR);
#endif     

    // Flash LED to inform user
    LED_Flash ();
    
    // Enter deep sleep
    ESP.deepSleep(TIME_SLEEP_ERROR*1e6);     
  }

  // Initialize SD card
  if (!SD.begin(PIN_SD_CS)) 
  {
#if DEBUG_SERIAL     
    Serial.println("Initialization failed!"); 
    Serial.printf("Going to sleep for %d seconds...", TIME_SLEEP_ERROR);
#endif

    // Flash LED to inform user
    LED_Flash ();
    
    // Enter deep sleep
    ESP.deepSleep(TIME_SLEEP_ERROR*1e6); 
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
    Serial.printf("Going to sleep for %d seconds... \n", TIME_SLEEP_ERROR);
#endif     

    // Flash LED to inform user
    LED_Flash ();
    
    // Enter deep sleep
    ESP.deepSleep(TIME_SLEEP_ERROR*1e6);     
  }
  
  // Open the log file
  Log = SD.open(LOG_FILE, FILE_WRITE);

  // If the file opened okay, write to it
  if (Log) 
  {
#if DEBUG_SERIAL    
    Serial.printf ("Writing to %s... \n", LOG_FILE);
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
    Serial.printf ("Error opening %s! \n", LOG_FILE);
#endif

    // Flash LED to inform user
    LED_Flash ();     
  }

  /* ----------------------------------------------------------------------------------------- */  

#if USE_WIFI    
#if DEBUG_SERIAL  
  Serial.print("Connecting to WiFi");
#endif

  // Start WiFi connection
  WiFi.begin(SERVER_WIFI_SSID, SERVER_WIFI_PASS);

  // Check connection status
  while(WiFi.status() != WL_CONNECTED)
  {
    if (ConnectionCounter >= WIFI_TIMEOUT)
    {
#if DEBUG_SERIAL   
      Serial.println();
      Serial.println("Connection timed out!");
      Serial.printf("Going to sleep for %d seconds... \n", TIME_SLEEP_ERROR);
#endif     
  
      // Flash LED to inform user
      LED_Flash ();
      
      // Enter deep sleep
      ESP.deepSleep(TIME_SLEEP_ERROR*1e6);           
    }
    
#if DEBUG_SERIAL    
    Serial.print(".");
#endif

    // 1 second delay
    delay(1000);

    // Increase counter
    ConnectionCounter++;    
  }

#if DEBUG_SERIAL
  Serial.println(" ");
  Serial.println("Connected.");

  Serial.println("Starting HTTPs client...");
#endif
  
  // Start HTTP client connection to host
  Log_Client.begin("https://script.google.com/macros/s/AKfycbyg0E05h6GBNsm6fbYCDYqk5fxI9bxTghrIhBhSW9CdWW3HI43l/exec", fingerprint);

//   while(!Log_Client.connected())
//   {
//    // 1 second delay
//    delay(1000);
//    
//#if DEBUG_SERIAL    
//    Serial.print(".");
//#endif
//  }
  
  // Define request timeout
  Log_Client.setTimeout(5000);
  
  // Define request content type - Required to perform posts
  Log_Client.addHeader("Content-Type", "application/x-www-form-urlencoded");

#if DEBUG_SERIAL  
  Serial.println("Done.");   
#endif 

#if DEBUG_SERIAL  
  Serial.println("Posting...");
#endif

  // Prepare log string
  snprintf (Buffer, sizeof(Buffer), "value=%02d/%02d/20%02d - %02d:%02d:%02d; %.4f; %.4f; %s", 
      Time.Day, Time.Month, Time.Year, Time.Hour, Time.Minute, Time.Second, BatteryVoltage, 
      SensorTemperature, RTCDataValid ? "Valid" : "Invalid");
      
  int httpCode = Log_Client.POST(Buffer);
  Log_Client.getString();

#if DEBUG_SERIAL    
  Serial.print("HTTP response code: ");
  Serial.println(httpCode);
#endif
#endif

  /* ----------------------------------------------------------------------------------------- */  

#if DEBUG_SERIAL    
  Serial.printf("Going to sleep for %d seconds... \n", TIME_SLEEP_SAMPLES);
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
// End of code
/* ------------------------------------------------------------------------------------------- */
