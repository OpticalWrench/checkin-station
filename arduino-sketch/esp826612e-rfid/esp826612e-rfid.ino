/**
 * ----------------------------------------------------------------------------
 * This sketch is an RFID checkin station.
 * Built with the ESP8266-12-E WiFi module and
 * an RC522A breakout board.
 *
 * Requires:
 *   MFRC522 library: https://github.com/miguelbalboa/rfid
 *     (from library author:) NOTE: The library file MFRC522.h has a lot of useful info. Please read it.
 * Uses:
 *   JSON Encode/Decode library: https://github.com/bblanchon/ArduinoJson
 *  
 * Install and configure arduino IDE: https://github.com/esp8266/Arduino
 *
 * 
 *
 * Released into the public domain.
 *
 *
 *
 * pin layout used:
 * -----------------------------------------
 *             MFRC522        ESP8266-12-E
 *             Reader/PCD   
 * Signal      Pin            GPIO Pin           
 * -----------------------------------------
 * RST/Reset   RST            4             
 * SPI SS      SDA(SS)        15            
 * SPI MOSI    MOSI           13   
 * SPI MISO    MISO           12
 * SPI SCK     SCK            14
 *             IRQ            NoConnection
 *
 */

#include <ESP8266WiFi.h>
#include <SPI.h>
#include <MFRC522.h>
#include <ArduinoJson.h>
#include "esp8266123-rfid.h" // put this file in this sketch's folder

// RC522A RFID reader SPI pins
// ESP8266-12E MOSI      GPIO13
// ESP8266-12E MISO      GPIO12
// ESP8266-12E CLK       GPIO14
const int RST_PIN = 4;  // RFID-RC522A reset pin
const int SS_PIN  = 15; // RFID-RC522A SPI slave (chip) select 

// Relay pin assignment
const int RELAY_PIN = 2;

// LED pin assignment
const int FRONT_PANEL_LED_PIN = 5;

MFRC522 mfrc522(SS_PIN, RST_PIN);   // Create MFRC522 instance.
const int RFID_antenna_gain = 4; // valid range: 0 to 7

const char* ssid = "VV";
const char* password = "LoopSoup";

const char* host = "factur.monkeywrenchmanufacturing.com";  // use DNS to resolve IP address of server
//const char* host = "192.168.1.118"; // do not use DNS to resolve IP address of server
const int server_port = 80; // http=80   https=443

const unsigned long time_between_allowing_taps = 3000; // milliSeconds
const unsigned long wifi_connect_timeout_mS = 10000; // milliSeconds
const unsigned long server_response_timeout_mS = 3000; // milliSeconds

String url = "/test/tap-in";
String station_id = "n77JEHgJExMdZnGjNNGtanc8rvw6AsWd";
String token = "no-token";

unsigned long tap_time = 0; // used to delay between tap-ins

unsigned long start_time = 0; //used for generic timeouts
unsigned long waiting_time = 0; // used for generic timeouts
bool tag_reader_enabled = false;

station_error_type rfid_error = RFID_OK;
wifi_error_type wifi_error = WIFI_OK;

io_state relay_state = OFF;
io_state front_panel_LED_state = OFF;

void setup() {
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(FRONT_PANEL_LED_PIN, OUTPUT);
  Serial.begin(115200); // Initialize serial communications with the PC
  delay(300);
    if(!Serial) {
      while(1){;} // Do nothing if no serial port is opened (added for Arduinos based on ATMEGA32U4)
    }
  SPI.begin(); // Init SPI bus
  mfrc522.PCD_Init(); // Init MFRC522 tag reader
  if(!setRFIDAntennaGain(RFID_antenna_gain)){
    rfid_error = ANTENNA_GAIN_ERROR;
  }
  
  wifiConnectToAP(); // attempt to connect to the wifi network

  tag_reader_enabled = true;

  Serial.println();
  Serial.println(F("Ready and waiting for checkin taps!"));
}

