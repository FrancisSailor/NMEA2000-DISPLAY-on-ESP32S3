#define DEBUG

#ifdef DEBUG
#define DEBUG_PRINT(x) Serial.print(x)
#define DEBUG_PRINTLN(x) Serial.println(x)
#define DEBUG_PRINTLNFMT(x,y) Serial.println(x,y)
#else
#define DEBUG_PRINT(x)
#define DEBUG_PRINTLN(x)
#define DEBUG_PRINTLNFMT(x,y)
#endif

#define KEYLOCK_TIMEOUT_MS 1000
#define TACK_TIMEOUT_MS 1000
#define HEADING_WIND_TIMEOUT_MS 2000

#define INPUT_ADJUST_HEADING_PLUS_1       4   //0100
#define INPUT_ADJUST_HEADING_PLUS_10      8   //1000
#define INPUT_ADJUST_HEADING_MINUS_1      2   //0010
#define INPUT_ADJUST_HEADING_MINUS_10     1   //0001
#define INPUT_ADJUST_HEADING_AGAINST_WIND 7   //0111
#define INPUT_TACK_STARBOARD              12  //1100
#define INPUT_TACK_PORT                   3   //0011
#define INPUT_STATE_AUTO                  6   //0110
#define INPUT_STATE_WIND                  10  //1010
#define INPUT_STATE_STANDBY               5   //0101
#define INPUT_STATE_ROUTE                 9   //1011
#define INPUT_KEYLOCK                     14  //1110




// ESP32-S3 CAN (TWAI) pin configuration
// TJA1051 transceiver: TX=GPIO6, RX=GPIO0
#define ESP32_CAN_TX_PIN GPIO_NUM_6
#define ESP32_CAN_RX_PIN GPIO_NUM_0

#include <Arduino.h>
#include <N2kMsg.h>
#include <NMEA2000.h>
#include <NMEA2000_esp32_twai.h>

// Create global NMEA2000 object using ESP32 TWAI driver
#include <N2kMessages.h>

tNMEA2000 &NMEA2000 = *(new NMEA2000_esp32_twai());

#include "RaymarinePilot.h"
#include "N2kDeviceList.h"
// On ESP32-S3 we do not use libmaple watchdog; provide no-op stubs
#define iwdg_init(... ) do { } while(0)
#define iwdg_feed() do { } while(0)

const unsigned long TransmitMessages[] PROGMEM={126208UL,0};
const unsigned long ReceiveMessages[] PROGMEM={127250L,65288L,65379L,0};

tN2kDeviceList *pN2kDeviceList;

byte inByte;

bool keylockEnabled = true;

long keylockTimeout = 0;
long tackTimeout = 0;
long headingToWindTimeout = 0;
long beepTimeout = 0;

unsigned short* beepPattern;
int beepPatternIndex = 0;

unsigned short BEEP_NULL[] = {0};
unsigned short BEEP_SINGLE_BUTTON[] = {100, 0};
unsigned short BEEP_KEYLOCK_ENABLE[] = {150, 50, 150, 50, 150, 0};
unsigned short BEEP_KEYLOCK_DISABLE[] = {150, 50, 150, 0};
unsigned short BEEP_TACK[] = {1000, 0};
unsigned short BEEP_HEADING_AGAINST_WIND[] = {1000, 200, 1000, 0};
unsigned short BEEP_STARTUP[] = {300, 0};

