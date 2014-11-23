#include <SPI.h>         
#include <Ethernet.h>
#include <EthernetUdp.h>
#include <DHT22.h>
#include <stdint.h>
#include <OneWire.h>
#include <JsonGenerator.h>
#include <JsonParser.h>
#include <DallasTemperature.h>
#include <RF24Network.h>
#include <RF24.h>
#include <EEPROM.h>

#define DEFAULT_REPORT_INTERVAL       10000
#define DEFAULT_POWER_SEND_INTERVAL   2000
#define DEFAULT_MY_PORT               55555
#define DEFAULT_SERVER_PORT           55555
#define DHT22_PIN                 31

#define NUM_OF_DS18B20_SENSORS    10
#define NUM_OF_DHT22_SENSORS      1
#define DIGITAL_INPUT_SENSOR      3  // The digital input you attached your light sensor.  (Only 2 and 3 generates interrupt!)
#define PULSE_FACTOR              10000      // Nummber of blinks per KWH
#define MAX_WATT                  30000
#define INTERRUPT                 DIGITAL_INPUT_SENSOR-2 // Usually the interrupt = pin -2 (on uno/nano anyway)
//#define UDP_TX_PACKET_MAX_SIZE    128
#define NUM_OF_STATUS_STRINGS     8 /*For DHT22*/
#define CE_PIN                    40
#define CSN_PIN                   41

#define ONE_WIRE_BUS              32
#define TEMPERATURE_PRECISION     11

typedef struct Config_s {
    IPAddress    serverIp;
    IPAddress    localIp;
    uint16_t     serverPort;
    uint16_t     localPort;
    uint16_t     reportInterval;
    uint16_t     powerReportInterval;
    uint8_t      radioCePin;
    uint8_t      radioCsnPin;
    uint8_t      dht22Pin;
    uint8_t      oneWireBus;
}Config_s;

Config_s         configuration;

using namespace ArduinoJson;
/*
IPAddress const serverIp   = IPAddress(192,168,0,244);
IPAddress const myIp       = IPAddress(192,168,0,99);
uint16_t  const serverPort = 55555;
uint16_t  const localPort  = 55555;
*/
RF24 radio(CE_PIN, CSN_PIN); // Create a Radio

// Network uses that radio
RF24Network network(radio);

// Address of our node
const uint16_t this_node = 0;

// Address of the other node
const uint16_t other_node = 1;



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
byte mac[] = { 0x54, 0x34, 0x41, 0x30, 0x30, 0x32 };                                      

EthernetUDP udp;

char rxPacketBuffer[UDP_TX_PACKET_MAX_SIZE];


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


void WritePersistent(void) {
    char tmp[20];
    uint8_t i;
    uint8_t j;
    uint16_t address = 0;
    uint8_t * srcPtr;

    EEPROM.write(address, 123);
    address++;

    srcPtr = (uint8_t*)&configuration;
    for(j=0; j<sizeof(configuration); j++) {
        EEPROM.write(address, srcPtr[j]);
        address++;
    }
}

void ReadPersistent(void) {
    char tmp[20];
    uint8_t i;
    uint8_t j;
    uint16_t address = 0;
    uint8_t * dstPtr;
    uint8_t eepromWritten = EEPROM.read(address); 
    address++;
    
    if (eepromWritten == 123) {
        /*Read meas resistor value */
        dstPtr = (uint8_t*)&configuration;
        for(j=0; j<sizeof(configuration); j++) {
            dstPtr[j] = EEPROM.read(address);
            address++;
        }
    } else {
        /* Use defaults */
        configuration.serverIp               = IPAddress(192,168,0,244);
        configuration.serverPort             = DEFAULT_SERVER_PORT;
        configuration.localIp                = IPAddress(192,168,0,99);
        configuration.localPort              = DEFAULT_MY_PORT;
        configuration.reportInterval         = DEFAULT_REPORT_INTERVAL;
        configuration.powerReportInterval    = DEFAULT_POWER_SEND_INTERVAL;
        configuration.radioCePin             = CE_PIN;
        configuration.radioCsnPin            = CSN_PIN;
        configuration.dht22Pin               = DHT22_PIN;
        configuration.oneWireBus             = ONE_WIRE_BUS;
    }
}

