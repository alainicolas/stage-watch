#pragma once
// Calibration de la jauge de batterie AXP2101 pour la batterie 470 mAh de la T-Watch S3.
// Données et procédure reprises à l'identique de LilyGO (LilyGoLib/src/LilyGoWatchS3.cpp, BATTERY_PARAMS_470mAh)
// et de XPowersLib (writeGaugeData/compareGaugeData, version récente). Écrit une seule fois, mémorisé en flash.
#include <XPowersLib.h>
#include <Preferences.h>

static const uint8_t BATTERY_PARAMS_470mAh[128] = {
    0x01, 0xF5, 0x40, 0x00, 0x1B, 0x1E, 0x28, 0x0F, 0x0C, 0x1E, 0x32, 0x2, 0x14, 0x05, 0x0A, 0x04,
    0x74, 0xFF, 0xB0, 0x0D, 0x43, 0x10, 0x01, 0xFC, 0x90, 0x01, 0xEA, 0x06, 0xE0, 0x06, 0x01, 0x05,
    0xF7, 0x0A, 0xED, 0x0F, 0xE5, 0x0F, 0x85, 0x0A, 0x33, 0x0F, 0xF6, 0x0E, 0xEF, 0x04, 0xE1, 0x04,
    0xD3, 0x09, 0xC3, 0x0E, 0xAD, 0x0E, 0xA7, 0x09, 0x9C, 0x0E, 0x82, 0x0E, 0x7B, 0x04, 0x6C, 0x04,
    0x69, 0x09, 0x5F, 0x0E, 0x06, 0x0D, 0x55, 0x06, 0x90, 0x5E, 0x2F, 0x2A, 0x20, 0x0F, 0x13, 0x09,
    0xC5, 0x98, 0x7E, 0x66, 0x4E, 0x44, 0x38, 0x1A, 0x12, 0x0A, 0xF6, 0x00, 0x00, 0xF6, 0x00, 0xF6,
    0x00, 0xFB, 0x00, 0x00, 0xFB, 0x00, 0x00, 0xFB, 0x00, 0x00, 0xF6, 0x00, 0x00, 0xF6, 0x00, 0xF6,
    0x00, 0xFB, 0x00, 0x00, 0xFB, 0x00, 0x00, 0xFB, 0x00, 0x00, 0xF6, 0x00, 0x00, 0xF6, 0x00, 0xF6
};

#define AXP_RESET_FUEL_GAUGE 0x17
#define AXP_BAT_PARAMS       0xA1
#define AXP_FUEL_GAUGE_CTRL  0xA2

// Relit les paramètres et réactive la jauge (compareGaugeData de XPowersLib)
static bool axpCompareGauge(XPowersAXP2101& pmu, const uint8_t* data) {
  uint8_t buf[128];
  pmu.clrRegisterBit(AXP_FUEL_GAUGE_CTRL, 0);
  pmu.setRegisterBit(AXP_FUEL_GAUGE_CTRL, 0);
  for (int i = 0; i < 128; i++) buf[i] = pmu.readRegister(AXP_BAT_PARAMS);
  pmu.clrRegisterBit(AXP_FUEL_GAUGE_CTRL, 0);
  pmu.setRegisterBit(AXP_FUEL_GAUGE_CTRL, 4);
  pmu.setRegisterBit(AXP_RESET_FUEL_GAUGE, 2);
  pmu.clrRegisterBit(AXP_RESET_FUEL_GAUGE, 2);
  return memcmp(data, buf, 128) == 0;
}

// writeGaugeData de XPowersLib
static bool axpWriteGauge(XPowersAXP2101& pmu, const uint8_t* data) {
  pmu.setRegisterBit(AXP_RESET_FUEL_GAUGE, 2);
  pmu.clrRegisterBit(AXP_RESET_FUEL_GAUGE, 2);
  pmu.clrRegisterBit(AXP_FUEL_GAUGE_CTRL, 0);
  pmu.setRegisterBit(AXP_FUEL_GAUGE_CTRL, 0);
  for (int i = 0; i < 128; i++) pmu.writeRegister(AXP_BAT_PARAMS, data[i]);
  pmu.clrRegisterBit(AXP_FUEL_GAUGE_CTRL, 0);
  pmu.setRegisterBit(AXP_FUEL_GAUGE_CTRL, 0);
  return axpCompareGauge(pmu, data);
}

// Une seule fois par montre (comme LilyGO : clé "calibration" mémorisée)
static bool calibrateBatteryOnce(XPowersAXP2101& pmu, Preferences& prefs) {
  if (prefs.getBool("batcal", false)) return true;
  bool ok = axpWriteGauge(pmu, BATTERY_PARAMS_470mAh);
  if (ok) prefs.putBool("batcal", true);
  return ok;
}
