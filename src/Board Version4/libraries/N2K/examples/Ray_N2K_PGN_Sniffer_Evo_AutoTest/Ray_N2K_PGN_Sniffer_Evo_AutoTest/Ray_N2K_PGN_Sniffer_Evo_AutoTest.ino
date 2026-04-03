#include <Arduino.h>

// Pins (match your working Ray_N2K example)
#define ESP32_CAN_RX_PIN GPIO_NUM_0
#define ESP32_CAN_TX_PIN GPIO_NUM_6

#include <NMEA2000_esp32_twai.h>
#include <NMEA2000.h>
#include <N2kMessages.h>
#include <math.h>

// Global NMEA2000 bus instance
static tNMEA2000 &NMEA2000 = *(new NMEA2000_esp32_twai());

// -------------------- Logging controls --------------------
static bool gLogEnabled = true;      // print whitelisted frames continuously
static bool gPilotOnly  = true;      // only print pilot frames (except wind/heading toggles)
static bool gPrintWind130306 = true; // rate-limited
static bool gPrintHdg127250  = false;// rate-limited
static bool gPrintAllPilotPGNs = false; // debug flood from pilot

static int gPilotSA = -1;            // discovered pilot source address
static uint32_t gCaptureUntilMs = 0; // during this window, print extra even if gLogEnabled=false

// Rate limit noisy PGNs
static uint32_t gLastWindMs = 0;
static uint32_t gLastHdgMs  = 0;

// -------------------- Auto test --------------------
static const uint32_t AUTO_STEP_DELAY_MS = 1500;
static const uint32_t AFTER_MODE_DELAY_MS = 2500;
static const uint32_t CAPTURE_WINDOW_MS = 1200;

struct Step {
  enum Type : uint8_t { MODE_WIND, WIND_TARGET_SIGNED_DEG } type;
  int16_t v; // signed degrees for WIND_TARGET_SIGNED_DEG
};

// Test sequence: start in WIND, then step through port and starboard targets.
// NOTE: target is signed: port negative, starboard positive.
static const Step kSteps[] = {
  { Step::MODE_WIND, 0 },
  { Step::WIND_TARGET_SIGNED_DEG, -120 },
  { Step::WIND_TARGET_SIGNED_DEG, -90  },
  { Step::WIND_TARGET_SIGNED_DEG, -60  },
  { Step::WIND_TARGET_SIGNED_DEG, -40  },
  { Step::WIND_TARGET_SIGNED_DEG, -20  },
  { Step::WIND_TARGET_SIGNED_DEG, -10  },
  { Step::WIND_TARGET_SIGNED_DEG, +10  },
  { Step::WIND_TARGET_SIGNED_DEG, +20  },
  { Step::WIND_TARGET_SIGNED_DEG, +40  },
  { Step::WIND_TARGET_SIGNED_DEG, +60  },
  { Step::WIND_TARGET_SIGNED_DEG, +90  },
  { Step::WIND_TARGET_SIGNED_DEG, +120 },
};
static const uint8_t kNumSteps = sizeof(kSteps) / sizeof(kSteps[0]);

static bool gAutoArmed = false;   // user pressed 't' but we may still be waiting for pilot SA
static bool gAutoRunning = false;
static uint8_t gAutoIdx = 0;
static uint32_t gNextAutoMs = 0;

// -------------------- Helpers --------------------
static inline float wrapSigned180(float deg) {
  while (deg >= 180.0f) deg -= 360.0f;
  while (deg <  -180.0f) deg += 360.0f;
  return deg;
}

static inline float wrap360(float deg) {
  while (deg < 0.0f) deg += 360.0f;
  while (deg >= 360.0f) deg -= 360.0f;
  return deg;
}

static inline double degToRad(double deg) {
  return deg * M_PI / 180.0;
}

static inline double radToDeg(double rad) {
  return rad * 180.0 / M_PI;
}