void loop() {

  uint32_t tagid = 0;
  byte piccType = MFRC522::PICC_TYPE_UNKNOWN;

  if(wifi_error != WIFI_OK) {
    wifiErrorHandler();
  }

  if(rfid_error != RFID_OK) {
    rfidErrorHandler();
  }

  digitalPinController(RELAY_PIN, relay_state);
  digitalPinController(FRONT_PANEL_LED_PIN, front_panel_LED_state);

  if((millis() - tap_time) >= time_between_allowing_taps) {
    tag_reader_enabled = true;
    front_panel_LED_state = ON;
  } else {
    tag_reader_enabled = false;
  }

/*****************************************************************************

RFID reader tap detection and verify readability of tag

*****************************************************************************/

  if ( !mfrc522.PICC_IsNewCardPresent()) {
    return; // No tag detected so, do nothing.
  }

  if(!tag_reader_enabled) {
    return;
  }

  tap_time = millis(); // timestamp of tap-in

  // tag detected, read data
  if ( !mfrc522.PICC_ReadCardSerial()) {
    rfid_error = TAG_DATA_READ_ERROR;
  }

  if(rfid_error == RFID_OK) {
    // Check for compatibility
    piccType = mfrc522.PICC_GetType(mfrc522.uid.sak);
    if (    piccType != MFRC522::PICC_TYPE_MIFARE_MINI
            &&  piccType != MFRC522::PICC_TYPE_MIFARE_1K
            &&  piccType != MFRC522::PICC_TYPE_MIFARE_4K) {
      rfid_error = TAG_TYPE_NOT_SUPPORTED;
    }
  }

/*****************************************************************************

tag ok, read Unique ID number of tag

*****************************************************************************/

  front_panel_LED_state = OFF;

  if(rfid_error == RFID_OK) {
    tagid = 0;
    // calculate large integer value of UID
    byte *uid;
    uid =  &mfrc522.uid.uidByte[0];
    tagid = *uid;
    for(int i=1; i<mfrc522.uid.size; i++) {
      tagid <<= 8;
      tagid |= *++uid;
    }
    // Show some details of the RFID tag)
    Serial.print(F("Tag type: "));
    Serial.println(mfrc522.PICC_GetTypeName(piccType));
    Serial.print(F("UID byte count: ")); Serial.println(mfrc522.uid.size);
    Serial.print(F("Tag UID (hex):"));
    dump_byte_array(mfrc522.uid.uidByte, mfrc522.uid.size);
    Serial.print(F("  (dec):"));
    Serial.println(tagid);
    Serial.println(F("Seems to be a Mifare Classic tag."));
    token = String(tagid);
    } else {
      token = "0";
    }

/*****************************************************************************

Send request to server

*****************************************************************************/

  WiFiClient client;
  
  if (wifi_error == WIFI_OK) {
    Serial.println();
    Serial.println(F("Connecting to server..."));
    // Initialize the Ethernet client library
    // with the IP address and port of the server
    // that you want to connect to (port 80 is default for HTTP):
    if (!client.connect(host, server_port)) {
      wifi_error = CONNECT_TO_SERVER_FAILED;
    }
  }

  if (wifi_error == WIFI_OK) {



/*
// Use this http request string to emulate a form submission to the server
    
    String form_data = "stationid=" + station_id + "&token=" + token;
    int form_data_length = form_data.length();

    String http_request = String("POST ") + url + " HTTP/1.1\r\n" +
                 "Host: " + host + "\r\n" + 
                 "Content-Type: application/x-www-form-urlencoded\r\n" +
                 "Content-Length: " + form_data_length + "\r\n" +
                 "Connection: close\r\n\r\n" +
                 form_data + "\r\n";
  */

// Use this http request string to send JSON data to the server as the request
    
    String json_data = "{\"stationid\":\"" + station_id + "\",\"token\":\"" + token + "\"}";
    int json_data_length = json_data.length();

    String http_request = String("POST ") + url + " HTTP/1.1\r\n" +
                 "Host: " + host + "\r\n" + 
                 "Content-Type: application/json\r\n" +
                 "Content-Length: " + json_data_length + "\r\n\r\n" +
                 json_data;
      
    //Serial.print(http_request); // debug printout
    //Serial.println(); // debug printout

    // Send the request to the server
    Serial.print(F("Requesting URL: "));
    Serial.println(url);
    Serial.println(F("Sending HTTP Request to Server:"));
    client.print(http_request);
    Serial.println(F("Finished Sending HTTP Request."));
  }

/*****************************************************************************

Receive response from server

*****************************************************************************/
  if(wifi_error == WIFI_OK) {
    // timeout waiting for server to respond.
    start_time = millis();
    waiting_time = 0;
    while(!client.available()) {
      waiting_time = millis() - start_time;
      if (waiting_time > server_response_timeout_mS) {
        wifi_error = SERVER_RESPONSE_TIMEOUT;
        break;
      }
    }
  }

  if(wifi_error == WIFI_OK) {

    Serial.println(F("Server response:"));
    String response;

    // if there are incoming bytes available
    // from the server, read them and process them:
    while (client.available()) {
      /*
      // char reads:
      char c = client.read();
      Serial.write(c);
      */    
      
      /*
      // String reads:
      String line = client.readStringUntil('\r');
      Serial.print(line);
      */

      // collect the entire server response to be parsed when server stops
      response += client.readStringUntil('\r');
    }

    String json_response = response;
    int open_brace = -1;
    int close_brace = -1;

    open_brace = json_response.indexOf("{"); // returns index of val within the String, or -1 if not found.
    close_brace = json_response.indexOf("}"); // returns index of val within the String, or -1 if not found.

    // make sure that the data contains both open and close braces.
    if(open_brace != -1 && close_brace != -1) {

      if((close_brace + 1) <= json_response.length()) {
        json_response = json_response.substring(open_brace, (close_brace + 1));
      } else {
        json_response = json_response.substring(open_brace, close_brace) + "}";
      }
      json_response.replace("\r", ""); // discard the carriage return characters
      json_response.replace("\n", ""); // discard the newline characters

    } else {
      wifi_error = JSON_RECEIVE_FAILED;
      //json_response = "error parsing server data response.";
    }  

    // checking wifi_error value again because if receiving JSon data failed then don't parse the JSON data
    if(wifi_error == WIFI_OK) {
      
      Serial.print(json_response);

      // verify laser cutter permission
      if(laser_permission(json_response) == true) {
        togglePin(RELAY_PIN, relay_state);
      }
    }

 } 
  
  // if the server's disconnected, stop the client:
  //if (!client.connected()) {
    Serial.println();
    Serial.println(F("Disconnected from or not connected to server."));
    client.stop();
  //}
  
}

