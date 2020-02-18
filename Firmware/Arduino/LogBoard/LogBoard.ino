/* ------------------------------------------------------------------------------------------- */
// LogBoard - Data logger firwmare for the ESP8266 LogBoard
// Version: 1.2
// Author:  Renan R. Duarte
// E-mail:  duarte.renan@hotmail.com
// Date:    February 18, 2020
//
// Notes:   Sending data to Google Sheets (via Google Script) could be done directly using the
//          WiFiClientSecure library. However, HTTPS requests require more heap memory than
//          the device has available in this code when the HTTPS connection is made.
//          As a solution, this code sends an HTTP request to a server where a HTTPs redirect
//          is performed using cURL.
//
// TODO: 	If log file is large, sync may take a lot of time. Must find a way to keep
//          track of where to start the sync.
//			  Include "device name" in config to use multiple devices with one online sheet
//			
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

// BME280 related functions
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>

/* ------------------------------------------------------------------------------------------- */
// Hardware defines
/* ------------------------------------------------------------------------------------------- */

// Peripheral pins
#define PIN_SD_CS               9
#define PIN_SD_CD               4
#define PIN_RTC_CS              10
#define PIN_TS_CS               5
#define PIN_BME_CS              15
#define PIN_LED                 2
#define PIN_ADC                 A0

// ADC gain
#define ADC_GAIN                0.005343342

/* ------------------------------------------------------------------------------------------- */
// Software default constants
/* ------------------------------------------------------------------------------------------- */

// Default settings filename
const PROGMEM char* FilenameNewSettings = "NewSettings.txt";

// Default firmware filename
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
#define ADDR_WIFI_REDIRECTURL   0x088 // 64 bytes (63c + '\n')
#define ADDR_WIFI_GSCRIPTURL    0x0C8 // 128 bytes (127c + '\n')
#define ADDR_CHECKSUM           0x148 // 4 bytes
#define ADDR_RTC_PENDING        0x14C // 1 byte
#define ADDR_SYNC_PENDING       0x14D // 1 byte

// EEPROM total bytes
#define EEPROM_SIZE             0x14E // 334 bytes

/* ------------------------------------------------------------------------------------------- */
// Global variables
/* ------------------------------------------------------------------------------------------- */

// Configuration variables
struct StructSettings
{
  char LogFilename[64] = {0};         // SD card log file       
  unsigned int LogInterval = 1;       // Time between samples (10 to 3600 seconds)
  unsigned int LogWaitTime = 1;       // Time to wait after on error (1 to 3600 seconds)
  int LogTimezone = 0;                // Timezone (-12 to +12, 0 = GMT)
  unsigned char WifiEnabled = 0;      // Use WiFi functions
  unsigned char WifiSyncEnable = 0;   // Sync data stored in SD card to Google Sheets
  char WifiSSID[32] = {0};            // WiFi SSID
  char WifiPsw[32] = {0};             // WiFi password
  char WifiRedirectURL[64] = {0};     // URL of the HTTP->HTTPs redirect host
  char WifiGScriptURL[128] = {0};     // Google Script ID
} Settings, NewSettings;

// Sampled data
struct StructSample
{
  float Temperature = 0;              // DS1722 temperature
  float BatteryVoltage = 0;           // ADC voltage
  unsigned long RTCEpoch = 0;         // RTC date and time - Epoch format
  bool RTCValid = false;              // RTC memory was not lost. Data is reliable
  bool BmeStatus = false;             // BME280 status
  float BmeTemperature = 0;           // BME280 temperature
  float BmePressure = 0;              // BME280 pressure
  float BmeHumidity = 0;              // BME280 humidity
} Sample;

// Code flags
struct StructFlag
{
  bool SDAvailable = false;           // SD is installed and accessible
  bool WifiAvailable = false;         // WiFi is connected
  bool NewFirmwareSuccess = false;    // New firmware was correctly loaded
  bool NewSettingsSuccess = false;    // New congiguration was correctly loaded
  bool SaveWifiSuccess = false;       // Sample was posted to Google Sheets
  bool SaveSDSuccess = false;         // Sample was saved in SD card
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

// HTTP post response - Expected only "-1", "0" or "1"
char PostResponseString[2] = {0};

/* ------------------------------------------------------------------------------------------- */
// Constructors
/* ------------------------------------------------------------------------------------------- */

// Temperature sensor
DS1722 TS (PIN_TS_CS);

// Real time clock
DS1390 RTC (PIN_RTC_CS);

// SD card
SdFat SDCard;

// New settings file
File FileNewSettings;

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

// BME280 - SPI mode
Adafruit_BME280 Bme280 (PIN_BME_CS);

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

