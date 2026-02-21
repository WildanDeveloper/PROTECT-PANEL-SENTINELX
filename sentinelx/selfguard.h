#pragma once
#include <atomic>

void selfguard_init();                      // lock binary + tulis PID
void selfguard_watchdog(std::atomic<bool> &running); // watchdog loop