void displayRFIDAntennaGain()
{
  Serial.println();
  Serial.print(F("RFID antenna gain: "));
  int gain = 0 + (mfrc522.PCD_GetAntennaGain() >> 4);

  switch (gain) {
    case 0:
      Serial.println(F("18dB"));
      break;

    case 1:
      Serial.println(F("23dB"));
      break;

    case 2:
      Serial.println(F("18dB"));
      break;

    case 3:
      Serial.println(F("23dB"));
      break;

    case 4:
      Serial.println(F("33dB"));
      break;

    case 5:
      Serial.println(F("38dB"));
      break;

    case 6:
      Serial.println(F("43dB"));
      break;

    case 7:
      Serial.println(F("48dB"));
      break;

    default:
      break;
  }

}

bool setRFIDAntennaGain(int new_gain)
{
  // returns true on success
  bool retval = false;

/*
 * Set the MFRC522 Receiver Gain (RxGain) to value specified by given mask.
 * From 9.3.3.6 / table 98 in http://www.nxp.com/documents/data_sheet/MFRC522.pdf

    000 18dB
    001 23dB
    010 18dB
    011 23dB
    100 33dB
    101 38dB
    110 43dB
    111 48dB

    gain is settable between 0 and 7 decimal
    NOTE: Default value is 4

   NOTE: Given mask is scrubbed with (0x07<<4)=01110000b as RCFfgReg may use reserved bits.
*/
  if(new_gain >= 0 && new_gain <= 7) {
    byte RFID_antenna_gain = (byte) new_gain << 4;
    mfrc522.PCD_SetAntennaGain(RFID_antenna_gain);
    Serial.println();
  }

  byte current_gain = mfrc522.PCD_GetAntennaGain() >> 4;
  if(current_gain == new_gain) {
    retval = true;
  }

  displayRFIDAntennaGain();

  return retval;
}

void wifiConnectToAP(void)
{
  Serial.println();
  Serial.print(F("Connecting to wifi network: "));
  Serial.println(ssid);
  
  WiFi.begin(ssid, password);
  
  // timeout waiting for server to respond.
  start_time = millis();
  waiting_time = 0;
  while (WiFi.status() != WL_CONNECTED) {
    waiting_time = millis() - start_time;
    Serial.print(F("."));
    if (waiting_time >= wifi_connect_timeout_mS) {
      Serial.println();
      Serial.println(F("wifi connect timeout."));
      wifi_error = AP_CONNECT_FAILED;
      break;
    }
    delay(1000);
  }
  
  if(wifi_error == WIFI_OK) {
    Serial.println();
    Serial.println(F("WiFi connected."));
    printWifiStatus();
  }

}

/**
 * Helper routine to dump a byte array as hex values to Serial.
 */
void dump_byte_array(byte *buffer, byte bufferSize)
{
  for (byte i = 0; i < bufferSize; i++) {
    Serial.print(buffer[i] < 0x10 ? " 0" : " ");
    Serial.print(buffer[i], HEX);
  }
}

void printWifiStatus()
{
  // print the SSID of the network you're attached to:
  Serial.print(F("SSID: "));
  Serial.println(WiFi.SSID());

  // print your WiFi shield's IP address:
  IPAddress ip = WiFi.localIP();
  Serial.print(F("IP Address: "));
  Serial.println(ip);

  // print the received signal strength:
  long rssi = WiFi.RSSI();
  Serial.print(F("signal strength (RSSI):"));
  Serial.print(rssi);
  Serial.println(F(" dBm"));
}