  #if DEBUG_SERIAL
  Serial.printf ("[%05d] Checking battery voltage... \n", millis());
  #endif
  
   // Get battery voltage
  Sample.BatteryVoltage = analogRead(PIN_ADC) * ADC_GAIN; 

  // Jump to loop if battery voltage is too low
  if (Sample.BatteryVoltage < VBAT_LOW)
    return;

  #if DEBUG_SERIAL
  Serial.printf ("[%05d] Done. \n", millis());  
  #endif     
  
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
    // Clear flags
    Flag.SDAvailable = false;
    Flag.SaveSDSuccess = false;
    
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
  // Settings update
  /* ----------------------------------------------------------------------------------------- */  

  // SD card is was initialized
  if (Flag.SDAvailable)
  {
     // Try to open new settings file
    FileNewSettings = SDCard.open (FilenameNewSettings, FILE_READ);   

    // Settings file was found and opened
    if (FileNewSettings)
    {
      // LED on
      digitalWrite (PIN_LED, LOW);    
      
      #if DEBUG_SERIAL
      Serial.printf ("[%05d] New settings detected. Updating... \n", millis());
      #endif  

      // Check if all parameters are correct in the file
      Flag.NewSettingsSuccess = getSettingsSD (NewSettings);
      
      // Close the settings file
      FileNewSettings.close();

      // Parameters were correct
      if (Flag.NewSettingsSuccess)
      {
        // Transfer NewSettings to EEPROM
        setSettingsEEPROM (NewSettings);
      }
      
      // LED off
      digitalWrite (PIN_LED, HIGH);
      
      // Update was successful
      if (Flag.NewSettingsSuccess)
      {                
        #if DEBUG_SERIAL
        Serial.printf ("[%05d] Done. \n", millis());
        Serial.printf ("[%05d] New settings: \n", millis());
        Serial.printf ("[%05d] LogFilename: \'%s\' \n", millis(), NewSettings.LogFilename);
        Serial.printf ("[%05d] LogInterval: %d \n", millis(), NewSettings.LogInterval);
        Serial.printf ("[%05d] LogWaitTime: %d \n", millis(), NewSettings.LogWaitTime);       
        Serial.printf ("[%05d] LogTimezone: %d \n", millis(), NewSettings.LogTimezone);
        Serial.printf ("[%05d] WifiEnabled: %d \n", millis(), NewSettings.WifiEnabled);
        Serial.printf ("[%05d] WifiSyncEnable: %d \n", millis(), NewSettings.WifiSyncEnable);
        Serial.printf ("[%05d] WifiSSID: \'%s\' \n", millis(), NewSettings.WifiSSID);
        Serial.printf ("[%05d] WifiPsw: \'%s\' \n", millis(), NewSettings.WifiPsw);       
        Serial.printf ("[%05d] WifiRedirectURL: \'%s\' \n", millis(), NewSettings.WifiRedirectURL);
        Serial.printf ("[%05d] WifiGScriptURL: \'%s\' \n", millis(), NewSettings.WifiGScriptURL);
        #endif
                
        #if DEBUG_SERIAL
        Serial.printf ("[%05d] Removing settings file... \n", millis());
        #endif
        
        // Remove settings file
        if (SDCard.remove(FilenameNewSettings))
        {
          #if DEBUG_SERIAL
          Serial.printf ("[%05d] Done. \n", millis());  
          #endif             
        }

        // Error removing settings file
        else
        {
          #if DEBUG_SERIAL
          Serial.printf ("[%05d] Error removing settings file! \n", millis());
          #endif          
        }

        // Set RTCPending flag to update RTC time - Saved in EEPROM in case update fails in this run
        Flag.PendingRTCUpdate = true;
        setRTCPending (Flag.PendingRTCUpdate);

        // Set reboot pending flag
        Flag.PendingReboot = true;

        // WiFi functions and Sync are enabled
        if (NewSettings.WifiEnabled && NewSettings.WifiSyncEnable)
        {
          // Set PendingWifiSync flag to sync SD content
          Flag.PendingWifiSync = true;
          setSyncPending (Flag.PendingWifiSync);             
        }

        // WiFi functions or Sync are disabled
        else
        {
          // Clear PendingWifiSync flag
          Flag.PendingWifiSync = false;
          setSyncPending (Flag.PendingWifiSync);                  
        }
      }

      // Error updating settings
      else
      {
        #if DEBUG_SERIAL
        Serial.printf ("[%05d] Error updating settings! \n", millis());
        #endif

        // Jump to loop
        return;
      }                              
    }
    
    // Settings file not found or failed to open
    else
    {
      #if DEBUG_SERIAL
      Serial.printf ("[%05d] New settings not detected. \n", millis());
      #endif
    }    
  }

