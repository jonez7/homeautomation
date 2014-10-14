/*
 Copyright (C) 2011 James Coliz, Jr. <maniacbug@ymail.com>

 This program is free software; you can redistribute it and/or
 modify it under the terms of the GNU General Public License
 version 2 as published by the Free Software Foundation.
 */

#include <RF24Network.h>
#include <RF24.h>
#include <SPI.h>

#define CE_PIN       9
#define CSN_PIN      10


// nRF24L01(+) radio attached to SPI and pins 9 & 10
RF24 radio(CE_PIN, CSN_PIN); // Create a Radio

// Network uses that radio
RF24Network network(radio);

// Address of our node
const uint16_t this_node = 0;

// Address of the other node
const uint16_t other_node = 1;

void setup(void)
{
  Serial.begin(57600);
  Serial.println("RF24Network/examples/helloworld_rx/");
 
  SPI.begin();
  radio.begin();
  network.begin(/*channel*/ 90, /*node address*/ this_node);
}

typedef struct message_s {
    uint8_t     type;
    uint8_t     myId;
    uint8_t     seq;
    uint8_t     spare;
    float       value;
}message_s;

message_s  message;



void loop(void)
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
    Serial.print(", value: ");
    Serial.println(message.value);
  }
}
