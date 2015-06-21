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

// RC522A RFID reader SPI pins
// ESP8266-12E MOSI      GPIO13
// ESP8266-12E MISO      GPIO12
// ESP8266-12E CLK       GPIO14
const int RST_PIN =     4;           // Configurable, see typical pin layout above
const int SS_PIN  =     15;          // Configurable, see typical pin layout above

MFRC522 mfrc522(SS_PIN, RST_PIN);   // Create MFRC522 instance.

const char* ssid = "your SSID";
const char* password = "your password";
const int httpServerPort = 80;
int status = WL_IDLE_STATUS;

//const char* host = "data.sparkfun.com";  // use DNS to resolve IP address of server
const char* host = "192.168.1.118"; // do not use DNS to resolve IP address of server
const char* streamId   = "....................";
const char* privateKey = "....................";
String url = "/input/";

void setup() {
  Serial.begin(115200); // Initialize serial communications with the PC
  while (!Serial);    // Do nothing if no serial port is opened (added for Arduinos based on ATMEGA32U4)
  SPI.begin();        // Init SPI bus
  mfrc522.PCD_Init(); // Init MFRC522 card
  
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);
  
  WiFi.begin(ssid, password);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.println("WiFi connected");
  
  printWifiStatus();
  
  Serial.println();
  Serial.println(F("Scan a MIFARE Classic PICC to demonstrate read and write."));
}

void loop() {
  
  // Look for new cards
  if ( ! mfrc522.PICC_IsNewCardPresent()) {
    //Serial.println(F("No card detected."));
    return;
  }

  // Select one of the cards
  if ( !mfrc522.PICC_ReadCardSerial()) {
    Serial.println(F("readCardSerial failed."));
    return;
  }
  
  // Show some details of the PICC (that is: the tag/card)
  Serial.print(F("Card UID:"));
  dump_byte_array(mfrc522.uid.uidByte, mfrc522.uid.size);
  Serial.println();
  Serial.print(F("PICC type: "));
  byte piccType = mfrc522.PICC_GetType(mfrc522.uid.sak);
  Serial.println(mfrc522.PICC_GetTypeName(piccType));

  // Check for compatibility
  if (    piccType != MFRC522::PICC_TYPE_MIFARE_MINI
          &&  piccType != MFRC522::PICC_TYPE_MIFARE_1K
          &&  piccType != MFRC522::PICC_TYPE_MIFARE_4K) {
    Serial.println(F("This Checkin Station only works with MIFARE Classic cards."));
    return;
  }

  WiFiClient client;

/*****************************************************************************

Send request to server

*****************************************************************************/
  Serial.println("\nStarting connection to server...");
  // Initialize the Ethernet client library
  // with the IP address and port of the server
  // that you want to connect to (port 80 is default for HTTP):
  if (!client.connect(host, httpServerPort)) {
    Serial.println("connection failed");
    return;
  }
  
  // We now create a URI for the request
  url += streamId;
  url += "?private_key=";
  url += privateKey;
  url += "&value=123";
  //url += value;
  
  Serial.print("Requesting URL: ");
  Serial.println(url);
  
  // This will send the request to the server
  client.print(String("GET ") + url + " HTTP/1.1\r\n" +
               "Host: " + host + "\r\n" + 
               "Connection: close\r\n\r\n");
  delay(10);
  
 /* 
  // if you get a connection, report back via serial:
  if (client.connect(server, httpServerPort)) {
    Serial.println("connected to server");
    // Make a HTTP request:
    client.println("GET / HTTP/1.1");
    client.println("Host: www.google.com");
    client.println("Connection: close");
    client.println();
  }
*/  
  
/*****************************************************************************

Receive response from server

*****************************************************************************/

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

  // if the server's disconnected, stop the client:
  if (!client.connected()) {
    Serial.println();
    Serial.println("disconnecting from server.");
    client.stop();
  }
  
  delay(3000);
  
}

/**
 * Helper routine to dump a byte array as hex values to Serial.
 */
void dump_byte_array(byte *buffer, byte bufferSize) {
  for (byte i = 0; i < bufferSize; i++) {
    Serial.print(buffer[i] < 0x10 ? " 0" : " ");
    Serial.print(buffer[i], HEX);
  }
}

void printWifiStatus() {
  // print the SSID of the network you're attached to:
  Serial.print("SSID: ");
  Serial.println(WiFi.SSID());

  // print your WiFi shield's IP address:
  IPAddress ip = WiFi.localIP();
  Serial.print("IP Address: ");
  Serial.println(ip);

  // print the received signal strength:
  long rssi = WiFi.RSSI();
  Serial.print("signal strength (RSSI):");
  Serial.print(rssi);
  Serial.println(" dBm");
}

