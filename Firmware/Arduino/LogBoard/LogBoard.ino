/* ------------------------------------------------------------------------------------------- */
// LogBoard - Data logger firwmare for the ESP8266 LogBoard
// Version: 1.1
// Author:  Renan R. Duarte
// E-mail:  duarte.renan@hotmail.com
// Date:    October 18, 2019
//
// Notes:   Sending data to Google Sheets (via Google Script) could be done directly using the
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

// ESP8266 UDP related functions
#include <WiFiUdp.h>

// NTP client related functions
#include <NTPClient.h>

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
#define ADC_GAIN                0.005343342

/* ------------------------------------------------------------------------------------------- */
// Software default constants
/* ------------------------------------------------------------------------------------------- */

// Default configuration file
const PROGMEM char* FilenameNewConfig = "NewConfig.txt";

// Default firmware file
const PROGMEM char* FilenameNewFirmware = "NewFirmware.bin";

// NTP server - Result is given by server closest to you, so it's usually in your timezone
const PROGMEM char* NTPServerURL = "0.br.pool.ntp.org";  // Brazilian server

/* ------------------------------------------------------------------------------------------- */
// Software defines
/* ------------------------------------------------------------------------------------------- */

// Code debug with serial port
#define DEBUG_SERIAL            true

// Time to sleep if an error occur loading settings from EEPROM (seconds)
#define ERROR_SLEEP_INTERVAL    60

// WiFi connection timeout (milliseconds)
#define WIFI_TIMEOUT            5000

// Battery voltage cutoff voltage (volts)
#define VBAT_LOW                3

// EEPROM addresses
#define ADDR_LOG_FILENAME       0x000 // 64 bytes (63c + '\n')
#define ADDR_LOG_INTERVAL       0x040 // 2 bytes (MSB + LSB)
#define ADDR_LOG_WAIT_TIME      0x042 // 2 bytes (MSB + LSB) 
#define ADDR_LOG_TIMEZONE       0x044 // 2 bytes (MSB + LSB)
#define ADDR_WIFI_ENABLED       0x046 // 1 byte
#define ADDR_WIFI_SYNC          0x047 // 1 byte
#define ADDR_WIFI_SSID          0x048 // 32 bytes (31c + '\n')
#define ADDR_WIFI_PSW           0x068 // 32 bytes (31c + '\n')
#define ADDR_WIFI_HOSTURL       0x088 // 64 bytes (63c + '\n')
#define ADDR_WIFI_SCRIPTID      0x0C8 // 64 bytes (63c + '\n')
#define ADDR_CHECKSUM           0x108 // 4 bytes
#define ADDR_RTC_PENDING        0x10C // 1 byte
#define ADDR_SYNC_PENDING       0x10D // 1 byte

// EEPROM total bytes
#define EEPROM_SIZE             0x10E // 270 bytes

/* ------------------------------------------------------------------------------------------- */
// Global variables
/* ------------------------------------------------------------------------------------------- */

// Configuration variables
struct StructConfig
{
  char LogFilename[64] = {0};         // SD card log file       
  unsigned int LogInterval = 1;       // Time between samples (10 to 3600 seconds)
  unsigned int LogWaitTime = 1;       // Time to wait after on error (1 to 3600 seconds)
  int LogTimezone = 0;                // Timezone (-12 to +12, 0 = GMT)
  unsigned char WifiEnabled = 0;      // Use WiFi functions
  unsigned char WifiSyncEnable = 0;   // Sync data stored in SD card to Google Sheets
  char WifiSSID[32] = {0};            // WiFi SSID
  char WifiPsw[32] = {0};             // WiFi password
  char WifiHostURL[64] = {0};         // URL of the HTTP->HTTPs redirect host
  char WifiScriptID[64] = {0};        // Google Script ID
} Config, NewConfig;

// Sampled data
struct StructSample
{
  float Temperature = 0;              // DS1722 temperature
  float BatteryVoltage = 0;           // ADC voltage
  unsigned long RTCEpoch = 0;         // RTC date and time - Epoch format
  bool RTCValid = false;              // RTC memory was not lost. Data is reliable
} Sample;

// Code flags
struct StructFlag
{
  bool SDAvailable = false;           // SD is installed and accessible
  bool WifiAvailable = false;         // WiFi is connected
  bool NewFirmwareSuccess = false;    // New firmware was correctly loaded
  bool NewConfigSuccess = false;      // New congiguration was correctly loaded
  bool PostSuccess = false;           // Sample was posted to Google Sheets
  bool SDSuccess = false;             // Sample was saved in SD card
  bool PendingReboot = false;         // Reboot is pending
  bool PendingRTCUpdate = false;      // RTC update is pending
  bool PendingWifiSync = false;       // WiFi sync is pending
} Flag;

// Code times
struct StructTime
{
  unsigned int RequestTS = 0;         // Temperature conversion request time
  unsigned int RequestWifi = 0;       // WiFi connection request time
} Time;

// Log buffer
char Buffer[200] = {0};

// HTTP post response code
short PostResponseCode = 0;

/* ------------------------------------------------------------------------------------------- */
// Constructors
/* ------------------------------------------------------------------------------------------- */

// Temperature sensor
DS1722 TS (PIN_TS_CS);

// Real time clock
DS1390 RTC (PIN_RTC_CS);

// SD card
SdFat SDCard;

// New configuration file
File FileNewConfig;

// New firmware file
File FileNewFirmware;

// Log file
File FileLog;

// HTTP client
HTTPClient LogClient;

// UDP
WiFiUDP UDP;

// NTP client
NTPClient NTP(UDP, NTPServerURL);

/* ------------------------------------------------------------------------------------------- */
// Name:        setup
// Description: This function runs on boot
// Arguments:   None
// Returns:     None
/* ------------------------------------------------------------------------------------------- */

