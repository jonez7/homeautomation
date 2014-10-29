#include <UIPEthernet.h> // Used for Ethernet
#include <DHT22.h>
#include <stdint.h>
#include <OneWire.h>
#include <DallasTemperature.h>

#define DHT22_PIN 31

// Data wire is plugged into port 2 on the Arduino
#define ONE_WIRE_BUS 32
#define TEMPERATURE_PRECISION 11

// Setup a oneWire instance to communicate with any OneWire devices (not just Maxim/Dallas temperature ICs)
OneWire oneWire(ONE_WIRE_BUS);

// Pass our oneWire reference to Dallas Temperature. 
DallasTemperature sensors(&oneWire);

typedef struct sensors_s {
  DeviceAddress address;
  char          name[30];
}sensors_s;


sensors_s tempsensors[5] = {
   0x28, 0xFF, 0x88, 0x09, 0x11, 0x14, 0x00, 0x18, "11140018", //"Kaukolampo Meno",
   0x28, 0x3C, 0x53, 0xA3, 0x05, 0x00, 0x00, 0x41, "05000041", //"Kaukolampo Paluu",
   0x28, 0xFF, 0x7F, 0x8B, 0x10, 0x14, 0x00, 0x9A, "1014009A", //"Lammitys Meno",
   0x28, 0xEE, 0x53, 0xA3, 0x05, 0x00, 0x00, 0xEB, "050000EB", //"Lammitys Paluu",
   0x10, 0x98, 0x4A, 0x7F, 0x01, 0x08, 0x00, 0x50, "01080050"  //"Ulko" 
};


// Setup a DHT22 instance
DHT22 myDHT22(DHT22_PIN);

// **** ETHERNET SETTING ****
// Arduino Uno pins: 10 = CS, 11 = MOSI, 12 = MISO, 13 = SCK
// Ethernet MAC address - must be unique on your network - MAC Reads T4A001 in hex (unique in your network)
byte mac[] = { 0x54, 0x34, 0x41, 0x30, 0x30, 0x31 };                                      
// For the rest we use DHCP (IP address and such)

EthernetClient client;
char server[] = "192.168.1.21"; // IP Adres (or name) of server to dump data to
int  interval = 2000; // Wait between dumps


void InitDsSensors(void) {
  int8_t i;
  sensors.begin();

  // locate devices on the bus
  Serial.print("Locating devices...");
  Serial.print("Found ");
  Serial.print(sensors.getDeviceCount(), DEC);
  Serial.println(" devices.");

  // report parasite power requirements
  Serial.print("Parasite power is: "); 
  if (sensors.isParasitePowerMode()) Serial.println("ON");
  else Serial.println("OFF");

  // show the addresses we found on the bus
  Serial.print("Device 0 Address: ");
  //printAddress(lammitysMeno);
  Serial.println();

  Serial.print("Device 1 Address: ");
  //printAddress(lammitysTulo);
  Serial.println();

  for(i=0; i<5; i++) {
      sensors.setResolution(tempsensors[i].address, TEMPERATURE_PRECISION);
  }
  for(i=0; i<5; i++) {
    Serial.print("Device Resolution: ");
    Serial.print(sensors.getResolution(tempsensors[i].address), DEC); 
    Serial.println();
  }
}

void setup() {

  Serial.begin(9600);
  Ethernet.begin(mac);

  Serial.print("IP Address        : ");
  Serial.println(Ethernet.localIP());
  Serial.print("Subnet Mask       : ");
  Serial.println(Ethernet.subnetMask());
  Serial.print("Default Gateway IP: ");
  Serial.println(Ethernet.gatewayIP());
  Serial.print("DNS Server IP     : ");
  Serial.println(Ethernet.dnsServerIP());
  InitDsSensors();
}

void SendToClient(char const * const serial,
                  char const * const statusStr,
                  float const temperature,
                  float const humidity) {
    client.print("ser=");
    client.print(serial);
    client.print("&st=");
    client.print(statusStr);
    client.print("&temp=");
    client.print(temperature);
    client.print("&hum=");
    client.print(humidity);
    client.print("&");
}

#define NUM_OF_STATUS_STRINGS   7

char * statusStrings[NUM_OF_STATUS_STRINGS] = {
   "ok",
   "busError",
   "sensorLost",
   "ackTimeout",
   "syncTimeout",
   "dataTimeout",
   "undefinedErr"
};

long lastReadingTime = 0;

void reportAllSensors(void) {
  int8_t i;
  int8_t j;
  for(i=0; i<5; i++) {
    sensors.getTempC(tempsensors[i].address);
    float tempC = sensors.getTempC(tempsensors[i].address);
    char tmp[16];
    SendToClient(tempsensors[i].name, statusStrings[0], tempC, 0.0);
    Serial.println("reportAllSensors loop DONE");

  }
}


void loop() {


  DHT22_ERROR_t errorCode;
  // if you get a connection, report back via serial:
  uint8_t statusStrIdx = 0;


  if (millis() - lastReadingTime > 1000) {
      Serial.print("Requesting temperatures...");
      sensors.requestTemperatures();
//      printTemperature(lammitysMeno);
      Serial.println("DONE");
      lastReadingTime = millis();
    }



  errorCode = myDHT22.readData();
  switch(errorCode)
  {
    case DHT_ERROR_NONE:
    case DHT_ERROR_CHECKSUM:
        statusStrIdx = 0;
    break;
    case DHT_BUS_HUNG:
        statusStrIdx = 1;
      break;
    case DHT_ERROR_NOT_PRESENT:
        statusStrIdx = 2;
      break;
    case DHT_ERROR_ACK_TOO_LONG:
        statusStrIdx = 3;
      break;
    case DHT_ERROR_SYNC_TIMEOUT:
        statusStrIdx = 4;
      break;
    case DHT_ERROR_DATA_TIMEOUT:
        statusStrIdx = 5;
      break;
    case DHT_ERROR_TOOQUICK:
        statusStrIdx = 6;
      break;
  }



  if (client.connect(server, 8888)) {
    Serial.println("-> Connected");
    // Make a HTTP request:
    client.print( "GET /report?");
    SendToClient("40903070", statusStrings[statusStrIdx], (float)myDHT22.getTemperatureCInt()/10, (float)myDHT22.getHumidityInt()/10);
    reportAllSensors();
    client.println( " HTTP/1.1");
    client.print( "Host: " );
    client.println(server);
    client.println( "Connection: close" );
    client.println();
    client.println();
    client.stop();
  }
  else {
    // you didn't get a connection to the server:
    Serial.println("--> connection failed/n");
  }

  delay(interval);
}
