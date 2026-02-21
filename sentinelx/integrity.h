#pragma once
#include <atomic>

// Monitor panel PHP files — detect webshell baru / file modifikasi
void integrity_start(std::atomic<bool> &running);
void integrity_scan_baseline(); // bangun hash awal
