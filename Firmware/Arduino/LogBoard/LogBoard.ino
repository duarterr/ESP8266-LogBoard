/* ------------------------------------------------------------------------------------------- */
// Logger - Data logger
// Author:  Renan R. Duarte
// E-mail:  duarte.renan@hotmail.com
// Date:    October 14, 2019
//
// Notes:   Sending data to Google Sheets (via Google Script) could be done directly using the
//          WiFiClientSecure library. However, HTTPS requests require more heap memory than
//          the device has available in this code when the HTTPS connection is made.
//          As a solution, this code sends an HTTP request to a server where a HTTPs redirect
//          is performed using cURL.
//
// Released into the public domain
/* ------------------------------------------------------------------------------------------- */
//
// Code workflow: 
// (1) Init EEPROM
// (2) SD card present? -  N: Log only via Wifi. Load config from EEPROM
//                         Y: SD has new firmware file? -  N: Move on
//                                                         Y: Update firmare
//                                                            Get NTP time and update RTC
//                            SD has new config file? -  N: Load config from EEPROM
//                                                       Y: Save to EEPROM. Delete config file
//                                                          Get NTP time and update RTC
// (3) Start wifi connection                                                  
// (4) Start peripherals
// (5) Sample data
// (6) WiFi connected? -  Y: Post data - Get result
//                        N: Save to SD card if present
// (7) Vbat < VBAT_LOW? - Y: Low: Sleep forever
//                        N: Normal: Sleep TIME_SAMPLE
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
// Software defines
/* ------------------------------------------------------------------------------------------- */

// Default configuration file
const PROGMEM char* ConfigFilename = "NewConfig.txt";

// Default firmware file
const PROGMEM char* UpdateFilename = "NewFirmware.bin";

// NTP server - Result is given by server closest to you, so it's usually in your timezone
const PROGMEM char* NTPServerURL = "0.br.pool.ntp.org";  // Brazilian server

// Code debug with serial port
#define DEBUG_SERIAL            true

// WiFi connection timeout (seconds)
#define WIFI_TIMEOUT            5

// Battery voltage cutoff voltage (volts)
#define VBAT_LOW                3

// EEPROM addresses
#define ADDR_FILENAME           0                     // 64 bytes max (+ '\n')
#define ADDR_HOSTURL            ADDR_FILENAME + 64    // 64 bytes max (+ '\n')
#define ADDR_SCRIPTID           ADDR_HOSTURL + 64     // 64 bytes max (+ '\n')
#define ADDR_TIMEZONE           ADDR_SCRIPTID + 64    // 2 bytes max
#define ADDR_WIFI_SSID          ADDR_TIMEZONE + 2     // 32 bytes max (+ '\n')
#define ADDR_WIFI_PSW           ADDR_WIFI_SSID + 32   // 32 bytes max (+ '\n')
#define ADDR_TIME_SAMPLE        ADDR_WIFI_PSW + 32    // 2 bytes max 
#define ADDR_TIME_ERROR         ADDR_TIME_SAMPLE + 2  // 2 bytes max 
#define ADDR_RTC_PENDING        ADDR_TIME_ERROR + 2   // 1 byte

/* ------------------------------------------------------------------------------------------- */
// Constructors
/* ------------------------------------------------------------------------------------------- */

// Temperature sensor
DS1722 TS (PIN_TS_CS);

// Real time clock
DS1390 RTC (PIN_RTC_CS);

// SD card
SdFat SDCard;

// Configuration file
File ConfigFile;

// Update file
File UpdateFile;

// Log file
File LogFile;
    
// HTTP client
HTTPClient LogClient;

// UDP
WiFiUDP UDP;

// NTP client
NTPClient NTP(UDP, NTPServerURL);

/* ------------------------------------------------------------------------------------------- */
// Global variables
/* ------------------------------------------------------------------------------------------- */