  /* ----------------------------------------------------------------------------------------- */
  // Reboot if new firmware or settings was applied
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
  // Get settings from EEPROM
  /* ----------------------------------------------------------------------------------------- */   

  #if DEBUG_SERIAL
  Serial.printf ("[%05d] Loading settings from EEPROM... \n", millis());
  #endif
  
  // Configuration was correctly loaded from EEPROM
  if (getSettingsEEPROM (Settings))
  {
    #if DEBUG_SERIAL
    Serial.printf ("[%05d] Done. \n", millis());
    Serial.printf ("[%05d] Configuration: \n", millis());
    Serial.printf ("[%05d] LogFilename: \'%s\' \n", millis(), Settings.LogFilename);
    Serial.printf ("[%05d] LogInterval: %d \n", millis(), Settings.LogInterval);
    Serial.printf ("[%05d] LogWaitTime: %d \n", millis(), Settings.LogWaitTime);       
    Serial.printf ("[%05d] LogTimezone: %d \n", millis(), Settings.LogTimezone);
    Serial.printf ("[%05d] WifiEnabled: %d \n", millis(), Settings.WifiEnabled);
    Serial.printf ("[%05d] WifiSyncEnable: %d \n", millis(), Settings.WifiSyncEnable);
    Serial.printf ("[%05d] WifiSSID: \'%s\' \n", millis(), Settings.WifiSSID);
    Serial.printf ("[%05d] WifiPsw: \'%s\' \n", millis(), Settings.WifiPsw);       
    Serial.printf ("[%05d] WifiRedirectURL: \'%s\' \n", millis(), Settings.WifiRedirectURL);
    Serial.printf ("[%05d] WifiGScriptURL: \'%s\' \n", millis(), Settings.WifiGScriptURL);
    #endif  
  }

  // An error occur loading settings from EEPROM
  else
  {
    #if DEBUG_SERIAL
    Serial.printf ("[%05d] Error getting settings from EEPROM! \n", millis());  
    Serial.printf ("[%05d] Trying again in %d seconds \n", millis(), ERROR_SLEEP_INTERVAL);  
    #endif         

    // Enter deep sleep - Account for spent time
    //ESP.deepSleep(ERROR_SLEEP_INTERVAL*1e6 - micros(), WAKE_RF_DEFAULT);
    ESP.deepSleep(ERROR_SLEEP_INTERVAL*1e6, WAKE_RF_DEFAULT);
  }
  
  /* ----------------------------------------------------------------------------------------- */
  // Check if data logging is possible
  /* ----------------------------------------------------------------------------------------- */

  // SD card is not availabe and WiFi is disabled
  if (!Flag.SDAvailable && !Settings.WifiEnabled)
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
    Serial.printf ("[%05d] RTC memory content was recently lost! Timestamp may be wrong! \n", millis());
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
  // Init BME280
  /* ----------------------------------------------------------------------------------------- */  

  #if DEBUG_SERIAL
  Serial.printf ("[%05d] Configuring BME280... \n", millis()); 
  #endif

  // Init BME
  Sample.BmeStatus = Bme280.begin();

  #if DEBUG_SERIAL
  if (!Sample.BmeStatus)
    Serial.printf ("[%05d] Could not find a valid BME280 sensor! \n", millis());
  else
  {
    // Set BME280 sampling mode - Forced mode
    Bme280.setSampling (Adafruit_BME280::MODE_FORCED,
                    Adafruit_BME280::SAMPLING_X1, // temperature
                    Adafruit_BME280::SAMPLING_X1, // pressure
                    Adafruit_BME280::SAMPLING_X1, // humidity
                    Adafruit_BME280::FILTER_OFF);

    Serial.printf ("[%05d] Done. \n", millis());  
  }
  #endif

