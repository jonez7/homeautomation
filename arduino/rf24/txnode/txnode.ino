#include <RF24Network.h>
#include <RF24.h>
#include <SPI.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <LowPower.h>

#define TX_PERIOD    10
#define ONE_WIRE_BUS 2
#define CE_PIN       9
#define CSN_PIN      10

// nRF24L01(+) radio attached to SPI and pins 9 & 10
RF24 radio(CE_PIN, CSN_PIN); // Create a Radio

inline void serial_begin(int _baud)
{
  Serial.begin(_baud);
}
#define board_start printf
#define toggleLED(x) (x)

// Network uses that radio
RF24Network network(radio);

// Address of our node
const uint16_t this_node = 1;

// Address of the other node
const uint16_t other_node = 0;

// Setup a oneWire instance to communicate with any OneWire devices (not just Maxim/Dallas temperature ICs)
OneWire oneWire(ONE_WIRE_BUS);

// Pass our oneWire reference to Dallas Temperature. 
DallasTemperature sensors(&oneWire);

// arrays to hold device address
DeviceAddress insideThermometer;

float   g_temperature   = -1000;
uint8_t g_wakeupcnt     = 0;


typedef struct message_s {
    uint8_t     type;
    uint8_t     myId;
    uint8_t     seq;
    uint8_t     spare;
    float       value;
}message_s;

message_s  message;


void setup(void) {
  serial_begin(57600);

  sensors.begin();
  if (!sensors.getAddress(insideThermometer, 0)) {
      g_temperature = -1000;  /*Report error this way */
  }
  // set the resolution to 9 bit (Each Dallas/Maxim device is capable of several different resolutions)
  sensors.setResolution(insideThermometer, 10);
 
  SPI.begin();
  radio.begin();
  network.begin(/*channel*/ 90, /*node address*/ this_node);
}




void loop()
{
  g_wakeupcnt++;
  if(g_wakeupcnt > TX_PERIOD) {
      // Pump the network regularly
      network.update();

      sensors.requestTemperatures(); // Send the command to get temperatures

      message.value = sensors.getTempC(insideThermometer);
      message.seq++;
      message.myId = this_node;
      message.type = 1;
      RF24NetworkHeader header(/*to node*/ other_node);

      bool ok = network.write(header,&message,sizeof(message_s));
  
      radio.powerDown();
      g_wakeupcnt = 0;
//      toggleLED();
  } 
  LowPower.powerDown(SLEEP_8S, ADC_OFF, BOD_OFF);
}//--(end main loop )---


