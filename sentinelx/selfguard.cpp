/*
 * ============================================================
 *  SentinelX — Self Protection
 *
 *  Proteksi:
 *  [1] chattr +i pada binary → tidak bisa dihapus/overwrite
 *  [2] PID file → kalau proses mati, systemd restart otomatis
 *  [3] Watchdog thread → detect kalau binary diubah → alert
 *  [4] Cek log file integrity setiap X detik
 * ============================================================
 */

#include "selfguard.h"
#include "config.h"
#include "telegram.h"
#include "logger.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <atomic>
#include <thread>
#include <chrono>
#include <cstdlib>
#include <unistd.h>
#include <sys/stat.h>
#include <openssl/sha.h>
#include <iomanip>

namespace fs = std::filesystem;

static const std::string PID_FILE = "/var/run/sentinelx.pid";
static std::string binary_hash_at_start = "";

// ── Hash file helper ─────────────────────────────────────────
static std::string sha256_file(const std::string &fpath) {
    std::ifstream f(fpath, std::ios::binary);
    if (!f) return "";
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr);
    char buf[8192];
    while (f.read(buf, sizeof(buf)) || f.gcount() > 0)
        EVP_DigestUpdate(ctx, buf, f.gcount());
    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hash_len = 0;
    EVP_DigestFinal_ex(ctx, hash, &hash_len);
    EVP_MD_CTX_free(ctx);
    std::ostringstream oss;
    for (unsigned int i = 0; i < hash_len; i++)
        oss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
    return oss.str();
}

// ── Tulis PID file ────────────────────────────────────────────
static void write_pid() {
    std::ofstream f(PID_FILE);
    if (f) f << getpid() << "\n";
}

// ── Lock binary dengan chattr +i ──────────────────────────────
static void lock_binary() {
    if (!cfg.self_protect_enabled) return;
    if (!fs::exists(cfg.self_binary_path)) {
        wlog("[SELFGUARD] Binary tidak ditemukan: " + cfg.self_binary_path
             + " — skip chattr.");
        return;
    }
    int ret = std::system(("chattr +i " + cfg.self_binary_path + " 2>/dev/null").c_str());
    if (ret == 0)
        wlog("[SELFGUARD] Binary dikunci (chattr +i): " + cfg.self_binary_path);
    else
        wlog("[SELFGUARD] chattr +i gagal (mungkin bukan root atau filesystem tidak support).");
}

// ── Init: lock + hash + PID ───────────────────────────────────
void selfguard_init() {
    write_pid();
    lock_binary();

    // Simpan hash binary saat startup
    if (fs::exists(cfg.self_binary_path)) {
        binary_hash_at_start = sha256_file(cfg.self_binary_path);
        wlog("[SELFGUARD] Hash binary startup: " + binary_hash_at_start.substr(0, 16) + "...");
    }
}

// ── Watchdog loop ─────────────────────────────────────────────
void selfguard_watchdog(std::atomic<bool> &running) {
    if (!cfg.self_protect_enabled) return;

    int check_interval = 30; // cek tiap 30 detik
    int elapsed = 0;

    while (running) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        elapsed++;
        if (elapsed < check_interval) continue;
        elapsed = 0;

        // Cek apakah binary masih ada
        if (!fs::exists(cfg.self_binary_path)) {
            wlog("[SELFGUARD] ⚠️ BINARY DIHAPUS! " + cfg.self_binary_path);
            send_tg_selfguard_alert("BINARY DIHAPUS",
                "File SentinelX tidak ditemukan di: " + cfg.self_binary_path +
                "\nKemungkinan attacker mencoba menonaktifkan proteksi!");
            continue;
        }

        // Cek apakah binary dimodifikasi
        if (!binary_hash_at_start.empty()) {
            std::string cur_hash = sha256_file(cfg.self_binary_path);
            if (!cur_hash.empty() && cur_hash != binary_hash_at_start) {
                wlog("[SELFGUARD] ⚠️ BINARY DIMODIFIKASI!");
                wlog("[SELFGUARD]   Hash awal: " + binary_hash_at_start);
                wlog("[SELFGUARD]   Hash kini : " + cur_hash);
                send_tg_selfguard_alert("BINARY DIMODIFIKASI",
                    "Hash SentinelX berubah!\n"
                    "Hash awal: " + binary_hash_at_start.substr(0, 16) + "...\n"
                    "Hash kini: " + cur_hash.substr(0, 16) + "...\n"
                    "Kemungkinan binary telah diganti/ditamper!");
                binary_hash_at_start = cur_hash; // update agar tidak spam
            }
        }

        // Cek apakah config file masih ada
        if (!fs::exists(CONFIG_FILE)) {
            wlog("[SELFGUARD] ⚠️ CONFIG FILE DIHAPUS! " + CONFIG_FILE);
            send_tg_selfguard_alert("CONFIG DIHAPUS",
                "File konfigurasi SentinelX tidak ditemukan: " + CONFIG_FILE);
        }
    }
}