static void printRawBytes(const tN2kMsg &m, int maxBytes = 16) {
  int n = m.DataLen;
  if (n > maxBytes) n = maxBytes;
  Serial.print(F("raw:"));
  for (int i = 0; i < n; i++) {
    int idx = i;
    uint8_t b = m.GetByte(idx);
    Serial.print(' ');
    if (b < 16) Serial.print('0');
    Serial.print(b, HEX);
  }
}

static bool isWhitelisted(uint32_t pgn) {
  switch (pgn) {
    case 65379UL: // Ray pilot state
    case 65345UL: // Ray pilot wind angle target
    case 65360UL: // Ray pilot locked heading
    case 126208UL: // Ray-pilot command/ack
    case 127237UL: // Heading/Track Control (sometimes echoed)
    case 130306UL: // Wind data
    case 127250UL: // Heading
      return true;
    default:
      return false;
  }
}

// -------------------- Raymarine command builders (same bytes as your library) --------------------
static void sendRaySetModeWind() {
  if (gPilotSA < 0) {
    Serial.println(F("[CMD] Pilot SA unknown; cannot send WIND mode."));
    return;
  }
  tN2kMsg msg;
  msg.SetPGN(126208UL);
  msg.Priority = 3;
  msg.Destination = (uint8_t)gPilotSA;

  msg.AddByte(1);
  msg.AddByte(0x63);
  msg.AddByte(0xff);
  msg.AddByte(0x00);
  msg.AddByte(0xf8);
  msg.AddByte(0x04);
  msg.AddByte(0x01);
  msg.AddByte(0x3b);
  msg.AddByte(0x07);
  msg.AddByte(0x03);
  msg.AddByte(0x04);
  msg.AddByte(0x04);

  // WIND mode = 0x0001
  msg.AddByte(0x00);
  msg.AddByte(0x01);

  msg.AddByte(0x05);
  msg.AddByte(0xff);
  msg.AddByte(0xff);

  Serial.print(F("[CMD] -> Pilot SA="));
  Serial.print(gPilotSA);
  Serial.println(F(" set MODE=WIND"));
  NMEA2000.SendMsg(msg);
}

// signedDeg: [-180, +180) where port is negative.
// Raymarine SetEvoPilotWind expects an *unsigned* [0..360) angle encoded as radians*10000 in uint16.
static void sendRayWindTargetSignedDeg(float signedDeg) {
  if (gPilotSA < 0) {
    Serial.println(F("[CMD] Pilot SA unknown; cannot send WIND target."));
    return;
  }

  signedDeg = wrapSigned180(signedDeg);
  float pilotDeg = (signedDeg < 0.0f) ? (signedDeg + 360.0f) : signedDeg;
  pilotDeg = wrap360(pilotDeg);

  double targetWindRad = degToRad(pilotDeg);
  uint16_t targetWind10000 = (uint16_t)(targetWindRad * 10000.0);
  uint8_t b0 = (uint8_t)(targetWind10000 & 0xFF);
  uint8_t b1 = (uint8_t)(targetWind10000 >> 8);

  tN2kMsg msg;
  msg.SetPGN(126208UL);
  msg.Priority = 3;
  msg.Destination = (uint8_t)gPilotSA;

  msg.AddByte(1);
  msg.AddByte(0x41);
  msg.AddByte(0xff);
  msg.AddByte(0x00);
  msg.AddByte(0xf8);
  msg.AddByte(0x03);
  msg.AddByte(0x01);
  msg.AddByte(0x3b);
  msg.AddByte(0x07);
  msg.AddByte(0x03);
  msg.AddByte(0x04);
  msg.AddByte(0x04);
  msg.AddByte(b0);
  msg.AddByte(b1);

  Serial.print(F("[CMD] -> Pilot SA="));
  Serial.print(gPilotSA);
  Serial.print(F(" set WIND target="));
  Serial.print(signedDeg, 1);
  Serial.print(F(" deg (pilot="));
  Serial.print(pilotDeg, 1);
  Serial.println(F(" deg 0..360)"));

  NMEA2000.SendMsg(msg);
}

