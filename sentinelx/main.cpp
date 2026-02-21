/*
 * ============================================================
 *  SentinelX — All-in-One Pterodactyl Protection Daemon
 *  v2.0
 *
 *  Proteksi aktif dalam 1 proses:
 *
 *  [1] DISK BOMB PROTECT
 *      - Scan file > batas ukuran → hapus + suspend + notif
 *      - inotify real-time TANPA delay
 *
 *  [2] CONTENT SCANNER (BARU)
 *      - Scan ISI file PHP: webshell, eval, base64_decode, dll.
 *      - Scan ISI script bash: rm -rf, curl|bash, reverse shell, dll.
 *      - Tidak bisa di-bypass dengan rename file
 *
 *  [3] PANEL FILE INTEGRITY (BARU)
 *      - Hash baseline semua file PHP panel saat startup
 *      - inotify real-time: file baru = webshell kandidat → hapus
 *      - File core dimodif → instant alert Telegram
 *
 *  [4] RATE LIMIT PROTECT
 *      - Deteksi spam buat akun/server baru
 *      - Auto-delete akun & server spam + notif
 *
 *  [5] MASS DELETE DETECTION (BARU)
 *      - Snapshot server/user setiap polling cycle
 *      - X server/user hilang dalam 1 cycle → instant alert
 *
 *  [6] SELF PROTECTION (BARU)
 *      - chattr +i binary (tidak bisa dihapus/overwrite)
 *      - Watchdog: monitor hash binary, alert kalau diubah
 *      - PID file untuk systemd restart
 *
 *  Build:
 *    g++ -O2 -std=c++17 \
 *        main.cpp config.cpp http.cpp telegram.cpp api.cpp \
 *        disk_protect.cpp rate_protect.cpp integrity.cpp \
 *        selfguard.cpp tracking.cpp logger.cpp bot.cpp db_guard.cpp \
 *        -o sentinelx -lcurl -lpthread -lssl -lcrypto
 *
 *  Run pertama:  sudo ./sentinelx
 *  Reset setup:  sudo ./sentinelx --reset
 * ============================================================
 */

#include <iostream>
#include <csignal>
#include <atomic>
#include <thread>
#include <chrono>

#include "config.h"
#include "logger.h"
#include "telegram.h"
#include "disk_protect.h"
#include "rate_protect.h"
#include "integrity.h"
#include "selfguard.h"
#include "tracking.h"
#include "bot.h"
#include "db_guard.h"
#include <curl/curl.h>
#include <filesystem>

namespace fs = std::filesystem;

#define RST "\033[0m"
#define GRN "\033[1;32m"
#define YLW "\033[1;33m"
#define CYN "\033[1;36m"
#define RED "\033[1;31m"
#define BLD "\033[1m"
#define BLU "\033[1;34m"

std::atomic<bool> running(true);

void sig_handler(int s) {
    wlog("[INFO] Signal " + std::to_string(s) + " -> daemon berhenti.");
    running = false;
}

void print_banner() {
    std::cout
        << "\n"
        << CYN "  ███████╗███████╗███╗  ██╗████████╗██╗███╗  ██╗███████╗██╗    ██╗  ██╗\n" RST
        << CYN "  ██╔════╝██╔════╝████╗ ██║╚══██╔══╝██║████╗ ██║██╔════╝██║    ╚██╗██╔╝\n" RST
        << CYN "  ███████╗█████╗  ██╔██╗██║   ██║   ██║██╔██╗██║█████╗  ██║     ╚███╔╝ \n" RST
        << CYN "  ╚════██║██╔══╝  ██║╚████║   ██║   ██║██║╚████║██╔══╝  ██║     ██╔██╗ \n" RST
        << CYN "  ███████║███████╗██║ ╚███║   ██║   ██║██║ ╚███║███████╗███████╗██╔╝╚██╗\n" RST
        << CYN "  ╚══════╝╚══════╝╚═╝  ╚══╝   ╚═╝   ╚═╝╚═╝  ╚══╝╚══════╝╚══════╝╚═╝  ╚═╝\n" RST
        << BLD "       Pterodactyl All-in-One Protection Daemon v2.0\n" RST
        << YLW "       Disk Bomb | Content Scan | Integrity | Rate Limit | Mass Delete | Self Guard\n" RST
        << "  " << std::string(70, '-') << "\n\n";
}

