
#include "Ray_N2K.h"
#include <N2kTypes.h>
#include <N2kMessages.h>
#include <math.h>


// ---- Raymarine EVO autopilot low-level interface (internal) ----
#include "N2kMsg.h"
#include "N2kTypes.h"
#include <stdint.h>
#include <NMEA2000.h>
#include <N2kMessages.h>

#ifndef RAYMARINEPILOT_H
#define RAYMARINEPILOT_H

enum RaymarinePilotModes {PILOT_MODE_STANDBY = 1, PILOT_MODE_AUTO = 2, PILOT_MODE_WIND = 3, PILOT_MODE_PRE_TRACK = 4, PILOT_MODE_TRACK = 5};

enum key_codes {KEY_PLUS_1 = 0x07f8, KEY_PLUS_10 = 0x08f7, KEY_MINUS_1 = 0x05fa, KEY_MINUS_10 = 0x06f9, KEY_MINUS_1_MINUS_10 = 0x21de, KEY_PLUS_1_PLUS_10 = 0x22dd, KEY_TACK_PORTSIDE = KEY_MINUS_1_MINUS_10, KEY_TACK_STARBORD = KEY_PLUS_1_PLUS_10};

class RaymarinePilot {
  public: 
    static double Heading, Variation;

    static bool alarmWaypoint;

    static uint8_t PilotMode;
    static int PilotSourceAddress;
  
    static void SetEvoPilotMode(tN2kMsg &N2kMsg, RaymarinePilotModes mode);
    static void SetEvoPilotWind(tN2kMsg &N2kMsg, double targetWindDirection);
    static void SetEvoPilotCourse(tN2kMsg &N2kMsg, double heading, int change);
    static inline void SetEvoPilotCourse(tN2kMsg &N2kMsg, double heading) {
      return SetEvoPilotCourse(N2kMsg, heading, 0);
    }
    static void TurnToWaypointMode(tN2kMsg &N2kMsg);
    static void TurnToWaypoint(tN2kMsg &N2kMsg);
    static void KeyCommand(tN2kMsg &N2kMsg, uint16_t command);
    
    static void HandleNMEA2000Msg(const tN2kMsg &N2kMsg);

    static bool ParseN2kPGN65288(const tN2kMsg &N2kMsg, unsigned char &AlarmState,  unsigned char &AlarmCode, unsigned char &AlarmGroup);
    static inline bool ParseN2kAlarm(const tN2kMsg &N2kMsg, unsigned char &AlarmState, unsigned char &AlarmCode, unsigned char &AlarmGroup) {
      return ParseN2kPGN65288(N2kMsg, AlarmState, AlarmCode, AlarmGroup);
    }

    static bool ParseN2kPGN65379(const tN2kMsg &N2kMsg, unsigned char &Mode, unsigned char &Submode);
    static inline bool ParseN2kPilotState(const tN2kMsg &N2kMsg, unsigned char &Mode, unsigned char &Submode) {
      return ParseN2kPGN65379(N2kMsg, Mode, Submode);
    }

    static bool ParseN2kPGN65345(const tN2kMsg &N2kMsg, double &WindAngle, double &RollingAverageWindAngle);
    static inline bool ParseN2kPilotWindAngle(const tN2kMsg &N2kMsg, double &WindAngle, double &RollingAverageWindAngle) {
      return ParseN2kPGN65345(N2kMsg, WindAngle, RollingAverageWindAngle);
    }

    static bool ParseN2kPGN65360(const tN2kMsg &N2kMsg, double &HeadingTrue, double &HeadingMagnetic);
    static inline bool ParseN2kPilotLockedHeading(const tN2kMsg &N2kMsg, double &HeadingTrue, double &HeadingMagnetic) {
      return ParseN2kPGN65360(N2kMsg, HeadingTrue, HeadingMagnetic);
    }   
    
};

#endif

#include <string.h>

double RaymarinePilot::Heading = 0;
double RaymarinePilot::Variation = 0;
int RaymarinePilot::PilotSourceAddress = -1;
uint8_t RaymarinePilot::PilotMode = PILOT_MODE_STANDBY;
unsigned int pilotHeadingFilterCount = 0;

bool RaymarinePilot::alarmWaypoint = false;

//PilotSourceAddress muss aus der tN2kDeviceList ausgelesen werden. Beispiel dazu: DeviceAnalyzer.ino

void RaymarinePilot::SetEvoPilotMode(tN2kMsg &N2kMsg,RaymarinePilotModes mode) {
  N2kMsg.SetPGN(126208UL);
  N2kMsg.Priority=3;
  N2kMsg.Destination=PilotSourceAddress;
  N2kMsg.AddByte(1); // Field 1, 1 = Command Message, 2 = Acknowledge Message...
  N2kMsg.AddByte(0x63);
  N2kMsg.AddByte(0xff);
  N2kMsg.AddByte(0x00);
  N2kMsg.AddByte(0xf8);
  N2kMsg.AddByte(0x04);
  N2kMsg.AddByte(0x01);
  N2kMsg.AddByte(0x3b);
  N2kMsg.AddByte(0x07);
  N2kMsg.AddByte(0x03);
  N2kMsg.AddByte(0x04);
  N2kMsg.AddByte(0x04);

  // 0x0000 = standby, 0x0040 = auto, 0x0100 = vane (wind), 0x0180 = pre_track, 0x0181 = track
  switch (mode) {
  case PILOT_MODE_STANDBY:
    N2kMsg.AddByte(0x00);
    N2kMsg.AddByte(0x00);
    break;
  case PILOT_MODE_AUTO:
    N2kMsg.AddByte(0x40);
    N2kMsg.AddByte(0x00);
    break;
  case PILOT_MODE_WIND:
    N2kMsg.AddByte(0x00);
    N2kMsg.AddByte(0x01);
    break;
  case PILOT_MODE_PRE_TRACK:
    N2kMsg.AddByte(0x80);
    N2kMsg.AddByte(0x01);
    break;
  case PILOT_MODE_TRACK:
    N2kMsg.AddByte(0x81);
    N2kMsg.AddByte(0x01);
    break;
}
 
  N2kMsg.AddByte(0x05);
  N2kMsg.AddByte(0xff);
  N2kMsg.AddByte(0xff);

  /*N2kMsg.Add3ByteInt(126208UL); // Field 2, commanded PGN 3 byte,  LSB is transmitted first  
  N2kMsg.AddByte(0xf0 | 0x8 ); // Field 3, 0x8 = Do not change priority and field 4 is nmea reserved
  N2kMsg.AddByte(1); // Field 5, 1 field/value pair
  N2kMsg.AddByte(4); // Field 6, field 4 to be commanded
  N2kMsg.Add4ByteUInt(0); // Field 7, value for first pair =0 for reset distance log.*/
}

void RaymarinePilot::SetEvoPilotCourse(tN2kMsg &N2kMsg,double heading, int change) {
  double course = heading + change;
  if ((course) >= 360){
    course -= 360;
  } else if ((course) < 0){
    course += 360;
  }
  
  uint16_t courseRadials10000 = (uint16_t) (DegToRad(course) * 10000); //(newCourse * 174.53); 

  byte byte0, byte1;
  byte0 = courseRadials10000 & 0xff;
  byte1 = courseRadials10000 >> 8;

  //01,50,ff,00,f8,03,01,3b,07,03,04,06,00,00
  
  N2kMsg.SetPGN(126208UL);
  N2kMsg.Priority=3;
  N2kMsg.Destination=PilotSourceAddress;
  N2kMsg.AddByte(1); // Field 1, 1 = Command Message, 2 = Acknowledge Message...
  N2kMsg.AddByte(0x50);
  N2kMsg.AddByte(0xff);
  N2kMsg.AddByte(0x00);
  N2kMsg.AddByte(0xf8);
  N2kMsg.AddByte(0x03);
  N2kMsg.AddByte(0x01);
  N2kMsg.AddByte(0x3b);
  N2kMsg.AddByte(0x07);
  N2kMsg.AddByte(0x03);
  N2kMsg.AddByte(0x04);
  N2kMsg.AddByte(0x06);
  N2kMsg.AddByte(byte0);
  N2kMsg.AddByte(byte1);
}

void RaymarinePilot::SetEvoPilotWind(tN2kMsg &N2kMsg, double targetWindDirection) {

  uint16_t targetWind10000 = (uint16_t) (targetWindDirection * 10000); 
  
  byte byte0, byte1;
  byte0 = targetWind10000 & 0xff;
  byte1 = targetWind10000 >> 8;

  //41,ff,00,f8,03,01,3b,07,03,04,04,00,00
  
  N2kMsg.SetPGN(126208UL);
  N2kMsg.Priority=3;
  N2kMsg.Destination=PilotSourceAddress;
  N2kMsg.AddByte(1); // Field 1, 1 = Command Message, 2 = Acknowledge Message...
  N2kMsg.AddByte(0x41);
  N2kMsg.AddByte(0xff);
  N2kMsg.AddByte(0x00);
  N2kMsg.AddByte(0xf8);
  N2kMsg.AddByte(0x03);
  N2kMsg.AddByte(0x01);
  N2kMsg.AddByte(0x3b);
  N2kMsg.AddByte(0x07);
  N2kMsg.AddByte(0x03);
  N2kMsg.AddByte(0x04);
  N2kMsg.AddByte(0x04);
  N2kMsg.AddByte(byte0);
  N2kMsg.AddByte(byte1);
  
}