// -------------------- Auto test state machine --------------------
static void armAutoTest() {
  gAutoArmed = true;
  gAutoRunning = false;
  gAutoIdx = 0;
  gNextAutoMs = 0;
  Serial.println();
  Serial.println(F("=== AutoTest armed ==="));
  Serial.println(F("Safety: this will command the autopilot wind target through multiple angles."));
  Serial.println(F("If this is unsafe (e.g., underway), DO NOT run."));
  if (gPilotSA < 0) {
    Serial.println(F("Waiting to discover pilot source address (SA)..."));
  } else {
    Serial.print(F("Pilot SA is ")); Serial.println(gPilotSA);
    Serial.println(F("Starting in ~1s..."));
    gAutoRunning = true;
    gNextAutoMs = millis() + 1000;
  }
}

static void serviceAutoTest() {
  if (!gAutoArmed) return;
  if (!gAutoRunning) {
    if (gPilotSA >= 0) {
      Serial.print(F("Pilot SA discovered: ")); Serial.println(gPilotSA);
      Serial.println(F("Starting AutoTest in ~1s..."));
      gAutoRunning = true;
      gNextAutoMs = millis() + 1000;
    }
    return;
  }

  const uint32_t now = millis();
  if ((int32_t)(now - gNextAutoMs) < 0) return;

  if (gAutoIdx >= kNumSteps) {
    Serial.println(F("=== AutoTest complete. Logging paused (press 'l' to resume). ==="));
    gAutoArmed = false;
    gAutoRunning = false;
    gLogEnabled = false;
    return;
  }

  const Step &s = kSteps[gAutoIdx];

  Serial.print(F("[AutoTest] Step ")); Serial.print(gAutoIdx);
  Serial.print(F("/")); Serial.print((int)kNumSteps - 1);
  Serial.print(F(" : "));

  if (s.type == Step::MODE_WIND) {
    Serial.println(F("Set MODE=WIND"));
    sendRaySetModeWind();
    gCaptureUntilMs = now + CAPTURE_WINDOW_MS;
    gNextAutoMs = now + AFTER_MODE_DELAY_MS;
  } else {
    Serial.print(F("Set WIND target signed(deg)=")); Serial.println(s.v);
    sendRayWindTargetSignedDeg((float)s.v);
    gCaptureUntilMs = now + CAPTURE_WINDOW_MS;
    gNextAutoMs = now + AUTO_STEP_DELAY_MS;
  }

  gAutoIdx++;
}

