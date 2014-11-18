#include <UIPEthernet.h> // Used for Ethernet
#include <DHT22.h>
#include <stdint.h>
#include <OneWire.h>
#include <JsonGenerator.h>
#include <DallasTemperature.h>

#define DHT22_PIN                 31
#define REPORT_INTERVAL           10000
#define NUM_OF_DS18B20_SENSORS    10
#define NUM_OF_DHT22_SENSORS      1
#define DIGITAL_INPUT_SENSOR      3  // The digital input you attached your light sensor.  (Only 2 and 3 generates interrupt!)
#define PULSE_FACTOR              10000      // Nummber of blinks per KWH
#define MAX_WATT                  30000
#define INTERRUPT                 DIGITAL_INPUT_SENSOR-2 // Usually the interrupt = pin -2 (on uno/nano anyway)

#define POWER_SEND_FREQUENCY      2000; // Minimum time between send (in milliseconds)

#define NUM_OF_STATUS_STRINGS     8 /*For DHT22*/


using namespace ArduinoJson::Generator;

IPAddress const serverIp   = IPAddress(192,168,1,21);
IPAddress const myIp       = IPAddress(192,168,1,99);
uint16_t  const serverPort = 55555;


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
uint16_t localUdpPort             = 7000;

#define USE_UDP

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
byte mac[] = { 0x54, 0x34, 0x41, 0x30, 0x30, 0x32 };                                      
// For the rest we use DHCP (IP address and such)

EthernetUDP udp;

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

    Ethernet.begin(mac, myIp);

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
#ifdef USE_UDP
#else
    client.print("ser=");
    client.print(serial);
    client.print("&st=");
    client.print(statusStr);
    client.print("&temp=");
    client.print(temperature);
    client.print("&hum=");
    client.print(humidity);
    client.print("&");
#endif
}



long lastReadingTime = 0;

JsonArray<NUM_OF_DS18B20_SENSORS + NUM_OF_DHT22_SENSORS> temperatureReportArray;
JsonObject<4> reportObjectTempOnly[NUM_OF_DS18B20_SENSORS];
JsonObject<5> reportObjectTempAndHum[NUM_OF_DHT22_SENSORS];

void ReportAllSensors(void) {
    int8_t i;
    uint8_t cnt = 0;
    
    
    for(i=0; i<NUM_OF_DS18B20_SENSORS; i++) {
        uint8_t statusidx = 0;
        sensors.getTempC(tempsensors[i].address);
        float tempC = sensors.getTempC(tempsensors[i].address);
        if (tempC == NAN) {
            statusidx = 2;
            tempC = -99.0;
        } else if (tempC == 85.0) {
            statusidx = 1;
            tempC = -99.0;
        }
        reportObjectTempOnly[i]["sensor"] = "TempOnly";
        reportObjectTempOnly[i]["id"]     = tempsensors[i].name;
        reportObjectTempOnly[i]["st"]     = statusStrings[statusidx];
        reportObjectTempOnly[i]["temp"]   = tempC;
        temperatureReportArray.add(reportObjectTempOnly[i]);
    }
    for(i=0; i<NUM_OF_DHT22_SENSORS; i++) {
        
        reportObjectTempAndHum[i]["sensor"] = "TempAndHum";
        reportObjectTempAndHum[i]["id"]     = dhtSensors[i].name;
        reportObjectTempAndHum[i]["st"]     = statusStrings[dhtSensors[i].statusIdx];
        reportObjectTempAndHum[i]["temp"]   = dhtSensors[i].temperature;
        reportObjectTempAndHum[i]["hum"]    = dhtSensors[i].humidity;
        temperatureReportArray.add(reportObjectTempAndHum[i]);
    }

    udp.beginPacket(serverIp, serverPort);
    
    temperatureReportArray.printTo(udp);    
    udp.endPacket();

    Serial.println(temperatureReportArray);
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

        //udp.beginPacket(udp.remoteIP(), 7001);
        JsonArray<1> array;

        JsonObject<5> reportObject;

        reportObject["sensor"] = "power";
        reportObject["id"]     = "main";
        reportObject["st"]     = "ok";
        reportObject["power"]  = (double)watt;
        reportObject["energy"] = kwh;
        array.add(reportObject);

        Serial.println(array);

        udp.beginPacket(serverIp, serverPort);
        
        array.printTo(udp);    
        udp.endPacket();

        lastSend = now;
    }  
}


void ReadTempMeas() { 
    DHT22_ERROR_t errorCode;
    uint8_t statusStrIdx = 0;
    sensors.requestTemperatures();

    errorCode = myDHT22.readData();
    dhtSensors[0].temperature = -99.9; /*Set to indicate error*/
    dhtSensors[0].humidity    = -99.9; /*Set to indicate error*/
    switch(errorCode) {
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
}



void loop() {
    unsigned long now = millis();
    bool sendTime = (now - tempLastSend) > REPORT_INTERVAL;
    
    SendPowerMeas();
    
    if (sendTime) {
        ReadTempMeas();
        ReportAllSensors();
        tempLastSend  = now;
    }
}
