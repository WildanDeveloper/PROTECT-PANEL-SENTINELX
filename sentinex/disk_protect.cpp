#include "disk_protect.h"
#include "config.h"
#include "api.h"
#include "telegram.h"
#include "logger.h"

#include <filesystem>
#include <regex>
#include <set>
#include <map>
#include <mutex>
#include <atomic>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <chrono>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

namespace fs = std::filesystem;

static int inotify_fd = -1;
static std::map<int,std::string> wd_map;
static std::mutex wd_mutex;

// Track file yang sudah di-alert (hindari spam)
static std::set<std::string> alerted;
static std::mutex alerted_mutex;



// ── Ransomware: track modifikasi file per UUID per window ─────
struct RansomTracker {
    std::vector<std::pair<long long /*ts*/, std::string /*file*/>> events;
    bool alerted = false;
};
static std::map<std::string /*uuid*/, RansomTracker> ransom_track;
static std::mutex ransom_mutex;

static long long now_ts() {
    return (long long)std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

// ── Extract UUID dari path ────────────────────────────────────
static std::string extract_uuid(const std::string &path) {
    std::regex re("[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}",
                  std::regex::icase);
    std::smatch m;
    if (std::regex_search(path, m, re)) return m[0].str();
    return "unknown";
}

// ── Cek keyword di nama file ──────────────────────────────────
static std::string match_keyword(const std::string &fname) {
    std::string lower = fname;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    for (auto &kw : DANGER_KEYWORDS)
        if (lower.find(kw) != std::string::npos) return kw;
    return "";
}

// ── Cek ekstensi ransomware ───────────────────────────────────
static bool is_ransom_ext(const std::string &fpath) {
    std::string lower = fpath;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    for (auto &ext : RANSOM_EXTENSIONS)
        if (lower.size() >= ext.size() &&
            lower.compare(lower.size() - ext.size(), ext.size(), ext) == 0)
            return true;
    return false;
}

// ── Cek nama ransom note ──────────────────────────────────────
static bool is_ransom_note(const std::string &fname) {
    std::string lower = fname;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    for (auto &n : RANSOM_NOTE_NAMES)
        if (lower.find(n) != std::string::npos) return true;
    return false;
}

// ── Scan isi file — cek pattern berbahaya ─────────────────────
static std::string scan_content(const std::string &fpath) {
    if (!cfg.scan_file_content) return "";

    long long limit = (long long)cfg.content_scan_limit_kb * 1024LL;
    std::ifstream f(fpath, std::ios::binary);
    if (!f) return "";

    std::string content;
    content.resize(std::min(limit, (long long)1024*1024));
    f.read(&content[0], content.size());
    content.resize(f.gcount());
    if (content.empty()) return "";

    std::string lower = content;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    std::string ext;
    auto dot = fpath.rfind('.');
    if (dot != std::string::npos) {
        ext = fpath.substr(dot + 1);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    }

    // Cek pattern PHP
    if (ext == "php" || ext == "php3" || ext == "php4" || ext == "php5"
        || ext == "phtml" || ext == "php7") {
        for (auto &pat : PHP_DANGEROUS_PATTERNS) {
            std::string pl = pat;
            std::transform(pl.begin(), pl.end(), pl.begin(), ::tolower);
            if (lower.find(pl) != std::string::npos)
                return "[PHP] " + pat;
        }
    }

    // Cek pattern bash/script/binary text
    for (auto &pat : BASH_DANGEROUS_PATTERNS) {
        std::string pl = pat;
        std::transform(pl.begin(), pl.end(), pl.begin(), ::tolower);
        if (lower.find(pl) != std::string::npos)
            return "[SCRIPT] " + pat;
    }

    // Cek PHP pattern di semua file (hacking tool bisa disimpan tanpa ekstensi)
    for (auto &pat : PHP_DANGEROUS_PATTERNS) {
        std::string pl = pat;
        std::transform(pl.begin(), pl.end(), pl.begin(), ::tolower);
        if (lower.find(pl) != std::string::npos)
            return "[PHP-GENERIC] " + pat;
    }

    return "";
}

// ── Buat action_id unik untuk callback tombol Telegram ────────
static std::string make_action_id() {
    // Pakai timestamp + random suffix supaya unik
    static std::atomic<int> counter{0};
    return std::to_string(now_ts()) + "_" + std::to_string(counter++);
}

// ── Handle script berbahaya ───────────────────────────────────
// .bin    → suspend langsung (handle_bin)
// script  → suspend langsung JIKA suspend_on_danger=true
//           kalau false → kirim alert + tombol Y/N ke Telegram
static void handle_dangerous(const std::string &fpath, const std::string &reason,
                              bool from_content = false) {
    {
        std::lock_guard<std::mutex> lk(alerted_mutex);
        if (alerted.count(fpath)) return;
        alerted.insert(fpath);
    }

    std::string uuid = extract_uuid(fpath);
    std::string source = from_content ? "KONTEN" : "NAMA FILE";
    wlog("[DANGER] Script berbahaya! sumber=" + source + " reason='" + reason
         + "' file=" + fpath + " server=" + uuid);

    // Hapus file jika dikonfigurasi
    std::string del_status = "Tidak dihapus";
    if (cfg.delete_dangerous) {
        std::error_code ec;
        fs::remove(fpath, ec);
        del_status = ec ? "GAGAL hapus: " + ec.message() : "✅ Berhasil dihapus";
        wlog("[DEL] " + del_status + " -> " + fpath);
    }

    SrvInfo si = api_get_server_info(uuid);

    if (cfg.suspend_on_danger) {
        // Mode otomatis — suspend langsung
        bool susp = false;
        if (!cfg.api_application.empty()) {
            api_suspend(uuid);
            susp = true;
            wlog("[SUSPEND] Server " + uuid + " di-suspend otomatis (script berbahaya).");
        }
        send_tg_danger_script(fpath, reason, del_status, susp,
            si.uuid, si.name, si.node, si.username, si.email, si.created_at);
    } else {
        // Mode konfirmasi — kirim tombol Suspend/Biarkan ke Telegram
        std::string action_id = make_action_id();
        wlog("[DANGER] Mengirim konfirmasi suspend ke owner. action_id=" + action_id);
        send_tg_danger_confirm(fpath, reason, del_status,
            si.uuid, si.name, si.node, si.username, si.email, si.created_at,
            action_id);
    }
}

// ── Handle disk bomb .bin ─────────────────────────────────────
static void handle_bin(const std::string &fpath, long long bytes) {
    long long mb = bytes / (1024LL * 1024LL);
    long long kb = (bytes % (1024LL * 1024LL)) / 1024LL;
    std::string uuid = extract_uuid(fpath);
    wlog("[ALERT] .bin besar! " + fpath + " (" + std::to_string(mb) + " MB) server=" + uuid);

    std::error_code ec;
    fs::remove(fpath, ec);
    std::string del = ec ? "GAGAL: " + ec.message() : "Berhasil dihapus";
    wlog("[DEL] " + del);

    SrvInfo si = api_get_server_info(uuid);
    bool susp = false;
    if (!cfg.api_application.empty()) { api_suspend(uuid); susp = true; }

    send_tg_disk_bomb(fpath, mb, kb, del, susp,
        si.uuid, si.name, si.node, si.username, si.email, si.created_at);
}

// ── Handle ransomware (ekstensi terenkripsi / ransom note) ────
static void handle_ransomware(const std::string &fpath, const std::string &reason) {
    std::string uuid = extract_uuid(fpath);

    // De-dup per UUID (sekali alert cukup)
    {
        std::lock_guard<std::mutex> lk(ransom_mutex);
        auto &t = ransom_track[uuid];
        if (t.alerted) return;
        t.alerted = true;
    }

    wlog("[RANSOM] ⚠️ RANSOMWARE TERDETEKSI! reason=" + reason
         + " file=" + fpath + " uuid=" + uuid);

    SrvInfo si = api_get_server_info(uuid);
    bool susp = false;
    if (!cfg.api_application.empty()) {
        api_suspend(uuid);
        susp = true;
        wlog("[SUSPEND] Server " + uuid + " di-suspend karena ransomware.");
    }

    send_tg_ransomware(fpath, reason, susp,
        si.uuid, si.name, si.node, si.username, si.email, si.created_at);
}

// ── Handle rapid file modification (ransomware behavior) ──────
static void track_modification(const std::string &fpath) {
    if (!cfg.ransomware_detection) return;
    std::string uuid = extract_uuid(fpath);
    if (uuid == "unknown") return;

    long long now = now_ts();
    std::lock_guard<std::mutex> lk(ransom_mutex);
    auto &t = ransom_track[uuid];
    if (t.alerted) return;

    // Tambah event, buang yang sudah expired
    t.events.push_back({now, fpath});
    t.events.erase(
        std::remove_if(t.events.begin(), t.events.end(),
            [&](auto &e) { return (now - e.first) > cfg.ransomware_window_sec; }),
        t.events.end());

    if ((int)t.events.size() >= cfg.ransomware_mod_threshold) {
        t.alerted = true;
        wlog("[RANSOM] Rapid modification: " + std::to_string(t.events.size())
             + " file dalam " + std::to_string(cfg.ransomware_window_sec)
             + "s di UUID=" + uuid);

        SrvInfo si = api_get_server_info(uuid);
        bool susp = false;
        if (!cfg.api_application.empty()) {
            api_suspend(uuid);
            susp = true;
        }
        send_tg_ransomware(
            fpath,
            "RAPID FILE MODIFICATION: " + std::to_string(t.events.size())
                + " file dalam " + std::to_string(cfg.ransomware_window_sec) + " detik",
            susp, si.uuid, si.name, si.node, si.username, si.email, si.created_at);
    }
}

// ── Cek satu file ─────────────────────────────────────────────
static void check_file(const std::string &fpath) {
    std::string fname = fs::path(fpath).filename().string();
    std::string fname_lower = fname;
    std::transform(fname_lower.begin(), fname_lower.end(), fname_lower.begin(), ::tolower);

    // 0. Cek ransom note (nama file)
    if (is_ransom_note(fname_lower)) {
        handle_ransomware(fpath, "RANSOM NOTE: " + fname);
        return;
    }

    // 1. Cek ekstensi ransomware
    if (is_ransom_ext(fpath)) {
        handle_ransomware(fpath, "RANSOM EXT: " + fs::path(fpath).extension().string());
        return;
    }

    // 2. Cek keyword di nama file
    std::string kw = match_keyword(fname);
    if (!kw.empty()) {
        handle_dangerous(fpath, kw, false);
        return;
    }

    // 3. Scan isi file
    std::string content_hit = scan_content(fpath);
    if (!content_hit.empty()) {
        handle_dangerous(fpath, content_hit, true);
        return;
    }

    // 4. Cek semua file > batas ukuran (bukan hanya .bin)
    {
        struct stat st;
        if (::stat(fpath.c_str(), &st) == 0) {
            long long bytes = (long long)st.st_size;
            if (bytes > cfg.max_size_mb * 1024LL * 1024LL)
                handle_bin(fpath, bytes);
        }
    }

    // 5. Track modifikasi (untuk deteksi ransomware behavior)
    track_modification(fpath);
}

// ── Full scan rekursif ────────────────────────────────────────
void disk_protect_scan() {
    try {
        auto it = fs::recursive_directory_iterator(
            cfg.volumes_path, fs::directory_options::skip_permission_denied);
        for (auto &e : it) {
            if (e.is_directory() && e.path().filename() == "node_modules") {
                it.disable_recursion_pending();
                continue;
            }
            if (e.is_regular_file()) check_file(e.path().string());
        }
    } catch (std::exception &ex) { wlog("[SCAN] Error: " + std::string(ex.what())); }
}

// ── inotify watch helpers ─────────────────────────────────────
static void add_watch(const std::string &path) {
    int wd = inotify_add_watch(inotify_fd, path.c_str(),
                               IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE);
    if (wd < 0) return;
    std::lock_guard<std::mutex> lk(wd_mutex);
    wd_map[wd] = path;
}

static void watch_recursive(const std::string &root) {
    add_watch(root);
    try {
        auto it = fs::recursive_directory_iterator(
            root, fs::directory_options::skip_permission_denied);
        for (auto &e : it) {
            if (e.is_directory()) {
                if (e.path().filename() == "node_modules") {
                    it.disable_recursion_pending(); continue;
                }
                add_watch(e.path().string());
            }
        }
    } catch (...) {}
}

// ── Main inotify loop ─────────────────────────────────────────
void disk_protect_start(std::atomic<bool> &running) {
    inotify_fd = inotify_init();
    if (inotify_fd < 0) { wlog("[DISK] inotify_init gagal!"); return; }

    wlog("[DISK] Memasang inotify watches rekursif...");
    watch_recursive(cfg.volumes_path);
    {
        std::lock_guard<std::mutex> lk(wd_mutex);
        wlog("[DISK] Watch aktif: " + std::to_string(wd_map.size()) + " direktori");
    }

    // inotify loop — zero delay, langsung proses setiap event
    // Pakai inotify_fd dengan O_NONBLOCK + poll loop ketat
    // Setiap file berubah → check_file dipanggil SAAT ITU JUGA
    fcntl(inotify_fd, F_SETFL, O_NONBLOCK);
    char buf[65536] __attribute__((aligned(8)));
    while (running) {
        // Tunggu event dengan timeout 200ms — cukup responsif,
        // tapi tetap bisa cek flag running agar daemon bisa stop
        fd_set fds; FD_ZERO(&fds); FD_SET(inotify_fd, &fds);
        struct timeval tv = {0, 200000}; // 200ms max wait
        if (select(inotify_fd + 1, &fds, nullptr, nullptr, &tv) <= 0) continue;
        int len = read(inotify_fd, buf, sizeof(buf));
        if (len <= 0) continue;
        for (int i = 0; i < len;) {
            auto *ev = (struct inotify_event *)&buf[i];
            if (ev->len > 0) {
                std::string fn(ev->name), dp;
                {
                    std::lock_guard<std::mutex> lk(wd_mutex);
                    auto it = wd_map.find(ev->wd);
                    if (it != wd_map.end()) dp = it->second;
                }
                std::string fp = dp + "/" + fn;
                if ((ev->mask & IN_ISDIR) && (ev->mask & (IN_CREATE | IN_MOVED_TO))) {
                    if (fn != "node_modules") watch_recursive(fp);
                }
                if (!(ev->mask & IN_ISDIR))
                    check_file(fp);
            }
            i += sizeof(struct inotify_event) + ev->len;
        }
    }
    close(inotify_fd);
}