// -------------------- N2K receive handler --------------------
static void HandleN2kMsg(const tN2kMsg &m) {
  const uint32_t now = millis();

  // Discover pilot SA from the Ray pilot PGNs.
  if ((m.PGN == 65379UL || m.PGN == 65345UL || m.PGN == 65360UL) && m.Source != 255) {
    if (gPilotSA < 0) {
      gPilotSA = (int)m.Source;
      Serial.print(F("=== Discovered pilot source address (SA): "));
      Serial.println(gPilotSA);
    }
  }

  // Always allow the NMEA2000 stack to run fast: keep handler lightweight.

  const bool inCapture = (gCaptureUntilMs != 0) && ((int32_t)(now - gCaptureUntilMs) <= 0);
  if (!gLogEnabled && !inCapture) {
    // Still print very interesting pilot frames during capture-off.
    return;
  }

  // Filter out non-interesting PGNs early
  if (!isWhitelisted(m.PGN)) return;

  // Pilot-only filter
  const bool fromPilot = (gPilotSA >= 0) ? ((int)m.Source == gPilotSA) : (m.PGN == 65379UL || m.PGN == 65345UL || m.PGN == 65360UL);

  if (gPilotOnly && !fromPilot) {
    // Allow wind/heading instrument PGNs when toggled
    if (!(m.PGN == 130306UL && gPrintWind130306) && !(m.PGN == 127250UL && gPrintHdg127250)) {
      return;
    }
  }

  if (!gPrintAllPilotPGNs && fromPilot) {
    // If we're filtering pilot frames, keep it tight to the plausible PGNs
    if (!(m.PGN == 65379UL || m.PGN == 65345UL || m.PGN == 65360UL || m.PGN == 126208UL || m.PGN == 127237UL)) {
      return;
    }
  }

  // Rate limit noisy PGNs
  if (m.PGN == 130306UL) {
    if (!gPrintWind130306) return;
    if (!inCapture && (now - gLastWindMs) < 500) return;
    gLastWindMs = now;
  }
  if (m.PGN == 127250UL) {
    if (!gPrintHdg127250) return;
    if (!inCapture && (now - gLastHdgMs) < 500) return;
    gLastHdgMs = now;
  }

  // Print header
  Serial.print('['); Serial.print(now); Serial.print(F(" ms] "));
  Serial.print(F("SA=")); Serial.print(m.Source);
  Serial.print(F(" PGN=")); Serial.print(m.PGN);
  Serial.print(F(" len=")); Serial.print(m.DataLen);
  Serial.print(F(" | "));

  // Decoders for key PGNs
  if (m.PGN == 65379UL) {
    int idx = 2;
    uint8_t mode = m.GetByte(idx);
    uint8_t sub  = m.GetByte(idx);
    Serial.print(F("PILOT_STATE mode=0x"));
    Serial.print(mode, HEX);
    Serial.print(F(" sub=0x"));
    Serial.print(sub, HEX);
    Serial.print(F(" "));
    printRawBytes(m, 8);
  } else if (m.PGN == 65345UL) {
    int idx = 2;
    double windDatumRad = m.Get2ByteDouble(0.0001, idx);
    double windAvgRad   = m.Get2ByteDouble(0.0001, idx);

    // Choose datum if available, otherwise avg
    double useRad = windDatumRad;
    if (N2kIsNA(useRad)) useRad = windAvgRad;

    if (!N2kIsNA(useRad) && isfinite(useRad)) {
      float deg360 = wrap360((float)radToDeg(useRad));
      float signedDeg = (deg360 > 180.0f) ? (deg360 - 360.0f) : deg360;
      Serial.print(F("PILOT_WIND set360=")); Serial.print(deg360, 1);
      Serial.print(F(" deg, setSigned=")); Serial.print(signedDeg, 1);
      Serial.print(F(" deg"));
    } else {
      Serial.print(F("PILOT_WIND set=NA"));
    }
    Serial.print(F(" | "));
    printRawBytes(m, 8);
  } else if (m.PGN == 65360UL) {
    int idx = 3;
    double hdgTrue = m.Get2ByteUDouble(0.0001, idx);
    double hdgMag  = m.Get2ByteUDouble(0.0001, idx);
    Serial.print(F("PILOT_LOCKED_HDG true="));
    if (!N2kIsNA(hdgTrue) && isfinite(hdgTrue)) Serial.print(radToDeg(hdgTrue), 1);
    else Serial.print(F("NA"));
    Serial.print(F(" deg, mag="));
    if (!N2kIsNA(hdgMag) && isfinite(hdgMag)) Serial.print(radToDeg(hdgMag), 1);
    else Serial.print(F("NA"));
    Serial.print(F(" deg | "));
    printRawBytes(m, 8);
  } else if (m.PGN == 130306UL) {
    unsigned char sid = 0;
    double ws = NAN, wa = NAN;
    tN2kWindReference ref;
    if (ParseN2kWindSpeed(m, sid, ws, wa, ref)) {
      const double kn = ws * 1.9438444924406046;
      float deg360 = wrap360((float)radToDeg(wa));
      float signedDeg = (deg360 > 180.0f) ? (deg360 - 360.0f) : deg360;
      Serial.print(F("WIND 130306 ws=")); Serial.print(kn, 2);
      Serial.print(F(" kn, waSigned=")); Serial.print(signedDeg, 1);
      Serial.print(F(" deg, wa360=")); Serial.print(deg360, 1);
      Serial.print(F(" deg, ref=")); Serial.print((int)ref);
    } else {
      Serial.print(F("WIND 130306 (parse failed)"));
    }
    Serial.print(F(" | "));
    printRawBytes(m, 8);
  } else if (m.PGN == 126208UL) {
    Serial.print(F("126208 (cmd/ack) "));
    printRawBytes(m, 16);
  } else if (m.PGN == 127237UL) {
    Serial.print(F("127237 (Heading/Track Ctrl) "));
    printRawBytes(m, 16);
  } else if (m.PGN == 127250UL) {
    // Heading: keep it simple, raw only (we mainly care that it exists + rate-limited)
    Serial.print(F("127250 (Heading) "));
    printRawBytes(m, 8);
  } else {
    printRawBytes(m, 16);
  }

  Serial.println();
}

