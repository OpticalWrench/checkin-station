/**
 * ----------------------------------------------------------------------------
 * This sketch is an RFID checkin station.
 * Built with the ESP8266-12-E WiFi module and
 * an RC522A breakout board.
 *
 * Requires the MFRC522 library: https://github.com/miguelbalboa/rfid
 *  
 * Install and configure arduino IDE: https://github.com/esp8266/Arduino
 *
 * NOTE: The library file MFRC522.h has a lot of useful info. Please read it.
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
 *
 */

#include <ESP8266WiFi.h>
#include <SPI.h>
#include <MFRC522.h>

typedef enum {
  RFID_OK,
  TAG_READER_FAULTY,
  TAG_DATA_READ_ERROR,
  TAG_TYPE_NOT_SUPPORTED,
} station_error_type;

typedef enum {
  WIFI_OK,
  AP_CONNECT_FAILED,
  STATIC_IP_FAILED,
  DHCP_FAILED,
  CONNECT_TO_SERVER_FAILED,
  SERVER_RESPONSE_TIMEOUT
} wifi_error_type;

typedef enum {
  OFF,
  ON,
  SLOW_BLINK,
  FAST_BLINK,
  SLOW_PULSE,
  FAST_PULSE
} LED_state;

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

const char* ssid = "yourSSID";
const char* password = "yourPassword";

const char* host = "your.wesite.io";  // use DNS to resolve IP address of server
//const char* host = "192.168.1.118"; // do not use DNS to resolve IP address of server
const int server_port = 80; // http=80   https=443

const unsigned long wifi_connect_timeout_mS = 10000;
const unsigned long server_response_timeout_mS = 3000;

String url = "/your/uri";
String station_id = "LaserStationID";
String token = "tokenHERE!";

unsigned long start_time = millis();
unsigned long waiting_time = 0;

station_error_type rfid_error = RFID_OK;
wifi_error_type wifi_error = WIFI_OK;
LED_state indicator_LED_brightness = OFF;

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
  
  wifiConnectToAP(); // attempt to connect to the wifi network

  Serial.println();
  Serial.println(F("Ready and waiting for checkin taps!"));
}

void loop() {

  uint32_t tagid = 0;
  byte piccType = MFRC522::PICC_TYPE_UNKNOWN;

/*****************************************************************************

RFID reader tap detection and verify readability of tag

*****************************************************************************/

  if ( !mfrc522.PICC_IsNewCardPresent()) {
    return; // No tag detected so, do nothing.
  }

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

    // if there are incoming bytes available
    // from the server, read them and print them:
    while (client.available()) {
      /*
      // char reads:
      char c = client.read();
      Serial.write(c);
      */    
      
      // String reads:
      String line = client.readStringUntil('\r');
      Serial.print(line);
      
    }
  }
  
  // if the server's disconnected, stop the client:
  //if (!client.connected()) {
    Serial.println();
    Serial.println(F("Disconnected from or not connected to server."));
    client.stop();
  //}


  wifiErrorHandler();
  rfidErrorHandler();

  delay(3000);
  
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