// Structure to store the configuration variables 
struct StructConfig
{
  char Filename[64] = {0};            // Log file    
  char WifiSSID[32] = {0};            // WiFi SSID
  char WifiPsw[32] = {0};             // WiFi password
  char HostURL[64] = {0};             // URL of the HTTP->HTTPs redirect host
  char ScriptID[64] = {0};            // Google script ID
  unsigned short LogInterval = 1;     // Time between samples (1 to 65535 seconds)
  unsigned short ErrorInterval = 1;   // Time to wait after on error (1 to 65535 seconds)
  int Timezone = 0;                   // Timezone (-12 to +12, 0 = GMT)
} LogConfig, NewConfig;

// Structure to store sampled data
struct StructSample
{
  float Temperature = 0;              // DS1722 temperature
  float BatteryVoltage = 0;           // ADC voltage
  unsigned long TimeEpoch = 0;        // RTC date and time - Epoch format
  bool TimeValid = false;             // RTC data valid flag
} Sample;

// Log flags
bool LogToSD = false;
bool LogToWifi = false;

// Log success flags
bool LogSDSuccess = false;
bool LogWifiSuccess = false;

// WiFi connection counter
unsigned char ConnectionCounter = 0;

// Log buffer
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
  EEPROM.begin (ADDR_RTC_PENDING + 1);

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

  // Check for SD card using Card Detect pin 
  LogToSD = !digitalRead (PIN_SD_CD);

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
    if (!SDCard.begin(PIN_SD_CS)) 
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

      // Try to open new firmware file
      UpdateFile = SDCard.open (UpdateFilename, FILE_READ);
    
      // Firmware file not found
      if (!UpdateFile) 
      {
        #if DEBUG_SERIAL
        Serial.printf ("[%05d] ", millis());
        Serial.println("New firmware not detected.");     
        #endif                            
      }

      // Firmware file was found
      else
      {        
        #if DEBUG_SERIAL     
        Serial.printf ("[%05d] ", millis());
        Serial.println("New firmware file detected. Updating..."); 
        #endif  

        // LED on
        digitalWrite (PIN_LED, LOW);

        // Start update with max available SketchSpace
        if (!Update.begin(((ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000), U_FLASH))
        { 
          #if DEBUG_SERIAL            
          Serial.printf ("[%05d] ", millis());
          Serial.println("Invalid firmware file!");
          #endif            
        }
    
        // Read all file bytes
        else
        {
          while (UpdateFile.available())
          {
            unsigned char ibuffer[128];
            UpdateFile.read((unsigned char *)ibuffer, 128);
            Update.write(ibuffer, sizeof(ibuffer));
          }   
        }

        // Close the file
        UpdateFile.close();

        // LED off
        digitalWrite (PIN_LED, HIGH);
          
        // Update was successful
        if (Update.end(true))
        {
          // Set RTCPending flag to update RTC time - Saved in EEPROM in case update fails in this run
          EEPROMSetRTCPending (true);          
          
          #if DEBUG_SERIAL  
          Serial.printf ("[%05d] ", millis());      
          Serial.println("Success!");
          #endif
        }

        // Error updating
        else
        {
          #if DEBUG_SERIAL  
          Serial.printf ("[%05d] ", millis());      
          Serial.println("Error updating!");
          #endif

          // Flash LED to inform user
          FlashLED ();
        }        

        #if DEBUG_SERIAL     
        Serial.printf ("[%05d] ", millis());
        Serial.println("Deleting firmware file..."); 
        #endif                     

        // Delete firmware file
        if (!SDCard.remove(UpdateFilename))
        {
          #if DEBUG_SERIAL     
          Serial.printf ("[%05d] ", millis());
          Serial.println("Error deleting firmware file!"); 
          #endif  
        }

        #if DEBUG_SERIAL     
        Serial.printf ("[%05d] ", millis());
        Serial.println("Done."); 
        #endif 
    
        #if DEBUG_SERIAL    
        Serial.printf ("[%05d] ", millis());
        Serial.println("Rebooting...");
        #endif 
    
        // Reboot using GPIO16 (Tied to RST)
        delay (250);
        pinMode(16, OUTPUT);
        digitalWrite(16, LOW);    
      }    

      // Try to open new config file
      ConfigFile = SDCard.open (ConfigFilename, FILE_READ);
    
      // Configuration file not found
      if (!ConfigFile) 
      {
        #if DEBUG_SERIAL
        Serial.printf ("[%05d] ", millis());
        Serial.println("New configuration file not detected.");     
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
        #if DEBUG_SERIAL     
        Serial.printf ("[%05d] ", millis());
        Serial.println("New configuration file detected. Parsing new values..."); 
        #endif  

        // Get configuration values from SD card - Store in NewConfig
        SDGetConfig (NewConfig);

        // Close the file
        ConfigFile.close();
        
        #if DEBUG_SERIAL     
        Serial.printf ("[%05d] ", millis());
        Serial.println("Done."); 
        #endif  
        
        #if DEBUG_SERIAL 
        Serial.printf ("[%05d] New log filename: \'%s\' \n", millis(), NewConfig.Filename);
        Serial.printf ("[%05d] New log host: \'%s\' \n", millis(), NewConfig.HostURL);
        Serial.printf ("[%05d] New log script ID: \'%s\' \n", millis(), NewConfig.ScriptID);
        Serial.printf ("[%05d] New log timezone: %d GMT \n", millis(), NewConfig.Timezone);  
        Serial.printf ("[%05d] New WiFi SSID: \'%s\' \n", millis(), NewConfig.WifiSSID);
        Serial.printf ("[%05d] New WiFi password: \'%s\' \n", millis(), NewConfig.WifiPsw);
        Serial.printf ("[%05d] New sample interval: %d seconds \n", millis(), NewConfig.LogInterval);
        Serial.printf ("[%05d] New wait on error: %d seconds \n", millis(), NewConfig.ErrorInterval);
        #endif        

        #if DEBUG_SERIAL     
        Serial.printf ("[%05d] ", millis());
        Serial.println("Saving new values in EEPROM..."); 
        #endif       

        // Save new values in EEPROM
        EEPROMSetConfig (NewConfig);

        // Get configuration values from EEPROM
        EEPROMGetConfig (LogConfig);

        // Set RTCPending flag to update RTC time - Saved in EEPROM in case update fails in this run
        EEPROMSetRTCPending (true);

        #if DEBUG_SERIAL     
        Serial.printf ("[%05d] ", millis());
        Serial.println("Done."); 
        #endif 

        #if DEBUG_SERIAL     
        Serial.printf ("[%05d] ", millis());
        Serial.println("Deleting configuration file..."); 
        #endif                     

        // Delete configuration file
        if (!SDCard.remove(ConfigFilename))
        {
          #if DEBUG_SERIAL     
          Serial.printf ("[%05d] ", millis());
          Serial.println("Error deleting configuration file!"); 
          #endif  
        }

        #if DEBUG_SERIAL     
        Serial.printf ("[%05d] ", millis());
        Serial.println("Done."); 
        #endif    
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
  Serial.printf ("[%05d] Sample interval: %d seconds \n", millis(), LogConfig.LogInterval);
  Serial.printf ("[%05d] Wait on error: %d seconds \n", millis(), LogConfig.ErrorInterval);
  #endif  

  /* ----------------------------------------------------------------------------------------- */

  #if DEBUG_SERIAL 
  Serial.printf ("[%05d] ", millis()); 
  Serial.println ("Starting WiFi connection...");
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
    
  // Delay for DS1390 boot (mandatory)
  delay (200);

  // Check if memory was lost recently
  Sample.TimeValid = RTC.getValidation ();

  // Unreliable RTC data
  if (!Sample.TimeValid)
  {
    // Set RTCPending flag to update RTC time - Saved in EEPROM in case update fails in this run
    EEPROMSetRTCPending (true);          
              
    #if DEBUG_SERIAL  
    Serial.printf ("[%05d] ", millis());
    Serial.println ("RTC memory content was recently lost!");  
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

  // Set DS1390 trickle charger mode (250 ohms with one series diode)
  RTC.setTrickleChargerMode (DS1390_TCH_250_D);   

  #if DEBUG_SERIAL 
  Serial.printf ("[%05d] ", millis());
  Serial.println("Done.");
  #endif    

  /* ----------------------------------------------------------------------------------------- */  

  // LED on
  digitalWrite (PIN_LED, LOW);

  // Calculate battery voltage
  Sample.BatteryVoltage = analogRead(PIN_ADC)*ADC_GAIN;   

  // Get current time
  Sample.TimeEpoch = RTC.getDateTimeEpoch (LogConfig.Timezone);
  
  // Get DS172 temperature
  Sample.Temperature = TS.getTemperature();

  // LED off
  digitalWrite (PIN_LED, HIGH);
    
  #if DEBUG_SERIAL  
  Serial.printf ("[%05d] Epoch: %d \n", millis(), Sample.TimeEpoch);
  Serial.printf ("[%05d] Vbat: %.4f V \n", millis(), Sample.BatteryVoltage);  
  Serial.printf ("[%05d] Temperature: %.4f C \n", millis(), Sample.Temperature);          
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
    LogClient.begin (LogConfig.HostURL);
  
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
   
    // Prepare log buffer
    snprintf (Buffer, sizeof(Buffer), "id=%s&log=EP:%010d-VB:%.4f-TS:%.4f-ST:%d-SD:%d", 
        LogConfig.ScriptID, Sample.TimeEpoch, Sample.BatteryVoltage, Sample.Temperature, Sample.TimeValid, LogToSD);     
  
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
    LogFile = SDCard.open(LogConfig.Filename, FILE_WRITE);
  
    // If the file opened okay, write to it
    if (LogFile) 
    {
      #if DEBUG_SERIAL    
      Serial.printf ("[%05d] ", millis());
      Serial.println ("Writing to SD card...");
      #endif    

      // Prepare log buffer
      snprintf (Buffer, sizeof(Buffer), "%010d; %.4f;  %.4f; %d; %d", 
          Sample.TimeEpoch, Sample.BatteryVoltage, Sample.Temperature, Sample.TimeValid, LogWifiSuccess);
         
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

  // RTC time needs to be updated
  if (EEPROMGetRTCPending ()) 
  {
    // WiFi is connected
    if (LogToWifi)
    {
      #if DEBUG_SERIAL       
      Serial.printf ("[%05d] ", millis());
      Serial.println ("RTC time update is pending. Updating..."); 
      #endif
           
      // Start NTP client
      NTP.begin(); 

      // Get NTP time
      if (NTP.update())
      { 
        // Update DS1390 time using NTP Epoch timestamp
        RTC.setDateTimeEpoch (NTP.getEpochTime(), LogConfig.Timezone);    

        // Reset update pending flag
        EEPROMSetRTCPending (false);
        
        #if DEBUG_SERIAL       
        Serial.printf ("[%05d] ", millis());
        Serial.println ("Done."); 
        #endif                      
      }

      else
      {
        #if DEBUG_SERIAL       
        Serial.printf ("[%05d] ", millis());
        Serial.println ("Error getting data from NTC server!"); 
        #endif        
      }
    }

    else
    {
      #if DEBUG_SERIAL       
      Serial.printf ("[%05d] ", millis());
      Serial.println ("RTC time update is pending but WiFi is not available!"); 
      #endif      
    }
  }

  /* ----------------------------------------------------------------------------------------- */

  // Battery voltage is low
  if (Sample.BatteryVoltage < VBAT_LOW)
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

  /* ----------------------------------------------------------------------------------------- */
  
  // Data was not saved
  if (!LogWifiSuccess && !LogSDSuccess)
  {
    #if DEBUG_SERIAL     
    Serial.printf ("[%05d] ", millis());  
    Serial.printf ("Trying again in %d seconds... \n", LogConfig.ErrorInterval);
    #endif      
    
    // Call error function
    RunOnError ();        
  }

  /* ----------------------------------------------------------------------------------------- */

  #if DEBUG_SERIAL
  Serial.printf ("[%05d] ", millis());    
  Serial.printf("Next update in %d seconds... \n", LogConfig.LogInterval);
  #endif
  
  // Enter deep sleep
  ESP.deepSleep(LogConfig.LogInterval*1e6);           
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
// Name:        RunOnError
// Description: This function is called when an error occur
// Arguments:   None
// Returns:     None
/* ------------------------------------------------------------------------------------------- */

void RunOnError () 
{
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
// Name:        SDGetConfig
// Description: Gets all the configuration variables from SD card (ConfigFilename)
// Arguments:   Buffer - Pointer to the buffer struct
// Returns:     None
/* ------------------------------------------------------------------------------------------- */

void SDGetConfig (StructConfig &Buffer)
{
  // Line buffer
  char BufferLine[100];

  // End of line index position
  unsigned char EOLPos;

  // Read all lines of file sequentially
  while ((EOLPos = ConfigFile.fgets(BufferLine, sizeof(BufferLine))) > 0) 
  {
    // Proccess only lines that contain variables
    if (BufferLine[0] == '$')
    {
      // Current line is $Filename
      if (strstr (BufferLine, "$Filename ")) 
      { 
        // Start index to extract text    
        unsigned char StartPos = 10;

        // Copy value to new config buffer from StartPos to '\n'
        strncpy (Buffer.Filename, (BufferLine + StartPos), (EOLPos - StartPos - 1));
      }

      // Current line is $HostURL
      else if (strstr (BufferLine, "$HostURL "))   
      { 
        // Start index to extract text    
        unsigned char StartPos = 9;

        // Copy value to buffer from StartPos to '\n'
        strncpy (Buffer.HostURL, (BufferLine + StartPos), (EOLPos - StartPos - 1));
      }

      // Current line is $ScriptID                          
      else if (strstr (BufferLine, "$ScriptID "))
      { 
        // Start index to extract text    
        unsigned char StartPos = 10;

        // Copy value to buffer from StartPos to '\n'
        strncpy (Buffer.ScriptID, (BufferLine + StartPos), (EOLPos - StartPos - 1));
      }

      // Current line is $WifiSSID                  
      else if (strstr (BufferLine, "$WifiSSID "))
      { 
        // Start index to extract text    
        unsigned char StartPos = 10;
        
        // Copy value to buffer from StartPos to '\n'
        strncpy (Buffer.WifiSSID, (BufferLine + StartPos), (EOLPos - StartPos - 1));
      }

      // Current line is $WifiPsw                
      else if (strstr (BufferLine, "$WifiPsw ")) 
      { 
        // Start index to extract text    
        unsigned char StartPos = 9;

        // Copy value to buffer from StartPos to '\n'
        strncpy (Buffer.WifiPsw, (BufferLine + StartPos), (EOLPos - StartPos - 1));     
      }

      // Current line is $Timezone
      else if (strstr (BufferLine, "$Timezone "))
      { 
        // Start index to extract text    
        unsigned char StartPos = 10;

        // Buffer
        char Value[8] = {0};

        // Copy value to buffer from StartPos to '\n'
        strncpy (Value, (BufferLine + StartPos), (EOLPos - StartPos - 1));

        // Convert value to number
        Buffer.Timezone = constrain(atoi(Value), -12, 12);       
      }
      
      // Current line is $LogInterval      
      else if (strstr (BufferLine, "$LogInterval "))
      { 
        // Start index to extract text    
        unsigned char StartPos = 12;

        // Buffer
        char Value[8] = {0};

        // Copy value to buffer from StartPos to '\n'
        strncpy (Value, (BufferLine + StartPos), (EOLPos - StartPos - 1));

        // Convert value to number
        Buffer.LogInterval = constrain(atoi(Value), 1, 65535);    
      }
      
      // Current line is $ErrorInterval   
      else if (strstr (BufferLine, "$ErrorInterval "))
      { 
        // Start index to extract text    
        unsigned char StartPos = 15;

        // Buffer
        char Value[8] = {0};

        // Copy value to buffer from StartPos to '\n'
        strncpy (Value, (BufferLine + StartPos), (EOLPos - StartPos - 1));

        // Convert value to number
        Buffer.ErrorInterval = constrain(atoi(Value), 1, 65535);                  
      }
    }
  }
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
  EEPROMGetLogInterval (&Buffer.LogInterval);
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
  EEPROMSetLogInterval (Buffer.LogInterval);
  EEPROMSetErrorInterval (Buffer.ErrorInterval); 
}

/* ------------------------------------------------------------------------------------------- */
// Name:        EEPROMGetLogFilename
// Description: Gets the log filename from EEPROM
// Arguments:   Buffer - Pointer to the buffer array (64 bytes)
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
// Arguments:   Buffer - Pointer to the array to be written (64 bytes)
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
// Arguments:   Buffer - Pointer to the buffer array (64 bytes)
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
// Arguments:   Buffer - Pointer to the array to be written (64 bytes)
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
// Arguments:   Buffer - Pointer to the buffer array (64 bytes)
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
// Arguments:   Buffer - Pointer to the array to be written (64 bytes)
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
// Arguments:   Buffer - Pointer to the buffer array (32 bytes)
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
// Arguments:   Buffer - Pointer to the array to be written (32 bytes)
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
// Arguments:   Buffer - Pointer to the buffer array (32 bytes)
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
// Arguments:   Buffer - Pointer to the array to be written (32 bytes)
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
  // Contrain value
  Timezone = constrain (Timezone, -12, 12);
  
  // Write upper and lower bytes of Timezone
  EEPROM.write (ADDR_TIMEZONE, Timezone >> 8);
  EEPROM.write (ADDR_TIMEZONE + 1, Timezone & 0xFF);

  // Apply changes in EEPROM
  EEPROM.commit();
}

/* ------------------------------------------------------------------------------------------- */
// Name:        EEPROMGetLogInterval
// Description: Gets the Log sample interval value from EEPROM
// Arguments:   Interval - Pointer to the buffer variable (unsigned short)
// Returns:     None
/* ------------------------------------------------------------------------------------------- */

void EEPROMGetLogInterval (unsigned short *Interval)
{
  // Read upper and lower bytes of Interval
  *Interval = ((EEPROM.read (ADDR_TIME_SAMPLE)) << 8) | (EEPROM.read (ADDR_TIME_SAMPLE + 1));
}

/* ------------------------------------------------------------------------------------------- */
// Name:        EEPROMSetLogInterval
// Description: Writes the Log sample interval value in EEPROM
// Arguments:   Interval - Value to be written (unsigned short)
// Returns:     None
/* ------------------------------------------------------------------------------------------- */

void EEPROMSetLogInterval (unsigned short Interval)
{
  // Contrain value	
  Interval = constrain (Interval, 0, 65535);
  
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
  // Contrain value	
  Interval = constrain (Interval, 0, 65535);
  
  // Write upper and lower bytes of Interval
  EEPROM.write (ADDR_TIME_ERROR, Interval >> 8);
  EEPROM.write (ADDR_TIME_ERROR + 1, Interval & 0xFF);

  // Apply changes in EEPROM
  EEPROM.commit();
}

/* ------------------------------------------------------------------------------------------- */
// End of code
/* ------------------------------------------------------------------------------------------- */

/* ------------------------------------------------------------------------------------------- */
// Name:        EEPROMGetRTCPending
// Description: Gets the RTCPending flag value from EEPROM
// Arguments:   None
// Returns:     Flag value
/* ------------------------------------------------------------------------------------------- */

bool EEPROMGetRTCPending ()
{
  // Read upper and lower bytes of Interval
  return EEPROM.read (ADDR_RTC_PENDING);
}

/* ------------------------------------------------------------------------------------------- */
// Name:        EEPROMSetRTCPending
// Description: Writes the RTCPending flag value in EEPROM
// Arguments:   Flag value
// Returns:     None
/* ------------------------------------------------------------------------------------------- */

void EEPROMSetRTCPending (bool Value)
{
  // Write upper and lower bytes of Interval
  EEPROM.write (ADDR_RTC_PENDING, Value);

  // Apply changes in EEPROM
  EEPROM.commit();
}