void setup()
{
  /* ----------------------------------------------------------------------------------------- */
  // Init Serial
  /* ----------------------------------------------------------------------------------------- */
  
  #if DEBUG_SERIAL
  Serial.begin(74880);
  while (!Serial);
  #endif

  /* ----------------------------------------------------------------------------------------- */
  // Init GPIO
  /* ----------------------------------------------------------------------------------------- */

  #if DEBUG_SERIAL
  Serial.printf ("[%05d] Initializing GPIOs... \n", millis());
  #endif
    
  // Set LED pin as digital output
  pinMode(PIN_LED, OUTPUT);

  // LED off
  digitalWrite (PIN_LED, HIGH);

  // Set Card Detect pin as input with pullup
  pinMode(PIN_SD_CD, INPUT_PULLUP);  

  #if DEBUG_SERIAL
  Serial.printf ("[%05d] Done. \n", millis());  
  #endif  

  /* ----------------------------------------------------------------------------------------- */
  // Check Vbat
  /* ----------------------------------------------------------------------------------------- */  

   // Get battery voltage
  Sample.BatteryVoltage = analogRead(PIN_ADC) * ADC_GAIN; 

  // Jump to loop if battery voltage is too low
  if (Sample.BatteryVoltage < VBAT_LOW)
    return;
  
  /* ----------------------------------------------------------------------------------------- */
  // Init EEPROM
  /* ----------------------------------------------------------------------------------------- */
  
  #if DEBUG_SERIAL
  Serial.printf ("[%05d] Initializing EEPROM... \n", millis());  
  #endif

  // Init EEPROM
  EEPROM.begin (EEPROM_SIZE);

  #if DEBUG_SERIAL
  Serial.printf ("[%05d] Done. \n", millis());  
  #endif

  /* ----------------------------------------------------------------------------------------- */
  // Init SD card
  /* ----------------------------------------------------------------------------------------- */

  #if DEBUG_SERIAL
  Serial.printf ("[%05d] Checking SD card... \n", millis());  
  #endif
    
  // SD card was detected
  if (!digitalRead (PIN_SD_CD))
  {
    #if DEBUG_SERIAL
    Serial.printf ("[%05d] SD card detected. \n", millis()); 
    #endif   

    // Initialize SD card
    if (SDCard.begin(PIN_SD_CS))
    {
      // Set flag
      Flag.SDAvailable = true;  

      #if DEBUG_SERIAL
      Serial.printf ("[%05d] Card initialized. Size: %d MB. \n", millis(), SDCard.card()->cardSize()/2048);   
      #endif
    }

    // Fails to initialize SD card
    else
    {
      // Clear flag
      Flag.SDAvailable = false; 

      #if DEBUG_SERIAL
      Serial.printf ("[%05d] Error initializing SD card! \n", millis());
      #endif                
    }
  }
  
  // SD card was not detected
  else
  {
    // Clear flag
    Flag.SDAvailable = false;     
    
    #if DEBUG_SERIAL
    Serial.printf ("[%05d] SD card not detected! \n", millis());
    #endif
  }

  /* ----------------------------------------------------------------------------------------- */
  // Firmware update
  /* ----------------------------------------------------------------------------------------- */  

  // SD card is was initialized
  if (Flag.SDAvailable)
  {
     // Try to open new firmware file
    FileNewFirmware = SDCard.open (FilenameNewFirmware, FILE_READ);   

    // Firmware file was found and opened
    if (FileNewFirmware)
    {
      // LED on
      digitalWrite (PIN_LED, LOW);    
      
      #if DEBUG_SERIAL
      Serial.printf ("[%05d] New firmware file detected. Updating... \n", millis());
      #endif  

      // Start update with max available SketchSpace
      if (Update.begin(((ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000), U_FLASH))
      {
        // Read all bytes        
        while (FileNewFirmware.available())
        {
          unsigned char Buffer[128];
          FileNewFirmware.read((unsigned char *)Buffer, 128);
          Update.write(Buffer, sizeof(Buffer));
        }
      }   
      
      // Update failed to start with max available SketchSpace
      else
      {
        #if DEBUG_SERIAL
        Serial.printf ("[%05d] Invalid firmware file! \n", millis());
        #endif

        // Jump to loop
        return;
      }

      // Check if update was successful
      Flag.NewFirmwareSuccess = Update.end (true);      

      // Close the firmware file
      FileNewFirmware.close();

      // LED off
      digitalWrite (PIN_LED, HIGH);
      
      // Update was successful
      if (Flag.NewFirmwareSuccess)
      {
        #if DEBUG_SERIAL
        Serial.printf ("[%05d] Done. \n", millis());
        #endif
                
        #if DEBUG_SERIAL
        Serial.printf ("[%05d] Removing firmware file... \n", millis());
        #endif
        
        // Remove firmware file
        if (SDCard.remove(FilenameNewFirmware))
        {
          #if DEBUG_SERIAL
          Serial.printf ("[%05d] Done. \n", millis());  
          #endif             
        }

        // Error removing firmware file
        else
        {
          #if DEBUG_SERIAL
          Serial.printf ("[%05d] Error removing firmware file! \n", millis());
          #endif          
        }

        // Set RTCPending flag to update RTC time - Saved in EEPROM in case update fails in this run
        Flag.PendingRTCUpdate = true;
        setRTCPending (Flag.PendingRTCUpdate);   

        // Set reboot pending flag
        Flag.PendingReboot = true;        
      }

      // Error updating
      else
      {
        #if DEBUG_SERIAL
        Serial.printf ("[%05d] Error updating! \n", millis());
        #endif

        // Jump to loop
        return;
      }                              
    }
    
    // Firmware file not found or failed to open
    else
    {
      #if DEBUG_SERIAL
      Serial.printf ("[%05d] New firmware not detected. \n", millis());
      #endif
    }    
  }

  /* ----------------------------------------------------------------------------------------- */
  // Config update
  /* ----------------------------------------------------------------------------------------- */  

  // SD card is was initialized
  if (Flag.SDAvailable)
  {
     // Try to open new config file
    FileNewConfig = SDCard.open (FilenameNewConfig, FILE_READ);   

    // Config file was found and opened
    if (FileNewConfig)
    {
      // LED on
      digitalWrite (PIN_LED, LOW);    
      
      #if DEBUG_SERIAL
      Serial.printf ("[%05d] New configuration file detected. Updating... \n", millis());
      #endif  

      // Check if all parameters are correct in the file
      Flag.NewConfigSuccess = getConfigSD (NewConfig);
      
      // Close the config file
      FileNewConfig.close();

      // Parameters were correct
      if (Flag.NewConfigSuccess)
      {
        // Transfer NewConfig to EEPROM
        setConfigEEPROM (NewConfig);
      }
      
      // LED off
      digitalWrite (PIN_LED, HIGH);
      
      // Update was successful
      if (Flag.NewConfigSuccess)
      {                
        #if DEBUG_SERIAL
        Serial.printf ("[%05d] Done. \n", millis());
        Serial.printf ("[%05d] New configuration: \n", millis());
        Serial.printf ("[%05d] LogFilename: \'%s\' \n", millis(), NewConfig.LogFilename);
        Serial.printf ("[%05d] LogInterval: %d \n", millis(), NewConfig.LogInterval);
        Serial.printf ("[%05d] LogWaitTime: %d \n", millis(), NewConfig.LogWaitTime);       
        Serial.printf ("[%05d] LogTimezone: %d \n", millis(), NewConfig.LogTimezone);
        Serial.printf ("[%05d] WifiEnabled: %d \n", millis(), NewConfig.WifiEnabled);
        Serial.printf ("[%05d] WifiSyncEnable: %d \n", millis(), NewConfig.WifiSyncEnable);
        Serial.printf ("[%05d] WifiSSID: \'%s\' \n", millis(), NewConfig.WifiSSID);
        Serial.printf ("[%05d] WifiPsw: \'%s\' \n", millis(), NewConfig.WifiPsw);       
        Serial.printf ("[%05d] WifiHostURL: \'%s\' \n", millis(), NewConfig.WifiHostURL);
        Serial.printf ("[%05d] WifiScriptID: \'%s\' \n", millis(), NewConfig.WifiScriptID);
        #endif
                
        #if DEBUG_SERIAL
        Serial.printf ("[%05d] Removing configuration file... \n", millis());
        #endif
        
        // Remove configuration file
        if (SDCard.remove(FilenameNewConfig))
        {
          #if DEBUG_SERIAL
          Serial.printf ("[%05d] Done. \n", millis());  
          #endif             
        }

        // Error removing configuration file
        else
        {
          #if DEBUG_SERIAL
          Serial.printf ("[%05d] Error removing configuration file! \n", millis());
          #endif          
        }

        // Set RTCPending flag to update RTC time - Saved in EEPROM in case update fails in this run
        Flag.PendingRTCUpdate = true;
        setRTCPending (Flag.PendingRTCUpdate);

        // Set reboot pending flag
        Flag.PendingReboot = true;
      }

      // Error updating config
      else
      {
        #if DEBUG_SERIAL
        Serial.printf ("[%05d] Error updating configuration! \n", millis());
        #endif

        // Jump to loop
        return;
      }                              
    }
    
    // Config file not found or failed to open
    else
    {
      #if DEBUG_SERIAL
      Serial.printf ("[%05d] New configuration file not detected. \n", millis());
      #endif
    }    
  }

  /* ----------------------------------------------------------------------------------------- */
  // Reboot if new firmware or config was applied
  /* ----------------------------------------------------------------------------------------- */  

  if (Flag.PendingReboot)
  {
    #if DEBUG_SERIAL
    Serial.printf ("[%05d] Rebooting... \n", millis());
    #endif
    
    // Reboot using GPIO16 (Tied to RST)
    delay (250);
    pinMode(16, OUTPUT);
    digitalWrite(16, LOW);
  }

  /* ----------------------------------------------------------------------------------------- */
  // Get configuration from EEPROM
  /* ----------------------------------------------------------------------------------------- */   

  #if DEBUG_SERIAL
  Serial.printf ("[%05d] Loading configuration from EEPROM... \n", millis());
  #endif
  
  // Configuration was correctly loaded from EEPROM
  if (getConfigEEPROM (Config))
  {
    #if DEBUG_SERIAL
    Serial.printf ("[%05d] Done. \n", millis());
    Serial.printf ("[%05d] Configuration: \n", millis());
    Serial.printf ("[%05d] LogFilename: \'%s\' \n", millis(), Config.LogFilename);
    Serial.printf ("[%05d] LogInterval: %d \n", millis(), Config.LogInterval);
    Serial.printf ("[%05d] LogWaitTime: %d \n", millis(), Config.LogWaitTime);       
    Serial.printf ("[%05d] LogTimezone: %d \n", millis(), Config.LogTimezone);
    Serial.printf ("[%05d] WifiEnabled: %d \n", millis(), Config.WifiEnabled);
    Serial.printf ("[%05d] WifiSyncEnable: %d \n", millis(), Config.WifiSyncEnable);
    Serial.printf ("[%05d] WifiSSID: \'%s\' \n", millis(), Config.WifiSSID);
    Serial.printf ("[%05d] WifiPsw: \'%s\' \n", millis(), Config.WifiPsw);       
    Serial.printf ("[%05d] WifiHostURL: \'%s\' \n", millis(), Config.WifiHostURL);
    Serial.printf ("[%05d] WifiScriptID: \'%s\' \n", millis(), Config.WifiScriptID);
    #endif  
  }

  // An error occur loading configuration from EEPROM
  else
  {
    #if DEBUG_SERIAL
    Serial.printf ("[%05d] Error getting configuration from EEPROM! \n", millis());  
    Serial.printf ("[%05d] Trying again in %d seconds \n", millis(), ERROR_SLEEP_INTERVAL);  
    #endif         

    // Enter deep sleep - Account for spent time
    ESP.deepSleep(ERROR_SLEEP_INTERVAL*1e6 - micros());
  }
  
  /* ----------------------------------------------------------------------------------------- */
  // Check if data logging is possible
  /* ----------------------------------------------------------------------------------------- */

  // SD card is not availabe and WiFi is disabled
  if (!Flag.SDAvailable && !Config.WifiEnabled)
  {
    #if DEBUG_SERIAL
    Serial.printf ("[%05d] Imposible to save data! No SD card or WiFi are available! \n", millis());  
    Serial.printf ("[%05d] Trying again in %d seconds \n", millis(), ERROR_SLEEP_INTERVAL);  
    #endif         

    // Enter deep sleep
    ESP.deepSleep(ERROR_SLEEP_INTERVAL * 1e6);        
  }

  /* ----------------------------------------------------------------------------------------- */
  // Init DS1722 temperature sensor
  /* ----------------------------------------------------------------------------------------- */  
  
  #if DEBUG_SERIAL
  Serial.printf ("[%05d] Configuring DS1722 temperature sensor... \n", millis()); 
  Serial.printf ("[%05d] %s library v%s \n", millis(), DS1722_CODE_NAME, DS1722_CODE_VERSION); 
  #endif

  // Set DS1722 conversion mode
  TS.setMode (DS1722_MODE_ONESHOT);

  // Set DS1722 resolution
  TS.setResolution (12);

  #if DEBUG_SERIAL
  Serial.printf ("[%05d] Requesting temperature conversion... \n", millis());  
  #endif
  
  // Request a temperature conversion - Will be ready in 1.2s @ 12bits
  TS.requestConversion ();

  // Get request time
  Time.RequestTS = millis();  

  #if DEBUG_SERIAL
  Serial.printf ("[%05d] Done. \n", millis());  
  #endif

  /* ----------------------------------------------------------------------------------------- */
  // Init DS390 real time clock
  /* ----------------------------------------------------------------------------------------- */  

  #if DEBUG_SERIAL
  Serial.printf ("[%05d] Configuring DS1390 RTC... \n", millis()); 
  Serial.printf ("[%05d] %s library v%s \n", millis(), DS1390_CODE_NAME, DS1390_CODE_VERSION); 
  #endif

  // Check if memory was lost recently
  Sample.RTCValid = RTC.getValidation ();  

  // Unreliable RTC data
  if (!Sample.RTCValid)
  {
    // Set RTCPending flag to update RTC time - Saved in EEPROM in case update fails in this run
    Flag.PendingRTCUpdate = true;
    setRTCPending (Flag.PendingRTCUpdate);   

    #if DEBUG_SERIAL
    Serial.printf ("[%05d] RTC memory content was recently lost! Timestamp is unreliable! \n", millis());
    #endif
  }

  // RTC data is valid
  else
  {
    #if DEBUG_SERIAL
    Serial.printf ("[%05d] RTC memory content is valid. \n", millis());
    #endif
  }

  // Set DS1390 trickle charger mode (250 ohms with one series diode)
  RTC.setTrickleChargerMode (DS1390_TCH_250_D);  

  #if DEBUG_SERIAL
  Serial.printf ("[%05d] Done. \n", millis());  
  #endif

  /* ----------------------------------------------------------------------------------------- */
  // Sample data
  /* ----------------------------------------------------------------------------------------- */ 

  #if DEBUG_SERIAL
  Serial.printf ("[%05d] Sampling data... \n", millis());  
  #endif  

  // Get current time
  Sample.RTCEpoch = RTC.getDateTimeEpoch (Config.LogTimezone);

  // Get battery voltage
  Sample.BatteryVoltage = analogRead(PIN_ADC) * ADC_GAIN;  

  // Wait if temperature sample is not finish
  while ((millis() - Time.RequestTS) < 1200)
    // Delay for a bit
    delay (100);

  // Get DS172 temperature
  Sample.Temperature = TS.getTemperature();

  #if DEBUG_SERIAL
  Serial.printf ("[%05d] Done. \n", millis());  
  Serial.printf ("[%05d] Epoch: %d (%s) \n", millis(), Sample.RTCEpoch, Sample.RTCValid ? "Reliable" : "Unreliable");  
  Serial.printf ("[%05d] Vbat: %.4f V \n", millis(), Sample.BatteryVoltage);
  Serial.printf ("[%05d] Temperature: %.4f C \n", millis(), Sample.Temperature);    
  #endif  

  /* ----------------------------------------------------------------------------------------- */
  // Start WiFi connection if necessary
  /* ----------------------------------------------------------------------------------------- */   

  // WiFi functions are enable
  if (Config.WifiEnabled)
  {
    #if DEBUG_SERIAL
    Serial.printf ("[%05d] Starting WiFi connection... \n", millis()); 
    #endif

    // Start WiFi connection
    WiFi.begin (Config.WifiSSID, Config.WifiPsw);

    // Get request time
    Time.RequestWifi = millis();

    #if DEBUG_SERIAL
    Serial.printf ("[%05d] Done. \n", millis());
    Serial.printf ("[%05d] Waiting for WiFi to connect... \n", millis());
    #endif
  
    // Wait for connection
    while ((WiFi.status() != WL_CONNECTED))
    {
      // Timeout time exceded
      if (((millis() - Time.RequestWifi) > WIFI_TIMEOUT))
        // Exit while loop
        break;

      // Delay for a bit
      delay (100);
    }
     
    // Timeout time exceded
    if (WiFi.status() != WL_CONNECTED)
    {
      #if DEBUG_SERIAL
      Serial.printf ("[%05d] WiFi connection timed-out! \n", millis());
      #endif

      // Reset flags
      Flag.WifiAvailable = false;      
      Flag.PostSuccess = false;
    }     

    // WiFi connected
    else
    {
      #if DEBUG_SERIAL
      Serial.printf ("[%05d] WiFi connected. \n", millis());
      #endif  

      // Set flag
      Flag.WifiAvailable = true; 
    }
  }

  // WiFi functions are disabled
  else
  {
    // Reset flags
    Flag.WifiAvailable = false;
    Flag.PostSuccess = false;
  }

  /* ----------------------------------------------------------------------------------------- */
  // Post data to Google Sheets if enabled
  /* ----------------------------------------------------------------------------------------- */   

  // WiFi is connected
  if (Flag.WifiAvailable)
  {
    #if DEBUG_SERIAL
    Serial.printf ("[%05d] Connecting to host... \n", millis());
    #endif

    // Start HTTP client connection to host
    LogClient.begin (Config.WifiHostURL);

    // Define request timeout
    LogClient.setTimeout(WIFI_TIMEOUT);

    // Define request content type - Required to perform posts
    LogClient.addHeader("Content-Type", "application/x-www-form-urlencoded");

    // Delay to establish connection
    delay (100);

    #if DEBUG_SERIAL
    Serial.printf ("[%05d] Done. \n", millis());
    #endif

    // Prepare log buffer
    snprintf (Buffer, sizeof(Buffer), "id=%s&log=EP:%d-TZ:%d-VB:%.4f-TS:%.4f-ST:%d-SD:%d",
              Config.WifiScriptID, Sample.RTCEpoch, Config.LogTimezone, Sample.BatteryVoltage, 
              Sample.Temperature, Sample.RTCValid, Flag.SDAvailable);

    #if DEBUG_SERIAL
    Serial.printf ("[%05d] Posting... \n", millis());
    #endif

    // Send post request to host and get response code
    PostResponseCode = LogClient.POST(Buffer);

    // Close client connection
    LogClient.end();

    // Post was successful
    if (PostResponseCode != -1)
    {
      // Set flag
      Flag.PostSuccess = true;

      #if DEBUG_SERIAL
      Serial.printf ("[%05d] Done. \n", millis());
      #endif
    }

    // Post failed
    else
    {
      // Reset flag
      Flag.PostSuccess = false;

      #if DEBUG_SERIAL
      Serial.printf ("[%05d] Failed to post data! \n", millis());
      #endif
    }

    #if DEBUG_SERIAL
    Serial.printf ("[%05d] Response code: %d \n", millis(), PostResponseCode);
    #endif
  }
  
  /* ----------------------------------------------------------------------------------------- */
  // Save data to SD card if available
  /* ----------------------------------------------------------------------------------------- */



















  /* ----------------------------------------------------------------------------------------- */

  #if DEBUG_SERIAL
  Serial.printf ("[%05d] Next update in %d seconds... \n", millis(), Config.LogInterval);
  #endif

  // Enter deep sleep - Account for spent time running
  ESP.deepSleep(Config.LogInterval*1e6 - micros());  
}

/* ------------------------------------------------------------------------------------------- */
// Name:        loop
// Description: This function runs in a loop after 'setup' finishes
// Arguments:   None
// Returns:     None
/* ------------------------------------------------------------------------------------------- */

void loop ()
{
  // Toggle LED
  digitalWrite(PIN_LED, !digitalRead(PIN_LED));

  // Delay
  delay (250);

  // Get battery voltage
  Sample.BatteryVoltage = analogRead(PIN_ADC) * ADC_GAIN; 
  
  // Enter deep sleep forever if voltage is too low
  if (Sample.BatteryVoltage < VBAT_LOW)
  {
    #if DEBUG_SERIAL
    Serial.printf ("[%05d] Low battery! Shuting down... \n", millis());
    #endif     

    ESP.deepSleep(0);
  }
}

/* ------------------------------------------------------------------------------------------- */
// Name:        getRTCPending
// Description: Gets the RTCPending flag value from EEPROM
// Arguments:   None
// Returns:     Flag value
/* ------------------------------------------------------------------------------------------- */

bool getRTCPending ()
{
  // Read upper and lower bytes of Interval
  return EEPROM.read (ADDR_RTC_PENDING);
}

/* ------------------------------------------------------------------------------------------- */
// Name:        setRTCPending
// Description: Writes the RTCPending flag value in EEPROM
// Arguments:   Flag value
// Returns:     None
/* ------------------------------------------------------------------------------------------- */

void setRTCPending (bool Value)
{
  // Write upper and lower bytes of Interval
  EEPROM.write (ADDR_RTC_PENDING, Value);

  // Apply changes in EEPROM
  EEPROM.commit();
}


/* ------------------------------------------------------------------------------------------- */
// Name:        getConfigSD
// Description: Gets all the configuration variables from SD card (FilenameNewConfig)
// Arguments:   Buffer - Pointer to the buffer struct
// Returns:     true if all parameters were parsed and false otherwise
/* ------------------------------------------------------------------------------------------- */

bool getConfigSD (StructConfig &Buffer)
{
  // Line buffer
  char BufferLine[100];

  // End of line index position
  unsigned char EOLPos = 0;

  // Checksum - 10 parameters are expected. Set 1 bit of Checksum for each
  unsigned int NewConfigChecksum = 0;

  // At this point, FileNewConfig is already open. Read all lines of file sequentially
  while ((EOLPos = FileNewConfig.fgets(BufferLine, sizeof(BufferLine))) > 0)
  {
    // Proccess only lines that contain variables
    if (BufferLine[0] == '$')
    {
      // Current line is $LogFilename
      if (strstr (BufferLine, "$LogFilename "))
      {
        // Start index to extract text
        unsigned char StartPos = 13;

        // Copy value to new config buffer from StartPos to '\n'
        strncpy (Buffer.LogFilename, (BufferLine + StartPos), (EOLPos - StartPos - 1));

        // Increment checksum
        NewConfigChecksum += 0x001;
      }

      // Current line is $LogInterval
      else if (strstr (BufferLine, "$LogInterval "))
      {
        // Start index to extract text
        unsigned char StartPos = 12;

        // Buffer
        char Value[5] = {0};

        // Copy value to buffer from StartPos to '\n'
        strncpy (Value, (BufferLine + StartPos), (EOLPos - StartPos - 1));

        // Convert value to number
        Buffer.LogInterval = atoi(Value);

        // Increment checksum if value is whithin limits
        if ((Buffer.LogInterval >= 10) && (Buffer.LogInterval <= 3600))
          NewConfigChecksum += 0x002;        
      }

      // Current line is $LogWaitTime
      else if (strstr (BufferLine, "$LogWaitTime "))
      {
        // Start index to extract text
        unsigned char StartPos = 12;

        // Buffer
        char Value[5] = {0};

        // Copy value to buffer from StartPos to '\n'
        strncpy (Value, (BufferLine + StartPos), (EOLPos - StartPos - 1));

        // Convert value to number
        Buffer.LogWaitTime = atoi(Value);

        // // Increment checksum if value is whithin limits
        if ((Buffer.LogWaitTime >= 1) && (Buffer.LogWaitTime <= 3600))          
          NewConfigChecksum += 0x004;        
      }

      // Current line is $LogTimezone
      else if (strstr (BufferLine, "$LogTimezone "))
      {
        // Start index to extract text
        unsigned char StartPos = 13;

        // Buffer
        char Value[4] = {0};

        // Copy value to buffer from StartPos to '\n'
        strncpy (Value, (BufferLine + StartPos), (EOLPos - StartPos - 1));

        // Convert value to number
        Buffer.LogTimezone = atoi(Value);

        // Increment checksum if value is whithin limits
        if ((Buffer.LogTimezone >= -12) && (Buffer.LogTimezone <= 12))
          NewConfigChecksum += 0x008;          
      }

      // Current line is $WifiEnabled
      else if (strstr (BufferLine, "$WifiEnabled "))
      {
        // Start index to extract text
        unsigned char StartPos = 13;

        // Buffer
        char Value[2] = {0};

        // Copy value to buffer from StartPos to '\n'
        strncpy (Value, (BufferLine + StartPos), (EOLPos - StartPos - 1));

        // Convert value to number
        Buffer.WifiEnabled = atoi(Value);

        // Increment checksum if value is whithin limits
        if ((Buffer.WifiEnabled == 0) || (Buffer.WifiEnabled == 1))
          NewConfigChecksum += 0x010;          
      }

      // Current line is $WifiSyncEnable
      else if (strstr (BufferLine, "$WifiSyncEnable "))
      {
        // Start index to extract text
        unsigned char StartPos = 16;

        // Buffer
        char Value[2] = {0};

        // Copy value to buffer from StartPos to '\n'
        strncpy (Value, (BufferLine + StartPos), (EOLPos - StartPos - 1));

        // Convert value to number
        Buffer.WifiSyncEnable = atoi(Value);

        // Increment checksum if value is whithin limits
        if ((Buffer.WifiSyncEnable == 0) || (Buffer.WifiSyncEnable == 1))
          NewConfigChecksum += 0x020;          
      }

      // Current line is $WifiSSID
      else if (strstr (BufferLine, "$WifiSSID "))
      {
        // Start index to extract text
        unsigned char StartPos = 10;

        // Copy value to buffer from StartPos to '\n'
        strncpy (Buffer.WifiSSID, (BufferLine + StartPos), (EOLPos - StartPos - 1));

        // Increment checksum
        NewConfigChecksum += 0x040;        
      }

      // Current line is $WifiPsw
      else if (strstr (BufferLine, "$WifiPsw "))
      {
        // Start index to extract text
        unsigned char StartPos = 9;

        // Copy value to buffer from StartPos to '\n'
        strncpy (Buffer.WifiPsw, (BufferLine + StartPos), (EOLPos - StartPos - 1));

        // Increment checksum
        NewConfigChecksum += 0x080;        
      }

      // Current line is $WifiHostURL
      else if (strstr (BufferLine, "$WifiHostURL "))
      {
        // Start index to extract text
        unsigned char StartPos = 13;

        // Copy value to buffer from StartPos to '\n'
        strncpy (Buffer.WifiHostURL, (BufferLine + StartPos), (EOLPos - StartPos - 1));

        // Increment checksum
        NewConfigChecksum += 0x100;           
      }

      // Current line is $WifiScriptID
      else if (strstr (BufferLine, "$WifiScriptID "))
      {
        // Start index to extract text
        unsigned char StartPos = 14;

        // Copy value to buffer from StartPos to '\n'
        strncpy (Buffer.WifiScriptID, (BufferLine + StartPos), (EOLPos - StartPos - 1));

        // Increment checksum
        NewConfigChecksum += 0x200;          
      }
    }
  }

  // Return true if all parameters were parsed
  if (NewConfigChecksum == 0x3FF)
    return true;
  else
    return false;
}

/* ------------------------------------------------------------------------------------------- */
// Name:        getConfigEEPROM
// Description: Gets all the configuration variables from EEPROM
// Arguments:   Buffer - Pointer to the buffer struct
// Returns:     true if all parameters were parsed and false otherwise
/* ------------------------------------------------------------------------------------------- */

bool getConfigEEPROM (StructConfig &Buffer)
{
  // Buffer index position for char arrays
  unsigned char Index = 0;
  
  // Address to be read
  unsigned int Addr = 0;

  // MSB and LSB buffers
  unsigned char MSB = 0;
  unsigned char LSB = 0;  

  // Checksum - Sum of all read bytes
  unsigned long Checksum = 0;  

  // Checksum - Stored in EEPROM
  unsigned long EEPROMChecksum = 0;

  /* ----------------------------------------------------------------------------------------- */
  
  // Get LogFilename
  Addr = ADDR_LOG_FILENAME;
  Index = 0;

  // Read data until carriage return or 64 read bytes
  while ((EEPROM.read(Addr)) != '\n' && (Index < 64))
  {
    // Save data to buffer  
    Buffer.LogFilename[Index] = EEPROM.read(Addr);

    // Add byte to checksum
    Checksum += Buffer.LogFilename[Index];

    // Increase index and address
    Index++;
    Addr++;
  }

  /* ----------------------------------------------------------------------------------------- */

  // Get LogInterval - Read upper and lower bytes
  MSB = EEPROM.read (ADDR_LOG_INTERVAL);
  LSB = EEPROM.read (ADDR_LOG_INTERVAL + 1);

  // Save to buffer
  Buffer.LogInterval = (MSB << 8) + LSB;

  // Add bytes to checksum
  Checksum += MSB;  
  Checksum += LSB;

  /* ----------------------------------------------------------------------------------------- */

  // Get LogWaitTime - Read upper and lower bytes
  MSB = EEPROM.read (ADDR_LOG_WAIT_TIME);
  LSB = EEPROM.read (ADDR_LOG_WAIT_TIME + 1);

  // Save to buffer
  Buffer.LogWaitTime = (MSB << 8) + LSB;

  // Add bytes to checksum
  Checksum += MSB;  
  Checksum += LSB;

  /* ----------------------------------------------------------------------------------------- */

  // Get LogTimezone - Read upper and lower bytes
  MSB = EEPROM.read (ADDR_LOG_TIMEZONE);
  LSB = EEPROM.read (ADDR_LOG_TIMEZONE + 1);

  // Save to buffer
  Buffer.LogTimezone = (short)((MSB << 8) + LSB);

  // Add bytes to checksum
  Checksum += MSB;  
  Checksum += LSB;

  /* ----------------------------------------------------------------------------------------- */

  // Get WifiEnabled
  Buffer.WifiEnabled = EEPROM.read (ADDR_WIFI_ENABLED);

  // Add byte to checksum
  Checksum += Buffer.WifiEnabled;  

  /* ----------------------------------------------------------------------------------------- */

  // Get WifiSyncEnable
  Buffer.WifiSyncEnable = EEPROM.read (ADDR_WIFI_SYNC);

  // Add byte to checksum
  Checksum += Buffer.WifiSyncEnable;  

  /* ----------------------------------------------------------------------------------------- */
  
  // Get WifiSSID
  Addr = ADDR_WIFI_SSID;
  Index = 0;

  // Read data until carriage return or 32 read bytes
  while ((EEPROM.read(Addr)) != '\n' && (Index < 32))
  {
    // Save data to buffer  
    Buffer.WifiSSID[Index] = EEPROM.read(Addr);

    // Add byte to checksum
    Checksum += Buffer.WifiSSID[Index];

    // Increase index and address
    Index++;
    Addr++;
  }

  /* ----------------------------------------------------------------------------------------- */
  
  // Get WifiPsw
  Addr = ADDR_WIFI_PSW;
  Index = 0;

  // Read data until carriage return or 32 read bytes
  while ((EEPROM.read(Addr)) != '\n' && (Index < 32))
  {
    // Save data to buffer  
    Buffer.WifiPsw[Index] = EEPROM.read(Addr);

    // Add byte to checksum
    Checksum += Buffer.WifiPsw[Index];

    // Increase index and address
    Index++;
    Addr++;
  }

  /* ----------------------------------------------------------------------------------------- */
  
  // Get WifiHostURL
  Addr = ADDR_WIFI_HOSTURL;
  Index = 0;

  // Read data until carriage return or 64 read bytes
  while ((EEPROM.read(Addr)) != '\n' && (Index < 64))
  {
    // Save data to buffer  
    Buffer.WifiHostURL[Index] = EEPROM.read(Addr);

    // Add byte to checksum
    Checksum += Buffer.WifiHostURL[Index];

    // Increase index and address
    Index++;
    Addr++;
  }

  /* ----------------------------------------------------------------------------------------- */
  
  // Get WifiScriptID
  Addr = ADDR_WIFI_SCRIPTID;
  Index = 0;

  // Read data until carriage return or 64 read bytes
  while ((EEPROM.read(Addr)) != '\n' && (Index < 64))
  {
    // Save data to buffer  
    Buffer.WifiScriptID[Index] = EEPROM.read(Addr);

    // Add byte to checksum
    Checksum += Buffer.WifiScriptID[Index];

    // Increase index and address
    Index++;
    Addr++;
  }  
  
  /* ----------------------------------------------------------------------------------------- */

  // Read EEPROM checksum - 4 bytes
  EEPROMChecksum += (EEPROM.read(ADDR_CHECKSUM) << 24);
  EEPROMChecksum += (EEPROM.read(ADDR_CHECKSUM + 1) << 16);
  EEPROMChecksum += (EEPROM.read(ADDR_CHECKSUM + 2) << 8);
  EEPROMChecksum += EEPROM.read(ADDR_CHECKSUM + 3);

  // Compare checksums
  if (Checksum == EEPROMChecksum)
    return true;
  else
    return false;
}

/* ------------------------------------------------------------------------------------------- */
// Name:        setConfigEEPROM
// Description: Sets all the configuration variables in EEPROM
// Arguments:   Buffer - Pointer to the data struct
// Returns:     None
/* ------------------------------------------------------------------------------------------- */

void setConfigEEPROM (StructConfig &Buffer)
{
  // Buffer index position for char arrays
  unsigned char Index = 0;
  
  // Address to be written
  unsigned int Addr = 0;

  // MSB and LSB buffers
  unsigned char MSB = 0;
  unsigned char LSB = 0;  
    
  // Checksum - Sum of all written bytes
  unsigned long Checksum = 0;  

  /* ----------------------------------------------------------------------------------------- */
  
  // Set LogFilename  
  Addr = ADDR_LOG_FILENAME;
  Index = 0;

  // Write all chars
  for (Index = 0; Index < strlen(Buffer.LogFilename); Index++)
  {
    // Send byte
    EEPROM.write(Addr, Buffer.LogFilename[Index]);

    // Add byte to checksum
    Checksum += Buffer.LogFilename[Index];
    
    // Increase address
    Addr++;
  }
  
  // Write carriage return
  EEPROM.write(Addr, '\n');

  /* ----------------------------------------------------------------------------------------- */

  // Set LogInterval
  MSB = Buffer.LogInterval >> 8;
  LSB = Buffer.LogInterval & 0xFF;

  // Write upper and lower bytes of Interval
  EEPROM.write (ADDR_LOG_INTERVAL, MSB);
  EEPROM.write (ADDR_LOG_INTERVAL + 1, LSB);

  // Add bytes to checksum
  Checksum += MSB;
  Checksum += LSB;

  /* ----------------------------------------------------------------------------------------- */

  // Set LogWaitTime
  MSB = Buffer.LogWaitTime >> 8;
  LSB = Buffer.LogWaitTime & 0xFF;

  // Write upper and lower bytes of Interval
  EEPROM.write (ADDR_LOG_WAIT_TIME, MSB);
  EEPROM.write (ADDR_LOG_WAIT_TIME + 1, LSB);

  // Add bytes to checksum
  Checksum += MSB;
  Checksum += LSB;

  /* ----------------------------------------------------------------------------------------- */

  // Set LogTimezone
  MSB = Buffer.LogTimezone >> 8;
  LSB = Buffer.LogTimezone & 0xFF;

  // Write upper and lower bytes of Interval
  EEPROM.write (ADDR_LOG_TIMEZONE, MSB);
  EEPROM.write (ADDR_LOG_TIMEZONE + 1, LSB);

  // Add bytes to checksum
  Checksum += MSB;
  Checksum += LSB;    

  /* ----------------------------------------------------------------------------------------- */

  // Set WifiEnabled
  EEPROM.write (ADDR_WIFI_ENABLED, Buffer.WifiEnabled);

  // Add byte to checksum
  Checksum += Buffer.WifiEnabled; 

  /* ----------------------------------------------------------------------------------------- */

  // Set WifiSyncEnable
  EEPROM.write (ADDR_WIFI_SYNC, Buffer.WifiSyncEnable);

  // Add byte to checksum
  Checksum += Buffer.WifiSyncEnable;   

  /* ----------------------------------------------------------------------------------------- */
  
  // Set WifiSSID  
  Addr = ADDR_WIFI_SSID;
  Index = 0;

  // Write all chars
  for (Index = 0; Index < strlen(Buffer.WifiSSID); Index++)  
  {
    // Send byte
    EEPROM.write(Addr, Buffer.WifiSSID[Index]);

    // Add byte to checksum
    Checksum += Buffer.WifiSSID[Index];
    
    // Increase address
    Addr++;
  }
  
  // Write carriage return
  EEPROM.write(Addr, '\n');

  /* ----------------------------------------------------------------------------------------- */
  
  // Set WifiPsw  
  Addr = ADDR_WIFI_PSW;
  Index = 0;

  // Write all chars
  for (Index = 0; Index < strlen(Buffer.WifiPsw); Index++) 
  {
    // Send byte
    EEPROM.write(Addr, Buffer.WifiPsw[Index]);

    // Add byte to checksum
    Checksum += Buffer.WifiPsw[Index];
    
    // Increase address
    Addr++;
  }
  
  // Write carriage return
  EEPROM.write(Addr, '\n'); 

  /* ----------------------------------------------------------------------------------------- */
  
  // Set WifiHostURL  
  Addr = ADDR_WIFI_HOSTURL;
  Index = 0;

  // Write all chars
  for (Index = 0; Index < strlen(Buffer.WifiHostURL); Index++) 
  {
    // Send byte
    EEPROM.write(Addr, Buffer.WifiHostURL[Index]);

    // Add byte to checksum
    Checksum += Buffer.WifiHostURL[Index];
    
    // Increase address
    Addr++;
  }
  
  // Write carriage return
  EEPROM.write(Addr, '\n');

  /* ----------------------------------------------------------------------------------------- */
  
  // Set WifiScriptID  
  Addr = ADDR_WIFI_SCRIPTID;
  Index = 0;

  // Write all chars
  for (Index = 0; Index < strlen(Buffer.WifiScriptID); Index++) 
  {
    // Send byte
    EEPROM.write(Addr, Buffer.WifiScriptID[Index]);

    // Add byte to checksum
    Checksum += Buffer.WifiScriptID[Index];
    
    // Increase address
    Addr++;
  }
  
  // Write carriage return
  EEPROM.write(Addr, '\n');        
  
  /* ----------------------------------------------------------------------------------------- */

  // Write checksum - 4 bytes
  EEPROM.write(ADDR_CHECKSUM, Checksum >> 24);
  EEPROM.write((ADDR_CHECKSUM + 1), ((Checksum >> 16) & 0xFF));
  EEPROM.write((ADDR_CHECKSUM + 2), ((Checksum >> 8) & 0xFF));
  EEPROM.write((ADDR_CHECKSUM + 3), (Checksum & 0xFF));
  
  /* ----------------------------------------------------------------------------------------- */  
  
  // Apply changes in EEPROM
  EEPROM.commit();
}

/* ------------------------------------------------------------------------------------------- */
// End of code
/* ------------------------------------------------------------------------------------------- */
