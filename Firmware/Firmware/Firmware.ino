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

// EEPROM module related functions
#include <EEPROM.h>

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

// WiFi connection timeout (seconds)
#define WIFI_TIMEOUT            5

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
const PROGMEM char* ConfigFilename = "NewConfig.csv";

// Configuration variables structure
struct StructConfig
{
  // Log file
  char Filename[64] = {0};
      
  // Wifi settings
  char WifiSSID[32] = {0};
  char WifiPsw[32] = {0};
  
  // Google script settings
  char HostURL[64] = {0};
  char ScriptID[64] = {0};
  
  // Time between samples (seconds) - Max 65535
  unsigned short SampleInterval = 0;
  
  // Deep sleep time if error (seconds) - Max 65535
  unsigned short ErrorInterval = 0;
  
  // Timezone (-12 to +12, 0 = GMT)
  int Timezone = 0;
} LogConfig;
   
// DS1722 temperature
float SensorTemperature = 0;

// ADC voltage
float BatteryVoltage = 0;

// Date and time - Epoch format
unsigned long EpochTime = 0;

// RTC data valid flag
bool RTCDataValid = false;
  
// Log flags
bool LogToSD = false;
bool LogToWifi = false;

// Log success flags
bool LogSDSuccess = false;
bool LogWifiSuccess = false;

// WiFi connection counter
unsigned char ConnectionCounter = 0;

