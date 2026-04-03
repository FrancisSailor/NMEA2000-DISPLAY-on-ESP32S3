#include <Arduino.h>

#define ESP32_CAN_RX_PIN GPIO_NUM_0
#define ESP32_CAN_TX_PIN GPIO_NUM_6

#include <NMEA2000_esp32_twai.h>
#include <NMEA2000.h>
#include <N2kMessages.h>

tNMEA2000 &NMEA2000 = *(new NMEA2000_esp32_twai());

// ---------------- Sniffer configuration ----------------

// Discovered from Raymarine pilot proprietary PGNs (65379/65360/65345)
static int16_t gPilotSA = -1;

// Print controls
static bool gPrintPilotOnly = true;       // after pilot SA is known, print only pilot messages (plus any whitelisted)
static bool gPrintAllPilotPGNs = false;   // if true, print EVERY PGN from pilot SA (can still be large)
static bool gPrintWind130306 = true;      // wind data (rate limited)
static bool gPrintHeading127250 = false;  // heading (rate limited)
static bool gPrint127237Raw = true;       // heading/track control (raw)
static bool gPrintISO60928Once = true;    // print ISO Address Claim for pilot once discovered

// Rate limits (ms)
static const uint32_t WIND_PRINT_MIN_INTERVAL_MS   = 200;  // 5 Hz max
static const uint32_t HDG_PRINT_MIN_INTERVAL_MS    = 250;  // 4 Hz max
static uint32_t gLastWindPrintMs = 0;
static uint32_t gLastHdgPrintMs  = 0;

// Track what we've seen from the pilot (unique PGNs + counts)
struct PgnCount {
  uint32_t pgn;
  uint32_t count;
};
static PgnCount gPilotPgns[64];
static uint8_t gPilotPgnsLen = 0;

static void recordPilotPgn(uint32_t pgn) {
  for (uint8_t i = 0; i < gPilotPgnsLen; i++) {
    if (gPilotPgns[i].pgn == pgn) {
      gPilotPgns[i].count++;
      return;
    }
  }
  if (gPilotPgnsLen < (sizeof(gPilotPgns) / sizeof(gPilotPgns[0]))) {
    gPilotPgns[gPilotPgnsLen].pgn = pgn;
    gPilotPgns[gPilotPgnsLen].count = 1;
    gPilotPgnsLen++;
  }
}

static void printHexPayload(const tN2kMsg &msg) {
  for (int i = 0; i < msg.DataLen; i++) {
    if (msg.Data[i] < 16) Serial.print('0');
    Serial.print(msg.Data[i], HEX);
    if (i != msg.DataLen - 1) Serial.print(' ');
  }
}

static double radToDeg(double rad) { return rad * 180.0 / M_PI; }
static double normDeg360(double deg) {
  double x = fmod(deg, 360.0);
  if (x < 0) x += 360.0;
  return x;
}

static bool isSignedAngleNA(int16_t raw) {
  // We don't have the full N2K NA semantics for this proprietary PGN;
  // handle the common NA patterns defensively.
  return (raw == (int16_t)0x7FFF) || (raw == (int16_t)0xFFFF);
}
static bool isUnsignedAngleNA(uint16_t raw) {
  return (raw == 0xFFFF);
}

static void decode65379(const tN2kMsg &msg) {
  if (msg.DataLen < 4) return;
  uint8_t mode = msg.Data[2];
  uint8_t sub  = msg.Data[3];
  Serial.print(F("  -> 65379 pilot state: Mode=0x"));
  if (mode < 16) Serial.print('0');
  Serial.print(mode, HEX);
  Serial.print(F(" Submode=0x"));
  if (sub < 16) Serial.print('0');
  Serial.print(sub, HEX);
  Serial.println();
}

static void decode65360(const tN2kMsg &msg) {
  if (msg.DataLen < 7) return;
  // Index = 3 in library parser, then two *unsigned* 2-byte angles scaled 0.0001 (rad)
  uint16_t rawTrue = (uint16_t)msg.Data[3] | ((uint16_t)msg.Data[4] << 8);
  uint16_t rawMag  = (uint16_t)msg.Data[5] | ((uint16_t)msg.Data[6] << 8);

  Serial.print(F("  -> 65360 locked heading: "));
  if (!isUnsignedAngleNA(rawTrue)) {
    double hTrueDeg = radToDeg(rawTrue * 0.0001);
    Serial.print(F("True="));
    Serial.print(normDeg360(hTrueDeg), 2);
    Serial.print(F("° "));
  } else {
    Serial.print(F("True=NA "));
  }

  if (!isUnsignedAngleNA(rawMag)) {
    double hMagDeg = radToDeg(rawMag * 0.0001);
    Serial.print(F("Mag="));
    Serial.print(normDeg360(hMagDeg), 2);
    Serial.print(F("°"));
  } else {
    Serial.print(F("Mag=NA"));
  }
  Serial.println();
}