void setup() {
  iwdg_init(IWDG_PRE_256, 3000); // init an 16+ second wd timer


  // Reserve enough buffer for sending all messages. This does not work on small memory devices like Uno or Mega
  NMEA2000.SetN2kCANReceiveFrameBufSize(150);
  NMEA2000.SetN2kCANMsgBufSize(8);
  // Set Product information
  NMEA2000.SetProductInformation("00000001", // Manufacturer's Model serial code
                                 100, // Manufacturer's product code
                                 "Evo Pilot Remote",  // Manufacturer's Model ID
                                 "1.0.0.0",  // Manufacturer's Software version code
                                 "1.0.0.0" // Manufacturer's Model version
                                );
  // Set device information
  NMEA2000.SetDeviceInformation(1111, // Unique number. Use e.g. Serial number.
                                132, // Device function=Analog to NMEA 2000 Gateway. See codes on http://www.nmea.org/Assets/20120726%20nmea%202000%20class%20&%20function%20codes%20v%202.00.pdf
                                25, // Device class=Inter/Intranetwork Device. See codes on  http://www.nmea.org/Assets/20120726%20nmea%202000%20class%20&%20function%20codes%20v%202.00.pdf
                                2046 // Just choosen free from code list on http://www.nmea.org/Assets/20121020%20nmea%202000%20registration%20list.pdf
                               );
  Serial.begin(115200);
  delay(100);

  #ifdef DEBUG
  NMEA2000.SetForwardStream(&Serial);  // PC output on due programming port
  NMEA2000.SetForwardType(tNMEA2000::fwdt_Text); // Show in clear text. Leave uncommented for default Actisense format.
  NMEA2000.SetForwardOwnMessages();
  #endif

  // If you also want to see all traffic on the bus use N2km_ListenAndNode instead of N2km_NodeOnly below
  NMEA2000.SetMode(tNMEA2000::N2km_NodeOnly, 51); //N2km_NodeOnly N2km_ListenAndNode
  NMEA2000.ExtendTransmitMessages(TransmitMessages);
  NMEA2000.ExtendReceiveMessages(ReceiveMessages);

  NMEA2000.SetMsgHandler(RaymarinePilot::HandleNMEA2000Msg);

  
  pN2kDeviceList = new tN2kDeviceList(&NMEA2000);
  #ifdef DEBUG
  NMEA2000.EnableForward(true);
  #endif
  NMEA2000.Open();

  unsigned long t = millis();
  
  while(RaymarinePilot::PilotSourceAddress < 0 && millis() - t < 5000){
    iwdg_feed();
    NMEA2000.ParseMessages();
    RaymarinePilot::PilotSourceAddress = getDeviceSourceAddress("EV-1");
    delay(50);
  }

  if(RaymarinePilot::PilotSourceAddress >= 0){
    Serial.println((String) "Found EV-1 Pilot: " + RaymarinePilot::PilotSourceAddress);
  }else{
    RaymarinePilot::PilotSourceAddress = 204;
    Serial.println((String) "EV-1 Pilot not found. Defaulting to " + RaymarinePilot::PilotSourceAddress);
  }

  iwdg_init(IWDG_PRE_256, 1250); // set watchdog timer to 8 seconds
}

