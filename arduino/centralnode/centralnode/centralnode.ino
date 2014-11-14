#include <UIPEthernet.h> // Used for Ethernet
#include <DHT22.h>
#include <stdint.h>
#include <OneWire.h>
#include <DallasTemperature.h>

#define DHT22_PIN 31
#define REPORT_INTERVAL           10000
#define NUM_OF_DS18B20_SENSORS    10
#define NUM_OF_DHT22_SENSORS      1
#define DIGITAL_INPUT_SENSOR      3  // The digital input you attached your light sensor.  (Only 2 and 3 generates interrupt!)
#define PULSE_FACTOR              10000      // Nummber of blinks per KWH
#define MAX_WATT                  30000
#define INTERRUPT                 DIGITAL_INPUT_SENSOR-2 // Usually the interrupt = pin -2 (on uno/nano anyway)

#define POWER_SEND_FREQUENCY      2000; // Minimum time between send (in milliseconds)


// Data wire is plugged into port 2 on the Arduino
#define ONE_WIRE_BUS              32
#define TEMPERATURE_PRECISION     11

double ppwh = ((double)PULSE_FACTOR)/1000;

volatile unsigned long pulseCount = 0;   
volatile unsigned long lastBlink  = 0;
volatile unsigned long watt       = 0;
unsigned long oldPulseCount       = 0;   
unsigned long oldWatt             = 0;
double oldKwh;
unsigned long lastSend;
unsigned long tempLastSend;

// Setup a oneWire instance to communicate with any OneWire devices (not just Maxim/Dallas temperature ICs)
OneWire oneWire(ONE_WIRE_BUS);

// Pass our oneWire reference to Dallas Temperature. 
DallasTemperature sensors(&oneWire);

typedef struct sensors_s {
  DeviceAddress address;
  char          name[10];
}
sensors_s;

typedef struct dhtsensor_s {
  char          name[9];
  uint8_t       statusIdx;
  float         temperature;
  float         humidity;
} 
dhtsensor_s;

dhtsensor_s dhtSensors[NUM_OF_DHT22_SENSORS] = {
  {"40903070", 0, -100.0, -100.0}
};