static void decode65345(const tN2kMsg &msg) {
  if (msg.DataLen < 6) return;
  // Index = 2 in library parser, then two *signed* 2-byte angles scaled 0.0001 (rad)
  int16_t rawA = (int16_t)((uint16_t)msg.Data[2] | ((uint16_t)msg.Data[3] << 8));
  int16_t rawB = (int16_t)((uint16_t)msg.Data[4] | ((uint16_t)msg.Data[5] << 8));

  Serial.print(F("  -> 65345 pilot wind datum: "));
  if (!isSignedAngleNA(rawA)) {
    double degSigned = radToDeg(rawA * 0.0001);
    Serial.print(F("TargetSigned="));
    Serial.print(degSigned, 2);
    Serial.print(F("° (norm "));
    Serial.print(normDeg360(degSigned), 2);
    Serial.print(F("°) "));
  } else {
    Serial.print(F("TargetSigned=NA "));
  }

  if (!isSignedAngleNA(rawB)) {
    double degSigned = radToDeg(rawB * 0.0001);
    Serial.print(F("AvgSigned="));
    Serial.print(degSigned, 2);
    Serial.print(F("° (norm "));
    Serial.print(normDeg360(degSigned), 2);
    Serial.print(F("°)"));
  } else {
    Serial.print(F("AvgSigned=NA"));
  }
  Serial.println();
}

static void printMsgHeader(const tN2kMsg &msg) {
  Serial.print(millis());
  Serial.print(F(" ms  SA="));
  Serial.print(msg.Source);
  Serial.print(F("  PGN="));
  Serial.print(msg.PGN);
  Serial.print(F("  LEN="));
  Serial.print(msg.DataLen);
  Serial.print(F("  PRI="));
  Serial.print(msg.Priority);
  Serial.print(F("  DST="));
  Serial.print(msg.Destination);
  Serial.print(F("  DATA="));
  printHexPayload(msg);
  Serial.println();
}

static bool shouldPrint(const tN2kMsg &msg) {
  const uint32_t pgn = msg.PGN;
  const uint8_t sa = msg.Source;

  // Always print the three Raymarine pilot PGNs that matter for this investigation
  if (pgn == 65379UL || pgn == 65360UL || pgn == 65345UL) return true;

  // If pilot SA is known, optionally print all from pilot
  if (gPilotSA >= 0 && sa == (uint8_t)gPilotSA) {
    if (gPrintAllPilotPGNs) return true;
    // Otherwise only print a short list of "plausible" PGNs from pilot
    if (pgn == 127237UL) return gPrint127237Raw;
    if (pgn == 126208UL) return true;
    if (pgn == 126720UL) return true;
    // Add more here if you discover something interesting
  }

  // Wind sensor PGN (not from pilot typically) — rate limited
  if (pgn == 130306UL && gPrintWind130306) {
    const uint32_t now = millis();
    if (now - gLastWindPrintMs >= WIND_PRINT_MIN_INTERVAL_MS) {
      gLastWindPrintMs = now;
      return true;
    }
    return false;
  }

  // Heading (optional) — rate limited
  if (pgn == 127250UL && gPrintHeading127250) {
    const uint32_t now = millis();
    if (now - gLastHdgPrintMs >= HDG_PRINT_MIN_INTERVAL_MS) {
      gLastHdgPrintMs = now;
      return true;
    }
    return false;
  }

  // If configured to print only pilot + whitelist, stop here
  if (gPrintPilotOnly) return false;

  return false;
}

static void HandleN2kMsg(const tN2kMsg &msg) {
  const uint32_t pgn = msg.PGN;

  // Discover pilot source address from proprietary Raymarine pilot PGNs
  if ((pgn == 65379UL || pgn == 65360UL || pgn == 65345UL) && gPilotSA < 0) {
    gPilotSA = msg.Source;
    Serial.print(F("=== Discovered pilot source address (SA): "));
    Serial.print(gPilotSA);
    Serial.println(F(" ==="));
  }

  if (gPilotSA >= 0 && msg.Source == (uint8_t)gPilotSA) {
    recordPilotPgn(pgn);
  }

  if (!shouldPrint(msg)) return;

  printMsgHeader(msg);

  // Decode key PGNs
  if (pgn == 65379UL) decode65379(msg);
  else if (pgn == 65360UL) decode65360(msg);
  else if (pgn == 65345UL) decode65345(msg);
  else if (pgn == 130306UL) {
    unsigned char SID;
    double ws, wa;
    tN2kWindReference ref;
    if (ParseN2kWindSpeed(msg, SID, ws, wa, ref)) {
      Serial.print(F("  -> 130306 wind: SID="));
      Serial.print(SID);
      Serial.print(F("  Ref="));
      Serial.print((int)ref);
      Serial.print(F("  Speed="));
      Serial.print(ws, 3);
      Serial.print(F(" m/s  Angle="));
      Serial.print(radToDeg(wa), 2);
      Serial.println(F("°"));
    }
  } else if (pgn == 127250UL) {
    unsigned char SID;
    double heading;
    double deviation;
    double variation;
    tN2kHeadingReference ref;
    if (ParseN2kHeading(msg, SID, heading, deviation, variation, ref)) {
      Serial.print(F("  -> 127250 heading: Ref="));
      Serial.print((int)ref);
      Serial.print(F("  Heading="));
      Serial.print(normDeg360(radToDeg(heading)), 2);
      Serial.println(F("°"));
    }
  }

  // If we just learned pilot SA, optionally print its ISO Address Claim when it appears
  if (gPilotSA >= 0 && gPrintISO60928Once && msg.Source == (uint8_t)gPilotSA && pgn == 60928UL) {
    gPrintISO60928Once = false;
  }
}

