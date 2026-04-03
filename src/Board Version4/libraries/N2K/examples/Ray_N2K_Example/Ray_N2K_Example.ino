#include <Arduino.h>

#define ESP32_CAN_RX_PIN GPIO_NUM_0
#define ESP32_CAN_TX_PIN GPIO_NUM_6

#include <NMEA2000_esp32_twai.h>
#include <NMEA2000.h>
#include <N2kMessages.h>

// Global NMEA2000 bus instance (same pattern as your working listener sketch)
tNMEA2000 &NMEA2000 = *(new NMEA2000_esp32_twai());

// Our combined listener + pilot helper library
#include <Ray_N2K.h>

void printHelp(){
  Serial.println();
  Serial.println(F("Commands:"));
  Serial.println(F("  a = AP AUTO"));
  Serial.println(F("  s = AP STANDBY"));
  Serial.println(F("  w = AP WIND"));
  Serial.println(F("  t = AP TRACK"));
  Serial.println(F("  1 = +1 deg"));
  Serial.println(F("  2 = +10 deg"));
  Serial.println(F("  3 = -1 deg"));
  Serial.println(F("  4 = -10 deg"));
  Serial.println(F("  h or ? = help"));
  Serial.println();
}

void setup(){
  Serial.begin(115200);
  delay(200);
  Serial.println(F("Ray_N2K example (ESP32-S3 + Raymarine EVO)"));

  // Basic NMEA2000 node setup (talker + listener)
  NMEA2000.SetN2kCANReceiveFrameBufSize(200);
  NMEA2000.SetN2kCANMsgBufSize(32);

  NMEA2000.SetProductInformation(
    "00000001",    // Model serial code
    100,           // Product code
    "Ray_N2K Node", // Model ID
    "0.4.0",       // Software version
    "0.4.0"        // Model version
  );

  NMEA2000.SetDeviceInformation(
    1,   // Unique number (e.g. serial)
    130, // Function (display)
    120, // Class (sensor/communication)
    2046 // Industry group
  );

  // Let this node both talk and listen
  NMEA2000.SetMode(tNMEA2000::N2km_ListenAndNode, 51);

  // Attach Ray_N2K smoothing + pilot helpers WITHOUT changing mode/product info
  N2K::attach(NMEA2000);

  // Use a slightly longer timeout for demo
  N2K::setTimeoutMs(5000);

  NMEA2000.Open();

  printHelp();
}

void handleSerial(){
  if (!Serial.available()) return;
  char c = Serial.read();
  switch (c) {
    case 'h':
    case '?':
      printHelp();
      break;
    case 'a':
      Serial.println(F("AP -> AUTO"));
      N2K::apSetModeAuto();
      break;
    case 's':
      Serial.println(F("AP -> STANDBY"));
      N2K::apSetModeStandby();
      break;
    case 'w':
      Serial.println(F("AP -> WIND"));
      N2K::apSetModeWind();
      break;
    case 't':
      Serial.println(F("AP -> TRACK"));
      N2K::apSetModeTrack();
      break;
    case '1':
      Serial.println(F("AP +1 deg"));
      N2K::apPlus1();
      break;
    case '2':
      Serial.println(F("AP +10 deg"));
      N2K::apPlus10();
      break;
    case '3':
      Serial.println(F("AP -1 deg"));
      N2K::apMinus1();
      break;
    case '4':
      Serial.println(F("AP -10 deg"));
      N2K::apMinus10();
      break;
    default:
      break;
  }
}

const char* apModeToStr(N2K::ApMode m){
  switch (m) {
    case N2K::AP_MODE_STANDBY: return "STANDBY";
    case N2K::AP_MODE_AUTO:    return "AUTO";
    case N2K::AP_MODE_WIND:    return "WIND";
    case N2K::AP_MODE_TRACK:   return "TRACK";
    default:                   return "UNKNOWN";
  }
}