void RaymarinePilot::TurnToWaypointMode(tN2kMsg &N2kMsg){
 //"01,63,ff,00,f8,04,01,3b,07,03,04,04,81,01,05,ff,ff" 
  N2kMsg.SetPGN(126208UL);
  N2kMsg.Priority=3;
  N2kMsg.Destination=PilotSourceAddress;
  N2kMsg.AddByte(0x01); // Field 1, 1 = Command Message, 2 = Acknowledge Message...
  N2kMsg.AddByte(0x63);
  N2kMsg.AddByte(0xff);
  N2kMsg.AddByte(0x00);
  N2kMsg.AddByte(0xf8);
  N2kMsg.AddByte(0x04);
  N2kMsg.AddByte(0x01);
  N2kMsg.AddByte(0x3b);
  N2kMsg.AddByte(0x07);
  N2kMsg.AddByte(0x03);
  N2kMsg.AddByte(0x04);
  N2kMsg.AddByte(0x04);
  N2kMsg.AddByte(0x81);
  N2kMsg.AddByte(0x01);
  N2kMsg.AddByte(0x05);
  N2kMsg.AddByte(0xff);
  N2kMsg.AddByte(0xff);
}

void RaymarinePilot::TurnToWaypoint(tN2kMsg &N2kMsg){
  //00,00,ef,01,ff,ff,ff,ff,ff,ff,04,01,3b,07,03,04,04,6c,05,1a,50"
  
  N2kMsg.SetPGN(126208UL);
  N2kMsg.Priority=3;
  N2kMsg.Destination=PilotSourceAddress;
  N2kMsg.AddByte(0x00);
  N2kMsg.AddByte(0x00);
  N2kMsg.AddByte(0xef);
  N2kMsg.AddByte(0x01);
  N2kMsg.AddByte(0xff);
  N2kMsg.AddByte(0xff);
  N2kMsg.AddByte(0xff);
  N2kMsg.AddByte(0xff);
  N2kMsg.AddByte(0xff);
  N2kMsg.AddByte(0xff);
  N2kMsg.AddByte(0x04);
  N2kMsg.AddByte(0x01);
  N2kMsg.AddByte(0x3b);
  N2kMsg.AddByte(0x07);
  N2kMsg.AddByte(0x03);
  N2kMsg.AddByte(0x04);
  N2kMsg.AddByte(0x04);
  N2kMsg.AddByte(0x6c);
  N2kMsg.AddByte(0x05);
  N2kMsg.AddByte(0x1a);
  N2kMsg.AddByte(0x50);
}

void RaymarinePilot::KeyCommand(tN2kMsg &N2kMsg,uint16_t command){
  //const key_command = "3b,9f,f0,81,86,21,%s,%s,ff,ff,ff,ff,ff,c1,c2,cd,66,80,d3,42,b1,c8"

  byte commandByte0, commandByte1;
  commandByte0 = command >> 8;
  commandByte1 = command & 0xff;
  
  N2kMsg.SetPGN(126720UL);
  N2kMsg.Priority=7;
  N2kMsg.Destination=PilotSourceAddress;

  N2kMsg.AddByte(0x3b);
  N2kMsg.AddByte(0x9f);
  N2kMsg.AddByte(0xf0);
  N2kMsg.AddByte(0x81);
  N2kMsg.AddByte(0x86);
  N2kMsg.AddByte(0x21);
  //N2kMsg.Add2ByteUInt(command);
  N2kMsg.AddByte(commandByte0);
  N2kMsg.AddByte(commandByte1);
  N2kMsg.AddByte(0xff);
  N2kMsg.AddByte(0xff);
  N2kMsg.AddByte(0xff);
  N2kMsg.AddByte(0xff);
  N2kMsg.AddByte(0xff);
  N2kMsg.AddByte(0xc1);
  N2kMsg.AddByte(0xc2);
  N2kMsg.AddByte(0xcd);
  N2kMsg.AddByte(0x66);
  N2kMsg.AddByte(0x80);
  N2kMsg.AddByte(0xd3);
  N2kMsg.AddByte(0x42);
  N2kMsg.AddByte(0xb1);
  N2kMsg.AddByte(0xc8);
}

void RaymarinePilot::HandleNMEA2000Msg(const tN2kMsg &N2kMsg) {
  if(N2kMsg.PGN == 127250L){ //Heading
    unsigned char SID;
    tN2kHeadingReference ref;
    double _Deviation=0;
    double _Variation;
    double _HeadingRad;
    
    if (ParseN2kHeading(N2kMsg, SID, _HeadingRad, _Deviation, _Variation, ref)) {
      if ( !N2kIsNA(_Variation) ) Variation=_Variation; // Update Variation
      if ( !N2kIsNA(_HeadingRad) && !N2kIsNA(Variation) ) _HeadingRad-=Variation;
      Heading = RadToDeg(_HeadingRad);
    }
  }
  else if(N2kMsg.PGN == 65288L){ //Alarm     
    unsigned char AlarmState;
    unsigned char AlarmCode;
    unsigned char AlarmGroup;

    char AlarmStateString[2] = {0};
    char AlarmCodeString[2] = {0};
    char AlarmGroupString[2] = {0};
    
    if(RaymarinePilot::ParseN2kAlarm(N2kMsg, AlarmState, AlarmCode, AlarmGroup)){
      sprintf(AlarmStateString,"%02X", (int) AlarmState);
      sprintf(AlarmCodeString,"%02X", (int) AlarmCode);
      sprintf(AlarmGroupString,"%02X", (int) AlarmGroup);
      
      Serial.print("Alarm Group: ");      
      Serial.print(AlarmGroupString);
      Serial.print(" Code: ");
      Serial.print(AlarmCodeString);
      Serial.print(" State: ");
      Serial.println(AlarmStateString);

      if(AlarmCode == 0x1d && AlarmGroup == 0x01){
        Serial.println("Alarm Waypoint");

        alarmWaypoint = true;
      }
    }
  }

  else if(N2kMsg.PGN == 65379L){ //Pilot State  
    unsigned char Mode;
    unsigned char Submode;
    
    if(ParseN2kPilotState(N2kMsg, Mode, Submode)){
      Serial.print("Mode / Submode ");
      Serial.print(Mode, HEX);
      Serial.print(" ");
      Serial.println(Submode, HEX);

      if(Mode == 0x00 && Submode == 0x00){
        //RaymarinePilot::PilotMode = PILOT_MODE_STANDBY;
        Serial.println("PILOT_MODE_STANDBY");
      }
      else if(Mode == 0x40 && Submode == 0x00){
        //RaymarinePilot::PilotMode = PILOT_MODE_AUTO;
        Serial.println("PILOT_MODE_AUTO");
      }
      
      else if(Mode == 0x00 && Submode == 0x01){
        //RaymarinePilot::PilotMode = PILOT_MODE_WIND;
        Serial.println("PILOT_MODE_WIND");
      }
      
      else if(Mode == 0x81 && Submode == 0x01){
        //RaymarinePilot::PilotMode = PILOT_MODE_PRE_TRACK;
        Serial.println("PILOT_MODE_PRE_TRACK");
      }
      
      else if(Mode == 0x80 && Submode == 0x01){
        //RaymarinePilot::PilotMode = PILOT_MODE_PRE_TRACK;
        Serial.println("PILOT_MODE_PRE_TRACK");
      }
      
    }
  }

  else if(N2kMsg.PGN == 65345L){ //Pilot Wind Angle
    double WindAngle;
    double RollingAverageWindAngle;
    
    if(RaymarinePilot::ParseN2kPilotWindAngle(N2kMsg, WindAngle, RollingAverageWindAngle)){

      WindAngle = RadToDeg(WindAngle);
      RollingAverageWindAngle = RadToDeg(RollingAverageWindAngle);

      Serial.println((String) "Wind angle: " + WindAngle + " Rolling average: " + RollingAverageWindAngle);
    }
  }

  else if(N2kMsg.PGN == 65360L){ //Pilot Heading
    double HeadingTrue;
    double HeadingMagnetic;

    pilotHeadingFilterCount++;
    pilotHeadingFilterCount = pilotHeadingFilterCount % 4;
    
    if(pilotHeadingFilterCount > 0){
      return;
    }
    
    if(RaymarinePilot::ParseN2kPGN65360(N2kMsg, HeadingTrue, HeadingMagnetic)){

      HeadingTrue = RadToDeg(HeadingTrue);
      HeadingMagnetic = RadToDeg(HeadingMagnetic);

      Serial.println((String) "Heading magnetic: " + HeadingMagnetic);
      String message;
      
      if(HeadingTrue != N2kDoubleNA){
        Serial.println((String) "Heading true: " + HeadingTrue);
      }
    }
  }
}

bool RaymarinePilot::ParseN2kPGN65288(const tN2kMsg &N2kMsg, unsigned char &AlarmStatus, unsigned char &AlarmCode, unsigned char &AlarmGroup) {
  if (N2kMsg.PGN!=65288L) return false;

  int Index=3;

  AlarmStatus = N2kMsg.GetByte(Index);
  AlarmCode = N2kMsg.GetByte(Index);
  AlarmGroup = N2kMsg.GetByte(Index);

  return true;
}