static void printHelp() {
  Serial.println();
  Serial.println(F("Raymarine EVO N2K PGN sniffer (filtered)"));
  Serial.println(F("Keys:"));
  Serial.println(F("  ? / h  : help"));
  Serial.println(F("  p      : toggle print-only-pilot mode (default ON)"));
  Serial.println(F("  a      : toggle print ALL pilot PGNs (default OFF)"));
  Serial.println(F("  w      : toggle print 130306 wind (rate limited)"));
  Serial.println(F("  g      : toggle print 127250 heading (rate limited)"));
  Serial.println(F("  c      : show pilot PGN counts seen so far"));
  Serial.println(F("Notes:"));
  Serial.println(F("  - Pilot SA is auto-detected from PGNs 65379/65360/65345."));
  Serial.println(F("  - For the 15deg port offset, focus on 65345 and any 127237 from the pilot."));
  Serial.println();
}

static void printPilotPgnCounts() {
  Serial.println(F("---- PGNs seen from pilot ----"));
  if (gPilotSA < 0) {
    Serial.println(F("Pilot SA not discovered yet (waiting for 65379/65360/65345)."));
    return;
  }
  Serial.print(F("Pilot SA="));
  Serial.println(gPilotSA);
  for (uint8_t i = 0; i < gPilotPgnsLen; i++) {
    Serial.print(F("  PGN "));
    Serial.print(gPilotPgns[i].pgn);
    Serial.print(F("  count="));
    Serial.println(gPilotPgns[i].count);
  }
  Serial.println(F("------------------------------"));
}

static void handleSerial() {
  if (!Serial.available()) return;
  const char c = (char)Serial.read();
  switch (c) {
    case 'h':
    case '?':
      printHelp();
      break;
    case 'p':
      gPrintPilotOnly = !gPrintPilotOnly;
      Serial.print(F("Print only pilot + whitelist: "));
      Serial.println(gPrintPilotOnly ? F("ON") : F("OFF"));
      break;
    case 'a':
      gPrintAllPilotPGNs = !gPrintAllPilotPGNs;
      Serial.print(F("Print ALL pilot PGNs: "));
      Serial.println(gPrintAllPilotPGNs ? F("ON") : F("OFF"));
      break;
    case 'w':
      gPrintWind130306 = !gPrintWind130306;
      Serial.print(F("Print wind 130306: "));
      Serial.println(gPrintWind130306 ? F("ON") : F("OFF"));
      break;
    case 'g':
      gPrintHeading127250 = !gPrintHeading127250;
      Serial.print(F("Print heading 127250: "));
      Serial.println(gPrintHeading127250 ? F("ON") : F("OFF"));
      break;
    case 'c':
      printPilotPgnCounts();
      break;
    default:
      break;
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println(F("Starting N2K PGN sniffer..."));

  // Bus setup
  NMEA2000.SetN2kCANReceiveFrameBufSize(200);
  NMEA2000.SetN2kCANMsgBufSize(32);

  NMEA2000.SetProductInformation(
    "00000002",          // Model serial code
    101,                 // Product code
    "PGN Sniffer",       // Model ID
    "1.0.0",             // Software version
    "1.0.0"              // Model version
  );

  // Function/Class are not critical for listen-only sniffing
  NMEA2000.SetDeviceInformation(2, 130, 120, 2046, 4);

  // Listen-only is safest for troubleshooting (won't inject traffic).
  // If you want this node to also transmit later, change to N2km_ListenAndNode.
  NMEA2000.SetMode(tNMEA2000::N2km_ListenOnly);
  NMEA2000.EnableForward(false);

  // We will receive everything (filtering is done in software above).
  NMEA2000.SetMsgHandler(HandleN2kMsg);

  // TWAI / pins are configured by NMEA2000_esp32_twai internally, but you may need:
  // ((NMEA2000_esp32_twai&)NMEA2000).SetCANRxPin(ESP32_CAN_RX_PIN);
  // ((NMEA2000_esp32_twai&)NMEA2000).SetCANTxPin(ESP32_CAN_TX_PIN);

  NMEA2000.Open();
  printHelp();
}

void loop() {
  NMEA2000.ParseMessages();
  handleSerial();
}
