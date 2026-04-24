#include "gt911_rev4.h"
#include "rev4_board.h"

namespace {
constexpr uint16_t GT911_REG_STATUS = 0x814E;
constexpr uint16_t GT911_REG_POINTS = 0x8150;
constexpr uint16_t GT911_REG_CONFIG = 0x8047;
constexpr uint8_t GT911_ADDR_LOW = 0x14;
constexpr uint8_t GT911_ADDR_HIGH = 0x5D;
constexpr uint8_t GT911_CONFIG_LEN = 186;
}

bool GT911Rev4::probeAddress(TwoWire &wire, uint8_t addr) {
  wire.beginTransmission(addr);
  return wire.endTransmission() == 0;
}

bool GT911Rev4::scanForAddress(TwoWire &wire, uint8_t &addr_out) {
  if (probeAddress(wire, GT911_ADDR_HIGH)) {
    addr_out = GT911_ADDR_HIGH;
    return true;
  }
  if (probeAddress(wire, GT911_ADDR_LOW)) {
    addr_out = GT911_ADDR_LOW;
    return true;
  }
  return false;
}

bool GT911Rev4::begin(TwoWire &wire, uint8_t addr) {
  m_wire = &wire;
  m_addr = addr;
  return probeAddress(wire, addr);
}

bool GT911Rev4::readReg(uint16_t reg, uint8_t *buf, size_t len) {
  if ((m_wire == nullptr) || (m_addr == 0)) return false;
  if (!Rev4Board::I2cLock()) return false;
  m_wire->beginTransmission(m_addr);
  m_wire->write(static_cast<uint8_t>(reg >> 8));
  m_wire->write(static_cast<uint8_t>(reg & 0xFF));
  if (m_wire->endTransmission(false) != 0) { Rev4Board::I2cUnlock(); return false; }
  size_t got = m_wire->requestFrom(static_cast<int>(m_addr), static_cast<int>(len));
  if (got != len) { Rev4Board::I2cUnlock(); return false; }
  for (size_t i = 0; i < len; ++i) {
    buf[i] = static_cast<uint8_t>(m_wire->read());
  }
  Rev4Board::I2cUnlock();
  return true;
}

bool GT911Rev4::writeReg(uint16_t reg, const uint8_t *buf, size_t len) {
  if ((m_wire == nullptr) || (m_addr == 0)) return false;
  if (!Rev4Board::I2cLock()) return false;
  m_wire->beginTransmission(m_addr);
  m_wire->write(static_cast<uint8_t>(reg >> 8));
  m_wire->write(static_cast<uint8_t>(reg & 0xFF));
  m_wire->write(buf, len);
  bool ok = m_wire->endTransmission() == 0;
  Rev4Board::I2cUnlock();
  return ok;
}

bool GT911Rev4::writeByte(uint16_t reg, uint8_t value) {
  return writeReg(reg, &value, 1);
}

bool GT911Rev4::applyRev4Config() {
  uint8_t cfg[GT911_CONFIG_LEN] = {0};
  for (uint16_t offset = 0; offset < 184; offset += 28) {
    const uint8_t chunk = min<uint16_t>(28, 184 - offset);
    if (!readReg(GT911_REG_CONFIG + offset, &cfg[offset], chunk)) {
      return false;
    }
  }

  cfg[0] = static_cast<uint8_t>(cfg[0] + 1);
  if (cfg[0] == 0) cfg[0] = 1;
  cfg[1] = 0xE0; cfg[2] = 0x01; // 480
  cfg[3] = 0xE0; cfg[4] = 0x01; // 480
  cfg[5] = 5;                   // max touches
  if (cfg[6] == 0) cfg[6] = 0x0D; // Module_Switch1: X2Y | INT rising if blank

  uint8_t checksum = 0;
  for (int i = 0; i < 184; ++i) checksum = static_cast<uint8_t>(checksum + cfg[i]);
  checksum = static_cast<uint8_t>((~checksum) + 1);
  cfg[184] = checksum;
  cfg[185] = 0x01;

  for (uint16_t offset = 0; offset < GT911_CONFIG_LEN; offset += 28) {
    const uint8_t chunk = min<uint16_t>(28, GT911_CONFIG_LEN - offset);
    if (!writeReg(GT911_REG_CONFIG + offset, &cfg[offset], chunk)) {
      return false;
    }
  }

  delay(100);
  (void)writeByte(GT911_REG_STATUS, 0);
  return true;
}


bool GT911Rev4::readProductId(char out[5]) {
  uint8_t buf[4] = {0};
  if (!readReg(0x8140, buf, sizeof(buf))) {
    return false;
  }
  out[0] = static_cast<char>(buf[0]);
  out[1] = static_cast<char>(buf[1]);
  out[2] = static_cast<char>(buf[2]);
  out[3] = static_cast<char>(buf[3]);
  out[4] = '\0';
  return true;
}

bool GT911Rev4::readStatus(uint8_t &status) {
  return readReg(GT911_REG_STATUS, &status, 1);
}

uint8_t GT911Rev4::readPoints(int16_t *x, int16_t *y, uint8_t max_points) {
  uint8_t status = 0;
  if (!readReg(GT911_REG_STATUS, &status, 1)) {
    return 0;
  }

  const uint8_t touched = static_cast<uint8_t>(status & 0x0F);
  const bool ready = (status & 0x80) != 0;
  if (!ready || touched == 0) {
    if (ready) {
      writeByte(GT911_REG_STATUS, 0);
    }
    return 0;
  }

  const uint8_t count = min<uint8_t>(touched, max_points);
  uint8_t point_buf[8 * 5] = {0};
  if (!readReg(GT911_REG_POINTS, point_buf, static_cast<size_t>(count) * 8U)) {
    writeByte(GT911_REG_STATUS, 0);
    return 0;
  }

  for (uint8_t i = 0; i < count; ++i) {
    const uint8_t *p = &point_buf[i * 8U];
    x[i] = static_cast<int16_t>(p[0] | (p[1] << 8));
    y[i] = static_cast<int16_t>(p[2] | (p[3] << 8));
  }

  writeByte(GT911_REG_STATUS, 0);
  return count;
}