  /* ----------------------------------------------------------------------------------------- */
  // Sample data
  /* ----------------------------------------------------------------------------------------- */ 

  #if DEBUG_SERIAL
  Serial.printf ("[%05d] Sampling data... \n", millis());  
  #endif  

  // Get current time
  Sample.RTCEpoch = RTC.getDateTimeEpoch (Settings.LogTimezone);

  // Get battery voltage
  Sample.BatteryVoltage = analogRead(PIN_ADC) * ADC_GAIN;  

  // Wait if temperature sample is not finish
  while ((millis() - Time.RequestTS) < 1200)
    // Delay for a bit
    delay (100);

  // Get DS172 temperature
  Sample.Temperature = TS.getTemperature();

  // Get BME280 data
  if (Sample.BmeStatus)
  {
    // Request sample
    Bme280.takeForcedMeasurement();

    // Read data
    Sample.BmeTemperature = Bme280.readTemperature();
    Sample.BmePressure = Bme280.readPressure() / 100.0F;
    Sample.BmeHumidity = Bme280.readHumidity();
  }

  #if DEBUG_SERIAL
  Serial.printf ("[%05d] Done. \n", millis());  
  Serial.printf ("[%05d] Epoch: %d (%s) \n", millis(), Sample.RTCEpoch, Sample.RTCValid ? "Reliable" : "Unreliable");  
  Serial.printf ("[%05d] Vbat: %.4f V \n", millis(), Sample.BatteryVoltage);
  Serial.printf ("[%05d] Temperature: %.4f C \n", millis(), Sample.Temperature);   
  Serial.printf ("[%05d] BME280 Temperature: %.4f C \n", millis(), Sample.BmeTemperature); 
  Serial.printf ("[%05d] BME280 Pressure: %.2f hPa \n", millis(), Sample.BmePressure); 
  Serial.printf ("[%05d] BME280 Humidity: %.4f% \n", millis(), Sample.BmeHumidity);    
  #endif  

  /* ----------------------------------------------------------------------------------------- */
  // Start WiFi connection if necessary
  /* ----------------------------------------------------------------------------------------- */   

  // WiFi functions are enable
  if (Settings.WifiEnabled)
  {
    #if DEBUG_SERIAL
    Serial.printf ("[%05d] Starting WiFi connection... \n", millis()); 
    #endif

    // Start WiFi connection
    WiFi.begin (Settings.WifiSSID, Settings.WifiPsw);

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
      Flag.SaveWifiSuccess = false;   
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
    Flag.SaveWifiSuccess = false;
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
    if (LogClient.begin (Settings.WifiRedirectURL))
    {
      // Define request timeout
      LogClient.setTimeout(4*WIFI_TIMEOUT);
  
      #if DEBUG_SERIAL
      Serial.printf ("[%05d] Done. \n", millis());
      #endif
  
      // Prepare log buffer
      snprintf (Buffer, sizeof(Buffer), "host=%s&payload=%d;%d;%.4f;%.4f;%.4f;%.2f;%.4f;%d;%d",
                Settings.WifiGScriptURL, Sample.RTCEpoch, Settings.LogTimezone, Sample.BatteryVoltage, 
                Sample.Temperature, Sample.BmeTemperature, Sample.BmePressure, Sample.BmeHumidity, 
                Sample.RTCValid, Flag.SDAvailable);

      // Define request content type - Required to perform posts
      LogClient.addHeader("Content-Type", "application/x-www-form-urlencoded");
      
      #if DEBUG_SERIAL
      Serial.printf ("[%05d] Posting... \n", millis());
      #endif
  
      // Send post request to host and get response code
      PostResponseCode = LogClient.POST(Buffer);

      // Get response
      String Response = LogClient.getString();
      Response.toCharArray(PostResponseString, 2);
  
      // Close client connection
      LogClient.end();
  
      // Post was successful
      if ((PostResponseCode == 200) && (PostResponseString[0] == '1'))
      {
        // Set flag
        Flag.SaveWifiSuccess = true;
  
        #if DEBUG_SERIAL
        Serial.printf ("[%05d] Done. \n", millis());
        #endif
      }
  
      // Post failed
      else
      {
        // Reset flag
        Flag.SaveWifiSuccess = false;
  
        #if DEBUG_SERIAL
        Serial.printf ("[%05d] Failed to post data! \n", millis());
        #endif
      }
  
      #if DEBUG_SERIAL
      Serial.printf ("[%05d] HTTP code: %d \n", millis(), PostResponseCode);
      Serial.printf ("[%05d] Response: %s \n", millis(), PostResponseString);
      #endif
    }

    // Failed to  connect to host
    else
    {
      #if DEBUG_SERIAL
      Serial.printf ("[%05d] Failed to connect to host! \n", millis());
      #endif      
    }
  }
  
