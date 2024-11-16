#include "BluetoothSerial.h"
BluetoothSerial SerialBT;

#include <SPI.h>
#include <DW1000Ng.hpp>
#include <DW1000NgUtils.hpp>
#include <DW1000NgRanging.hpp>

// 필설정
const uint8_t PIN_SCK = 18;
const uint8_t PIN_MOSI = 23;
const uint8_t PIN_MISO = 19;
const uint8_t PIN_SS = 4;
const uint8_t PIN_RST = 15;
const uint8_t PIN_IRQ = 17;

//const uint8_t PIN_dist_out = 27; // anchor로 거리 output할 pin

// messages used in the ranging protocol
// TODO replace by enum
#define POLL 0
#define POLL_ACK 1
#define RANGE 2
#define RANGE_REPORT 3
#define RANGE_FAILED 255


#define BUZZER_PIN 27//4
//#define InPin 33 //UWB로부터 거리 받음

// message flow state
volatile byte expectedMsgId = POLL;
// message sent/received state
volatile boolean sentAck = false;
volatile boolean receivedAck = false;
// protocol error state
boolean protocolFailed = false;
// timestamps to remember
uint64_t timePollSent;
uint64_t timePollReceived;
uint64_t timePollAckSent;
uint64_t timePollAckReceived;
uint64_t timeRangeSent;
uint64_t timeRangeReceived;

uint64_t timeComputedRange;
// last computed range/time
// data buffer
#define LEN_DATA 16
byte data[LEN_DATA];
// watchdog and reset period
uint32_t lastActivity;
uint32_t resetPeriod = 250;
// reply times (same on both sides for symm. ranging)
uint16_t replyDelayTimeUS = 3000;
// ranging counter (per second)
uint16_t successRangingCount = 0;
uint32_t rangingCountPeriod = 0;
float samplingRate = 0;

device_configuration_t DEFAULT_CONFIG = {
  false,
  true,
  true,
  true,
  false,
  SFDMode::STANDARD_SFD,
  Channel::CHANNEL_5,
  DataRate::RATE_850KBPS,
  PulseFrequency::FREQ_16MHZ,
  PreambleLength::LEN_256,
  PreambleCode::CODE_3
};

interrupt_configuration_t DEFAULT_INTERRUPT_CONFIG = {
  true,
  true,
  true,
  false,
  true
};

double dist;
int delay_time;

void setup() {
  // put your setup code here, to run once:
  pinMode(BUZZER_PIN, OUTPUT); // 부저 핀을 출력으로 설정
  Serial.begin(115200);
  
  /*내가 추가한 것*/
  //pinMode(PIN_dist_out, OUTPUT); //distance를 output

  /*          */

  pinMode(PIN_RST, OUTPUT);
  digitalWrite(PIN_RST, LOW);
  delay(100);
  digitalWrite(PIN_RST, HIGH);
  
  SerialBT.begin("UWB-DIST");
  delay(1000);
  Serial.println(F("### DW1000Ng-arduino-ranging-anchor ###"));
  // initialize the driver
  DW1000Ng::initialize(PIN_SS, PIN_IRQ, PIN_RST);
  Serial.println(F("DW1000Ng initialized ..."));
  // general configuration
  DW1000Ng::applyConfiguration(DEFAULT_CONFIG);
  DW1000Ng::applyInterruptConfiguration(DEFAULT_INTERRUPT_CONFIG);

  DW1000Ng::setDeviceAddress(1);

  DW1000Ng::setAntennaDelay(16436);

  Serial.println(F("Committed configuration ..."));
  // DEBUG chip info and registers pretty printed
  char msg[128];
  DW1000Ng::getPrintableDeviceIdentifier(msg);
  Serial.print("Device ID: "); Serial.println(msg);
  DW1000Ng::getPrintableExtendedUniqueIdentifier(msg);
  Serial.print("Unique ID: "); Serial.println(msg);
  DW1000Ng::getPrintableNetworkIdAndShortAddress(msg);
  Serial.print("Network ID & Device Address: "); Serial.println(msg);
  DW1000Ng::getPrintableDeviceMode(msg);
  Serial.print("Device mode: "); Serial.println(msg);
  // attach callback for (successfully) sent and received messages
  DW1000Ng::attachSentHandler(handleSent);
  DW1000Ng::attachReceivedHandler(handleReceived);
  // anchor starts in receiving mode, awaiting a ranging poll message

  receiver();
  noteActivity();
  // for first time ranging frequency computation
  rangingCountPeriod = millis();
}

void noteActivity() {
  // update activity timestamp, so that we do not reach "resetPeriod"
  lastActivity = millis();
}

void resetInactive() {
  // anchor listens for POLL
  expectedMsgId = POLL;
  receiver();
  noteActivity();
}

void handleSent() {
  // status change on sent success
  sentAck = true;
}

void handleReceived() {
  // status change on received success
  receivedAck = true;
}

void transmitPollAck() {
  data[0] = POLL_ACK;
  DW1000Ng::setTransmitData(data, LEN_DATA);
  DW1000Ng::startTransmit();
}

