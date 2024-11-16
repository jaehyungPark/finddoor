#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <HttpClient.h>

#include <SPI.h>
#include <DW1000Ng.hpp>
#include <DW1000NgUtils.hpp>
#include <DW1000NgTime.hpp>
#include <DW1000NgConstants.hpp>

#define BUZZER_PIN 27


#define buttonPin 26 // interrupt 용
volatile bool buttonPressed = false; // interrupt 용
void IRAM_ATTR handleButtonPress(){ // interrupt 용
  buttonPressed = true;
}



//#define SENDER_PIN 33 //Sender(UWB) 제어용(on) 핀 (처음에 16으로 하려 함)
int UWB_SWITCH = 0;
 
// connection pins
const uint8_t PIN_SCK = 18;
const uint8_t PIN_MOSI = 23;
const uint8_t PIN_MISO = 19;
const uint8_t PIN_SS = 4;
const uint8_t PIN_RST = 15;
const uint8_t PIN_IRQ = 17;

// messages used in the ranging protocol
// TODO replace by enum
#define POLL 0
#define POLL_ACK 1
#define RANGE 2
#define RANGE_REPORT 3
#define RANGE_FAILED 255
// message flow state
volatile byte expectedMsgId = POLL_ACK;
// message sent/received state
volatile boolean sentAck = false;
volatile boolean receivedAck = false;
// timestamps to remember
uint64_t timePollSent;
uint64_t timePollAckReceived;
uint64_t timeRangeSent;
// data buffer
#define LEN_DATA 16
byte data[LEN_DATA];
// watchdog and reset period
uint32_t lastActivity;
uint32_t resetPeriod = 250;
// reply times (same on both sides for symm. ranging)
uint16_t replyDelayTimeUS = 3000;

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



const char* ssid = "JaehPark";    //wift 아이디
const char* password =  "00000099";    // wifi 비번 
const char* serverUrl = "http://54.180.158.203:5000/"; // 웹서버주소
int value;
int tag_num = 11;  // 임의의 숫자를 넣어주었다. 
//int analog = 25;  // esp에 연결된 핀 번호


IPAddress hostIp(54, 180, 158, 203);  //웹서버의 ip 주소
int SERVER_PORT = 5000;  // 웹서버 포트 번호
WiFiClient client;

void setup() {
  /* Tag setup part */
  pinMode(2, OUTPUT); //blink
  pinMode(BUZZER_PIN, OUTPUT); // 부저 핀을 출력으로 설정

  pinMode(buttonPin, INPUT_PULLUP); // interrupt 용
  attachInterrupt(digitalPinToInterrupt(buttonPin), handleButtonPress, FALLING); // interrupt 용

  //pinMode(SENDER_CONTROL_PIN, OUTPUT); // Sender(UWB) 핀을 출력으로 설정
  
  Serial.begin(115200);

  WiFi.begin(ssid, password);   // 와이파이 접속
 
  while (WiFi.status() != WL_CONNECTED) { //Check for the connection
    delay(1000);
    Serial.println("Connecting to WiFi..");
  }
 
  Serial.println("Connected to the WiFi network");
  



  /* UWB setup part*/
  pinMode(PIN_RST, OUTPUT);
  digitalWrite(PIN_RST, LOW);
  delay(100);
  digitalWrite(PIN_RST, HIGH);

  Serial.println(F("### DW1000Ng-arduino-ranging-tag ###"));
  // initialize the driver
  DW1000Ng::initialize(PIN_SS, PIN_IRQ, PIN_RST);
  Serial.println("DW1000Ng initialized ...");
  // general configuration
  DW1000Ng::applyConfiguration(DEFAULT_CONFIG);
  DW1000Ng::applyInterruptConfiguration(DEFAULT_INTERRUPT_CONFIG);

  DW1000Ng::setNetworkId(10);

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
  // anchor starts by transmitting a POLL message
  transmitPoll();
  noteActivity();
}

void noteActivity() {
  // update activity timestamp, so that we do not reach "resetPeriod"
  lastActivity = millis();
}

