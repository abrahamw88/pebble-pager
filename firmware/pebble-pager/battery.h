// Battery level. The board cannot report it, so two equal resistors halve the battery's voltage for pin D1.
// It is read at every start, before the radio is on: Wi-Fi pulls the voltage down while it transmits.
#ifndef PEBBLE_BATTERY_H
#define PEBBLE_BATTERY_H
#include "config.h"

RTC_DATA_ATTR int battMv = 0;                 // the last reading, in millivolts at the battery
RTC_DATA_ATTR unsigned long battLowAt = 0;    // clockSeconds() of the last amber blink
RTC_DATA_ATTR unsigned long battReportAt = 0; // ...and of the last report to the phone
bool battReports = false;                     // report to the phone every hour (saved; the "battery on" command)

int batteryRead() {
  uint32_t mv = 0;
  for (int i = 0; i < BATT_SAMPLES; i++) mv += analogReadMilliVolts(BATT);
  battMv = 2 * mv / BATT_SAMPLES;   // the resistors halve it
  return battMv;
}

void batteryBegin() {
  battReports = prefs.getBool("battRep", false);
  batteryRead();
}

bool batteryFitted() { return battMv >= BATT_MISSING_MV; }   // false on a bare board: the pin is not wired

// A resting lithium cell's voltage at each 10%, empty to full. Under load, or while charging, this is only a guide.
int batteryPercent() {
  static const int mv[11] = {3300, 3650, 3710, 3735, 3760, 3790, 3820, 3860, 3920, 4030, 4200};
  if (battMv <= mv[0]) return 0;
  for (int i = 1; i < 11; i++)
    if (battMv < mv[i]) return 10 * (i - 1) + 10 * (battMv - mv[i - 1]) / (mv[i] - mv[i - 1]);
  return 100;
}

String batteryText() {
  if (!batteryFitted()) return "battery not connected (sensor reads " + String(battMv / 1000.0, 2) + " V)";
  return "battery " + String(battMv / 1000.0, 2) + " V, " + String(batteryPercent()) + "%";
}

void batteryReports(bool on) {
  battReports = on;
  prefs.putBool("battRep", on);
  battReportAt = clockSeconds();
}

// True once every BATT_LOW_BLINK_S while the battery is low: time for the amber blink.
bool batteryLowDue() {
  if (!batteryFitted() || batteryPercent() >= BATT_LOW_PERCENT) return false;
  if (battLowAt && clockSeconds() - battLowAt < BATT_LOW_BLINK_S) return false;
  battLowAt = clockSeconds() ? clockSeconds() : 1;   // (not `| 1`: that can put it a second ahead of the clock)
  return true;
}

// True once every BATT_REPORT_S while reports are switched on: time to tell the phone.
bool batteryReportDue() {
  if (!battReports || clockSeconds() - battReportAt < BATT_REPORT_S) return false;
  battReportAt = clockSeconds();
  return true;
}

#endif