void setup() {
    ReadPersistent();

    Serial.begin(9600);

    Ethernet.begin(mac, configuration.localIp);
    udp.begin(configuration.localPort);

    radio.begin();
    network.begin(/*channel*/ 90, /*node address*/ this_node);

    Serial.print("IP Address        : ");
    Serial.println(Ethernet.localIP());
    Serial.print("Subnet Mask       : ");
    Serial.println(Ethernet.subnetMask());
    Serial.print("Default Gateway IP: ");
    Serial.println(Ethernet.gatewayIP());
    Serial.print("DNS Server IP     : ");
    Serial.println(Ethernet.dnsServerIP());
//    InitDsSensors();
    attachInterrupt(INTERRUPT, powerMeasPulse, RISING);
    lastSend     = millis();
    tempLastSend = millis();
}


long lastReadingTime = 0;

Generator::JsonArray<NUM_OF_DS18B20_SENSORS + NUM_OF_DHT22_SENSORS> temperatureReportArray;
Generator::JsonObject<4> reportObjectTempOnly[NUM_OF_DS18B20_SENSORS];
Generator::JsonObject<5> reportObjectTempAndHum[NUM_OF_DHT22_SENSORS];

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

    udp.beginPacket(configuration.serverIp, configuration.serverPort);
    
    temperatureReportArray.printTo(udp);    
    udp.endPacket();

    Serial.println(temperatureReportArray);
}

void SendPowerMeas() { 
    unsigned long now = millis();
    bool sendTime = (now - lastSend) > configuration.powerReportInterval;
    if (sendTime) {
        double kwh = ((double)pulseCount/((double)PULSE_FACTOR));     
        oldPulseCount = pulseCount;
        if (kwh != oldKwh) {
            oldKwh = kwh;
        }

        //udp.beginPacket(udp.remoteIP(), 7001);
        Generator::JsonArray<1> array;

        Generator::JsonObject<5> reportObject;

        reportObject["sensor"] = "power";
        reportObject["id"]     = "main";
        reportObject["st"]     = "ok";
        reportObject["power"]  = (double)watt;
        reportObject["energy"] = kwh;
        array.add(reportObject);

        Serial.println(array);

        udp.beginPacket(configuration.serverIp, configuration.serverPort);
        
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

void checkUdpPackets() {
    // if there's data available, read a packet
    int16_t packetSize = udp.parsePacket();

    if(packetSize) {
        Serial.print("Received packet of size ");
        Serial.println(packetSize);
        Serial.print("From ");
        IPAddress remote = udp.remoteIP();
        for (int i =0; i < 4; i++) {
            Serial.print(remote[i], DEC);
        if (i < 3) {
        Serial.print(".");
        }
    }
    Serial.print(", port ");
    Serial.println(udp.remotePort());
    
    // read the packet into packetBufffer
    udp.read(rxPacketBuffer,UDP_TX_PACKET_MAX_SIZE);

    Parser::JsonParser<32> parser;
    
    Parser::JsonObject root = parser.parse(rxPacketBuffer);

    udp.beginPacket(udp.remoteIP(), udp.remotePort());

    Serial.println("Contents:");
    Serial.println(rxPacketBuffer);

    if (root.success()) {
        long value = root["PwrInterval"];
        configuration.powerReportInterval = value;
        udp.write("PowerReportInterval set to ");
        udp.write(configuration.powerReportInterval);
        WritePersistent();
    } else {
        udp.write("Parsing failed");
        Serial.println("parsing failed");
    }
    udp.endPacket();
    }
}


typedef struct message_s {
    uint8_t     type;
    uint8_t     myId;
    uint8_t     seq;
    uint8_t     spare;
    float       value1;
    float       value2;
}message_s;

message_s  message;

void ReceiveRF24(void)
{
    // Pump the network regularly
    network.update();
    // Is there anything ready for us?
    while ( network.available() )
    {
        // If so, grab it and print it out
        RF24NetworkHeader header;
        network.read(header,&message,sizeof(message));
        Serial.print("type: ");
        Serial.print(message.type);
        Serial.print(", Id: ");
        Serial.print(message.myId);
        Serial.print(", seq: ");
        Serial.print(message.seq);
        Serial.print(", value1: ");
        Serial.print(message.value1);
        Serial.print(", value2: ");
        Serial.println(message.value2);
    }
}


void loop() {
    unsigned long now = millis();
    bool sendTime = (now - tempLastSend) > configuration.reportInterval;
    
    SendPowerMeas();
    
    if (sendTime) {
        ReadTempMeas();
        ReportAllSensors();
        tempLastSend  = now;
    }
    ReceiveRF24();
    checkUdpPackets();
}


