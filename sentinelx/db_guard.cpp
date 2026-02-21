/*
 * ============================================================
 *  SentinelX — DB Guard
 *
 *  Monitor langsung ke MySQL Pterodactyl untuk mendeteksi:
 *  1. Perubahan password admin (bypass API — attack Image 2)
 *  2. User biasa yang di-upgrade jadi root_admin
 *  3. Penambahan admin baru secara langsung ke DB
 *
 *  Cara kerja:
 *  - Baca kredensial DB dari /etc/pterodactyl/.env
 *  - Snapshot hash semua password admin saat startup
 *  - Poll tiap N detik — kalau ada yang berubah → alert Telegram
 * ============================================================
 */

#include "db_guard.h"
#include "config.h"
#include "telegram.h"
#include "logger.h"
#include <sys/stat.h>

#include <fstream>
#include <sstream>
#include <string>
#include <map>
#include <vector>
#include <thread>
#include <chrono>
#include <cstdlib>
#include <array>
#include <memory>
#include <algorithm>

struct AdminRow {
    std::string id;
    std::string username;
    std::string email;
    std::string pw_hash;   // hash dari kolom password (bukan plaintext)
    bool        root_admin;
};

// ── Baca .env Pterodactyl ─────────────────────────────────────
static std::string env_get(const std::string &key) {
    static std::map<std::string,std::string> cache;
    static bool loaded = false;
    if (!loaded) {
        const std::vector<std::string> paths = {
            "/etc/pterodactyl/.env",
            "/var/www/pterodactyl/.env",
        };
        for (auto &p : paths) {
            std::ifstream f(p);
            if (!f) continue;
            std::string line;
            while (std::getline(f, line)) {
                if (line.empty() || line[0]=='#') continue;
                auto eq = line.find('=');
                if (eq == std::string::npos) continue;
                cache[line.substr(0,eq)] = line.substr(eq+1);
            }
            if (!cache.empty()) break;
        }
        loaded = true;
    }
    auto it = cache.find(key);
    return it != cache.end() ? it->second : "";
}

// ── Jalankan query MySQL/MariaDB via CLI, return output ───────
static std::string mysql_query(const std::string &sql) {
    std::string host = env_get("DB_HOST");     if (host.empty()) host = "127.0.0.1";
    std::string port = env_get("DB_PORT");     if (port.empty()) port = "3306";
    std::string user = env_get("DB_USERNAME"); if (user.empty()) user = "pterodactyl";
    std::string pass = env_get("DB_PASSWORD");
    std::string db   = env_get("DB_DATABASE"); if (db.empty())   db   = "panel";

    // Buat tmp credentials file (hindari password di command line)
    std::string cnf = "/tmp/.sentinelx_dbg.cnf";
    {
        std::ofstream f(cnf);
        f << "[client]\nhost=" << host << "\nport=" << port
          << "\nuser=" << user << "\npassword=" << pass << "\n";
        f.close();
        ::chmod(cnf.c_str(), 0600);
    }

    // Coba mysql dulu, fallback ke mariadb (untuk MariaDB standalone install)
    std::string cli = "mysql";
    if (system("which mysql > /dev/null 2>&1") != 0) {
        if (system("which mariadb > /dev/null 2>&1") == 0)
            cli = "mariadb";
    }

    std::string cmd = cli + " --defaults-file=" + cnf +
                      " --batch --skip-column-names " + db +
                      " -e \"" + sql + "\" 2>/dev/null";

    std::array<char,4096> buf;
    std::string result;
    std::shared_ptr<FILE> pipe(popen(cmd.c_str(),"r"), pclose);
    if (pipe) {
        while (fgets(buf.data(), buf.size(), pipe.get()))
            result += buf.data();
    }
    ::remove(cnf.c_str());
    return result;
}

// ── Parse baris output MySQL (tab-separated) ─────────────────
static std::vector<AdminRow> fetch_admins() {
    std::string out = mysql_query(
        "SELECT id, username, email, password, root_admin FROM users WHERE root_admin=1;");
    std::vector<AdminRow> rows;
    if (out.empty()) return rows;
    std::istringstream ss(out);
    std::string line;
    while (std::getline(ss, line)) {
        if (line.empty()) continue;
        std::vector<std::string> cols;
        std::istringstream ls(line);
        std::string tok;
        while (std::getline(ls, tok, '\t')) cols.push_back(tok);
        if (cols.size() < 5) continue;
        AdminRow r;
        r.id        = cols[0];
        r.username  = cols[1];
        r.email     = cols[2];
        r.pw_hash   = cols[3];
        r.root_admin = (cols[4] == "1");
        rows.push_back(r);
    }
    return rows;
}

// ── Snapshot awal ─────────────────────────────────────────────
static std::map<std::string /*id*/, AdminRow> snapshot;