bool RaymarinePilot::ParseN2kPGN65379(const tN2kMsg &N2kMsg, unsigned char &Mode, unsigned char &Submode) {
  if (N2kMsg.PGN!=65379L) return false;

  int Index=2;

  Mode = N2kMsg.GetByte(Index);
  Submode = N2kMsg.GetByte(Index);

  return true;
}

bool RaymarinePilot::ParseN2kPGN65345(const tN2kMsg &N2kMsg, double &WindAngle, double &RollingAverageWindAngle) {
  if (N2kMsg.PGN!=65345L) return false;

  int Index=2;

  // Raymarine pilot wind angles are encoded as UNSIGNED 16-bit with 0.0001 rad resolution.
  // Values represent [0..2π) (e.g. port angles are typically >180°), so we must not decode as signed.
  WindAngle = N2kMsg.Get2ByteUDouble(0.0001,Index);
  RollingAverageWindAngle = N2kMsg.Get2ByteUDouble(0.0001,Index);

  return true;
}

bool RaymarinePilot::ParseN2kPGN65360(const tN2kMsg &N2kMsg, double &HeadingTrue, double &HeadingMagnetic) {
  if (N2kMsg.PGN!=65360L) return false;

  int Index=3;

  HeadingTrue = N2kMsg.Get2ByteUDouble(0.0001,Index);
  HeadingMagnetic = N2kMsg.Get2ByteUDouble(0.0001,Index);

  return true;
}

// ---- N2K smoothing + AP helpers implementation ----
namespace N2K {
  // Runtime-configurable smoothing factors (exponential moving averages)
  // Defaults match the original hard-coded values.
  float A_HDG = 0.20f;
  float A_SPD = 0.05f;
  float A_WIND = 0.05f;
  float A_RDR = 0.25f;

  void setSmoothingFactors(float aHdg, float aSpd, float aWind, float aRdr) {
    A_HDG  = aHdg;
    A_SPD  = aSpd;
    A_WIND = aWind;
    A_RDR  = aRdr;
  }
}