  /* ----------------------------------------------------------------------------------------- */
  // Save data to SD card if available
  /* ----------------------------------------------------------------------------------------- */

  // SD card is available  
  if (Flag.SDAvailable)
  {
    // Open the log file
    FileLog = SDCard.open(Settings.LogFilename, FILE_WRITE);

    // If the file opened okay, write to it
    if (FileLog)
    {
      #if DEBUG_SERIAL
      Serial.printf ("[%05d] Saving to Log file... \n", millis());
      #endif

      // File is empty
      if(FileLog.size() == 0)
        // Write header
        FileLog.println ("EP;TZ;VB;TS;BT;BP;BH;ST;WF");              
      
      // Prepare log buffer
      snprintf (Buffer, sizeof(Buffer), "%d;%d;%.4f;%.4f;%.4f;%.2f;%.4f;%d;%d",
                Sample.RTCEpoch, Settings.LogTimezone, Sample.BatteryVoltage, 
                Sample.Temperature, Sample.BmeTemperature, Sample.BmePressure, Sample.BmeHumidity,
                Sample.RTCValid, Flag.SaveWifiSuccess); 

      // Write to file
      FileLog.println(Buffer);

      // Close the file
      FileLog.close();

      // Set flag
      Flag.SaveSDSuccess = true;

      #if DEBUG_SERIAL
      Serial.printf ("[%05d] Done. \n", millis());
      #endif
    }

    // Error opening the file
    else
    {
      // Reset the flag
      Flag.SaveSDSuccess = false;       

      #if DEBUG_SERIAL
      Serial.printf ("[%05d] Error opening the Log file! \n", millis());
      #endif
    }
  }

  /* ----------------------------------------------------------------------------------------- */
  // Check if the SD card content will need to be synced
  /* ----------------------------------------------------------------------------------------- */

  // WiFi functions and Sync are enabled
  if (Settings.WifiEnabled && Settings.WifiSyncEnable)
  {
    // Data was saved only in the SD card
    if (Flag.SaveSDSuccess && !Flag.SaveWifiSuccess)
    {
      // Set PendingWifiSync flag to sync SD content - Saved in EEPROM in case update fails in this run
      Flag.PendingWifiSync = true;
      setSyncPending (Flag.PendingWifiSync);             
    }
  }

  // WiFi functions or Sync are disable
  else
  {
    // Clear PendingWifiSync flag
    Flag.PendingWifiSync = false;
    setSyncPending (Flag.PendingWifiSync);      
  }

  /* ----------------------------------------------------------------------------------------- */
  // Update RTC if necessary
  /* ----------------------------------------------------------------------------------------- */

  // Get flag from EEPROM
  Flag.PendingRTCUpdate = getRTCPending ();
  
  // RTC data need to be updated
  if (Flag.PendingRTCUpdate)
  {
    // WiFi is connected
    if (Flag.WifiAvailable)
    {
      #if DEBUG_SERIAL
      Serial.printf ("[%05d] RTC time update is pending. Updating... \n", millis());
      #endif

      // Start NTP client
      NTP.begin();

      // NTP data was successfuly updated
      if (NTP.update())
      {
        // Update DS1390 time using NTP Epoch timestamp
        RTC.setDateTimeEpoch (NTP.getEpochTime(), Settings.LogTimezone);

        // Clear update pending flag
        Flag.PendingRTCUpdate = false;
        setRTCPending (Flag.PendingRTCUpdate);

        #if DEBUG_SERIAL
        Serial.printf ("[%05d] Done. \n", millis());
        #endif
      }

      // Error getting NTP data
      else
      {
        #if DEBUG_SERIAL
        Serial.printf ("[%05d] Error getting data from NTC server! \n", millis());
        #endif
      }
    }

    // WiFi is not connected
    else
    {
      #if DEBUG_SERIAL
      Serial.printf ("[%05d] RTC time update is pending but WiFi is not available! \n", millis());
      #endif
    }
  }
 
