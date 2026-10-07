// Sleep. The device spends almost all its time in deep sleep: it wakes on a timer to check for messages, or at
// once when a button is pressed, does what is needed and sleeps again. Deep sleep restarts the program, so
// anything that must be remembered lives in settings or in RTC memory.
#ifndef PEBBLE_POWER_H
#define PEBBLE_POWER_H
#include "esp_sleep.h"
#include "esp_timer.h"
#include "config.h"

enum { WAKE_COLD, WAKE_TIMER, WAKE_BUTTON };   // power-on or restart / time to check / a button
int wakeReason = WAKE_COLD;
unsigned long awakeUntil = 0;       // stay awake at least until this millis()
volatile unsigned long loopAt = 0;  // when loop() last ran, for the stall guard
volatile bool stallAllowed = false; // a long job (a firmware download) is running on purpose

// Counts kept through sleep, for the "status" report.
RTC_DATA_ATTR unsigned long statChecks = 0, statCheckMs = 0, statMaxCheckMs = 0;   // timer wakes: how many, total and longest time awake
RTC_DATA_ATTR unsigned long statButtonWakes = 0, statStalls = 0;
RTC_DATA_ATTR unsigned long statStartMs = 0, statJoinMs = 0, statPollMs = 0;   // totals over timer wakes: start-up, fast join, inbox read

void powerSleep(unsigned long ms);

// If loop() stops running for STALL_MS (a request that never returns), sleep anyway: a hang must not drain the battery.
static void stallGuard(void*) {
  if (stallAllowed || millis() - loopAt < STALL_MS) return;
  statStalls++;
  powerSleep(CHECK_INTERVAL_MS);
}

void powerBegin() {
  esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
  wakeReason = cause == ESP_SLEEP_WAKEUP_TIMER ? WAKE_TIMER : cause == ESP_SLEEP_WAKEUP_GPIO ? WAKE_BUTTON : WAKE_COLD;
  if (wakeReason == WAKE_BUTTON) statButtonWakes++;
  if (wakeReason == WAKE_COLD) awakeUntil = COLD_AWAKE_MS;   // after power-on or a restart, give the user time to press something
  loopAt = millis();
  esp_timer_create_args_t args = {};
  args.callback = stallGuard;
  args.name = "stall";
  esp_timer_handle_t timer;
  esp_timer_create(&args, &timer);
  esp_timer_start_periodic(timer, 5000000);
}

void powerTouch() { loopAt = millis(); }                                // call every loop()
void powerHold(unsigned long ms) {                                      // stay awake at least this much longer
  if ((long)(millis() + ms - awakeUntil) > 0) awakeUntil = millis() + ms;
}
void powerRelease() { awakeUntil = millis(); }                          // drop any hold
bool powerHeld() { return (long)(awakeUntil - millis()) > 0; }

// Deep sleep for `ms` (0 = until a button is pressed). Either real button wakes the device at once.
void powerSleep(unsigned long ms) {
  if (wakeReason == WAKE_TIMER) {   // an ordinary check: this is the number that decides battery life
    unsigned long awake = millis() + WAKE_LEAD_MS;
    statChecks++;
    statCheckMs += awake;
    if (awake > statMaxCheckMs) statMaxCheckMs = awake;
  }
  if (Serial) Serial.flush();   // only when a computer is listening
  esp_deep_sleep_enable_gpio_wakeup((1ULL << BTN1) | (1ULL << BTN2), ESP_GPIO_WAKEUP_GPIO_LOW);
  if (ms) esp_sleep_enable_timer_wakeup(ms * 1000ULL);
  esp_deep_sleep_start();
}

#endif
