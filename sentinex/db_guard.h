#pragma once
#include <atomic>

// DB Guard — monitor MySQL Pterodactyl untuk:
// 1. Perubahan password admin secara langsung (bypass panel)
// 2. Pembuatan admin baru yang tidak wajar
// 3. Perubahan root_admin flag
void db_guard_run(std::atomic<bool> &running);