void resetInactive() {
  // tag sends POLL and listens for POLL_ACK
  expectedMsgId = POLL_ACK;
  DW1000Ng::forceTRxOff();
  transmitPoll();
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

void transmitPoll() {
  data[0] = POLL;
  DW1000Ng::setTransmitData(data, LEN_DATA);
  DW1000Ng::startTransmit();
}

void transmitRange() {
  data[0] = RANGE;

  /* Calculation of future time */
  byte futureTimeBytes[LENGTH_TIMESTAMP];

  timeRangeSent = DW1000Ng::getSystemTimestamp();
  timeRangeSent += DW1000NgTime::microsecondsToUWBTime(replyDelayTimeUS);
  DW1000NgUtils::writeValueToBytes(futureTimeBytes, timeRangeSent, LENGTH_TIMESTAMP);
  DW1000Ng::setDelayedTRX(futureTimeBytes);
  timeRangeSent += DW1000Ng::getTxAntennaDelay();

  DW1000NgUtils::writeValueToBytes(data + 1, timePollSent, LENGTH_TIMESTAMP);
  DW1000NgUtils::writeValueToBytes(data + 6, timePollAckReceived, LENGTH_TIMESTAMP);
  DW1000NgUtils::writeValueToBytes(data + 11, timeRangeSent, LENGTH_TIMESTAMP);
  DW1000Ng::setTransmitData(data, LEN_DATA);
  DW1000Ng::startTransmit(TransmitMode::DELAYED);
  //Serial.print("Expect RANGE to be sent @ "); Serial.println(timeRangeSent.getAsFloat());
}

void waitForButtonPress(){ // interrupt 용
  while(!buttonPressed){
    delay(10);
  }
  digitalWrite(BUZZER_PIN, LOW);
}


void loop() {
  
  if(WiFi.status()== WL_CONNECTED){   //Check WiFi connection status
     
    // digitalWrite(2, HIGH);
    // delay(1000);
    // digitalWrite(2, LOW);
    // delay(1000);

    digitalWrite(BUZZER_PIN, HIGH);
    delay(1000);
    digitalWrite(BUZZER_PIN, LOW);
    delay(1000);
    // tone(BUZZER_PIN, 1000);  // 부저 ON
    // delay(1000);
    // noTone(BUZZER_PIN);   // 부저 OFF
    // delay(1000);






    // /* UWB Test용 코드 시작 */
    // UWB_SWITCH = 1;
    // LABEL_TEST:
    // while(UWB_SWITCH == 1){
    //   if (!sentAck && !receivedAck) {
    //     // check if inactive
    //     if (millis() - lastActivity > resetPeriod) {
    //       resetInactive();
    //     }
    //     Serial.println("a");
    //     goto LABEL_TEST;
    //     return;
    //   }
    //   // continue on any success confirmation
    //   if (sentAck) {
    //     sentAck = false;
    //     DW1000Ng::startReceive();
    //   }
    //   if (receivedAck) {
    //     receivedAck = false;
    //     // get message and parse
    //     DW1000Ng::getReceivedData(data, LEN_DATA);
    //     byte msgId = data[0];
    //     if (msgId != expectedMsgId) {
    //       // unexpected message, start over again
    //       //Serial.print("Received wrong message # "); Serial.println(msgId);
    //       expectedMsgId = POLL_ACK;
    //       transmitPoll();
    //       Serial.println("b");
    //       goto LABEL_TEST;
    //       return;
    //     }
    //     if (msgId == POLL_ACK) {
    //       timePollSent = DW1000Ng::getTransmitTimestamp();
    //       timePollAckReceived = DW1000Ng::getReceiveTimestamp();
    //       expectedMsgId = RANGE_REPORT;
    //       transmitRange();
    //       noteActivity();
    //     } else if (msgId == RANGE_REPORT) {
    //       expectedMsgId = POLL_ACK;
    //       float curRange;
    //       memcpy(&curRange, data + 1, 4);
    //       delay(100);
    //       transmitPoll();
    //       noteActivity();
    //     } else if (msgId == RANGE_FAILED) {
    //       expectedMsgId = POLL_ACK;
    //       transmitPoll();
    //       noteActivity();
    //     }
    //   }
    // }

    // /* UWB Test용 코드 끝 */






    HTTPClient http; 

    //서버 연결 요청
    String connectUrl = String(serverUrl) + "/connect";
    http.begin(connectUrl);  
   
    //http.begin("http://192.0.0.2:5001");  //Specify destination for HTTP request
    http.addHeader("Content-Type",  "application/json");   //Specify content-type header,  Json형식의 타입이다.
    String jsonData = "{\"tag_num\":" + String(tag_num) + "}";
    int httpResponseCode = http.POST(jsonData);  // POST 요청

    // String httpRequestData = "tag_num="+String(tag_num)+"&value="+String(value);  // 가장 중요한 Json 데이터를 입력하는 부분이다  = 의 왼쪽이 key값 오른쪽이 value 값이고 &를 기준으로 쌍이 나뉘어진다.
    // Serial.println(httpRequestData); //시리얼 모니터에 Json 형식의 데이터를 찍어준다.
    // int httpResponseCode = http.POST(httpRequestData);   //Send the actual POST request




    if (httpResponseCode == 200) {
      String response = http.getString();
      Serial.println("Connection established: " + response);
      delay(5000);

      digitalWrite(BUZZER_PIN, HIGH); // interrupt 용
      waitForButtonPress(); // interrupt 용
      buttonPressed = false; // interrupt 용

      Serial.println("UWB activated!");


      /* UWB Test용 코드 시작 */
      UWB_SWITCH = 1;
      LABEL_TEST:
      while(UWB_SWITCH == 1){
        if (!sentAck && !receivedAck) {
          // check if inactive
          if (millis() - lastActivity > resetPeriod) {
            resetInactive();
          }
          //Serial.println("a");
          goto LABEL_TEST;
          return;
        }
        // continue on any success confirmation
        if (sentAck) {
          sentAck = false;
          DW1000Ng::startReceive();
        }
        if (receivedAck) {
          receivedAck = false;
          // get message and parse
          DW1000Ng::getReceivedData(data, LEN_DATA);
          byte msgId = data[0];
          if (msgId != expectedMsgId) {
            // unexpected message, start over again
            //Serial.print("Received wrong message # "); Serial.println(msgId);
            expectedMsgId = POLL_ACK;
            transmitPoll();
            //Serial.println("b");
            goto LABEL_TEST;
            return;
          }
          if (msgId == POLL_ACK) {
            timePollSent = DW1000Ng::getTransmitTimestamp();
            timePollAckReceived = DW1000Ng::getReceiveTimestamp();
            expectedMsgId = RANGE_REPORT;
            transmitRange();
            noteActivity();
          } else if (msgId == RANGE_REPORT) {
            expectedMsgId = POLL_ACK;
            float curRange;
            memcpy(&curRange, data + 1, 4);
            delay(100);
            transmitPoll();
            noteActivity();
          } else if (msgId == RANGE_FAILED) {
            expectedMsgId = POLL_ACK;
            transmitPoll();
            noteActivity();
          }
        }
      }

      /* UWB Test용 코드 끝 */
    } else {
      Serial.println("Connection failed. Status code: " + String(httpResponseCode));
    }
    http.end();

    // 서버 신호 대기
    Serial.println("Waiting for server signal...");
    delay(5000);  // 잠시 대기

    String signalUrl = String(serverUrl) + "get_signal/" + String(tag_num);
    Serial.println("signalUrl: " + signalUrl);
    http.begin(signalUrl);
    http.addHeader("Content-Type",  "application/json");

    unsigned long timeout = millis() + 620000;  // 10분+여유시간

    jsonData = "{\"tag_num\":" + String(tag_num) + "}";
    int signalResponseCode = http.POST(jsonData);  // POST 요청
    Serial.println("signalResponseCode: " + String(signalResponseCode));

    //while (millis() < timeout) {
      //int signalResponseCode = http.GET();
      if (signalResponseCode > 0){
        if (signalResponseCode == 200) {
          String response = http.getString();
          Serial.println("Received response: " + response);

          // JSON 파싱 (간단한 방식)
          if (response.indexOf("\"signal\":\"on\"") >= 0) {
            Serial.println("Performing ON signal actions...");
            
            
            // 부저 활성화 코드
            // tone(BUZZER_PIN, 1000);  // 부저 ON
            // delay(1000);
            // noTone(BUZZER_PIN);   // 부저 OFF
            // delay(1000);

            /* UWB 코드 시작 */
            Serial.println("UWB activated!");
            UWB_SWITCH = 1;
            LABEL: // ######################################
            while(UWB_SWITCH == 1){
              if (!sentAck && !receivedAck) {
                // check if inactive
                if (millis() - lastActivity > resetPeriod) {
                  resetInactive();
                }
                goto LABEL; // ######################################
                return;
              }
              // continue on any success confirmation
              if (sentAck) {
                sentAck = false;
                DW1000Ng::startReceive();
              }
              if (receivedAck) {
                receivedAck = false;
                // get message and parse
                DW1000Ng::getReceivedData(data, LEN_DATA);
                byte msgId = data[0];
                if (msgId != expectedMsgId) {
                  // unexpected message, start over again
                  //Serial.print("Received wrong message # "); Serial.println(msgId);
                  expectedMsgId = POLL_ACK;
                  transmitPoll();
                  goto LABEL; // ######################################
                  return;
                }
                if (msgId == POLL_ACK) {
                  timePollSent = DW1000Ng::getTransmitTimestamp();
                  timePollAckReceived = DW1000Ng::getReceiveTimestamp();
                  expectedMsgId = RANGE_REPORT;
                  transmitRange();
                  noteActivity();
                } else if (msgId == RANGE_REPORT) {
                  expectedMsgId = POLL_ACK;
                  float curRange;
                  memcpy(&curRange, data + 1, 4);
                  delay(100);
                  transmitPoll();
                  noteActivity();
                } else if (msgId == RANGE_FAILED) {
                  expectedMsgId = POLL_ACK;
                  transmitPoll();
                  noteActivity();
                }
              }

              //UWB 끄는 조건 넣어야함 (넣지 말까?)
            }
          }
            /* UWB 코드 끝 */

          else if (response.indexOf("\"signal\":\"off\"") >= 0) {
            Serial.println("Performing OFF signal actions...");
            // OFF 처리 코드 추가
          }
          //break;  // 신호를 받은 후 종료
        }
        
        else {
          Serial.println("Error: " + String(signalResponseCode));
        }
        delay(10000);  // 재시도 간격
      }
    else {

      Serial.println("HTTP GET failed, error: "+ String(signalResponseCode));
    }
    http.end();
    //}
  }

  else{
    Serial.println("Error in WiFi connection");   
  }
  Serial.println("All Done…");
  delay(10000);  //Send a request every 10 seconds
 
}