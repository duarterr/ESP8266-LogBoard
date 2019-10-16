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
//
// Code workflow: 
// (1) Init
// (2) Check for SD card -  N: Only Wifi, load last config from EEPROM
//                          Y: Check for new config file -  N: Load last from EEPROM
//                                                          Y: Save to EEPROM, delete config file
// (3) Start wifi connection                                                  
// (4) Start peripherals
// (5) Sample data
// (6) Check wifi connection -  Y: Post data - Get result
//                              N: Save to SD card if present
// (7) Check Vbat -   Normal: Sleep TIME_SAMPLE                          
//                    Low: Sleep forever
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
// Constructors
/* ------------------------------------------------------------------------------------------- */

// Temperature sensor
DS1722 TS (PIN_TS_CS);

// Real time clock
DS1390 RTC (PIN_RTC_CS);

// SD card
SdFat SD;

// Configuration file
File ConfigFile;

// Log file
File LogFile;
    
// HTTP client
HTTPClient LogClient;

/* ------------------------------------------------------------------------------------------- */
// Global variables
/* ------------------------------------------------------------------------------------------- */

// Configuration file
const char* ConfigFilename = "Config.csv";

// Log file
const char* LogFilename = "Log.csv";
    
// Wifi settings
const char* WifiSSID = "GEDRE Pos Graduacao";
const char* WifiPsw = "gedrepos2016";

// Google script settings
const char* LogHostUrl = "http://coral.ufsm.br/gedre/accesscontrol/";
const char* LogScriptID = "AKfycbyg0E05h6GBNsm6fbYCDYqk5fxI9bxTghrIhBhSW9CdWW3HI43l";

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
    
// WiFi connection counter
unsigned char ConnectionCounter = 0;







// Log flags
bool LogToSD = false;
bool LogToWifi = false;

// Log success flags
bool LogSDSuccess = false;
bool LogWifiSuccess = false;


