Ray_N2K_PGN_Sniffer_Evo
=======================

Purpose
-------
This sketch listens on your NMEA2000 network and prints a *filtered* log focused on
Raymarine EV-1 / EVO autopilot "vane/wind setpoint" investigation.

It auto-detects your pilot's N2K source address (SA) using Raymarine proprietary PGNs:
- 65379 (pilot state)
- 65360 (pilot locked heading)
- 65345 (pilot wind datum / setpoint)

Then it prints:
- always: 65379 / 65360 / 65345 (decoded + raw bytes)
- from pilot: 127237 / 126208 / 126720 (raw) (and more if you enable "ALL pilot PGNs")
- optional: 130306 (wind data, rate-limited) (decoded + raw bytes)
- optional: 127250 (heading, rate-limited) (decoded + raw bytes)

Serial commands
---------------
h or ? : help
p      : toggle "print only pilot + whitelist" (default ON)
a      : toggle "print ALL pilot PGNs" (default OFF)
w      : toggle wind 130306 printing
g      : toggle heading 127250 printing
c      : show counts of unique PGNs seen from pilot SA

How to capture a useful trace
-----------------------------
1) Flash this sketch.
2) Wait until you see:
     "Discovered pilot source address (SA): <number>"
3) Put the pilot in WIND mode and set a target on starboard then port
   (e.g., +40° and -40°), while watching these lines:
   - PGN 65345 (decoded: TargetSigned / norm degrees)
   - any PGN 127237 from the pilot (raw data)
4) Copy/paste ~30–60 seconds of this filtered output into the chat.

Notes
-----
- This sketch runs in LISTEN-ONLY mode by default and should not add network traffic.
- If your CAN pins differ, adjust ESP32_CAN_RX_PIN / ESP32_CAN_TX_PIN and uncomment
  the SetCANRxPin/SetCANTxPin lines if needed.