namespace {

tNMEA2000* gBus = nullptr;

// Runtime-configurable timeout (defaults to compile-time TIMEOUT_MS)
uint32_t gTimeoutMs = N2K::TIMEOUT_MS;

// If true, prefer calculated true wind (TWA/TWS) derived from AWA/AWS + STW (fallback SOG).
static bool gUseCalculatedTrueWind = false;


// ---------------- Helpers -----------------------------------------------------
inline bool finiteF(float v){ return isfinite(v); }
inline bool finiteD(double v){ return isfinite(v); }
inline float wrap360(float d){ if(!finiteF(d)) return d; while(d<0)d+=360; while(d>=360)d-=360; return d; }
inline bool fresh(uint32_t tlast, uint32_t now, uint32_t to){ return tlast && (uint32_t)(now - tlast) <= to; }

static const double MS_TO_KNOTS_D = 1.9438444924406046;
static const double M_TO_NM_D      = 1.0/1852.0;


// EMA for angles via sin/cos
struct AngleEMA {
  bool  init=false;
  float s=0.0f, c=0.0f;
  void reset(){ init=false; s=0; c=0; }
  void update(float deg, float a){
    if(!finiteF(deg)) return;
    float r=deg*(float)DEG_TO_RAD, sn=sinf(r), cs=cosf(r);
    if(!init){ s=sn; c=cs; init=true; } else { s+=a*(sn-s); c+=a*(cs-c); }
  }
  float valueDeg() const {
    if(!init) return NAN;
    float ang = atan2f(s,c)*(float)RAD_TO_DEG;
    if(ang<0) ang += 360.0f;
    return ang;
  }
};

// ---------------- State (EMA + sticky last-good + timestamps) -----------------
AngleEMA hdgTrueE, hdgMagE, cogE, awaE, twaE, btwE, trkE, htsE;
float sogE=NAN, stwE=NAN, awsE=NAN, twsE=NAN, rdrE=NAN, dtwE=NAN, xtcE=NAN;

float hdgTrueS=NAN, hdgMagS=NAN, cogS=NAN, sogS=NAN, stwS=NAN;
float awsS=NAN, awaS=NAN, twsS=NAN, twaS=NAN;

// Calculated-true-wind state (derived), separate from PGN true wind.
AngleEMA twaCalcE;
float    twsCalcE=NAN;
float    twsCalcS=NAN, twaCalcS=NAN;
uint32_t tTWS_CALC=0, tTWA_CALC=0;

float rdrS=NAN, dtwS=NAN, btwS=NAN, trkS=NAN, htsS=NAN, xtcS=NAN;
float magS=NAN, depthS=NAN, latS=NAN, lonS=NAN;

uint32_t tHDG_ANY=0, tHDG_TRUE=0, tHDG_MAG=0, tCOG=0, tSOG=0, tSTW=0, tAWS=0, tAWA=0, tTWS=0, tTWA=0, tRDR=0;
uint32_t tMAG=0, tDEP=0, tLAT=0, tLON=0;
uint32_t tDTW=0, tBTW=0, tTRK=0, tHTS=0, tXTC=0, tETW=0;
tN2kHeadingReference htsRefS = N2khr_true; // Reference for PGN 127237 Heading-To-Steer

// ETW (estimated time to waypoint, time-to-go)
int16_t etwDaysVal=0;
int8_t  etwHoursVal=0;
int8_t  etwMinutesVal=0;
bool    etwValid=false;


// Autopilot (Raymarine EVO) state
N2K::ApMode apModeState = N2K::AP_MODE_UNKNOWN;
float apHeadingE_deg = NAN;
float apHeadingS_deg = NAN;
float apWindAngleE_deg = NAN;
float apWindAngleS_deg = NAN;
uint32_t tAPHeading = 0;
uint32_t tAPWind    = 0;
uint32_t tAPMode    = 0;
int      apPilotSourceSA = -1;   // cached pilot source address for commands

// Raymarine pilot alert state (PGN 65288). These are latched (sticky) until
// a future reset routine clears them.
bool        apAlarmLatched   = false;
bool        apWarningLatched = false;
int         apAlarmCodeVal   = 0;
int         apWarningCodeVal = 0;
const char* apAlarmTextVal   = "";
const char* apWarningTextVal = "";

// Last received Seatalk alarm identifiers (PGN 65288).
// Used to target the subsequent "SeaTalk: Silence Alarm" command (PGN 65361).
uint8_t     lastSeatalkAlarmId    = 0xFF;
uint8_t     lastSeatalkAlarmGroup = 0xFF;
bool        lastSeatalkAlarmValid = false;

// Last *active* autopilot Seatalk alarm identifiers (PGN 65288 state==1),
// used as the preferred target for the "SeaTalk: Silence Alarm" command (PGN 65361).
uint8_t     lastActiveSeatalkAlarmId    = 0xFF;
uint8_t     lastActiveSeatalkAlarmGroup = 0xFF;
bool        lastActiveSeatalkAlarmValid = false;

static inline bool isPilotWarningCode(const uint8_t code) {
  // 0x44 is explicitly named a warning in the provided list.
  return (code == 0x44) || (code >= 0x45 && code <= 0x4A);
}

static const char* pilotAlertText(const uint8_t code) {
  // Autolearn failures: 0x3E..0x43
  if (code >= 0x3E && code <= 0x43) {
    static const char* const kAutolearn[] = {
      "Pilot Autolearn Fail1",
      "Pilot Autolearn Fail2",
      "Pilot Autolearn Fail3",
      "Pilot Autolearn Fail4",
      "Pilot Autolearn Fail5",
      "Pilot Autolearn Fail6",
    };
    return kAutolearn[code - 0x3E];
  }

  switch (code) {
    // Watch / course keeping / wind
    case 0x13: return "Pilot Watch";
    case 0x14: return "Pilot Off Course";
    case 0x15: return "Pilot Wind Shift";
    case 0x17: return "Pilot Last Minute Of Watch";

    // Power / data / navigation inputs
    case 0x16: return "Pilot Low Battery";
    case 0x18: return "Pilot No NMEA Data";
    case 0x19: return "Pilot Large XTE";
    case 0x1A: return "Pilot NMEA DataError";
    case 0x2A: return "Pilot No GPS Fix";
    case 0x2B: return "Pilot No GPS COG";
    case 0x33: return "Pilot No Wind Data";
    case 0x34: return "Pilot No Speed Data";
    case 0x3A: return "Pilot No Nav Data";
    case 0x3B: return "Pilot Lost Waypoint Data";

    // Control head / comms / system integrity
    case 0x1B: return "Pilot CU Disconnected";
    case 0x22: return "Pilot No Pilot";
    case 0x2E: return "Pilot No Compass";
    case 0x2F: return "Pilot Rate Gyro Fault";
    case 0x35: return "Pilot Seatalk Fail1";
    case 0x36: return "Pilot Seatalk Fail2";
    case 0x65: return "Pilot No drive detected";
    case 0x67: return "Pilot Unexpected Reset While Engaged";

    // Waypoint / pre_track advance and “turn to waypoint” adjacent
    case 0x1D: return "Pilot Way Point Advance";
    case 0x31: return "Pilot Way Point Advance Port";
    case 0x32: return "Pilot Way Point Advance Stbd";

    // Drive / mechanics / limits
    case 0x1E: return "Pilot Drive Stopped";
    case 0x30: return "Pilot Current Limit";
    case 0x39: return "Pilot Turn Too Fast";
    case 0x3D: return "Pilot Rudder Feedback Fail";

    // Calibration / autolearn
    case 0x20: return "Pilot Calibration Required";

    // Warnings (softer conditions)
    case 0x44: return "Pilot Warning Cal Required";
    case 0x45: return "Pilot Warning OffCourse";
    case 0x46: return "Pilot Warning XTE";
    case 0x47: return "Pilot Warning Wind Shift";
    case 0x48: return "Pilot Warning Drive Short";
    case 0x49: return "Pilot Warning Clutch Short";
    case 0x4A: return "Pilot Warning Solenoid Short";

    // Command validity
    case 0x50: return "Pilot Invalid Command";

    // Operational state / misc
    case 0x1C: return "Pilot Auto Release";
    case 0x21: return "Pilot Last Heading";
    case 0x23: return "Pilot Route Complete";
    case 0x2C: return "Pilot Start Up";
    case 0x2D: return "Pilot Too Slow";
  }

  return nullptr;
}

static void latchPilotAlert(const uint8_t code) {
  const char* const text = pilotAlertText(code);
  if (!text) return;

  if (isPilotWarningCode(code)) {
    apWarningLatched = true;
    apWarningCodeVal = (int)code;
    apWarningTextVal = text;
  } else {
    apAlarmLatched = true;
    apAlarmCodeVal = (int)code;
    apAlarmTextVal = text;
  }
}


uint32_t tLastPrint=0;

// Smoothing factors are defined in the N2K namespace above.

// Select boat speed for true-wind calculation: prefer STW, fall back to SOG
// when STW is missing/invalid or stuck at 0 while SOG indicates movement.
static bool pickBoatSpeedKn(const uint32_t now, float &boatKn){
  const bool haveSTW = fresh(tSTW, now, gTimeoutMs) && finiteF(stwS);
  const bool haveSOG = fresh(tSOG, now, gTimeoutMs) && finiteF(sogS);

  if (!haveSTW && !haveSOG) return false;

  float stw = haveSTW ? stwS : NAN;
  float sog = haveSOG ? sogS : NAN;

  // Basic sanity
  if (haveSTW && stw < 0) stw = NAN;
  if (haveSOG && sog < 0) sog = NAN;

  // If STW is non-finite, negative, or "stuck" near zero while SOG shows movement,
  // treat STW as unreliable.
  bool stwOk = finiteF(stw);
  if (stwOk && stw <= 0.05f && finiteF(sog) && sog > 0.5f) stwOk = false;

  if (stwOk) {
    boatKn = stw;
    return true;
  }

  if (finiteF(sog)) {
    boatKn = sog;
    return true;
  }

  return false;
}

// Update derived TWA/TWS from apparent wind and boat speed.
// Uses AWA/AWS (kn, deg) and boat speed (kn). Stores TWA as 0..360 degrees.
static void updateCalculatedTrueWind(const uint32_t now){
  if (!fresh(tAWA, now, gTimeoutMs) || !fresh(tAWS, now, gTimeoutMs)) return;
  if (!finiteF(awaS) || !finiteF(awsS)) return;

  float boatKn;
  if (!pickBoatSpeedKn(now, boatKn)) return;
  if (!finiteF(boatKn)) return;

  // Vector math in boat frame using "wind-from" convention:
  // trueWindFrom = apparentWindFrom - boatSpeedForward.
  const float awaRad = awaS * (float)DEG_TO_RAD;
  const float x = awsS * cosf(awaRad) - boatKn;   // forward component
  const float y = awsS * sinf(awaRad);            // starboard component

  const float tws = sqrtf(x*x + y*y);
  if (finiteF(tws)) {
    if (isnan(twsCalcE)) twsCalcE = tws; else twsCalcE += N2K::A_WIND * (tws - twsCalcE);
    if (finiteF(twsCalcE)) { twsCalcS = twsCalcE; tTWS_CALC = now; }
  }

  float twa = atan2f(y, x) * (float)RAD_TO_DEG;
  if (twa < 0) twa += 360.0f;
  twa = wrap360(twa);

  twaCalcE.update(twa, N2K::A_WIND);
  const float cand = twaCalcE.init ? twaCalcE.valueDeg() : twa;
  if (finiteF(cand)) { twaCalcS = cand; tTWA_CALC = now; }
}


// ---------------- PGN list ----------------------------------------------------
const unsigned long RxPGNs[] PROGMEM = {
  127250UL, // Heading
  127258UL, // Magnetic Variation
  129026UL, // COG/SOG Rapid
  128259UL, // STW
  128267UL, // Depth
  130306UL, // Wind
  129025UL, // Position Rapid
  129029UL, // GNSS
  129283UL, // Cross Track Error
  129284UL, // Navigation info
  127237UL, // Heading/Track Control
  127245UL, // Rudder
  65288UL,  // Raymarine pilot alarm/warning (proprietary)
  65345UL, // Raymarine pilot wind angle
  65360UL, // Raymarine pilot locked heading
  65379UL, // Raymarine pilot state
  0
};

// ---------------- Message handler ---------------------------------------------
void HandleMsg(const tN2kMsg& m){
  const uint32_t now = millis();
  unsigned char SID;

  switch(m.PGN){

    case 127250UL: { // Heading
      double h, dev, var; tN2kHeadingReference ref;
      if (ParseN2kHeading(m, SID, h, dev, var, ref)) {
        // Update magnetic variation if present in this PGN
        if (!N2kIsNA(var) && finiteD(var)) {
          float mv = (float)(var * RAD_TO_DEG);
          if (finiteF(mv)) { magS = mv; tMAG = now; }
        }

        if (!N2kIsNA(h) && finiteD(h)) {
          // Heading value in degrees as transmitted (reference indicated by 'ref')
          const float hdIn = wrap360((float)(h * RAD_TO_DEG));

          // Variation in degrees (positive east). Needed to compute the complementary reference.
          const bool haveVar = fresh(tMAG, now, gTimeoutMs) && finiteF(magS);
          const float varDeg = haveVar ? magS : NAN;

          float hdTrue = NAN;
          float hdMag  = NAN;

          if (ref == N2khr_true) {
            hdTrue = hdIn;
            if (haveVar) hdMag = wrap360(hdTrue - varDeg);
          } else if (ref == N2khr_magnetic) {
            hdMag = hdIn;
            if (haveVar) hdTrue = wrap360(hdMag + varDeg);
          } else {
            // Unknown reference: keep behavior deterministic by updating both with the incoming value.
            // (No variation conversion attempted.)
            hdTrue = hdIn;
            hdMag  = hdIn;
          }

          if (finiteF(hdTrue)) {
            hdgTrueE.update(hdTrue, N2K::A_HDG);
            float cand = hdgTrueE.init ? hdgTrueE.valueDeg() : hdTrue;
            if (finiteF(cand)) { hdgTrueS = cand; tHDG_TRUE = now; tHDG_ANY = now; }
          }

          if (finiteF(hdMag)) {
            hdgMagE.update(hdMag, N2K::A_HDG);
            float cand = hdgMagE.init ? hdgMagE.valueDeg() : hdMag;
            if (finiteF(cand)) { hdgMagS = cand; tHDG_MAG = now; tHDG_ANY = now; }
          }
        }
      }
    } break;

    case 127258UL: { // Magnetic Variation
      tN2kMagneticVariation src; unsigned short age; double var;
      if (ParseN2kMagneticVariation(m, SID, src, age, var)) {
        if (!N2kIsNA(var) && finiteD(var)) {
          float mv = (float)(var * RAD_TO_DEG);
          if (finiteF(mv)) { magS = mv; tMAG = now; }
        }
      }
    } break;

    case 129026UL: { // COG/SOG Rapid
      tN2kHeadingReference ref; double cr, sm;
      if (ParseN2kCOGSOGRapid(m, SID, ref, cr, sm)) {
        if (!N2kIsNA(cr) && finiteD(cr)) {
          float cd = wrap360((float)(cr * RAD_TO_DEG));
          cogE.update(cd, N2K::A_SPD);
          float cand = cogE.init ? cogE.valueDeg() : cd;
          if (finiteF(cand)) { cogS = cand; tCOG = now; }
        }
        if (!N2kIsNA(sm) && finiteD(sm)) {
          float sog = (float)(sm * MS_TO_KNOTS_D);
          if (isnan(sogE)) sogE = sog; else sogE += N2K::A_SPD * (sog - sogE);
          if (finiteF(sogE)) { sogS = sogE; tSOG = now; }
          updateCalculatedTrueWind(now);
        }
      }
    } break;

    case 128259UL: { // STW
      double wmps, gmps; tN2kSpeedWaterReferenceType rt;
      if (ParseN2kBoatSpeed(m, SID, wmps, gmps, rt)) {
        if (!N2kIsNA(wmps) && finiteD(wmps)) {
          float stw = (float)(wmps * MS_TO_KNOTS_D);
          if (isnan(stwE)) stwE = stw; else stwE += N2K::A_SPD * (stw - stwE);
          if (finiteF(stwE)) { stwS = stwE; tSTW = now; }
          updateCalculatedTrueWind(now);
        }
      }
    } break;

    case 128267UL: { // Depth
      double d, off;
      if (ParseN2kWaterDepth(m, SID, d, off)) {
        if (!N2kIsNA(d) && finiteD(d)) {
          float dp = (float)d;
          if (finiteF(dp)) { depthS = dp; tDEP = now; }
        }
      }
    } break;

    case 130306UL: { // Wind
      double ws, wa; tN2kWindReference wr;
      if (ParseN2kWindSpeed(m, SID, ws, wa, wr)) {
        if (wr == N2kWind_Apparent) {
          if (!N2kIsNA(ws) && finiteD(ws)) {
            float sp = (float)(ws * MS_TO_KNOTS_D);
            if (isnan(awsE)) awsE = sp; else awsE += N2K::A_WIND * (sp - awsE);
            if (finiteF(awsE)) { awsS = awsE; tAWS = now; }
            updateCalculatedTrueWind(now);
          }
          if (!N2kIsNA(wa) && finiteD(wa)) {
            float an = wrap360((float)(wa * RAD_TO_DEG));
            awaE.update(an, N2K::A_WIND);
            float cand = awaE.init ? awaE.valueDeg() : an;
            if (finiteF(cand)) { awaS = cand; tAWA = now; }
            updateCalculatedTrueWind(now);
          }
        } else { // True wind
          if (!N2kIsNA(ws) && finiteD(ws)) {
            float sp = (float)(ws * MS_TO_KNOTS_D);
            if (isnan(twsE)) twsE = sp; else twsE += N2K::A_WIND * (sp - twsE);
            if (finiteF(twsE)) { twsS = twsE; tTWS = now; }
          }
          if (!N2kIsNA(wa) && finiteD(wa)) {
            float an = wrap360((float)(wa * RAD_TO_DEG));
            twaE.update(an, N2K::A_WIND);
            float cand = twaE.init ? twaE.valueDeg() : an;
            if (finiteF(cand)) { twaS = cand; tTWA = now; }
          }
        }
      }
    } break;

    case 129025UL: { // Position Rapid
      double la, lo;
      if (ParseN2kPositionRapid(m, la, lo)) {
        if (!N2kIsNA(la) && finiteD(la)) { latS = (double)la; tLAT = now; }
        if (!N2kIsNA(lo) && finiteD(lo)) { lonS = (double)lo; tLON = now; }
      }
    } break;

    case 129029UL: { // GNSS
      uint16_t days; double sec; tN2kGNSStype gt, rst; tN2kGNSSmethod gm; unsigned char ns, nrs;
      double hdop, pdop, geo; uint16_t rsid; double la, lo, al, age;
      if (ParseN2kGNSS(m, SID, days, sec, la, lo, al, gt, gm, ns, hdop, pdop, geo, nrs, rst, rsid, age)) {
        if (!N2kIsNA(la) && finiteD(la)) { latS = (double)la; tLAT = now; }
        if (!N2kIsNA(lo) && finiteD(lo)) { lonS = (double)lo; tLON = now; }
      }
    } break;


    case 129283UL: { // Cross Track Error
      tN2kXTEMode mode;
      bool        navTerminated;
      double      xte_m;
      if (ParseN2kXTE(m, SID, mode, navTerminated, xte_m)) {
        if (!N2kIsNA(xte_m) && finiteD(xte_m)) {
          float xte_nm = (float)(xte_m * M_TO_NM_D);
          if (finiteF(xte_nm)) {
            if (isnan(xtcE)) xtcE = xte_nm; else xtcE += N2K::A_SPD * (xte_nm - xtcE);
            if (finiteF(xtcE)) { xtcS = xtcE; tXTC = now; }
          }
        }
      }
    } break;

    case 129284UL: { // Navigation info
      double dist_m;
      tN2kHeadingReference        brgRef;
      bool                        perpCrossed, arrivalCircle;
      tN2kDistanceCalculationType calcType;
      double etaTime;
      int16_t etaDate;
      double brgOrig, brgPos;
      uint32_t wpOrig, wpDest;
      double destLat, destLon;
      double closing_ms;

      if (ParseN2kNavigationInfo(m, SID, dist_m, brgRef,
                                 perpCrossed, arrivalCircle, calcType,
                                 etaTime, etaDate,
                                 brgOrig, brgPos,
                                 wpOrig, wpDest,
                                 destLat, destLon,
                                 closing_ms)) {

        // Distance to waypoint (nm), smoothed
        if (!N2kIsNA(dist_m) && finiteD(dist_m)) {
          float dtwNm = (float)(dist_m * M_TO_NM_D);
          if (finiteF(dtwNm)) {
            if (isnan(dtwE)) dtwE = dtwNm; else dtwE += N2K::A_SPD * (dtwNm - dtwE);
            if (finiteF(dtwE)) { dtwS = dtwE; tDTW = now; }
          }
        }

        // Bearing from current position to destination waypoint (deg), smoothed as angle
        if (!N2kIsNA(brgPos) && finiteD(brgPos)) {
          float brgDeg = wrap360((float)(brgPos * RAD_TO_DEG));
          btwE.update(brgDeg, N2K::A_HDG);
          float cand = btwE.init ? btwE.valueDeg() : brgDeg;
          if (finiteF(cand)) { btwS = cand; tBTW = now; }
        }

        // Bearing from origin to destination waypoint (deg), i.e. route-leg/pre_track bearing (PGN 129284 field 9).
        // Smoothed as an angle for stable UI display.
        if (!N2kIsNA(brgOrig) && finiteD(brgOrig)) {
          float trkDeg = wrap360((float)(brgOrig * RAD_TO_DEG));
          trkE.update(trkDeg, N2K::A_HDG);
          float cand = trkE.init ? trkE.valueDeg() : trkDeg;
          if (finiteF(cand)) { trkS = cand; tTRK = now; }
        }

                // Estimated time to waypoint (time-to-go)
                // Prefer closing velocity from PGN 129284; fall back to speed-based estimate.
                bool   gotETW = false;
                double secToGo  = N2kDoubleNA;

                // Pick distance in meters (prefer current message, else last known DTW)
                double dist_m_etw = dist_m;
                if (N2kIsNA(dist_m_etw) && fresh(tDTW, now, gTimeoutMs) && finiteF(dtwS)) {
                  dist_m_etw = (double)dtwS / M_TO_NM_D; // nm -> m
                }

                // Pick bearing-to-waypoint in radians (prefer current message, else last known BTW)
                double brg_wp_rad = brgPos;
                if (N2kIsNA(brg_wp_rad) && fresh(tBTW, now, gTimeoutMs) && finiteF(btwS)) {
                  brg_wp_rad = (double)btwS * (double)DEG_TO_RAD;
                }

                // Tier 1: distance / closing velocity (best if available)
                if (!N2kIsNA(dist_m_etw) && !N2kIsNA(closing_ms) &&
                    finiteD(dist_m_etw) && finiteD(closing_ms) &&
                    fabs(closing_ms) > 0.01) {

                  secToGo  = dist_m_etw / fabs(closing_ms);
                  gotETW   = (secToGo >= 0.0);
                }

                // Tier 2: speed-based estimate (SOG preferred, STW fallback). If we also have COG and
                // bearing-to-waypoint, use VMG along the pre_track line.
                if (!gotETW &&
                    !N2kIsNA(dist_m_etw) && finiteD(dist_m_etw)) {

                  float spdKn = NAN;

                  if (fresh(tSOG, now, gTimeoutMs) && finiteF(sogS) && sogS > 0.2f) {
                    spdKn = sogS;
                  } else if (fresh(tSTW, now, gTimeoutMs) && finiteF(stwS) && stwS > 0.2f) {
                    spdKn = stwS;
                  }

                  if (finiteF(spdKn)) {
                    double spd_ms = (double)spdKn / MS_TO_KNOTS_D; // kn -> m/s

                    if (fresh(tCOG, now, gTimeoutMs) && finiteF(cogS) &&
                        !N2kIsNA(brg_wp_rad) && finiteD(brg_wp_rad)) {

                      double d = (double)cogS * (double)DEG_TO_RAD - brg_wp_rad;

                      while (d >  PI) d -= 2.0 * PI;
                      while (d < -PI) d += 2.0 * PI;

                      const double vmg_ms = spd_ms * cos(d);

                      // If VMG is reasonable and positive, use it; otherwise fall back to speed magnitude.
                      if (vmg_ms > 0.05) spd_ms = vmg_ms;
                    }

                    if (spd_ms > 0.05) {
                      secToGo = dist_m_etw / spd_ms;
                      gotETW  = (secToGo >= 0.0);
                    }
                  }
                }

                if (gotETW) {
                  uint32_t whole = (uint32_t)(secToGo + 0.5); // round to nearest second

                  int32_t  days = whole / 86400UL;
                  uint32_t rem  = whole % 86400UL;
                  int32_t  hrs  = rem / 3600UL;
                  uint32_t rem2 = rem % 3600UL;
                  int32_t  mins = rem2 / 60UL;

                  etwDaysVal    = (int16_t)days;
                  etwHoursVal   = (int8_t)hrs;
                  etwMinutesVal = (int8_t)mins;
                  etwValid      = true;
                  tETW          = now;
                } else {
                  // Don't clear a previously valid ETW on a single bad frame; let it go stale naturally.
                  if (!fresh(tETW, now, gTimeoutMs)) etwValid = false;
                }

      }
    } break;

    case 65288UL: { // Raymarine pilot alarm/warning (proprietary)
      unsigned char AlarmState;
      unsigned char AlarmCode;
      unsigned char AlarmGroup;

      if (RaymarinePilot::ParseN2kAlarm(m, AlarmState, AlarmCode, AlarmGroup)) {
        // Remember last alarm identifiers for a possible later "Silence_Alarm" command.
        lastSeatalkAlarmId    = (uint8_t)AlarmCode;
        lastSeatalkAlarmGroup = (uint8_t)AlarmGroup;
        lastSeatalkAlarmValid = true;


        // Prefer tracking the currently active autopilot alarm (state==1, group==Autopilot)
        // as the target for a subsequent "Silence_Alarm" command.
        if (AlarmState == 1 && AlarmGroup == 0x01) {
          lastActiveSeatalkAlarmId    = (uint8_t)AlarmCode;
          lastActiveSeatalkAlarmGroup = (uint8_t)AlarmGroup;
          lastActiveSeatalkAlarmValid = true;
        }
        // Alarm status values (critical):
        //   0 = Alarm condition not met (cleared)
        //   1 = Alarm condition met and not silenced (active + beeping)
        //   2 = Alarm condition met and silenced (active but should stop beeping)
        // We only trigger/latch on state==1. States 0 and 2 are treated as
        // silence/clear and reset the exposed latched alert state.
        if (AlarmState == 1) {
          // Latch and expose the most recent pilot alert.
          latchPilotAlert((uint8_t)AlarmCode);

          // Preserve existing internal behavior (only when alarm is active).
          if (AlarmCode == 0x1d && AlarmGroup == 0x01) {
            RaymarinePilot::alarmWaypoint = true;
          }
        } else if (AlarmState == 0 || AlarmState == 2) {
          apAlarmLatched   = false;
          apWarningLatched = false;
          apAlarmCodeVal   = 0;
          apWarningCodeVal = 0;
          apAlarmTextVal   = "";
          apWarningTextVal = "";
        }

        // Learn pilot source address.
        if (m.Source != 255) {
          apPilotSourceSA = (int)m.Source;
          RaymarinePilot::PilotSourceAddress = m.Source;
        }
      }
    } break;

    case 65379UL: { // Raymarine pilot state (mode)
      unsigned char Mode;
      unsigned char Submode;
      if (RaymarinePilot::ParseN2kPGN65379(m, Mode, Submode)) {
        N2K::ApMode newMode = N2K::AP_MODE_UNKNOWN;
        if (Mode == 0x00 && Submode == 0x00) {
          newMode = N2K::AP_MODE_STANDBY;
        } else if (Mode == 0x40 && Submode == 0x00) {
          newMode = N2K::AP_MODE_AUTO;
        } else if (Mode == 0x00 && Submode == 0x01) {
          newMode = N2K::AP_MODE_WIND;
        } else if (Mode == 0x80 && Submode == 0x01) {
          newMode = N2K::AP_MODE_PRE_TRACK;
        } else if (Mode == 0x81 && Submode == 0x01) {
          newMode = N2K::AP_MODE_TRACK;
        }
        apModeState = newMode;
        tAPMode = now;
        // Learn pilot source address from this PGN
        if (m.Source != 255) {
          apPilotSourceSA = (int)m.Source;
          RaymarinePilot::PilotSourceAddress = m.Source;
        }
      }
    } break;

    case 65345UL: { // Raymarine pilot wind angle target
      double WindAngle;               // Wind datum (setpoint)
      double RollingAverageWindAngle; // Actual wind, smoothed
      if (RaymarinePilot::ParseN2kPilotWindAngle(m, WindAngle, RollingAverageWindAngle)) {
        // Prefer the wind datum (setpoint); fall back to rolling average if datum is NA
        double useAngle = WindAngle;
        if (N2kIsNA(useAngle)) useAngle = RollingAverageWindAngle;
        if (!N2kIsNA(useAngle) && finiteD(useAngle)) {
          float deg = wrap360((float)RadToDeg(useAngle));
          // No additional smoothing: pre_track pilot's datum directly
          apWindAngleE_deg = deg;
          apWindAngleS_deg = deg;
          tAPWind = now;
          // Learn pilot source address
          if (m.Source != 255) {
            apPilotSourceSA = (int)m.Source;
            RaymarinePilot::PilotSourceAddress = m.Source;
          }
        }
      }
    } break;

    case 65360UL: { // Raymarine pilot locked heading
      double HeadingTrue;
      double HeadingMagnetic;
      if (RaymarinePilot::ParseN2kPilotLockedHeading(m, HeadingTrue, HeadingMagnetic)) {
        double raw = N2kIsNA(HeadingMagnetic) ? HeadingTrue : HeadingMagnetic;
        if (!N2kIsNA(raw) && finiteD(raw)) {
          float deg = wrap360((float)RadToDeg(raw));
          // No smoothing on pilot locked heading: pre_track setpoint directly
          apHeadingE_deg = deg;
          apHeadingS_deg = deg;
          tAPHeading = now;
          // Learn pilot source address
          if (m.Source != 255) {
            apPilotSourceSA = (int)m.Source;
            RaymarinePilot::PilotSourceAddress = m.Source;
          }
        }
      }
    } break;
    
    case 127237UL: { // Heading/Track Control (Heading-To-Steer)
      tN2kOnOff RudderLimitExceeded, OffHeadingLimitExceeded, OffTrackLimitExceeded, Override;
      tN2kSteeringMode SteeringMode;
      tN2kTurnMode TurnMode;
      tN2kHeadingReference HeadingReference;
      tN2kRudderDirectionOrder CommandedRudderDirection;
      double CommandedRudderAngle;
      double HeadingToSteerCourse;
      double Track;
      double RudderLimit;
      double OffHeadingLimit;
      double RadiusOfTurnOrder;
      double RateOfTurnOrder;
      double OffTrackLimit;
      double VesselHeading;

      if (ParseN2kHeadingTrackControl(m,
                                     RudderLimitExceeded, OffHeadingLimitExceeded, OffTrackLimitExceeded, Override,
                                     SteeringMode, TurnMode, HeadingReference,
                                     CommandedRudderDirection,
                                     CommandedRudderAngle,
                                     HeadingToSteerCourse,
                                     Track,
                                     RudderLimit, OffHeadingLimit,
                                     RadiusOfTurnOrder, RateOfTurnOrder,
                                     OffTrackLimit,
                                     VesselHeading)) {

        if (!N2kIsNA(HeadingToSteerCourse) && finiteD(HeadingToSteerCourse)) {
          float htsDeg = wrap360((float)(HeadingToSteerCourse * RAD_TO_DEG));
          htsE.update(htsDeg, N2K::A_HDG);
          float cand = htsE.init ? htsE.valueDeg() : htsDeg;
          if (finiteF(cand)) { htsS = cand; tHTS = now; htsRefS = HeadingReference; }
        }
      }
      break;
    }

case 127245UL: { // Rudder
      unsigned char inst; tN2kRudderDirectionOrder ord; double ao, pos;
      if (ParseN2kRudder(m, pos, inst, ord, ao)) {
        float rd = NAN;
        if (!N2kIsNA(pos) && finiteD(pos)) rd = (float)(pos * RAD_TO_DEG);
        else if (!N2kIsNA(ao) && finiteD(ao)) rd = (float)(ao * RAD_TO_DEG);
        if (finiteF(rd)) {
          if (isnan(rdrE)) rdrE = rd; else rdrE += N2K::A_RDR * (rd - rdrE);
          if (finiteF(rdrE)) { rdrS = rdrE; tRDR = now; }
        }
      }
    } break;

    default: break;
  }
}

} // namespace

