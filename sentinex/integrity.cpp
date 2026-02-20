/*
 * ============================================================
 *  SentinelX — Panel File Integrity Monitor
 *
 *  Proteksi:
 *  - Bangun hash SHA256 semua file PHP panel saat startup
 *  - inotify real-time: file baru / file dimodif → alert
 *  - File PHP baru di panel = webshell kandidat → hapus + alert
 *  - File core panel dimodif → alert + tunjukkan path
 * ============================================================
 */

#include "integrity.h"
#include "config.h"
#include "telegram.h"
#include "logger.h"

#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <mutex>
#include <sstream>
#include <algorithm>
#include <atomic>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <openssl/sha.h>
static bool is_critical_file(const std::string &fpath); // forward decl

namespace fs = std::filesystem;

// ── Hash map: path → sha256 hex ──────────────────────────────
static std::map<std::string, std::string> baseline;
static std::mutex baseline_mutex;

// ── Direktori yang WAJIB dimonitor di panel ───────────────────
static const std::vector<std::string> WATCH_DIRS = {
    "app", "public", "routes", "resources/views", "bootstrap"
};

// ── Direktori yang BOLEH diabaikan ───────────────────────────
static const std::vector<std::string> SKIP_DIRS = {
    "vendor", "node_modules", "storage", ".git"
};

static bool should_skip(const std::string &path) {
    for (auto &s : SKIP_DIRS)
        if (path.find("/" + s + "/") != std::string::npos
            || path.find("/" + s) == path.size() - s.size() - 1)
            return true;
    return false;
}

// ── Hitung SHA256 file (hex string) ──────────────────────────
static std::string sha256_file(const std::string &fpath) {
    std::ifstream f(fpath, std::ios::binary);
    if (!f) return "";

    SHA256_CTX ctx;
    SHA256_Init(&ctx);
    char buf[8192];
    while (f.read(buf, sizeof(buf)) || f.gcount() > 0)
        SHA256_Update(&ctx, buf, f.gcount());

    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256_Final(hash, &ctx);

    std::ostringstream oss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++)
        oss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
    return oss.str();
}

// ── Bangun baseline hash semua file PHP panel ─────────────────
void integrity_scan_baseline() {
    if (!cfg.integrity_enabled) return;
    if (!fs::exists(cfg.panel_path)) {
        wlog("[INTEGRITY] Panel path tidak ada: " + cfg.panel_path);
        return;
    }

    std::lock_guard<std::mutex> lk(baseline_mutex);
    baseline.clear();
    int count = 0;

    try {
        for (auto &e : fs::recursive_directory_iterator(
                cfg.panel_path, fs::directory_options::skip_permission_denied)) {
            if (!e.is_regular_file()) continue;
            std::string p = e.path().string();
            if (should_skip(p)) continue;

            std::string ext = e.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (ext != ".php" && ext != ".php7" && ext != ".phtml") {
                // Tetap hash file kritis non-PHP
                if (!is_critical_file(p)) continue;
            }

            std::string h = sha256_file(p);
            if (!h.empty()) { baseline[p] = h; count++; }
        }
    } catch (std::exception &ex) {
        wlog("[INTEGRITY] Error saat baseline: " + std::string(ex.what()));
    }

    wlog("[INTEGRITY] Baseline selesai. " + std::to_string(count) + " file PHP di-hash.");
}

// ── Handle file PHP baru (webshell kandidat) ──────────────────
static void handle_new_php(const std::string &fpath) {
    wlog("[INTEGRITY] ⚠️ File PHP BARU terdeteksi: " + fpath);

    std::string del_status = "Tidak dihapus";
    if (cfg.integrity_delete_new_php) {
        std::error_code ec;
        fs::remove(fpath, ec);
        del_status = ec ? "GAGAL: " + ec.message() : "Berhasil dihapus";
        wlog("[INTEGRITY] " + del_status + " -> " + fpath);
    }

    send_tg_integrity_new(fpath, del_status);
}

// ── Handle file PHP core yang dimodifikasi ────────────────────
static void handle_modified_php(const std::string &fpath,
                                  const std::string &old_hash,
                                  const std::string &new_hash) {
    wlog("[INTEGRITY] ⚠️ File PHP DIMODIFIKASI: " + fpath);
    wlog("[INTEGRITY]   Hash lama: " + old_hash);
    wlog("[INTEGRITY]   Hash baru: " + new_hash);

    send_tg_integrity_modified(fpath, old_hash, new_hash);

    // Update baseline dengan hash baru agar tidak spam
    std::lock_guard<std::mutex> lk(baseline_mutex);
    baseline[fpath] = new_hash;
}