  /* ----------------------------------------------------------------------------------------- */
  // Sync SD card content if necessary
  /* ----------------------------------------------------------------------------------------- */

  // Get flag from EEPROM
  Flag.PendingWifiSync = getSyncPending ();
  
  // SD card data needs to be synced
  if (Flag.PendingWifiSync)
  {
    // WiFi is connected and SD is available
    if (Flag.WifiAvailable && Flag.SDAvailable)
    {
      #if DEBUG_SERIAL
      Serial.printf ("[%05d] SD card sync is pending. Syncing... \n", millis());
      #endif      

      // Sync was successful
      if (syncLog())
      {
        // Clear PendingWifiSync flag
        Flag.PendingWifiSync = false;
        setSyncPending (Flag.PendingWifiSync);       
  
        #if DEBUG_SERIAL
        Serial.printf ("[%05d] Done. \n", millis());
        #endif          
      } 

      // Sync failed
      else
      {
        #if DEBUG_SERIAL
        Serial.printf ("[%05d] Error syncing data! \n", millis());
        #endif             
      }
    }

    // WiFi is not connected or SD is not available
    else
    {
      // WiFi is not connected and SD is available
      if (!Flag.WifiAvailable && Flag.SDAvailable)
      {    
        #if DEBUG_SERIAL
        Serial.printf ("[%05d] SD card data needs to be synced but WiFi is not available! \n", millis());
        #endif
      }

      // WiFi is connected but SD is not available
      else if (Flag.WifiAvailable && !Flag.SDAvailable)
      {    
        #if DEBUG_SERIAL
        Serial.printf ("[%05d] SD card data needs to be synced but SD card is not available! \n", millis());
        #endif
      }

      // Nothing is availabe
      else
      {
         #if DEBUG_SERIAL
        Serial.printf ("[%05d] Impossible to sync data! \n", millis());
        #endif       
      }
    }
  }

  /* ----------------------------------------------------------------------------------------- */
  // Check if data was saved somewhere
  /* ----------------------------------------------------------------------------------------- */

  // Data was not saved
  if (!Flag.SaveWifiSuccess && !Flag.SaveSDSuccess)
  {
    #if DEBUG_SERIAL
    Serial.printf ("[%05d] Data was not saved! Trying again in %d seconds... \n", millis(), Settings.LogWaitTime);
    #endif

    // Enter deep sleep - Account for spent time running
    //ESP.deepSleep(Settings.LogWaitTime*1e6 - micros(), WAKE_RF_DEFAULT);  
    ESP.deepSleep(Settings.LogWaitTime*1e6, WAKE_RF_DEFAULT);  
  }