// ---------------- Public API --------------------------------------------------
namespace N2K {

void begin(tNMEA2000 &bus){
  gBus = &bus;
  bus.SetN2kCANReceiveFrameBufSize(200);
  bus.SetN2kCANMsgBufSize(32);

  bus.SetProductInformation("00000001", 100, "ESP32-S3 N2K (sticky last-good, RT timeout)", "0.9.4", "A");
  bus.SetDeviceInformation(1, 130, 120, 2046, 4);

  bus.SetMode(tNMEA2000::N2km_ListenOnly);
  bus.EnableForward(false);
  bus.ExtendReceiveMessages(RxPGNs);
  bus.SetMsgHandler(HandleMsg);
  bus.Open();
}
void attach(tNMEA2000 &bus){
  gBus = &bus;
  bus.ExtendReceiveMessages(RxPGNs);
  bus.SetMsgHandler(HandleMsg);
}

void process(tNMEA2000 &bus){ bus.ParseMessages(); }

// Runtime timeout control
void setTimeoutMs(uint32_t ms){ gTimeoutMs = ms; }
uint32_t getTimeoutMs(){ return gTimeoutMs; }

// True wind source selection
void setUseCalculatedTrueWind(bool enable){ gUseCalculatedTrueWind = enable; }
bool getUseCalculatedTrueWind(){ return gUseCalculatedTrueWind; }

// Single source of truth for freshness
inline bool freshNow(uint32_t t){ return t && (uint32_t)(millis() - t) <= gTimeoutMs; }

// Smoothed getters (sticky last-good while fresh)
float hdgTrueSmoothed(){ return freshNow(tHDG_TRUE) ? hdgTrueS : NAN; }
float hdgMagSmoothed(){ return freshNow(tHDG_MAG) ? hdgMagS : NAN; }
float cogSmoothed(){ return freshNow(tCOG) ? cogS : NAN; }
float sogSmoothed(){ return freshNow(tSOG) ? sogS : NAN; }
float stwSmoothed(){ return freshNow(tSTW) ? stwS : NAN; }
float awsSmoothed(){ return freshNow(tAWS) ? awsS : NAN; }
float awaSmoothed(){ return freshNow(tAWA) ? awaS : NAN; }
float twsSmoothed(){
  if (gUseCalculatedTrueWind && freshNow(tTWS_CALC)) return twsCalcS;
  return freshNow(tTWS) ? twsS : NAN;
}
float twaSmoothed(){
  if (gUseCalculatedTrueWind && freshNow(tTWA_CALC)) return twaCalcS;
  return freshNow(tTWA) ? twaS : NAN;
}
float rdrSmoothed(){ return freshNow(tRDR) ? rdrS : NAN; }

float dtwNmSmoothed(){ return freshNow(tDTW) ? dtwS : NAN; }
float btwSmoothed()  { return freshNow(tBTW) ? btwS  : NAN; }
float xtcNmSmoothed(){ return freshNow(tXTC) ? xtcS : NAN; }

// Essentials (sticky)
float magVar()   { return freshNow(tMAG) ? magS   : NAN; }
float depth()    { return freshNow(tDEP) ? depthS : NAN; }
double latitude() { return freshNow(tLAT) ? latS   : NAN; }
double longitude(){ return freshNow(tLON) ? lonS   : NAN; }

// Estimated time to waypoint (time-to-go)
int16_t etwDays() {
  return (etwValid && freshNow(tETW)) ? etwDaysVal : (int16_t)-1;
}
int8_t etwHours() {
  return (etwValid && freshNow(tETW)) ? etwHoursVal : (int8_t)-1;
}
int8_t etwMinutes() {
  return (etwValid && freshNow(tETW)) ? etwMinutesVal : (int8_t)-1;
}

// Freshness helpers
bool hasFreshHDG(){ return freshNow(tHDG_ANY); }
bool hasFreshCOG(){ return freshNow(tCOG); }
bool hasFreshSOG(){ return freshNow(tSOG); }
bool hasFreshSTW(){ return freshNow(tSTW); }
bool hasFreshAWS(){ return freshNow(tAWS); }
bool hasFreshAWA(){ return freshNow(tAWA); }
bool hasFreshTWS(){
  if (gUseCalculatedTrueWind && freshNow(tTWS_CALC)) return true;
  return freshNow(tTWS);
}
bool hasFreshTWA(){
  if (gUseCalculatedTrueWind && freshNow(tTWA_CALC)) return true;
  return freshNow(tTWA);
}
bool hasFreshDepth(){ return freshNow(tDEP); }
bool hasFreshLatitude(){ return freshNow(tLAT); }
bool hasFreshLongitude(){ return freshNow(tLON); }
bool hasFreshRDR(){ return freshNow(tRDR); }
bool hasFreshMagVar(){ return freshNow(tMAG); }
bool hasFreshDTW(){ return freshNow(tDTW); }
bool hasFreshBTW(){ return freshNow(tBTW); }
bool hasFreshXTC(){ return freshNow(tXTC); }
bool hasFreshETW(){ return etwValid && freshNow(tETW); }
ApMode apMode(){
  if (!tAPMode || (uint32_t)(millis() - tAPMode) > gTimeoutMs) return AP_MODE_UNKNOWN;
  return apModeState;
}

float apHeadingTarget(){
  ApMode m = apMode();
  if (!(m == AP_MODE_AUTO || m == AP_MODE_PRE_TRACK || m == AP_MODE_TRACK)) return NAN;
  return freshNow(tAPHeading) ? apHeadingS_deg : NAN;
}

float apWindAngleTarget(){
  ApMode m = apMode();
  if (m != AP_MODE_WIND) return NAN;
  if (!freshNow(tAPWind)) return NAN;

  float deg = apWindAngleS_deg;
  if (!finiteF(deg)) return NAN;

  // Convert from [0,360) to [-180,180)
  if (deg > 180.0f) deg -= 360.0f;
  return deg;
}

float apTrackHeading(){
  // Signed heading change (deg) for the current Track/GoTo acquire/confirm prompt.
  // Prefer PGN 127237 Heading-To-Steer (field 11) when available, as this is what
  // Raymarine displays as the requested turn. Fall back to stored pre_track/leg bearing.
  float target = NAN;
  tN2kHeadingReference ref = N2khr_true;

  if (freshNow(tHTS) && finiteF(htsS)) {
    target = htsS;
    ref = htsRefS;
  } else if (freshNow(tTRK) && finiteF(trkS)) {
    target = trkS;
    ref = N2khr_true;
  } else {
    return NAN;
  }

  float hdg = (ref == N2khr_magnetic) ? hdgMagSmoothed() : hdgTrueSmoothed();
  if (!finiteF(hdg)) return NAN;

  float d = target - hdg;
  while (d > 180.0f) d -= 360.0f;
  while (d < -180.0f) d += 360.0f;
  return d;
}

bool apHasAlarm(){ return apAlarmLatched; }
bool apHasWarning(){ return apWarningLatched; }
int apAlarmCode(){ return apAlarmCodeVal; }
int apWarningCode(){ return apWarningCodeVal; }
const char* apAlarmText(){ return apAlarmTextVal; }
const char* apWarningText(){ return apWarningTextVal; }

// Build Raymarine proprietary "SeaTalk: Silence Alarm" (PGN 65361).
// Field layout (canboat): Manufacturer+Industry (16 bits), Alarm ID (8 bits),
// Alarm Group (8 bits), Reserved (32 bits). The Manufacturer/Industry word for
// Raymarine (1851) / Marine (4) is 0x873B (little endian: 0x3B,0x87).
static void buildSeatalkSilenceAlarm(tN2kMsg &msg, uint8_t alarmId, uint8_t alarmGroup) {
  msg.SetPGN(65361UL);
  msg.Priority = 7;       // Seatalk proprietary default priority (observed for related PGNs)
  msg.Destination = 255;  // PDU2 PGN; destination ignored (broadcast)

  // Manufacturer / industry header (2 bytes) for Raymarine (1851), industry=4.
  // Use Reserved2=3 to match Raymarine Axiom behavior on PGN 65361.
  const uint16_t mfgHeader = (uint16_t)(1851U | (3U<<11) | (4U<<13));
  msg.AddByte((uint8_t)(mfgHeader & 0xFF));
  msg.AddByte((uint8_t)((mfgHeader >> 8) & 0xFF));
  msg.AddByte(alarmId);
  msg.AddByte(alarmGroup);
  msg.AddByte(0xFF);
  msg.AddByte(0xFF);
  msg.AddByte(0xFF);
  msg.AddByte(0xFF);
}

void Silence_Alarm() {
  // Send the "Silence Alarm" PGN first (if possible).
  if (gBus) {
    tN2kMsg msg;
    const uint8_t alarmId = lastActiveSeatalkAlarmValid ? lastActiveSeatalkAlarmId :
                            (lastSeatalkAlarmValid ? lastSeatalkAlarmId : 0xFF);
    const uint8_t alarmGroup = lastActiveSeatalkAlarmValid ? lastActiveSeatalkAlarmGroup :
                              (lastSeatalkAlarmValid ? lastSeatalkAlarmGroup : 0xFF);
    buildSeatalkSilenceAlarm(msg, alarmId, alarmGroup);
    gBus->SendMsg(msg);
  }

  // Clear latched alert state (as requested).
  apAlarmLatched      = false;
  apWarningLatched    = false;
  apAlarmCodeVal      = 0;
  apWarningCodeVal    = 0;
  apAlarmTextVal      = "";
  apWarningTextVal    = "";
  lastSeatalkAlarmId    = 0xFF;
  lastSeatalkAlarmGroup = 0xFF;
  lastSeatalkAlarmValid = false;
  lastActiveSeatalkAlarmId    = 0xFF;
  lastActiveSeatalkAlarmGroup = 0xFF;
  lastActiveSeatalkAlarmValid = false;
}

bool hasFreshApHeading(){
  ApMode m = apMode();
  return (m == AP_MODE_AUTO || m == AP_MODE_PRE_TRACK || m == AP_MODE_TRACK) && freshNow(tAPHeading);
}

bool hasFreshApWindAngle(){
  ApMode m = apMode();
  return (m == AP_MODE_WIND) && freshNow(tAPWind);
}

void apSetPilotSource(uint8_t sa){
  apPilotSourceSA = (int)sa;
  RaymarinePilot::PilotSourceAddress = sa;
}

int apGetPilotSource(){
  if (apPilotSourceSA >= 0) return apPilotSourceSA;
  return RaymarinePilot::PilotSourceAddress;
}

// Internal helper: send a pilot command if we have a bus and pilot source
static void apSendCmd(void (*fn)(tN2kMsg &)){
  if (!gBus) return;
  if (RaymarinePilot::PilotSourceAddress < 0 && apPilotSourceSA >= 0){
    RaymarinePilot::PilotSourceAddress = (uint8_t)apPilotSourceSA;
  }
  if (RaymarinePilot::PilotSourceAddress < 0) return;
  tN2kMsg msg;
  fn(msg);
  gBus->SendMsg(msg);
}

// Overload for SetEvoPilotCourse which needs heading/change
static void apSendCourseCmd(void (*fn)(tN2kMsg &, double, int), double headingDeg, int delta){
  if (!gBus) return;
  if (RaymarinePilot::PilotSourceAddress < 0 && apPilotSourceSA >= 0){
    RaymarinePilot::PilotSourceAddress = (uint8_t)apPilotSourceSA;
  }
  if (RaymarinePilot::PilotSourceAddress < 0) return;
  tN2kMsg msg;
  fn(msg, headingDeg, delta);
  gBus->SendMsg(msg);
}

// Overload for SetEvoPilotWind which needs a target wind direction (radians)
static void apSendWindCmd(void (*fn)(tN2kMsg &, double), double targetWindRad){
  if (!gBus) return;
  if (RaymarinePilot::PilotSourceAddress < 0 && apPilotSourceSA >= 0){
    RaymarinePilot::PilotSourceAddress = (uint8_t)apPilotSourceSA;
  }
  if (RaymarinePilot::PilotSourceAddress < 0) return;
  tN2kMsg msg;
  fn(msg, targetWindRad);
  gBus->SendMsg(msg);
}

void apSetModeStandby(){ apSendCmd([](tN2kMsg &m){ RaymarinePilot::SetEvoPilotMode(m, PILOT_MODE_STANDBY); }); }
void apSetModeAuto()   { apSendCmd([](tN2kMsg &m){ RaymarinePilot::SetEvoPilotMode(m, PILOT_MODE_AUTO);    }); }
void apSetModeNoDrift(){ apSendCmd([](tN2kMsg &m){ RaymarinePilot::SetEvoPilotMode(m, PILOT_MODE_TRACK); }); }
void apSetModeWind()   { apSendCmd([](tN2kMsg &m){ RaymarinePilot::SetEvoPilotMode(m, PILOT_MODE_WIND);    }); }
void apSetModeTrack()  { apSendCmd([](tN2kMsg &m){ RaymarinePilot::SetEvoPilotMode(m, PILOT_MODE_PRE_TRACK);   }); }
void apTurnToWaypointMode(){ apSendCmd([](tN2kMsg &m){ RaymarinePilot::TurnToWaypointMode(m); }); }
void apTurnToWaypoint(double targetHeading){ (void)targetHeading; apSendCmd([](tN2kMsg &m){ RaymarinePilot::TurnToWaypoint(m); }); }


static float currentHeadingForDelta(){
  float hd = apHeadingTarget();
  if (isfinite(hd)) return hd;
  hd = hdgMagSmoothed();
  if (!isfinite(hd)) hd = hdgTrueSmoothed();
  if (isfinite(hd)) return hd;
  return NAN;
}

void apPlus1()  {
  ApMode m = apMode();
  if (m == AP_MODE_WIND) {
    // Adjust wind datum directly via SetEvoPilotWind
    float cur = apWindAngleTarget(); // [-180,180)
    if (!finiteF(cur)) return;
    float newDeg = cur + ((cur < 0.0f) ? -1.0f : 1.0f) * 1.0f;
    // Wrap back into [-180,180)
    if (newDeg > 180.0f) newDeg -= 360.0f;
    else if (newDeg < -180.0f) newDeg += 360.0f;
    // Convert to [0,360) for pilot
    float pilotDeg = (newDeg < 0.0f) ? (newDeg + 360.0f) : newDeg;
    double targetRad = DegToRad(pilotDeg);
    apSendWindCmd(RaymarinePilot::SetEvoPilotWind, targetRad);
  } else {
    float hd = currentHeadingForDelta();
    if (!isfinite(hd)) return;
    apSendCourseCmd(RaymarinePilot::SetEvoPilotCourse, hd,  1);
  }
}

void apPlus10() {
  ApMode m = apMode();
  if (m == AP_MODE_WIND) {
    float cur = apWindAngleTarget();
    if (!finiteF(cur)) return;
    float newDeg = cur + ((cur < 0.0f) ? -1.0f : 1.0f) * 10.0f;
    if (newDeg > 180.0f) newDeg -= 360.0f;
    else if (newDeg < -180.0f) newDeg += 360.0f;
    float pilotDeg = (newDeg < 0.0f) ? (newDeg + 360.0f) : newDeg;
    double targetRad = DegToRad(pilotDeg);
    apSendWindCmd(RaymarinePilot::SetEvoPilotWind, targetRad);
  } else {
    float hd = currentHeadingForDelta();
    if (!isfinite(hd)) return;
    apSendCourseCmd(RaymarinePilot::SetEvoPilotCourse, hd, 10);
  }
}

void apMinus1() {
  ApMode m = apMode();
  if (m == AP_MODE_WIND) {
    float cur = apWindAngleTarget();
    if (!finiteF(cur)) return;
    float newDeg = cur - ((cur < 0.0f) ? -1.0f : 1.0f) * 1.0f;
    if (newDeg > 180.0f) newDeg -= 360.0f;
    else if (newDeg < -180.0f) newDeg += 360.0f;
    float pilotDeg = (newDeg < 0.0f) ? (newDeg + 360.0f) : newDeg;
    double targetRad = DegToRad(pilotDeg);
    apSendWindCmd(RaymarinePilot::SetEvoPilotWind, targetRad);
  } else {
    float hd = currentHeadingForDelta();
    if (!isfinite(hd)) return;
    apSendCourseCmd(RaymarinePilot::SetEvoPilotCourse, hd, -1);
  }
}

void apMinus10(){
  ApMode m = apMode();
  if (m == AP_MODE_WIND) {
    float cur = apWindAngleTarget();
    if (!finiteF(cur)) return;
    float newDeg = cur - ((cur < 0.0f) ? -1.0f : 1.0f) * 10.0f;
    if (newDeg > 180.0f) newDeg -= 360.0f;
    else if (newDeg < -180.0f) newDeg += 360.0f;
    float pilotDeg = (newDeg < 0.0f) ? (newDeg + 360.0f) : newDeg;
    double targetRad = DegToRad(pilotDeg);
    apSendWindCmd(RaymarinePilot::SetEvoPilotWind, targetRad);
  } else {
    float hd = currentHeadingForDelta();
    if (!isfinite(hd)) return;
    apSendCourseCmd(RaymarinePilot::SetEvoPilotCourse, hd,-10);
  }
}


// Compact log
void printEMA(){
  static uint32_t tLast = 0;
  const uint32_t now = millis();
  if ((uint32_t)(now - tLast) < PRINT_INTERVAL_MS) return;
  tLast = now;

  Serial.print(F("N2K timeout(ms): ")); Serial.println(gTimeoutMs);

  Serial.println(F("----- N2K snapshot (sticky last-good) -----"));
  Serial.print(F("hdg true: ")); { float v=hdgTrueSmoothed(); if (finiteF(v)) { Serial.print(v,1); Serial.println(F(" deg")); } else Serial.println(F("NA")); }
  Serial.print(F("hdg mag: "));  { float v=hdgMagSmoothed();  if (finiteF(v)) { Serial.print(v,1); Serial.println(F(" deg")); } else Serial.println(F("NA")); }
  Serial.print(F("mag var: ")); { float v=magVar(); if (finiteF(v)) { Serial.print(v,1); Serial.println(F(" deg")); } else Serial.println(F("NA")); }
  Serial.print(F("cog/sog: ")); { float c=cogSmoothed(), s=sogSmoothed();
    if (finiteF(c)) { Serial.print(c,1); Serial.print(F(" deg, ")); } else { Serial.print(F("NA deg, ")); }
    if (finiteF(s)) { Serial.print(s,2); Serial.println(F(" kn")); } else Serial.println(F("NA kn")); }
  Serial.print(F("stw: ")); { float v=stwSmoothed(); if (finiteF(v)) { Serial.print(v,2); Serial.println(F(" kn")); } else Serial.println(F("NA")); }
  Serial.print(F("aws/awa: ")); { float s=awsSmoothed(), a=awaSmoothed();
    if (finiteF(s)) { Serial.print(s,2); Serial.print(F(" kn, ")); } else { Serial.print(F("NA kn, ")); }
    if (finiteF(a)) { Serial.print(a,1); Serial.println(F(" deg")); } else Serial.println(F("NA deg")); }
  Serial.print(F("tws/twa: ")); { float s=twsSmoothed(), a=twaSmoothed();
    if (finiteF(s)) { Serial.print(s,2); Serial.print(F(" kn, ")); } else { Serial.print(F("NA kn, ")); }
    if (finiteF(a)) { Serial.print(a,1); Serial.println(F(" deg")); } else Serial.println(F("NA deg")); }
  Serial.print(F("depth: ")); { float v=depth(); if (finiteF(v)) { Serial.print(v,2); Serial.println(F(" m")); } else Serial.println(F("NA")); }
  Serial.print(F("position: ")); {
    double la=latitude(), lo=longitude();
    if (finiteF(la)) Serial.print(la,4); else Serial.print(F("NA"));
    Serial.print(F(", "));
    if (finiteF(lo)) Serial.println(lo,4); else Serial.println(F("NA"));
  }
  Serial.print(F("rdr: ")); { float v=rdrSmoothed(); if (finiteF(v)) { Serial.print(v,1); Serial.println(F(" deg")); } else Serial.println(F("NA")); }
  Serial.println();
}

} // namespace N2K