void digitalPinController(int pin, io_state state)
{

  /*
    Collection of blinking and pulsing, etc routines for controlling digital output pins
  */

  // TODO write the pwm and timing routines to makes the pulsing and blinking
  switch(state) {
    case OFF:
      digitalWrite(pin, 0);
      break;

    case ON:
      digitalWrite(pin, 1);
      break;

    case SLOW_BLINK:
      digitalWrite(pin, 1);
      break;

    case FAST_BLINK:
      digitalWrite(pin, 1);
      break;

    case SLOW_PULSE:
      digitalWrite(pin, 1);
      break;

    case FAST_PULSE:
      digitalWrite(pin, 1);
      break;

  }
}

void togglePin(int pin, io_state &pin_state)
{
  // debugging prints
  //Serial.println();
  //Serial.print(F("pin state = "));
  //Serial.println(pin_state);

  // NOTE: only works if the IO pin is either ON or OFF!
  // NOTE: does not alter the pin state if blinking, pulsing, etc!
  if(pin_state == OFF) {
    pin_state = ON;
  } else if(pin_state == ON) {
    pin_state = OFF;
  }
}

bool laser_permission(String json_string)
{
    const int JSON_BUFFER_SIZE = 1024;
    char char_buffer[JSON_BUFFER_SIZE];
    StaticJsonBuffer<JSON_BUFFER_SIZE> jsonBuffer;
    bool retval = false;

    json_string.toCharArray(char_buffer, JSON_BUFFER_SIZE);

    JsonObject& root = jsonBuffer.parseObject(char_buffer);
    if (!root.success()) {
      wifi_error = JSON_PARSE_FAILED;
    }

    // verify laser cutter permission
    if(root["allowlaser"] == 1) {
      //int pin, io_state &state)
      retval = true;
    }

    return retval;
}

void wifiErrorHandler(void)
{
  if (WiFi.status() != WL_CONNECTED) {
    wifi_error = AP_CONNECT_FAILED;
  }

  switch (wifi_error) {
    case WIFI_OK:
      break;

    case AP_CONNECT_FAILED:
      Serial.println(F("WiFi is NOT connected!"));
      Serial.println(F("Trying wifi connect again."));
      wifi_error = WIFI_OK;
      wifiConnectToAP();
      break;

    case STATIC_IP_FAILED:
      Serial.println(F("WiFi is NOT connected. STATIC IP error!"));
      Serial.println(F("Trying wifi connect again."));
      wifi_error = WIFI_OK;
      wifiConnectToAP();
      break;

    case DHCP_FAILED:
      Serial.println(F("WiFi is NOT connected. DHCP error!"));
      Serial.println(F("Trying wifi connect again."));
      wifi_error = WIFI_OK;
      wifiConnectToAP();
      break;

    case CONNECT_TO_SERVER_FAILED:
      Serial.print(F("Connection to server "));
      Serial.print(host);
      Serial.print(F(" on port "));
      Serial.print(server_port);
      Serial.println(F(" failed."));
      wifi_error = WIFI_OK;
      break;
    case SERVER_RESPONSE_TIMEOUT:
      Serial.println(F("Server timeout waiting for response."));
      wifi_error = WIFI_OK;
      break;

    case JSON_PARSE_FAILED:
      Serial.println(F("parseObject() failed"));
      wifi_error = WIFI_OK;
      break;

    default:
      break;
  }
}

void rfidErrorHandler(void)
{
  switch (rfid_error) {
    case RFID_OK:
        Serial.println();
        Serial.println(F("Ready and waiting for checkin taps!"));
      break;

    case ANTENNA_GAIN_ERROR:
      Serial.println(F("Setting RFID antenna gain failed.  Setting to default."));
      if(setRFIDAntennaGain(4)) {
        rfid_error = RFID_OK;
      }
      break;

    case TAG_READER_FAULTY:
      break;

    case TAG_DATA_READ_ERROR:
      Serial.println(F("Tag detected but readCardSerial failed."));
      rfid_error = RFID_OK;
      break;

    case TAG_TYPE_NOT_SUPPORTED:
      Serial.println(F("This Checkin Station only works with MIFARE Classic tags."));
      rfid_error = RFID_OK;
    default:
      break;
  }
}

void printBinaryByte(byte data)
{
  for (byte mask = 0x80; mask; mask >>= 1) {
    Serial.print(mask&data?'1':'0');
  }
}

void printBinaryInt(int data)
{
  for (unsigned int mask = 0x8000; mask; mask >>= 1) {
    Serial.print(mask&data?'1':'0');
  }
}