  // Data was saved in SD card or WiFi
  else
  {
    #if DEBUG_SERIAL
    Serial.printf ("[%05d] Finished. Next update in %d seconds... \n", millis(), Settings.LogInterval);
    #endif
  
    // Enter deep sleep - Account for spent time running
    //ESP.deepSleep(Settings.LogInterval*1e6 - micros(), WAKE_RF_DEFAULT);  
    ESP.deepSleep(Settings.LogInterval*1e6, WAKE_RF_DEFAULT);  
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

    ESP.deepSleep(0, WAKE_RF_DEFAULT);
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
  // Read byte and return
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
  // Write value
  EEPROM.write (ADDR_RTC_PENDING, Value);

  // Apply changes in EEPROM
  EEPROM.commit();
}

/* ------------------------------------------------------------------------------------------- */
// Name:        getSyncPending
// Description: Gets the WifiSyncPending flag value from EEPROM
// Arguments:   None
// Returns:     Flag value
/* ------------------------------------------------------------------------------------------- */

bool getSyncPending ()
{
  // Read byte and return
  return EEPROM.read (ADDR_SYNC_PENDING);
}

/* ------------------------------------------------------------------------------------------- */
// Name:        setSyncPending
// Description: Writes the WifiSyncPending flag value in EEPROM
// Arguments:   Flag value
// Returns:     None
/* ------------------------------------------------------------------------------------------- */

void setSyncPending (bool Value)
{
  // Write value
  EEPROM.write (ADDR_SYNC_PENDING, Value);

  // Apply changes in EEPROM
  EEPROM.commit();
}

/* ------------------------------------------------------------------------------------------- */
// Name:        getSettingsSD
// Description: Gets all the settings from SD card (FilenameNewSettings)
// Arguments:   Buffer - Pointer to the buffer struct
// Returns:     true if all parameters were parsed and false otherwise
/* ------------------------------------------------------------------------------------------- */

bool getSettingsSD (StructSettings &Buffer)
{
  // Line buffer
  char BufferLine[128];

  // End of line index position
  unsigned char EOLPos = 0;

  // Checksum - 10 parameters are expected. Set 1 bit of Checksum for each
  unsigned int NewSettingsChecksum = 0;

  // At this point, FileNewSettings is already open. Read all lines of file sequentially
  while ((EOLPos = FileNewSettings.fgets(BufferLine, sizeof(BufferLine))) > 0)
  {
    // Proccess only lines that contain variables
    if (BufferLine[0] == '$')
    {
      // Current line is $LogFilename
      if (strstr (BufferLine, "$LogFilename "))
      {
        // Start index to extract text
        unsigned char StartPos = 13;

        // Copy value to new settings buffer from StartPos to '\n'
        strncpy (Buffer.LogFilename, (BufferLine + StartPos), (EOLPos - StartPos - 1));

        // Increment checksum
        NewSettingsChecksum += 0x001;
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
          NewSettingsChecksum += 0x002;        
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
          NewSettingsChecksum += 0x004;        
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
          NewSettingsChecksum += 0x008;          
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
          NewSettingsChecksum += 0x010;          
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
          NewSettingsChecksum += 0x020;          
      }

      // Current line is $WifiSSID
      else if (strstr (BufferLine, "$WifiSSID "))
      {
        // Start index to extract text
        unsigned char StartPos = 10;

        // Copy value to buffer from StartPos to '\n'
        strncpy (Buffer.WifiSSID, (BufferLine + StartPos), (EOLPos - StartPos - 1));

        // Increment checksum
        NewSettingsChecksum += 0x040;        
      }

      // Current line is $WifiPsw
      else if (strstr (BufferLine, "$WifiPsw "))
      {
        // Start index to extract text
        unsigned char StartPos = 9;

        // Copy value to buffer from StartPos to '\n'
        strncpy (Buffer.WifiPsw, (BufferLine + StartPos), (EOLPos - StartPos - 1));

        // Increment checksum
        NewSettingsChecksum += 0x080;        
      }

      // Current line is $WifiRedirectURL
      else if (strstr (BufferLine, "$WifiRedirectURL "))
      {
        // Start index to extract text
        unsigned char StartPos = 17;

        // Copy value to buffer from StartPos to '\n'
        strncpy (Buffer.WifiRedirectURL, (BufferLine + StartPos), (EOLPos - StartPos - 1));

        // Increment checksum
        NewSettingsChecksum += 0x100;           
      }

      // Current line is $WifiGScriptURL
      else if (strstr (BufferLine, "$WifiGScriptURL "))
      {
        // Start index to extract text
        unsigned char StartPos = 16;

        // Copy value to buffer from StartPos to '\n'
        strncpy (Buffer.WifiGScriptURL, (BufferLine + StartPos), (EOLPos - StartPos - 1));

        // Increment checksum
        NewSettingsChecksum += 0x200;          
      }
    }
  }

  // Return true if all parameters were parsed
  if (NewSettingsChecksum == 0x3FF)
    return true;
  else
    return false;
}

/* ------------------------------------------------------------------------------------------- */
// Name:        getSettingsEEPROM
// Description: Gets all the settings from EEPROM
// Arguments:   Buffer - Pointer to the buffer struct
// Returns:     true if all parameters were parsed and false otherwise
/* ------------------------------------------------------------------------------------------- */

bool getSettingsEEPROM (StructSettings &Buffer)
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
  
  // Get WifiRedirectURL
  Addr = ADDR_WIFI_REDIRECTURL;
  Index = 0;

  // Read data until carriage return or 64 read bytes
  while ((EEPROM.read(Addr)) != '\n' && (Index < 64))
  {
    // Save data to buffer  
    Buffer.WifiRedirectURL[Index] = EEPROM.read(Addr);

    // Add byte to checksum
    Checksum += Buffer.WifiRedirectURL[Index];

    // Increase index and address
    Index++;
    Addr++;
  }