// -------------------- Serial controls --------------------
static void printHelp() {
  Serial.println();
  Serial.println(F("Commands:"));
  Serial.println(F("  t = arm & run AutoTest (sets WIND mode, then sends target angles)"));
  Serial.println(F("  l = toggle continuous logging (default ON)"));
  Serial.println(F("  p = toggle pilot-only filter (default ON)"));
  Serial.println(F("  a = toggle print ALL pilot PGNs (default OFF)"));
  Serial.println(F("  w = toggle print PGN 130306 (wind) (default ON, rate-limited)"));
  Serial.println(F("  g = toggle print PGN 127250 (heading) (default OFF, rate-limited)"));
  Serial.println(F("  h or ? = help"));
  Serial.println();
}

static void handleSerial() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') continue;
    switch (c) {
      case 'h':
      case '?':
        printHelp();
        break;
      case 'l':
        gLogEnabled = !gLogEnabled;
        Serial.print(F("Logging: ")); Serial.println(gLogEnabled ? F("ON") : F("OFF"));
        break;
      case 'p':
        gPilotOnly = !gPilotOnly;
        Serial.print(F("Pilot-only filter: ")); Serial.println(gPilotOnly ? F("ON") : F("OFF"));
        break;
      case 'a':
        gPrintAllPilotPGNs = !gPrintAllPilotPGNs;
        Serial.print(F("Print ALL pilot PGNs: ")); Serial.println(gPrintAllPilotPGNs ? F("ON") : F("OFF"));
        break;
      case 'w':
        gPrintWind130306 = !gPrintWind130306;
        Serial.print(F("Print 130306: ")); Serial.println(gPrintWind130306 ? F("ON") : F("OFF"));
        break;
      case 'g':
        gPrintHdg127250 = !gPrintHdg127250;
        Serial.print(F("Print 127250: ")); Serial.println(gPrintHdg127250 ? F("ON") : F("OFF"));
        break;
      case 't':
        armAutoTest();
        break;
      default:
        break;
    }
  }
}

// -------------------- Setup / loop --------------------
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println(F("Raymarine EVO PGN Sniffer + AutoTest (filtered)"));

  // Basic NMEA2000 node setup (talker + listener)
  NMEA2000.SetN2kCANReceiveFrameBufSize(200);
  NMEA2000.SetN2kCANMsgBufSize(32);

  NMEA2000.SetProductInformation(
    "00000002",       // Model serial code
    101,              // Product code
    "Ray Sniffer",    // Model ID
    "1.0.0",          // Software version
    "1.0.0"           // Model version
  );

  NMEA2000.SetDeviceInformation(
    2,    // Unique number
    130,  // Function
    120,  // Class
    2046  // Industry group
  );

  NMEA2000.SetMode(tNMEA2000::N2km_ListenAndNode, 52);
  NMEA2000.EnableForward(false);
  NMEA2000.SetMsgHandler(HandleN2kMsg);

  // Only request/accept PGNs we actually care about to keep bus load minimal
  static const unsigned long RxPGNs[] = {
    65379UL, 65345UL, 65360UL,
    126208UL, 127237UL,
    130306UL, 127250UL,
    0
  };
  NMEA2000.ExtendReceiveMessages(RxPGNs);

  NMEA2000.Open();

  printHelp();
}

void loop() {
  NMEA2000.ParseMessages();
  handleSerial();
  serviceAutoTest();
  yield();
}
