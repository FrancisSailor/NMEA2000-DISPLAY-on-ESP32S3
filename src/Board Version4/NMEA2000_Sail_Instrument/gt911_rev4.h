#pragma once

#include <Arduino.h>
#include <Wire.h>

class GT911Rev4 {
public:
  bool begin(TwoWire &wire, uint8_t addr);
  uint8_t address() const { return m_addr; }
  uint8_t readPoints(int16_t *x, int16_t *y, uint8_t max_points);
  static bool probeAddress(TwoWire &wire, uint8_t addr);
  static bool scanForAddress(TwoWire &wire, uint8_t &addr_out);
  bool applyRev4Config();
  bool readProductId(char out[5]);
  bool readStatus(uint8_t &status);

private:
  bool readReg(uint16_t reg, uint8_t *buf, size_t len);
  bool writeReg(uint16_t reg, const uint8_t *buf, size_t len);
  bool writeByte(uint16_t reg, uint8_t value);

  TwoWire *m_wire = nullptr;
  uint8_t m_addr = 0;
};