  /* ----------------------------------------------------------------------------------------- */
  
  // Get WifiGScriptURL
  Addr = ADDR_WIFI_GSCRIPTURL;
  Index = 0;

  // Read data until carriage return or 128 read bytes
  while ((EEPROM.read(Addr)) != '\n' && (Index < 128))
  {
    // Save data to buffer  
    Buffer.WifiGScriptURL[Index] = EEPROM.read(Addr);

    // Add byte to checksum
    Checksum += Buffer.WifiGScriptURL[Index];

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
// Name:        setSettingsEEPROM
// Description: Sets all the settings in EEPROM
// Arguments:   Buffer - Pointer to the data struct
// Returns:     None
/* ------------------------------------------------------------------------------------------- */

void setSettingsEEPROM (StructSettings &Buffer)
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
  
  // Set WifiRedirectURL  
  Addr = ADDR_WIFI_REDIRECTURL;
  Index = 0;

  // Write all chars
  for (Index = 0; Index < strlen(Buffer.WifiRedirectURL); Index++) 
  {
    // Send byte
    EEPROM.write(Addr, Buffer.WifiRedirectURL[Index]);

    // Add byte to checksum
    Checksum += Buffer.WifiRedirectURL[Index];
    
    // Increase address
    Addr++;
  }
  
  // Write carriage return
  EEPROM.write(Addr, '\n');

  /* ----------------------------------------------------------------------------------------- */
  
  // Set WifiGScriptURL  
  Addr = ADDR_WIFI_GSCRIPTURL;
  Index = 0;

  // Write all chars
  for (Index = 0; Index < strlen(Buffer.WifiGScriptURL); Index++) 
  {
    // Send byte
    EEPROM.write(Addr, Buffer.WifiGScriptURL[Index]);

    // Add byte to checksum
    Checksum += Buffer.WifiGScriptURL[Index];
    
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

bool syncLog ()
{
  // Line buffer
  char BufferLine[100];
  
  // End of line index position
  unsigned char EOLPos = 0;
    
  // At this point SD card and WiFi are available
  
  // Open the log file
  FileLog = SDCard.open(Settings.LogFilename, O_READ | O_WRITE);

  // File opened
  if (FileLog)
  {
    // Read all lines of file sequentially
    while ((EOLPos = FileLog.fgets(BufferLine, sizeof(BufferLine))) > 0)
    {
      // Proccess only lines with data that failed to be posted
      if (BufferLine[EOLPos - 2] == '0')
      {             
        // Modify read line - Remove '\n' and change SaveWifiSuccess flag
        BufferLine[EOLPos - 1] = '\0';
        BufferLine[EOLPos - 2] = '1';
                                 
        // Start HTTP client connection to host
        if (LogClient.begin (Settings.WifiRedirectURL))
        {
          // Define request timeout
          LogClient.setTimeout(4*WIFI_TIMEOUT);
          
          // Define request content type - Required to perform posts
          LogClient.addHeader("Content-Type", "application/x-www-form-urlencoded");       
  
          // Prepare post buffer
          snprintf (Buffer, sizeof(Buffer), "host=%s&payload=%s",
                    Settings.WifiGScriptURL, BufferLine);     
                                                            
          // Send post request to host and get response code
          PostResponseCode = LogClient.POST(Buffer);

          // Get response
          String Response = LogClient.getString();
          Response.toCharArray(PostResponseString, 2);
   
          // Post failed
          if ((PostResponseCode != 200) || (PostResponseString[0] != '1'))
          {
            // Close file
            FileLog.close ();
  
            // Return
            return false;
          }
  
          // Post was successful
          else
          {
            // Get pointer position in file
            unsigned long PointerPos = FileLog.curPosition ();
    
            // Return position pointer to WiFiSyncSuccess (last two bytes are \r \n, so return 3)
            FileLog.seekCur (-3);
            
            // Change WiFiSyncSuccess value
            FileLog.println("1");

            // Return pointer to original position
            FileLog.seekSet (PointerPos);
          }
        }

        // Not connected with host
        else
        {
          return false;
        }
      }
    }

    // Close file
    FileLog.close ();
          
    // At this point all data has been synced
    return true;
  }
  
  // Error opening the file
  else
  {
    return false;
  }
}