// Log string
char Buffer[200] = {0};

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
  Serial.println("Initializing EEPROM...");
  #endif
  
  // Init EEPROM
  EEPROM.begin(ADDR_TIME_ERROR + 2);

  #if DEBUG_SERIAL 
  Serial.printf ("[%05d] ", millis());
  Serial.println("Done.");
  #endif  

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
    
    // Get configuration values from EEPROM
    EEPROMGetConfig (LogConfig);     
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

      // Get configuration values from EEPROM
      EEPROMGetConfig (LogConfig);              
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
        Serial.println("New configuration file not found.");     
        #endif                  
        
        // Get configuration values from EEPROM
        #if DEBUG_SERIAL     
        Serial.printf ("[%05d] ", millis());
        Serial.println("Getting last configuration from EEPROM..."); 
        #endif   

        // Get configuration values from EEPROM
        EEPROMGetConfig (LogConfig);             
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
  Serial.printf ("[%05d] Log filename: \'%s\' \n", millis(), LogConfig.Filename);
  Serial.printf ("[%05d] Log host: \'%s\' \n", millis(), LogConfig.HostURL);
  Serial.printf ("[%05d] Log script ID: \'%s\' \n", millis(), LogConfig.ScriptID);
  Serial.printf ("[%05d] Log timezone: %d GMT \n", millis(), LogConfig.Timezone);  
  Serial.printf ("[%05d] WiFi SSID: \'%s\' \n", millis(), LogConfig.WifiSSID);
  Serial.printf ("[%05d] WiFi password: \'%s\' \n", millis(), LogConfig.WifiPsw);
  Serial.printf ("[%05d] Sample interval: %d seconds \n", millis(), LogConfig.SampleInterval);
  Serial.printf ("[%05d] Wait on error: %d seconds \n", millis(), LogConfig.ErrorInterval);
  #endif  

  /* ----------------------------------------------------------------------------------------- */

  #if DEBUG_SERIAL 
  Serial.printf ("[%05d] ", millis()); 
  Serial.println ("Connecting to WiFi...");
  #endif

  // Start WiFi connection
  WiFi.begin (LogConfig.WifiSSID, LogConfig.WifiPsw);
  
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
  EpochTime = RTC.getDateTimeEpoch (LogConfig.Timezone);
  
  // Get DS172 temperature
  SensorTemperature = TS.getTemperature();

  // LED off
  digitalWrite (PIN_LED, HIGH);
    
  #if DEBUG_SERIAL  
  Serial.printf ("[%05d] Epoch: %d \n", millis(), EpochTime);
  Serial.printf ("[%05d] Vbat: %.4f V \n", millis(), BatteryVoltage);  
  Serial.printf ("[%05d] Temperature: %.4f C \n", millis(), SensorTemperature);          
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
    Serial.println ("Connecting to host...");
    #endif
  
    // Start HTTP client connection to host
    LogClient.begin ((String)LogConfig.HostURL);
  
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
    snprintf (Buffer, sizeof(Buffer), "id=%s&log=EP:%010d-VB:%.4f-TS:%.4f-ST:%d-SD:%d", 
        LogConfig.ScriptID, EpochTime, BatteryVoltage, SensorTemperature, RTCDataValid, LogToSD);     
  
    #if DEBUG_SERIAL
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
    LogFile = SD.open(LogConfig.Filename, FILE_WRITE);
  
    // If the file opened okay, write to it
    if (LogFile) 
    {
      #if DEBUG_SERIAL    
      Serial.printf ("[%05d] ", millis());
      Serial.println ("Writing to SD card...");
      #endif    

      // Prepare log string
      snprintf (Buffer, sizeof(Buffer), "%010d; %.4f;  %.4f; %d; %d", 
          EpochTime, BatteryVoltage, SensorTemperature, RTCDataValid, LogWifiSuccess);
         
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
      Serial.println ("Error opening the file!");
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
    Serial.println ("Data was saved only online.");
    #endif      
  }

  else if (LogSDSuccess)
  {
    #if DEBUG_SERIAL       
    Serial.printf ("[%05d] ", millis());
    Serial.println ("Data was saved only in the SD card.");
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
    Serial.printf("Running again in %d seconds... \n", LogConfig.SampleInterval);
    #endif
    
    // Enter deep sleep
    ESP.deepSleep(LogConfig.SampleInterval*1e6);     
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
  Serial.printf ("Going to sleep for %d seconds... \n", LogConfig.ErrorInterval);
#endif     

  // Flash LED to inform user
  FlashLED ();
  
  // Enter deep sleep
  ESP.deepSleep(LogConfig.ErrorInterval*1e6);    
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
// Name:        EEPROMGetConfig
// Description: Gets all the configuration variables from EEPROM
// Arguments:   Buffer - Pointer to the buffer struct
// Returns:     None
/* ------------------------------------------------------------------------------------------- */

void EEPROMGetConfig (StructConfig &Buffer)
{
  // Get each value from EEPROM
  EEPROMGetLogFilename (Buffer.Filename);
  EEPROMGetLogHost (Buffer.HostURL);
  EEPROMGetScriptID (Buffer.ScriptID);
  EEPROMGetWifiSSID (Buffer.WifiSSID);
  EEPROMGetWifiPsw (Buffer.WifiPsw);
  EEPROMGetTimezone (&Buffer.Timezone);
  EEPROMGetSampleInterval (&Buffer.SampleInterval);
  EEPROMGetErrorInterval (&Buffer.ErrorInterval); 
}

/* ------------------------------------------------------------------------------------------- */
// Name:        EEPROMSetConfig
// Description: Sets all the configuration variables inm EEPROM
// Arguments:   Buffer - Pointer to the buffer struct
// Returns:     None
/* ------------------------------------------------------------------------------------------- */

void EEPROMSetConfig (StructConfig &Buffer)
{
  // Set each value in EEPROM
  EEPROMSetLogFilename (Buffer.Filename);
  EEPROMSetLogHost (Buffer.HostURL);
  EEPROMSetScriptID (Buffer.ScriptID);
  EEPROMSetWifiSSID (Buffer.WifiSSID);
  EEPROMSetWifiPsw (Buffer.WifiPsw);
  EEPROMSetTimezone (Buffer.Timezone);
  EEPROMSetSampleInterval (Buffer.SampleInterval);
  EEPROMSetErrorInterval (Buffer.ErrorInterval); 
}

/* ------------------------------------------------------------------------------------------- */
// Name:        EEPROMGetLogFilename
// Description: Gets the log filename from EEPROM
// Arguments:   Buffer - Pointer to the buffer string (64 bytes)
// Returns:     None
/* ------------------------------------------------------------------------------------------- */

void EEPROMGetLogFilename (char *Buffer)
{
  unsigned char Index = 0;
  unsigned int Addr = ADDR_FILENAME;

  // Read all chars until carriage return or 64 read bytes
  while ((EEPROM.read(Addr)) != '\n' && (Index < 64))
    Buffer[Index++] = EEPROM.read(Addr++);
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
// Name:        EEPROMGetLogHost
// Description: Gets the host url from EEPROM
// Arguments:   Buffer - Pointer to the buffer string (64 bytes)
// Returns:     None
/* ------------------------------------------------------------------------------------------- */

void EEPROMGetLogHost (char *Buffer)
{
  unsigned char Index = 0;
  unsigned int Addr = ADDR_HOSTURL;

  // Read all chars until carriage return or 64 read bytes
  while ((EEPROM.read(Addr)) != '\n' && (Index < 64))
    Buffer[Index++] = EEPROM.read(Addr++);
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
// Name:        EEPROMGetScriptID
// Description: Gets Google script ID from EEPROM
// Arguments:   Buffer - Pointer to the buffer string (64 bytes)
// Returns:     None
/* ------------------------------------------------------------------------------------------- */

void EEPROMGetScriptID (char *Buffer)
{
  unsigned char Index = 0;
  unsigned int Addr = ADDR_SCRIPTID;

  // Read all chars until carriage return or 64 read bytes
  while ((EEPROM.read(Addr)) != '\n' && (Index < 64))
    Buffer[Index++] = EEPROM.read(Addr++);
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
// Name:        EEPROMGetWifiSSID
// Description: Gets the WiFi SSID from EEPROM
// Arguments:   Buffer - Pointer to the buffer string (32 bytes)
// Returns:     None
/* ------------------------------------------------------------------------------------------- */

void EEPROMGetWifiSSID (char *Buffer)
{
  unsigned char Index = 0;
  unsigned int Addr = ADDR_WIFI_SSID;

  // Read all chars until carriage return or 32 read bytes
  while ((EEPROM.read(Addr)) != '\n' && (Index < 32))
    Buffer[Index++] = EEPROM.read(Addr++);
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
// Name:        EEPROMGetWifiPsw
// Description: Gets the WiFi password from EEPROM
// Arguments:   Buffer - Pointer to the buffer string (32 bytes)
// Returns:     None
/* ------------------------------------------------------------------------------------------- */

void EEPROMGetWifiPsw (char *Buffer)
{
  unsigned char Index = 0;
  unsigned int Addr = ADDR_WIFI_PSW;

  // Read all chars until carriage return or 32 read bytes
  while ((EEPROM.read(Addr)) != '\n' && (Index < 32))
    Buffer[Index++] = EEPROM.read(Addr++);
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

/* ------------------------------------------------------------------------------------------- */
// Name:        EEPROMGetTimezone
// Description: Gets the Log timezone value from EEPROM
// Arguments:   Timezone - Pointer to the buffer variable (int)
// Returns:     None
/* ------------------------------------------------------------------------------------------- */

void EEPROMGetTimezone (int *Timezone)
{
  // Read upper and lower bytes of Timezone
  *Timezone = (short)((EEPROM.read (ADDR_TIMEZONE)) << 8) | (EEPROM.read (ADDR_TIMEZONE + 1));
}

/* ------------------------------------------------------------------------------------------- */
// Name:        EEPROMSetTimezone
// Description: Writes the log timezone value in EEPROM
// Arguments:   Timezone - Value to be written (int)
// Returns:     None
/* ------------------------------------------------------------------------------------------- */

void EEPROMSetTimezone (int Timezone)
{
  // Write upper and lower bytes of Timezone
  EEPROM.write (ADDR_TIMEZONE, Timezone >> 8);
  EEPROM.write (ADDR_TIMEZONE + 1, Timezone & 0xFF);

  // Apply changes in EEPROM
  EEPROM.commit();
}

/* ------------------------------------------------------------------------------------------- */
// Name:        EEPROMGetSampleInterval
// Description: Gets the Log sample interval value from EEPROM
// Arguments:   Interval - Pointer to the buffer variable (unsigned short)
// Returns:     None
/* ------------------------------------------------------------------------------------------- */

void EEPROMGetSampleInterval (unsigned short *Interval)
{
  // Read upper and lower bytes of Interval
  *Interval = ((EEPROM.read (ADDR_TIME_SAMPLE)) << 8) | (EEPROM.read (ADDR_TIME_SAMPLE + 1));
}

/* ------------------------------------------------------------------------------------------- */
// Name:        EEPROMSetSampleInterval
// Description: Writes the Log sample interval value in EEPROM
// Arguments:   Interval - Value to be written (unsigned short)
// Returns:     None
/* ------------------------------------------------------------------------------------------- */

void EEPROMSetSampleInterval (unsigned short Interval)
{
  // Write upper and lower bytes of Interval
  EEPROM.write (ADDR_TIME_SAMPLE, Interval >> 8);
  EEPROM.write (ADDR_TIME_SAMPLE + 1, Interval & 0xFF);

  // Apply changes in EEPROM
  EEPROM.commit();
}

/* ------------------------------------------------------------------------------------------- */
// Name:        EEPROMGetErrorInterval
// Description: Gets the log error interval value from EEPROM
// Arguments:   Interval - Pointer to the buffer variable (unsigned short)
// Returns:     None
/* ------------------------------------------------------------------------------------------- */

void EEPROMGetErrorInterval (unsigned short *Interval)
{
  // Read upper and lower bytes of Interval
  *Interval = ((EEPROM.read (ADDR_TIME_ERROR)) << 8) | (EEPROM.read (ADDR_TIME_ERROR + 1));
}

/* ------------------------------------------------------------------------------------------- */
// Name:        EEPROMSetErrorInterval
// Description: Writes the log error interval value in EEPROM
// Arguments:   Interval - Value to be written (unsigned int)
// Returns:     None
/* ------------------------------------------------------------------------------------------- */

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