void printSnapshot(){
  static unsigned long last = 0;
  const unsigned long now = millis();
  if (now - last < 1000) return;
  last = now;

  Serial.println(F("---- N2K + AP snapshot ----"));

  // Wind (boat sensors)
  if (N2K::hasFreshAWS() || N2K::hasFreshTWS()) {
    float aws = N2K::awsSmoothed();
    float awa = N2K::awaSmoothed();
    float tws = N2K::twsSmoothed();
    float twa = N2K::twaSmoothed();
    Serial.print(F("AWS/AWA: "));
    if (isfinite(aws)) { Serial.print(aws,1); Serial.print(F(" kn, ")); } else Serial.print(F("NA kn, "));
    if (isfinite(awa)) { Serial.print(awa,1); Serial.println(F(" deg")); } else Serial.println(F("NA deg"));
    Serial.print(F("TWS/TWA: "));
    if (isfinite(tws)) { Serial.print(tws,1); Serial.print(F(" kn, ")); } else Serial.print(F("NA kn, "));
    if (isfinite(twa)) { Serial.print(twa,1); Serial.println(F(" deg")); } else Serial.println(F("NA deg"));
  }

  // Heading / COG / SOG
  float hdgT = N2K::hdgTrueSmoothed();
  float hdgM = N2K::hdgMagSmoothed();
  float cog = N2K::cogSmoothed();
  float sog = N2K::sogSmoothed();
  Serial.print(F("HDG(T)/HDG(M)/COG/SOG: ") );
  if (isfinite(hdgT)) { Serial.print(hdgT,1); Serial.print(F(" / ") ); } else Serial.print(F("NA / ") );
  if (isfinite(hdgM)) { Serial.print(hdgM,1); Serial.print(F(" / ") ); } else Serial.print(F("NA / ") );
  if (isfinite(cog)) { Serial.print(cog,1); Serial.print(F(" / ")); } else Serial.print(F("NA / "));
  if (isfinite(sog)) { Serial.print(sog,1); Serial.println(F(" kn")); } else Serial.println(F("NA kn"));

  // Rudder
  float rdr = N2K::rdrSmoothed();
  Serial.print(F("RDR: "));
  if (isfinite(rdr)) { Serial.print(rdr,1); Serial.println(F(" deg")); } else Serial.println(F("NA"));

  // Autopilot mode + targets
  N2K::ApMode m = N2K::apMode();
  Serial.print(F("AP mode: "));
  Serial.println(apModeToStr(m));

  int sa = N2K::apGetPilotSource();
  Serial.print(F("AP source address: "));
  if (sa >= 0) Serial.println(sa);
  else Serial.println(F("unknown"));

  // Course / wind targets from the pilot (respecting modes)
  float apHdg = N2K::apHeadingTarget();
  float apW   = N2K::apWindAngleTarget();

  Serial.print(F("AP locked heading: "));
  if (isfinite(apHdg)) Serial.println(apHdg,1);
  else Serial.println(F("NA"));

  Serial.print(F("AP wind target: "));
  if (isfinite(apW)) Serial.println(apW,1);
  else Serial.println(F("NA"));

  // Navigation to active waypoint
  float dtw = N2K::dtwNmSmoothed();
  float btw = N2K::btwSmoothed();
  float xtc = N2K::xtcNmSmoothed();
  Serial.print(F("DTW/BTW/XTC: "));
  if (isfinite(dtw)) { Serial.print(dtw,2); Serial.print(F(" nm, ")); } else Serial.print(F("NA nm, "));
  if (isfinite(btw)) { Serial.print(btw,1); Serial.print(F(" deg, ")); } else Serial.print(F("NA deg, "));
  if (isfinite(xtc)) { Serial.print(xtc,3); Serial.println(F(" nm")); } else Serial.println(F("NA"));

  Serial.print(F("ETW: "));
  if (N2K::hasFreshETW()) {
    int16_t d = N2K::etwDays();
    int8_t  h = N2K::etwHours();
    int8_t  m = N2K::etwMinutes();
    Serial.print(d); Serial.print(F("d "));
    Serial.print(h); Serial.print(F("h "));
    Serial.print(m); Serial.println(F("m"));
  } else {
    Serial.println(F("NA"));
  }

  Serial.println();
}

void loop(){
  N2K::process(NMEA2000);
  handleSerial();
  printSnapshot();
}
