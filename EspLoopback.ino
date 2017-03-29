#include <ESP8266WiFi.h>

#include <EEPROM.h>

WiFiServer server(1234);
WiFiServer configuration(1235);

#define DEBUG true

// in flash (library EEPROM... whatever) we need:
// 64 char for password (default no password)
// 32 char for SSID (default: ESP)
// 4 byte for ip (default 192.168.0.123)
// 4 byte for netmask (defautl 255.255.255.0)
// 4 byte for gateway (default 192.168.0.1)
// 4 byte for baudrate (defult 921600)
// 2 byte for EEPROM data version, also used as eeprom validity (actual version: 1)
// 1 byte for connection timeout (default 10s, 0 to disable. Time to switch back to AP mode if no connection as Client succed)
// 1 byte flag option. Byte 0: AP/Client, Byte 2: static/DHCP


struct memory_s{
  uint8_t password[64];
  uint8_t ssid[32];
  uint32_t ip;
  uint32_t netmask;
  uint32_t gateway;
  uint32_t baudrate;
  uint16_t version;
  uint8_t timeout_connect_seconds;
  struct{
    uint8_t is_client:1;
    uint8_t is_dhcp:1;
  };
};

union eeprom_u{
  uint8_t raw_byte[sizeof(struct memory_s)];
  struct memory_s preferences;
} eeprom;

void writeEEPROM(uint8_t size, uint8_t* data){
  EEPROM.begin( size );
  
  for (uint8_t i; i < size; i++){
    EEPROM.write(i, data[i]);
  }
  EEPROM.end();
}

void resetPreferences(){
  //data on eeprom is invalid. set default values ans save it
  eeprom.preferences.ssid[0] = 'E';
  eeprom.preferences.ssid[1] = 'S';
  eeprom.preferences.ssid[2] = 'P';
  eeprom.preferences.ssid[3] = '\0';
  
  eeprom.preferences.password[0] = '\0';

  eeprom.preferences.ip = 192 << 24 | 168 << 16 | 0 << 8 | 123;

  eeprom.preferences.netmask = 255 << 24 | 255 << 16 | 255 << 8 | 0;

  eeprom.preferences.gateway = 192 << 24 | 168 << 16 | 0 << 8 | 1;

  eeprom.preferences.baudrate = 115200;

  eeprom.preferences.version = 1;

  eeprom.preferences.timeout_connect_seconds = 10;

  eeprom.preferences.is_client = 0;
  eeprom.preferences.is_dhcp = 0;
}

bool validatePreferences(struct memory_s *p){
  bool ok = true;
  switch (p->version){
    case 1:
      //ok
      break;
    default:
      ok = false;
  }

  return false;
}

void readPreferences(){
  static uint8_t size = sizeof(eeprom.preferences);
  EEPROM.begin( size );
  
  for (uint8_t i; i < size; i++){
    eeprom.raw_byte[i] = EEPROM.read(i);
  }
  EEPROM.end();

  if ( !validatePreferences( &(eeprom.preferences) ) ){
    resetPreferences();
    writeEEPROM(size, eeprom.raw_byte);
  }
}

void tryConnect(bool is_client){
  char ssid[33], password[65];
  
  for (uint8_t i = 0; i < 32; i++){
    ssid[i] = eeprom.preferences.ssid[i];
    if (eeprom.preferences.ssid[i] == '/0');
  }
  ssid[32] = '\0'; //be sure there is an end of string

  for (uint8_t i = 0; i < 64; i++){
    ssid[i] = eeprom.preferences.password[i];
  }
  password[64] = '\0'; //be sure there is an end of string

  if (is_client){
    if (DEBUG) Serial.printf("CONNECTING AS CLIENT TO %s %s\n", ssid, password);
    WiFi.begin(ssid, password);
  }else{
    if (DEBUG) Serial.printf("CONNECTING AS AP %s %s\n", ssid, password);
    WiFi.softAP(ssid, password);
  }

  if (DEBUG) Serial.println("CONNECTING");

  //eventully set static IP
  if (! (eeprom.preferences.is_dhcp && is_client) ){
    if (DEBUG) Serial.println("CONFIG NOT DHCP OR AP");
    WiFi.config( IPAddress(eeprom.preferences.ip), IPAddress(eeprom.preferences.netmask), IPAddress(eeprom.preferences.gateway) );
  }

  bool status = true;
  uint64_t endTimeout = millis() + eeprom.preferences.timeout_connect_seconds * 1000UL;
  while (WiFi.status() != WL_CONNECTED && (eeprom.preferences.timeout_connect_seconds == 0 || millis() < endTimeout) ){
    if (DEBUG) Serial.println("CONNECTING...");
    delay(500);
    digitalWrite(LED_BUILTIN, !status);
    status = !status;
  }
  
  if (DEBUG) (WiFi.status() == WL_CONNECTED)? Serial.println("CONNECTED"):Serial.println("NOT CONNECTED");
}

void setup()
{

  if (DEBUG) Serial.println("START");
  
  readPreferences();
  if (DEBUG) resetPreferences();

  if (DEBUG) Serial.println("setting RX buffer");
  Serial.setRxBufferSize(1024);
  if (DEBUG) Serial.printf("setting baudrate to %d\n", eeprom.preferences.baudrate);
  Serial.begin(eeprom.preferences.baudrate, SERIAL_8E1);

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, !LOW);

  if (eeprom.preferences.is_client){
    tryConnect(1);
    if (WiFi.status() != WL_CONNECTED){
      resetPreferences();
    }
  }
  
  if (WiFi.status() != WL_CONNECTED){
    tryConnect(0);
  }

  while (WiFi.status() != WL_CONNECTED){
    if (DEBUG) Serial.println("Not even AP is connected!");
    delay(500);
  }

  //start server
  server.begin();
  configuration.begin();
  digitalWrite(LED_BUILTIN, !HIGH);
  if (DEBUG) Serial.println("Server started");
}


void loop(){
  
  WiFiClient client = server.available(); 
  
  if (client){
    digitalWrite(LED_BUILTIN, !LOW);

    //flush all the buffer
    while(Serial.available()){
      Serial.read();
    }
    
    while (client.connected()){

      while ( client.available() )
      {
        Serial.write( client.read() );
      }
    
      while ( Serial.available() )
      {
        client.write( Serial.read() );
      }
    }
    digitalWrite(LED_BUILTIN, !HIGH);
  }

  //look if we have configuration connection
  client = configuration.available();
  if (client){
    while (client.connected()){
      static uint8_t size = sizeof(eeprom.preferences);
      if ( client.read() == 'R' ){
        //write preferences to client
        for (uint8_t i; i < size; i++){
          client.write( eeprom.raw_byte[i] );
        }
      }
      if ( client.read() == 'W' ){
        //read preferences from client
        union eeprom_u tmp;
        for (uint8_t i; i < size; i++){
          eeprom.raw_byte[i] = client.write( tmp.raw_byte[i] );
        }
    
        if ( validatePreferences( &(tmp.preferences) ) ){
          writeEEPROM(size, tmp.raw_byte);
          readPreferences();
        }
      }
    }
  }
}
