
#ifndef N2K_MODULE_H
#define N2K_MODULE_H

#include <Arduino.h>
#include <NMEA2000.h>

namespace N2K {

// Smoothing factors (exponential moving averages)
// FVT: A_WIND is by default 0.2, A_SPD also 0.2
extern float A_HDG;
extern float A_SPD;
extern float A_WIND;
extern float A_RDR;

// Convenience helper to set all smoothing factors at once.
void setSmoothingFactors(float aHdg, float aSpd, float aWind, float aRdr);


// ---------------- Default compile-time timeout (can be overridden) ------------
#ifndef N2K_TIMEOUT_MS
  static const uint32_t TIMEOUT_MS = 1000;
#else
  static const uint32_t TIMEOUT_MS = N2K_TIMEOUT_MS;
#endif

#ifndef N2K_PRINT_GLOBAL_INTERVAL_MS
  static const uint32_t PRINT_INTERVAL_MS = 500;
#else
  static const uint32_t PRINT_INTERVAL_MS = N2K_PRINT_GLOBAL_INTERVAL_MS;
#endif

// ---------------- Init / service / debug --------------------------------------
void begin(tNMEA2000 &bus);
void process(tNMEA2000 &bus);
void printEMA();

// Attach only message handler & internal bus pointer without configuring/opening the bus.
// Useful when you want to configure tNMEA2000 yourself (e.g. talker / node mode) but still
// use the RayN2K smoothing + autopilot helpers.
void attach(tNMEA2000 &bus);

// ---------------- Autopilot helpers (Raymarine EVO) -----------------------------
// Note: these are additive; existing public API above remains fully compatible.

enum ApMode {
  AP_MODE_UNKNOWN = 0,
  AP_MODE_STANDBY = 1,
  AP_MODE_AUTO    = 2,
  AP_MODE_WIND    = 3,
  AP_MODE_PRE_TRACK   = 4,
  AP_MODE_TRACK = 5
};

// Current pilot mode (UNKNOWN or STANDBY/AUTO/WIND/PRE_TRACK).
ApMode apMode();

// Locked heading / target course from the pilot (deg).
// Returns NaN unless the value is fresh AND the pilot is in AUTO or PRE_TRACK mode.
float apHeadingTarget();

// Target wind angle from the pilot when in WIND mode (deg).
// Returns NaN otherwise.
float apWindAngleTarget();

// Freshness helpers for autopilot-related values.
bool hasFreshApHeading();
bool hasFreshApWindAngle();

// Configure which NMEA2000 source address to talk to for pilot commands.
// Typically this is the EV-1 pilot's source address.
void apSetPilotSource(uint8_t sa);
int  apGetPilotSource();

// High-level pilot control helpers using that source address and the shared
// NMEA2000 bus (set via begin/attach). Safe to call even if the bus/pilot
// are not yet known; they will simply no-op.
void apSetModeStandby();
void apSetModeAuto();
void apSetModeTrack();
void apSetModeWind();
void apSetModePreTrack();
void apTurnToWaypointMode();
void apTurnToWaypoint(double targetHeading);

// Signed Track acquisition heading change (deg).
// Computed as: (Originâ†’Destination bearing from PGN 129284) - (current true heading),
// normalized to [-180,+180]. Negative=port, positive=starboard.
// Returns NaN if required values are stale.
float apTrackHeading();

// ---------------- Pilot alerts (Raymarine EVO) --------------------------------
// Parsed from proprietary PGN 65288.
// These flags are "latched": once they become true, they stay true until a
// future reset routine clears them.
bool apHasAlarm();
bool apHasWarning();
int  apAlarmCode();
int  apWarningCode();
const char* apAlarmText();
const char* apWarningText();

// Sends SeaTalk: Silence Alarm (PGN 65361) and clears the latched
// alarm/warning flags exposed above.
void Silence_Alarm();


// Course change helpers (+/- 1 or 10 degrees). They try to base the change on
// the locked heading (if available), otherwise fall back to smoothed HDG.
void apPlus1();
void apPlus10();
void apMinus1();
void apMinus10();

// ---------------- Runtime timeout control -------------------------------------
void     setTimeoutMs(uint32_t ms);
uint32_t getTimeoutMs();

// True wind source selection
// If enabled, the library will prefer calculated TWA/TWS derived from AWA/AWS + STW (fallback SOG).
void setUseCalculatedTrueWind(bool enable);
bool getUseCalculatedTrueWind();

// ---------------- Smoothed getters (sticky last-good while fresh) -------------
float hdgTrueSmoothed();  // deg
float hdgMagSmoothed();   // deg
float cogSmoothed();  // deg
float sogSmoothed();  // kn
float stwSmoothed();  // kn
float awsSmoothed();  // kn
float awaSmoothed();  // deg
float twsSmoothed();  // kn
float twaSmoothed();  // deg
float rdrSmoothed();  // deg

// Navigation to waypoint
float dtwNmSmoothed(); // nm, distance to active waypoint
float btwSmoothed();   // deg, bearing to waypoint (position -> destination)
float xtcNmSmoothed(); // nm, signed cross-pre_track error (-port, +stbd)

// Estimated time to waypoint (time-to-go, not absolute ETA)
int16_t etwDays();     // days to waypoint (-1 if stale)
int8_t  etwHours();    // hours to waypoint (-1 if stale)
int8_t  etwMinutes();  // minutes to waypoint (-1 if stale)

// ---------------- Essentials (no smoothing, sticky) ---------------------------
float magVar();       // deg
float depth();        // m
double latitude();     // deg
double longitude();    // deg

// ---------------- Per-field freshness helpers ---------------------------------
bool hasFreshHDG();
bool hasFreshCOG();
bool hasFreshSOG();
bool hasFreshSTW();
bool hasFreshAWS();
bool hasFreshAWA();
bool hasFreshTWS();
bool hasFreshTWA();
bool hasFreshDepth();
bool hasFreshLatitude();
bool hasFreshLongitude();
bool hasFreshRDR();
bool hasFreshMagVar();

bool hasFreshDTW();
bool hasFreshBTW();
bool hasFreshXTC();
bool hasFreshETW();

// Back-compat aliases (deprecated)
inline float rudderSmoothed() { return rdrSmoothed(); }
inline bool  hasFreshRudder() { return hasFreshRDR(); }

} // namespace N2K

#endif // N2K_MODULE_H