/* ------------------------------------------------------------------------------------------- */
// Name:        setup
// Description: This function runs on boot
// Arguments:   None
// Returns:     None
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
  LogToSD = SDCardPresent();

  // SD card was not detected
  if (!LogToSD)
  {
    #if DEBUG_SERIAL   
    Serial.printf ("[%05d] ", millis());
    Serial.println("SD card not detected. Logging only via WiFi.");
    #endif         

    // Get configuration values from EEPROM
    #if DEBUG_SERIAL     
    Serial.printf ("[%05d] ", millis());
    Serial.println("Getting last configuration from EEPROM..."); 
    #endif   
  }

  // SD card was detected
  else
  {
    // Fails to initialize SD card
    if (!SD.begin(PIN_SD_CS)) 
    {
       #if DEBUG_SERIAL     
      Serial.printf ("[%05d] ", millis());
      Serial.println("Initialization failed! Logging only via WiFi."); 
      #endif   

      // Get configuration values from EEPROM
      #if DEBUG_SERIAL     
      Serial.printf ("[%05d] ", millis());
      Serial.println("Getting last configuration from EEPROM..."); 
      #endif         
    }     

    // SD card was initialized
    else
    {
      #if DEBUG_SERIAL
      Serial.printf ("[%05d] ", millis());
      Serial.println("Done.");     
      #endif       

      // Open the config file
      ConfigFile = SD.open (ConfigFilename, FILE_READ);
    
      // Configuration file not found
      if (!LogFile) 
      {
        #if DEBUG_SERIAL
        Serial.printf ("[%05d] ", millis());
        Serial.println("Configuration file not found.");     
        #endif                  
        
        // Get configuration values from EEPROM
        #if DEBUG_SERIAL     
        Serial.printf ("[%05d] ", millis());
        Serial.println("Getting last configuration from EEPROM..."); 
        #endif           
      }

      // Configuration file was found
      else
      {
        // Get configuration values from SD card
        #if DEBUG_SERIAL     
        Serial.printf ("[%05d] ", millis());
        Serial.println("Getting new configuration from SD card..."); 
        #endif  

        // Save values to EEPROM
        
        // Delete configuration file
        if (!ConfigFile.remove())
        {
          #if DEBUG_SERIAL     
          Serial.printf ("[%05d] ", millis());
          Serial.println("Error deleting configuration file!"); 
          #endif  
        }
      }
    }
  }

  /* ----------------------------------------------------------------------------------------- */

  #if DEBUG_SERIAL 
  Serial.printf ("[%05d] ", millis()); 
  Serial.printf ("Connecting to \'%s\'... \n", WifiSSID);
  #endif

  // Start WiFi connection
  WiFi.begin (WifiSSID, WifiPsw);
  
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

  // Set DS1390 trickle charger mode (250 ohms with one series diode)
  RTC.setTrickleChargerMode (DS1390_TCH_250_D); 
    
  // Delay for DS1390 boot (mandatory)
  delay (200);

  #if DEBUG_SERIAL 
  Serial.printf ("[%05d] ", millis());
  Serial.println("Done.");
  #endif  

  // Check if memory was lost recently
  RTCDataValid = RTC.getValidation ();

  // Unreliable RTC data - Proceed anyway
  if (!RTCDataValid)
  {
    #if DEBUG_SERIAL  
    Serial.printf ("[%05d] ", millis());
    Serial.println ("RTC memory content was recently lost! Please update date and time values.");  
    #endif         
  }
  
  // RTC data is valid
  else
  {
    #if DEBUG_SERIAL
    Serial.printf ("[%05d] ", millis());
    Serial.println ("RTC memory content is valid.");
    #endif      
  }

  /* ----------------------------------------------------------------------------------------- */  

  // LED on
  digitalWrite (PIN_LED, LOW);

  // Calculate battery voltage
  BatteryVoltage = analogRead(PIN_ADC)*ADC_GAIN;   

  // Get current time
  EpochTime = RTC.getDateTimeEpoch (TIMEZONE);
  
  // Get DS172 temperature
  SensorTemperature = TS.getTemperature();

  // LED off
  digitalWrite (PIN_LED, HIGH);
    
  #if DEBUG_SERIAL  
  // Save values to formatted buffer
  snprintf (Buffer, sizeof(Buffer), "Epoch: %d, Vbat: %.4f V, Temp: %.4f C", 
    EpochTime, BatteryVoltage, SensorTemperature);   
   
  Serial.printf ("[%05d] ", millis());      
  Serial.println(Buffer);
  #endif    

  /* ----------------------------------------------------------------------------------------- */  

  #if DEBUG_SERIAL   
  Serial.printf ("[%05d] ", millis());      
  Serial.println("Waiting for WiFi to connect...");
  #endif  

  // Check connection status
  while(WiFi.status() != WL_CONNECTED)
  {
    // Connection timeout exceded
    if (ConnectionCounter >= (WIFI_TIMEOUT * 4))
    {    
      // Online log not available
      LogToWifi = false;

      #if DEBUG_SERIAL   
      Serial.printf ("[%05d] ", millis());      
      Serial.println("WiFi connection timed-out!");
      #endif  

      // Exit while loop
      break;
    }

    // Increase timeout counter
    else
    {
      // 1 second delay
      delay(250);
  
      // Increase counter
      ConnectionCounter++;    
    }
  }

  // WiFi connected
  if (WiFi.status() == WL_CONNECTED)
  {
    // Online log available
    LogToWifi = true;
       
    #if DEBUG_SERIAL
    Serial.printf ("[%05d] ", millis());  
    Serial.println("WiFi connected.");
    #endif
  }

  /* ----------------------------------------------------------------------------------------- */  

  // Post data if WiFi is connected
  if (LogToWifi)
  {
    #if DEBUG_SERIAL
    Serial.printf ("[%05d] ", millis());
    Serial.printf ("Connecting to \'%s\'... \n", LogHostUrl);
    #endif
  
    // Start HTTP client connection to host
    LogClient.begin ((String)LogHostUrl);
  
    // Define request timeout
    LogClient.setTimeout(5000);
  
    // Define request content type - Required to perform posts
    LogClient.addHeader("Content-Type", "application/x-www-form-urlencoded");
  
    // Delay to establish connection
    delay (100);
    
    #if DEBUG_SERIAL
    Serial.printf ("[%05d] ", millis());  
    Serial.println("Done.");   
    #endif
   
    // Prepare log string
    snprintf (Buffer, sizeof(Buffer), "id=%s&log=Epo=%010dVbt=%.4fTmp=%.4fSts=%s", 
        LogScriptID, EpochTime, BatteryVoltage, SensorTemperature, RTCDataValid ? "Valid" : "Invalid");     
  
    #if DEBUG_SERIAL
    Serial.printf ("[%05d] ", millis());  
    Serial.printf("POST payload: %s \n", Buffer);  
    Serial.printf ("[%05d] ", millis());  
    Serial.println ("Posting...");  
    #endif
  
    // HTTP post request to host and get response code
    short PostResponseCode = LogClient.POST(Buffer); 
    
    // Close client connection
    LogClient.end();

    // Post was successful
    if (PostResponseCode != -1)
    {
      LogWifiSuccess = true;

      #if DEBUG_SERIAL 
      Serial.printf ("[%05d] ", millis());   
      Serial.println ("Done.");
      #endif         
    }

    // Post failed
    else
    {
      LogWifiSuccess = false;
      
      #if DEBUG_SERIAL 
      Serial.printf ("[%05d] ", millis());   
      Serial.println ("Failed to post data!");
      #endif 
    } 

    #if DEBUG_SERIAL 
    Serial.printf ("[%05d] ", millis());   
    Serial.printf ("Response code: %d \n", PostResponseCode);
    #endif          
  }

  /* ----------------------------------------------------------------------------------------- */

  // Save data to SD card if available
  if (LogToSD)
  {    
    // Open the log file
    LogFile = SD.open(LogFilename, FILE_WRITE);
  
    // If the file opened okay, write to it
    if (LogFile) 
    {
      #if DEBUG_SERIAL    
      Serial.printf ("[%05d] ", millis());
      Serial.printf ("Writing to \'%s\'... \n", LogFilename);
      #endif    
  
      // Write to file
      LogFile.println(Buffer);
      
      // Close the file
      LogFile.close();

      // Write was successful
      LogSDSuccess = true;
           
      #if DEBUG_SERIAL     
      Serial.printf ("[%05d] ", millis());
      Serial.println("Done.");
      #endif            
    } 
    
    // Error opening the file
    else 
    {
      LogSDSuccess = false;
            
      #if DEBUG_SERIAL       
      Serial.printf ("[%05d] ", millis());
      Serial.printf ("Error opening \'%s\'! \n", LogFilename);
      #endif
    }
  }

  /* ----------------------------------------------------------------------------------------- */ 

  if (LogWifiSuccess && LogSDSuccess)
  {
    #if DEBUG_SERIAL       
    Serial.printf ("[%05d] ", millis());
    Serial.println ("Data was saved online and in the SD card.");
    #endif      
  }

  else if (LogWifiSuccess)
  {
    #if DEBUG_SERIAL       
    Serial.printf ("[%05d] ", millis());
    Serial.println ("Data was saved online only.");
    #endif      
  }

  else if (LogSDSuccess)
  {
    #if DEBUG_SERIAL       
    Serial.printf ("[%05d] ", millis());
    Serial.println ("Data was saved in the SD card only.");
    #endif      
  }  

  else
  {
    #if DEBUG_SERIAL       
    Serial.printf ("[%05d] ", millis());
    Serial.println ("Data was not saved!");
    #endif      
  }          
  
  /* ----------------------------------------------------------------------------------------- */   

  // Battery voltage is low
  if (BatteryVoltage < 3)
  {
    #if DEBUG_SERIAL 
    Serial.printf ("[%05d] ", millis());
    Serial.println ("Low battery!");
    Serial.printf ("[%05d] ", millis());
    Serial.println ("Shutting down...");
    #endif  

    // Flash LED to inform user
    FlashLED ();
    
    // Enter deep sleep forever
    ESP.deepSleep(0);  
  }

  else
  {
    #if DEBUG_SERIAL
    Serial.printf ("[%05d] ", millis());    
    Serial.printf("Running again in %d seconds... \n", TIME_SLEEP_SAMPLES);
    #endif
    
    // Enter deep sleep
    ESP.deepSleep(TIME_SLEEP_SAMPLES*1e6);     
  }
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
// Name:        SDCardPresent
// Description: Verify if the SD card is present by checking the status of Card Detect pin
// Arguments:   None
// Returns:     true if card is present or false
/* ------------------------------------------------------------------------------------------- */

bool SDCardPresent ()
{
  return (!digitalRead(PIN_SD_CD));
}

/* ------------------------------------------------------------------------------------------- */
// Name:        RunOnError
// Description: This function is called when an error occur
// Arguments:   None
// Returns:     None
/* ------------------------------------------------------------------------------------------- */

void RunOnError () 
{
#if DEBUG_SERIAL 
  Serial.printf ("[%05d] ", millis());  
  Serial.printf ("Going to sleep for %d seconds... \n", TIME_SLEEP_ERROR);
#endif     

  // Flash LED to inform user
  FlashLED ();
  
  // Enter deep sleep
  ESP.deepSleep(TIME_SLEEP_ERROR*1e6);    
}

/* ------------------------------------------------------------------------------------------- */
// Name:        FlashLED
// Description: Flashes the onboard LED
// Arguments:   None
// Returns:     None
/* ------------------------------------------------------------------------------------------- */

void FlashLED ()
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
// End of code
/* ------------------------------------------------------------------------------------------- */