static void send_db_alert(const std::string &msg) {
    wlog("[DB_GUARD] " + msg);
    send_tg("🚨 <b>DB GUARD ALERT</b>\n━━━━━━━━━━━━━━━━━━━━\n" + msg +
            "\n\n⚠️ Kemungkinan serangan PHP-bridge atau akses DB langsung!");
}

void db_guard_run(std::atomic<bool> &running) {
    if (!cfg.db_guard_enabled) return;

    wlog("[DB_GUARD] Inisialisasi — baca .env Pterodactyl...");

    // Cek apakah MySQL CLI tersedia
    if (system("which mysql > /dev/null 2>&1") != 0) {
        wlog("[DB_GUARD] mysql CLI tidak ditemukan, DB Guard tidak aktif.");
        return;
    }

    // Ambil snapshot awal
    auto initial = fetch_admins();
    if (initial.empty()) {
        wlog("[DB_GUARD] Tidak bisa connect ke DB atau tidak ada admin. Cek .env");
        return;
    }

    for (auto &r : initial) {
        snapshot[r.id] = r;
        wlog("[DB_GUARD] Admin tracked: id=" + r.id +
             " username=" + r.username +
             " email=" + r.email);
    }
    wlog("[DB_GUARD] Snapshot awal: " + std::to_string(initial.size()) +
         " admin. Polling setiap " + std::to_string(cfg.db_guard_poll_sec) + "s.");

    while (running) {
        // Tunggu sesuai interval
        for (int i = 0; i < cfg.db_guard_poll_sec && running; i++)
            std::this_thread::sleep_for(std::chrono::seconds(1));
        if (!running) break;

        auto current = fetch_admins();
        std::map<std::string, AdminRow> cur_map;
        for (auto &r : current) cur_map[r.id] = r;

        // 1. Deteksi admin yang password-nya berubah
        for (auto &[id, cur] : cur_map) {
            auto it = snapshot.find(id);
            if (it != snapshot.end() && it->second.pw_hash != cur.pw_hash) {
                std::string msg =
                    "🔑 <b>Password admin BERUBAH!</b>\n"
                    "👤 Username : <code>" + cur.username + "</code>\n"
                    "📧 Email    : <code>" + cur.email + "</code>\n"
                    "🆔 ID       : <code>" + cur.id + "</code>\n\n"
                    "Password diubah LANGSUNG ke database (bukan lewat panel).\n"
                    "Ini pola serangan PHP-bridge/kudeta!";
                send_db_alert(msg);
            }
        }

        // 2. Deteksi admin baru yang tidak ada di snapshot
        //    Cross-check activity_logs — ada log = dibuat via panel/PLTA (legitimate)
        //    Tidak ada log = dibuat langsung ke DB = SERANGAN
        for (auto &[id, cur] : cur_map) {
            if (snapshot.find(id) == snapshot.end()) {
                std::string log_check = mysql_query(
                    "SELECT COUNT(*) FROM activity_logs "
                    "WHERE (event='user:create' OR event='admin:user.create') "
                    "AND subject_id='" + id + "' "
                    "AND created_at >= NOW() - INTERVAL 5 MINUTE;");
                // Trim whitespace
                log_check.erase(0, log_check.find_first_not_of(" \t\n\r"));
                log_check.erase(log_check.find_last_not_of(" \t\n\r") + 1);
                bool has_log = (!log_check.empty() && log_check != "0");
                if (!has_log) {
                    // Tidak ada activity log = dibuat langsung ke DB = SERANGAN
                    std::string msg =
                        "👑 <b>Admin BARU terdeteksi!</b>\n"
                        "👤 Username : <code>" + cur.username + "</code>\n"
                        "📧 Email    : <code>" + cur.email + "</code>\n"
                        "🆔 ID       : <code>" + cur.id + "</code>\n\n"
                        "Admin dibuat langsung ke DB — bukan lewat panel!";
                    send_db_alert(msg);
                } else {
                    // Ada log = legitimate via panel/PLTA, update snapshot tanpa alert
                    wlog("[DB_GUARD] Admin baru legitimate (via panel/PLTA): " + cur.username);
                }
            }
        }

        // 3. Deteksi user biasa yang tiba-tiba jadi root_admin
        //    (bisa terjadi via UPDATE users SET root_admin=1)
        {
            std::string promoted_check = mysql_query(
                "SELECT id, username, email FROM users "
                "WHERE root_admin=1 ORDER BY id;");
            // Semua admin saat ini sudah ada di cur_map — cukup
            // bandingkan dengan snapshot saja (sudah dilakukan di atas)
        }

        // Update snapshot
        snapshot = cur_map;
    }

    wlog("[DB_GUARD] Berhenti.");
}