sensors_s tempsensors[NUM_OF_DS18B20_SENSORS] = {
  0x28, 0xFF, 0x88, 0x09, 0x11, 0x14, 0x00, 0x18, "11140018", //"Kaukolampo Meno"
  0x28, 0x3C, 0x53, 0xA3, 0x05, 0x00, 0x00, 0x41, "05000041", //"Kaukolampo Paluu"
  0x28, 0xFF, 0x7F, 0x8B, 0x10, 0x14, 0x00, 0x9A, "1014009A", //"Lammitys Meno"
  0x28, 0xEE, 0x53, 0xA3, 0x05, 0x00, 0x00, 0xEB, "050000EB", //"Lammitys Paluu"
  0x10, 0x98, 0x4A, 0x7F, 0x01, 0x08, 0x00, 0x50, "01080050",  //"Tekn tila" 
  0x28, 0x90, 0xDC, 0x5F, 0x03, 0x00, 0x00, 0x76, "90DC5F03",
  0x28, 0xFE, 0x10, 0x60, 0x03, 0x00, 0x00, 0x77, "FE106003",
  0x28, 0xCD, 0x14, 0x60, 0x03, 0x00, 0x00, 0xDC, "CD146003",
  0x28, 0xCB, 0x11, 0x60, 0x03, 0x00, 0x00, 0xBC, "CB116030",
  0x28, 0xAB, 0xED, 0x5F, 0x03, 0x00, 0x00, 0x2A, "ABED5F03"
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


void powerMeasPulse()     
{ 
    unsigned long newBlink = micros();  
    unsigned long interval = newBlink-lastBlink;
    if (interval<50000L) { // Sometimes we get interrupt on RISING
        return;
    }
    watt = (3600000000.0 /interval) / ppwh;
    lastBlink = newBlink;
    pulseCount++;
}


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

  for(i=0; i<NUM_OF_DS18B20_SENSORS; i++) {
      sensors.setResolution(tempsensors[i].address, TEMPERATURE_PRECISION);
  }
  for(i=0; i<NUM_OF_DS18B20_SENSORS; i++) {
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
  attachInterrupt(INTERRUPT, powerMeasPulse, RISING);
  lastSend     = millis();
  tempLastSend = millis();
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

#define NUM_OF_STATUS_STRINGS   8

char const * const statusStrings[NUM_OF_STATUS_STRINGS] = {
   "ok",
   "busError",
   "sensorLost",
   "ackTimeout",
   "syncTimeout",
   "dataTimeout",
   "undefinedErr",
   "checkSumErr"
};

long lastReadingTime = 0;

void reportAllSensors(void) {
  int8_t i;
  for(i=0; i<NUM_OF_DS18B20_SENSORS; i++) {
    sensors.getTempC(tempsensors[i].address);
    float tempC = sensors.getTempC(tempsensors[i].address);
    SendToClient(tempsensors[i].name, statusStrings[0], tempC, 0.0);
    Serial.println("reportAllSensors loop DONE");

  }
}




void SendPowerMeas() { 
  unsigned long now = millis();
  bool sendTime = (now - lastSend) > POWER_SEND_FREQUENCY;
  if (sendTime) {
      double kwh = ((double)pulseCount/((double)PULSE_FACTOR));     
      oldPulseCount = pulseCount;
      if (kwh != oldKwh) {
          oldKwh = kwh;
      }

      if (client.connect(server, 8888)) {
        Serial.println("-> Connected");
        // Make a HTTP request:
        client.print("GET /reportpower?watt=");
        client.print(watt);
        client.print("&kwh=");
        client.print(kwh);
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
      Serial.print("Watt:");
      Serial.print(watt);
      Serial.print(" khW:");
      Serial.println(kwh);
    lastSend = now;
    }  
}


void SendTempMeas() { 
  unsigned long now = millis();
  bool sendTime = (now - tempLastSend) > REPORT_INTERVAL;
  if (sendTime) {
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
      dhtSensors[0].temperature = -99.9; /*Set to indicate error*/
      dhtSensors[0].humidity    = 0.0;
      switch(errorCode)
      {
          case DHT_ERROR_NONE:
            dhtSensors[0].statusIdx = 0;
            dhtSensors[0].temperature = myDHT22.getTemperatureC();
            dhtSensors[0].humidity    = myDHT22.getHumidity();
            break;
          case DHT_ERROR_CHECKSUM:
            dhtSensors[0].statusIdx = 7;
          case DHT_BUS_HUNG:
            dhtSensors[0].statusIdx = 1;
            break;
          case DHT_ERROR_NOT_PRESENT:
            dhtSensors[0].statusIdx = 2;
            break;
          case DHT_ERROR_ACK_TOO_LONG:
            dhtSensors[0].statusIdx = 3;
            break;
          case DHT_ERROR_SYNC_TIMEOUT:
            dhtSensors[0].statusIdx = 4;
            break;
          case DHT_ERROR_DATA_TIMEOUT:
            dhtSensors[0].statusIdx = 5;
            break;
          case DHT_ERROR_TOOQUICK:
            dhtSensors[0].statusIdx = 6;
            break;
      }
      Serial.print("DHT: ");
      Serial.print(dhtSensors[0].temperature);
      Serial.print(", ");
      Serial.print(dhtSensors[0].humidity);
      Serial.print(", ");
      Serial.println(dhtSensors[0].statusIdx);
    
      if (client.connect(server, 8888)) {
        Serial.println("-> Connected");
        // Make a HTTP request:
        client.print( "GET /report?");
        SendToClient("40903070", 
                     statusStrings[dhtSensors[0].statusIdx], 
                     dhtSensors[0].temperature, 
                     dhtSensors[0].humidity);
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
  }
}



void loop() {
  SendPowerMeas();
  SendTempMeas();
}
