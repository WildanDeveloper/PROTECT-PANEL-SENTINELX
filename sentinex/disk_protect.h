#pragma once
#include <atomic>

// Jalankan inotify real-time loop (blocking, taruh di thread sendiri)
void disk_protect_start(std::atomic<bool> &running);

// Full scan rekursif sekali jalan
void disk_protect_scan();