int main(int argc, char *argv[]) {
    bool force = false;
    for (int i = 1; i < argc; i++)
        if (std::string(argv[i]) == "--reset" || std::string(argv[i]) == "--setup")
            force = true;

    print_banner();

    if (!force && load_cfg()) {
        std::cout << GRN "  [OK] " RST "Config: " << CONFIG_FILE << "\n";
        std::cout << BLD "  Panel          : " RST << cfg.panel_domain      << "\n";
        std::cout << BLD "  Panel Path     : " RST << cfg.panel_path        << "\n";
        std::cout << BLD "  Volumes        : " RST << cfg.volumes_path      << "\n";
        std::cout << BLD "  Hapus file >  : " RST << cfg.max_size_mb       << " MB\n";
        std::cout << BLD "  Content Scan   : " RST << (cfg.scan_file_content ? "Aktif" : "Nonaktif") << "\n";
        std::cout << BLD "  Integrity      : " RST << (cfg.integrity_enabled ? "Aktif" : "Nonaktif") << "\n";
        std::cout << BLD "  Script bahaya  : " RST << (cfg.delete_dangerous ? "Alert+Hapus" : "Alert saja") << "\n";
        std::cout << BLD "  Rate limit     : " RST
                  << cfg.threshold_accounts << " akun / "
                  << cfg.threshold_servers  << " server dalam "
                  << cfg.window_seconds     << " detik\n";
        std::cout << BLD "  Mass delete    : " RST << "threshold " << cfg.mass_delete_threshold << "\n";
        std::cout << BLD "  Self Guard     : " RST << (cfg.self_protect_enabled ? "Aktif" : "Nonaktif") << "\n";
        std::cout << "\n" YLW "  Gunakan --reset untuk setup ulang\n\n" RST;
    } else {
        setup_wizard();
    }

    signal(SIGINT,  sig_handler);
    signal(SIGTERM, sig_handler);

    if (!fs::exists(cfg.volumes_path)) {
        std::cout << RED "  [!!] " RST "Path volumes tidak ada: " << cfg.volumes_path << "\n";
        return 1;
    }

    log_init(cfg.log_file);
    curl_global_init(CURL_GLOBAL_ALL);

    // ── Self protection: lock binary + tulis PID ──────────────
    selfguard_init();

    // ── Init tracking (baca .env, test MySQL) ─────────────────
    if (tracking_init())
        wlog("[INIT] Tracking MySQL OK.");
    else
        wlog("[INIT] Tracking MySQL tidak aktif (cek .env Pterodactyl).");

    // ── Notif startup ke Telegram ─────────────────────────────
    send_tg_startup();
    wlog("[INIT] SentinelX v2.0 aktif. Semua proteksi berjalan.");

    // ── Thread 1: Full disk scan awal (1x saat startup) ─────────
    // Setelah ini semua deteksi 100% real-time via inotify — tanpa jeda
    std::thread disk_scan_thread([&]() {
        wlog("[DISK] Full scan awal volumes...");
        disk_protect_scan();
        wlog("[DISK] Full scan awal selesai. inotify real-time aktif — tanpa jeda.");
    });

    // ── Thread 2: Panel integrity baseline + monitor ──────────
    std::thread integrity_thread([&]() {
        wlog("[INTEGRITY] Membangun baseline hash panel PHP...");
        integrity_scan_baseline();
        integrity_start(running);
    });

    // ── Thread 3: Rate limit + mass delete polling ────────────
    std::thread rate_thread([&]() {
        wlog("[RATE] Polling loop aktif, interval " + std::to_string(cfg.poll_interval_sec) + "s");
        while (running) {
            rate_protect_run();
            for (int i = 0; i < cfg.poll_interval_sec && running; i++)
                std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    });

    // ── Thread 4: Self-guard watchdog ────────────────────────
    std::thread selfguard_thread([&]() {
        selfguard_watchdog(running);
    });

    // ── Thread 5: DB Guard ───────────────────────────────────
    std::thread db_guard_thread([&]() {
        db_guard_run(running);
    });

    // ── Thread 6: Telegram command bot ───────────────────────
    std::thread bot_thread([&]() {
        bot_run(running);
    });

    // ── Main thread: inotify volumes real-time (blocking) ─────
    disk_protect_start(running);

    disk_scan_thread.join();
    integrity_thread.join();
    rate_thread.join();
    selfguard_thread.join();
    db_guard_thread.join();
    bot_thread.join();

    curl_global_cleanup();
    wlog("[INFO] SentinelX berhenti.");
    return 0;
}

/*
 * =====================================================
 *  INSTALASI SYSTEMD
 * =====================================================
 *
 *  1. Install dependency:
 *     sudo apt install build-essential libcurl4-openssl-dev libssl-dev
 *
 *  2. Build:
 *     g++ -O2 -std=c++17 \
 *         main.cpp config.cpp http.cpp telegram.cpp api.cpp \
 *         disk_protect.cpp rate_protect.cpp integrity.cpp \
 *         selfguard.cpp tracking.cpp logger.cpp bot.cpp db_guard.cpp \
 *         -o sentinelx -lcurl -lpthread -lssl -lcrypto
 *
 *  3. Setup pertama kali:
 *     sudo ./sentinelx
 *
 *  4. Copy ke system:
 *     sudo cp sentinelx /usr/local/bin/sentinelx
 *
 *  5. Lock binary:
 *     sudo chattr +i /usr/local/bin/sentinelx
 *
 *  6. Buat service:
 *     sudo nano /etc/systemd/system/sentinelx.service
 *
 *     [Unit]
 *     Description=SentinelX Pterodactyl Protection Daemon
 *     After=network.target
 *
 *     [Service]
 *     Type=simple
 *     ExecStart=/usr/local/bin/sentinelx
 *     Restart=always
 *     RestartSec=3
 *     User=root
 *
 *     [Install]
 *     WantedBy=multi-user.target
 *
 *  7. Aktifkan:
 *     sudo systemctl daemon-reload
 *     sudo systemctl enable --now sentinelx
 *
 *  8. Cek log:
 *     tail -f /var/log/sentinelx.log
 *
 *  9. Reset config:
 *     sudo sentinelx --reset
 * =====================================================
 */