void loop() {
  iwdg_feed();
  
  if (Serial.available() > 0) {
    inByte = Serial.read();

    if (inByte == 's') {
      Serial.println("Setting PILOT_MODE_STANDBY");
      
      tN2kMsg N2kMsg;
      RaymarinePilot::SetEvoPilotMode(N2kMsg, PILOT_MODE_STANDBY);
      NMEA2000.SendMsg(N2kMsg);
    } 
    
    else if (inByte == 'a') {
      Serial.println("Setting PILOT_MODE_AUTO");
      
      tN2kMsg N2kMsg;
      RaymarinePilot::SetEvoPilotMode(N2kMsg, PILOT_MODE_AUTO);
      NMEA2000.SendMsg(N2kMsg);
    } 
    
    else if (inByte == 'w') {
      Serial.println("Setting PILOT_MODE_WIND");
      
      tN2kMsg N2kMsg;
      RaymarinePilot::SetEvoPilotMode(N2kMsg, PILOT_MODE_WIND);
      NMEA2000.SendMsg(N2kMsg);
    } 
    
    else if (inByte == 't') {
      Serial.println("Setting PILOT_MODE_TRACK");
      
      tN2kMsg N2kMsg;
      RaymarinePilot::SetEvoPilotMode(N2kMsg, PILOT_MODE_TRACK);
      NMEA2000.SendMsg(N2kMsg);
    }
    
    else if (inByte == '+') {
      Serial.print("Setting course to ");
      Serial.println((((int) RaymarinePilot::Heading + 10) % 360));
      
      tN2kMsg N2kMsg;
      RaymarinePilot::SetEvoPilotCourse(N2kMsg, RaymarinePilot::Heading, 10);
      NMEA2000.SendMsg(N2kMsg);
    }
    
    else if (inByte == '-') {
      Serial.print("Setting course to ");
      Serial.println((((int) RaymarinePilot::Heading - 10 + 360) % 360));
      
      tN2kMsg N2kMsg;
      RaymarinePilot::SetEvoPilotCourse(N2kMsg, RaymarinePilot::Heading, -10);
      NMEA2000.SendMsg(N2kMsg);
    }

    else if(inByte == 'c'){
        Serial.println("Confirm Waypoint");
      
        tN2kMsg N2kMsg1;
        RaymarinePilot::TurnToWaypointMode(N2kMsg1);
        NMEA2000.SendMsg(N2kMsg1);

        tN2kMsg N2kMsg2;
        RaymarinePilot::TurnToWaypoint(N2kMsg2);
        NMEA2000.SendMsg(N2kMsg2);
    }

    else if(inByte == '1'){
        Serial.println("Plus 1");
      
        tN2kMsg N2kMsg;
        RaymarinePilot::KeyCommand(N2kMsg, KEY_PLUS_1);
        NMEA2000.SendMsg(N2kMsg);
    }

    else if(inByte == '2'){
        Serial.println("Plus 10");
      
        tN2kMsg N2kMsg;
        RaymarinePilot::KeyCommand(N2kMsg, KEY_PLUS_10);
        NMEA2000.SendMsg(N2kMsg);
    }

    else if(inByte == '3'){
        Serial.println("Minus 1");
      
        tN2kMsg N2kMsg;
        RaymarinePilot::KeyCommand(N2kMsg, KEY_MINUS_1);
        NMEA2000.SendMsg(N2kMsg);
    }

    else if(inByte == '4'){
        Serial.println("Minus 10");
      
        tN2kMsg N2kMsg;
        RaymarinePilot::KeyCommand(N2kMsg, KEY_MINUS_10);
        NMEA2000.SendMsg(N2kMsg);
    }

    else if(inByte == '5'){
        Serial.println("Minus 1 Minus 10");
      
        tN2kMsg N2kMsg;
        RaymarinePilot::KeyCommand(N2kMsg, KEY_MINUS_1_MINUS_10);
        NMEA2000.SendMsg(N2kMsg);
    }

    else if(inByte == '6'){
        Serial.println("Plus 1 Plus 10");
      
        tN2kMsg N2kMsg;
        RaymarinePilot::KeyCommand(N2kMsg, KEY_PLUS_1_PLUS_10);
        NMEA2000.SendMsg(N2kMsg);
    }
  }

  if(RaymarinePilot::alarmWaypoint){
    Serial.println("Confirm Waypoint");
      
    tN2kMsg N2kMsg1;
    RaymarinePilot::TurnToWaypointMode(N2kMsg1);
    NMEA2000.SendMsg(N2kMsg1);

    tN2kMsg N2kMsg2;
    RaymarinePilot::TurnToWaypoint(N2kMsg2);
    NMEA2000.SendMsg(N2kMsg2);

    RaymarinePilot::alarmWaypoint = false;
  }

  NMEA2000.ParseMessages();  
}

int getDeviceSourceAddress(String model) {
  if (!pN2kDeviceList->ReadResetIsListUpdated()) return -1;

  for (uint8_t i = 0; i < N2kMaxBusDevices; i++){ 
    const tNMEA2000::tDevice *device=pN2kDeviceList->FindDeviceBySource(i);
    if ( device == 0 ) continue;
    
    String modelVersion = device->GetModelVersion();
    
    if(modelVersion.indexOf(model) >= 0){
      return device->GetSource();
    }
  }

  return -2;
}
