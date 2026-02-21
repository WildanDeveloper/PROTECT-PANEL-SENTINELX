#pragma once
#include <atomic>
#include <string>

// Jalankan polling Telegram bot (blocking)
void bot_run(std::atomic<bool> &running);

// Log ancaman ke history (dipanggil dari modul lain, ditampilkan via /threats)
void bot_log_threat(const std::string &msg);