// ── File kritis yang harus dimonitor (bukan hanya .php) ──────
static bool is_critical_file(const std::string &fpath) {
    std::string fname = fs::path(fpath).filename().string();
    // .env Pterodactyl — berisi DB credentials
    if (fname == ".env") return true;
    // Config DB panel
    if (fpath.find("config/database.php") != std::string::npos) return true;
    if (fpath.find("config/app.php") != std::string::npos) return true;
    return false;
}

// ── Cek satu file setelah event inotify ──────────────────────
static void check_panel_file(const std::string &fpath) {
    if (should_skip(fpath)) return;

    // Cek file kritis non-PHP (.env, config)
    if (is_critical_file(fpath)) {
        wlog("[INTEGRITY] ⚠️ File KRITIS dimodifikasi: " + fpath);
        std::string new_hash = sha256_file(fpath);
        std::lock_guard<std::mutex> lk(baseline_mutex);
        auto it = baseline.find(fpath);
        if (it == baseline.end()) {
            baseline[fpath] = new_hash;
        } else if (it->second != new_hash) {
            send_tg_integrity_modified(fpath, it->second, new_hash);
            it->second = new_hash;
        }
        return;
    }

    std::string ext = fs::path(fpath).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    if (ext != ".php" && ext != ".php7" && ext != ".phtml") return;

    if (!fs::exists(fpath)) return;

    std::string new_hash = sha256_file(fpath);
    if (new_hash.empty()) return;

    std::lock_guard<std::mutex> lk(baseline_mutex);
    auto it = baseline.find(fpath);

    if (it == baseline.end()) {
        // File baru — tidak ada di baseline
        baseline[fpath] = new_hash;
        handle_new_php(fpath);
    } else if (it->second != new_hash) {
        // File lama tapi hash berubah
        std::string old = it->second;
        handle_modified_php(fpath, old, new_hash);
    }
}

// ── inotify helpers ───────────────────────────────────────────
static int int_inotify_fd = -1;
static std::map<int, std::string> int_wd_map;
static std::mutex int_wd_mutex;

static void int_add_watch(const std::string &path) {
    int wd = inotify_add_watch(int_inotify_fd, path.c_str(),
                               IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE | IN_MODIFY);
    if (wd < 0) return;
    std::lock_guard<std::mutex> lk(int_wd_mutex);
    int_wd_map[wd] = path;
}

static void int_watch_recursive(const std::string &root) {
    if (should_skip(root)) return;
    int_add_watch(root);
    try {
        for (auto &e : fs::recursive_directory_iterator(
                root, fs::directory_options::skip_permission_denied)) {
            if (e.is_directory() && !should_skip(e.path().string()))
                int_add_watch(e.path().string());
        }
    } catch (...) {}
}

// ── Main integrity monitoring loop ───────────────────────────
void integrity_start(std::atomic<bool> &running) {
    if (!cfg.integrity_enabled) return;
    if (!fs::exists(cfg.panel_path)) {
        wlog("[INTEGRITY] Path panel tidak ditemukan, integrity monitor tidak aktif.");
        return;
    }

    int_inotify_fd = inotify_init();
    if (int_inotify_fd < 0) { wlog("[INTEGRITY] inotify_init gagal!"); return; }

    int_watch_recursive(cfg.panel_path);
    {
        std::lock_guard<std::mutex> lk(int_wd_mutex);
        wlog("[INTEGRITY] Watch aktif: " + std::to_string(int_wd_map.size())
             + " direktori panel. Baseline: " + std::to_string(baseline.size()) + " file.");
    }

    fcntl(int_inotify_fd, F_SETFL, O_NONBLOCK);
    char buf[65536] __attribute__((aligned(8)));
    while (running) {
        fd_set fds; FD_ZERO(&fds); FD_SET(int_inotify_fd, &fds);
        struct timeval tv = {0, 200000}; // 200ms max wait — instant response
        if (select(int_inotify_fd + 1, &fds, nullptr, nullptr, &tv) <= 0) continue;
        int len = read(int_inotify_fd, buf, sizeof(buf));
        if (len <= 0) continue;
        for (int i = 0; i < len;) {
            auto *ev = (struct inotify_event *)&buf[i];
            if (ev->len > 0) {
                std::string fn(ev->name), dp;
                {
                    std::lock_guard<std::mutex> lk(int_wd_mutex);
                    auto it = int_wd_map.find(ev->wd);
                    if (it != int_wd_map.end()) dp = it->second;
                }
                if (!dp.empty()) {
                    std::string fp = dp + "/" + fn;
                    if ((ev->mask & IN_ISDIR) && (ev->mask & (IN_CREATE | IN_MOVED_TO)))
                        int_watch_recursive(fp);
                    if (!(ev->mask & IN_ISDIR))
                        check_panel_file(fp);
                }
            }
            i += sizeof(struct inotify_event) + ev->len;
        }
    }
    close(int_inotify_fd);
}