void transmitRangeReport(float curRange) {
  data[0] = RANGE_REPORT;
  // write final ranging result
  memcpy(data + 1, &curRange, 4);
  DW1000Ng::setTransmitData(data, LEN_DATA);
  DW1000Ng::startTransmit();
}

void transmitRangeFailed() {
  data[0] = RANGE_FAILED;
  DW1000Ng::setTransmitData(data, LEN_DATA);
  DW1000Ng::startTransmit();
}

void receiver() {
  DW1000Ng::forceTRxOff();
  // so we don't need to restart the receiver manually
  DW1000Ng::startReceive();
}


void loop() {
  int32_t curMillis = millis();
  if (!sentAck && !receivedAck) {
    // check if inactive
    if (curMillis - lastActivity > resetPeriod) {
      resetInactive();
    }
    return;
  }
  // continue on any success confirmation
  if (sentAck) {
    sentAck = false;
    byte msgId = data[0];
    if (msgId == POLL_ACK) {
      timePollAckSent = DW1000Ng::getTransmitTimestamp();
      noteActivity();
    }
    DW1000Ng::startReceive();
  }
  if (receivedAck) {
    receivedAck = false;
    // get message and parse
    DW1000Ng::getReceivedData(data, LEN_DATA);
    byte msgId = data[0];
    if (msgId != expectedMsgId) {
      // unexpected message, start over again (except if already POLL)
      protocolFailed = true;
    }
    if (msgId == POLL) {
      // on POLL we (re-)start, so no protocol failure
      protocolFailed = false;
      timePollReceived = DW1000Ng::getReceiveTimestamp();
      expectedMsgId = RANGE;
      transmitPollAck();
      noteActivity();
    }
    else if (msgId == RANGE) {
      timeRangeReceived = DW1000Ng::getReceiveTimestamp();
      expectedMsgId = POLL;
      if (!protocolFailed) {
        timePollSent = DW1000NgUtils::bytesAsValue(data + 1, LENGTH_TIMESTAMP);
        timePollAckReceived = DW1000NgUtils::bytesAsValue(data + 6, LENGTH_TIMESTAMP);
        timeRangeSent = DW1000NgUtils::bytesAsValue(data + 11, LENGTH_TIMESTAMP);
        // (re-)compute range as two-way ranging is done
        double distance = DW1000NgRanging::computeRangeAsymmetric(timePollSent,
                          timePollReceived,
                          timePollAckSent,
                          timePollAckReceived,
                          timeRangeSent,
                          timeRangeReceived);
        /* Apply simple bias correction */
        distance = DW1000NgRanging::correctRange(distance);

        String rangeString = "Range: "; rangeString += distance; rangeString += " m";

        //analogWrite(PIN_dist_out, abs(int(distance))); //###                      #########################################
        boozer(distance);

        rangeString += "\t RX power: "; rangeString += DW1000Ng::getReceivePower(); rangeString += " dBm";
        rangeString += "\t Sampling: "; rangeString += samplingRate; rangeString += " Hz";
        Serial.println(rangeString);
        SerialBT.println(distance);
        //Serial.print("FP power is [dBm]: "); Serial.print(DW1000Ng::getFirstPathPower());
        //Serial.print("RX power is [dBm]: "); Serial.println(DW1000Ng::getReceivePower());
        //Serial.print("Receive quality: "); Serial.println(DW1000Ng::getReceiveQuality());
        // update sampling rate (each second)
        transmitRangeReport(distance * DISTANCE_OF_RADIO_INV);
        successRangingCount++;
        if (curMillis - rangingCountPeriod > 100) {
          samplingRate = (1000.0f * successRangingCount) / (curMillis - rangingCountPeriod);
          rangingCountPeriod = curMillis;
          successRangingCount = 0;
        }
      }
      else {
        transmitRangeFailed();
      }

      noteActivity();
    }
  }
}



void boozer(double dist) {
  // put your main code here, to run repeatedly:
  //dist = analogRead(InPin); // UWB로부터 거리 받음
  //Serial.println(dist);

  // if(dist<0.3){
  //   delay_time = 500;
  // }
  // else if(dist>=0.3 && dist<0.6){
  //   delay_time = 750;
  // }
  // else if(dist>=0.6 && dist<0.9){
  //   delay_time = 1000;
  // }
  // else if(dist>=0.9 && dist<1.2){
  //   delay_time = 1250;
  // }
  // else if(dist>=1.2 && dist<1.5){
  //   delay_time = 1500;
  // }
  // else if(dist>=1.5 && dist<1.8){
  //   delay_time = 1750;
  // }
  // else if(dist>=1.8 && dist<2.1){
  //   delay_time = 2000;
  // }
  // else if(dist>=2.1 && dist<2.4){
  //   delay_time = 2250;
  // }
  // else{
  //   delay_time = 2500;
  // }
  if(dist < 0.3){
    delay_time = 250;
  }
  else if(dist > 5){
    delay_time = 1450;
  }
  else{
    delay_time = 250 + 75*(abs(dist)/0.3);
  }
  digitalWrite(BUZZER_PIN, HIGH);  // 부저 ON
  delay(250);
  digitalWrite(BUZZER_PIN, LOW);   // 부저 OFF
  delay(delay_time);
}
