/*
 * ============================================================
 *  SentinelX — User/Server Lineage Tracker
 *
 *  Cara kerja:
 *  1. Baca kredensial DB dari /var/www/pterodactyl/.env
 *  2. Query activity_logs MySQL Pterodactyl:
 *     - event = 'user:create'  → siapa yang bikin user
 *     - event = 'server:create' → siapa yang bikin server
 *     - actor_type = User   → dibuat oleh admin via panel
 *     - actor_type = null   → dibuat via PLTA (API)
 *  3. Cross-reference dengan API untuk data user/server terkini
 *  4. Build tree lineage
 *  5. Format untuk Telegram
 * ============================================================
 */

#include "tracking.h"
#include "config.h"
#include "api.h"
#include "http.h"
#include "logger.h"

#include <fstream>
#include <sstream>
#include <map>
#include <vector>
#include <algorithm>
#include <mutex>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <array>
#include <regex>
#include <iomanip>
#include <set>

// ── DB Credentials (dibaca dari .env) ────────────────────────
static std::string db_host, db_port, db_name, db_user, db_pass;
static bool db_ready = false;

// ── Cache data ────────────────────────────────────────────────
static std::map<std::string, TrackUser>   users_map;   // key = user_id
static std::map<std::string, TrackServer> servers_map; // key = server_id
static std::mutex tracking_mutex;

// ── Escape string untuk argumen shell ────────────────────────
static std::string shell_escape(const std::string &s) {
    std::string out;
    for (char c : s) {
        if (c == '\'') out += "'\\''";
        else           out += c;
    }
    return "'" + out + "'";
}

// ── Jalankan query MySQL, return output ───────────────────────
static std::string mysql_query(const std::string &sql) {
    if (!db_ready) return "";

    std::string cmd =
        "mysql"
        " -h " + shell_escape(db_host) +
        " -P " + shell_escape(db_port) +
        " -u " + shell_escape(db_user) +
        " -p" + shell_escape(db_pass) +
        " " + shell_escape(db_name) +
        " --batch --skip-column-names"
        " -e " + shell_escape(sql) +
        " 2>/dev/null";

    std::array<char, 4096> buf;
    std::string result;
    std::shared_ptr<FILE> pipe(popen(cmd.c_str(), "r"), pclose);
    if (!pipe) return "";
    while (fgets(buf.data(), buf.size(), pipe.get()))
        result += buf.data();
    return result;
}

// ── Parse baris tab-separated dari mysql --batch ─────────────
static std::vector<std::vector<std::string>> parse_tsv(const std::string &s) {
    std::vector<std::vector<std::string>> rows;
    std::istringstream ss(s);
    std::string line;
    while (std::getline(ss, line)) {
        if (line.empty()) continue;
        std::vector<std::string> cols;
        std::istringstream ls(line);
        std::string col;
        while (std::getline(ls, col, '\t'))
            cols.push_back(col);
        rows.push_back(cols);
    }
    return rows;
}

// ── Baca .env Pterodactyl ─────────────────────────────────────
static std::string env_get(const std::string &content, const std::string &key) {
    std::regex re(key + "=([^\r\n]+)");
    std::smatch m;
    if (std::regex_search(content, m, re)) {
        std::string val = m[1].str();
        // Hapus quote jika ada
        if (val.size() >= 2 && val.front() == '"' && val.back() == '"')
            val = val.substr(1, val.size() - 2);
        if (val.size() >= 2 && val.front() == '\'' && val.back() == '\'')
            val = val.substr(1, val.size() - 2);
        return val;
    }
    return "";
}

bool tracking_init() {
    const std::string env_path = "/var/www/pterodactyl/.env";
    std::ifstream f(env_path);
    if (!f) {
        wlog("[TRACKING] Tidak bisa baca " + env_path);
        return false;
    }
    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());

    db_host = env_get(content, "DB_HOST");
    db_port = env_get(content, "DB_PORT");
    db_user = env_get(content, "DB_USERNAME");
    db_pass = env_get(content, "DB_PASSWORD");
    db_name = env_get(content, "DB_DATABASE");

    if (db_host.empty()) db_host = "127.0.0.1";
    if (db_port.empty()) db_port = "3306";

    if (db_user.empty() || db_name.empty()) {
        wlog("[TRACKING] Gagal parse DB credentials dari .env");
        return false;
    }

    // Test koneksi
    std::string test = mysql_query("SELECT 1");
    if (test.find("1") == std::string::npos) {
        wlog("[TRACKING] Koneksi MySQL gagal! host=" + db_host + " db=" + db_name);
        db_ready = false;
        return false;
    }

    db_ready = true;
    wlog("[TRACKING] Koneksi MySQL OK. DB=" + db_name + " host=" + db_host);
    return true;
}

// ── JSON extract sederhana (reuse dari api.cpp) ───────────────
static std::string jget(const std::string &json, const std::string &key) {
    std::string needle = "\"" + key + "\":";
    auto p = json.find(needle);
    if (p == std::string::npos) return "";
    p += needle.size();
    while (p < json.size() && (json[p]==' '||json[p]=='\t')) p++;
    if (p >= json.size()) return "";
    if (json[p] == '"') {
        p++; std::string out;
        while (p < json.size() && json[p] != '"') {
            if (json[p]=='\\' && p+1<json.size()) { p++; out+=json[p]; }
            else out += json[p];
            p++;
        }
        return out;
    }
    auto e = json.find_first_of(",}\n", p);
    return json.substr(p, e==std::string::npos?std::string::npos:e-p);
}

static std::vector<std::string> split_json_objects(const std::string &json) {
    std::vector<std::string> objs;
    auto dp = json.find("\"data\"");
    if (dp == std::string::npos) return objs;
    auto as = json.find('[', dp);
    if (as == std::string::npos) return objs;
    int depth = 0; size_t os = std::string::npos;
    for (size_t i = as; i < json.size(); i++) {
        if (json[i]=='{') { if(depth==0) os=i; depth++; }
        else if (json[i]=='}') {
            depth--;
            if (depth==0 && os!=std::string::npos) {
                objs.push_back(json.substr(os, i-os+1));
                os = std::string::npos;
            }
        }
        else if (json[i]==']' && depth==0) break;
    }
    return objs;
}

static std::string get_attributes(const std::string &json) {
    auto p = json.find("\"attributes\"");
    if (p == std::string::npos) return json;
    auto b = json.find('{', p);
    if (b == std::string::npos) return json;
    int depth = 0; size_t end = b;
    for (; end < json.size(); end++) {
        if (json[end]=='{') depth++;
        else if (json[end]=='}') { depth--; if (depth==0) break; }
    }
    return json.substr(b, end-b+1);
}

// ── Ambil semua user dari Pterodactyl API ─────────────────────
static std::map<std::string, TrackUser> fetch_all_users() {
    std::map<std::string, TrackUser> result;
    if (cfg.api_application.empty() || cfg.panel_domain.empty()) return result;

    std::vector<std::string> hdrs = {
        "Authorization: Bearer " + cfg.api_application,
        "Accept: application/json"
    };

    // Ambil semua page
    int page = 1;
    while (true) {
        std::string r = http_get(
            cfg.panel_domain + "/api/application/users?per_page=100&page="
            + std::to_string(page), hdrs);
        auto objs = split_json_objects(r);
        if (objs.empty()) break;

        for (auto &obj : objs) {
            std::string attr = get_attributes(obj);
            TrackUser u;
            u.id         = jget(attr, "id");
            u.username   = jget(attr, "username");
            u.email      = jget(attr, "email");
            u.created_at = jget(attr, "created_at");
            if (!u.id.empty()) result[u.id] = u;
        }

        // Cek apakah masih ada halaman berikutnya
        auto meta_p = r.find("\"meta\"");
        if (meta_p == std::string::npos) break;
        std::string total_s = jget(r.substr(meta_p), "total");
        if (total_s.empty()) break;
        int total = std::stoi(total_s);
        if ((int)result.size() >= total) break;
        page++;
        if (page > 50) break; // safety
    }
    return result;
}

// ── Ambil semua server dari API ───────────────────────────────
static std::vector<TrackServer> fetch_all_servers() {
    std::vector<TrackServer> result;
    if (cfg.api_application.empty() || cfg.panel_domain.empty()) return result;

    std::vector<std::string> hdrs = {
        "Authorization: Bearer " + cfg.api_application,
        "Accept: application/json"
    };

    int page = 1;
    while (true) {
        std::string r = http_get(
            cfg.panel_domain + "/api/application/servers?per_page=100&page="
            + std::to_string(page) + "&include=node", hdrs);
        auto objs = split_json_objects(r);
        if (objs.empty()) break;

        for (auto &obj : objs) {
            std::string attr = get_attributes(obj);
            TrackServer s;
            s.id         = jget(attr, "id");
            s.uuid       = jget(attr, "uuid");
            s.name       = jget(attr, "name");
            s.owner_id   = jget(attr, "user");
            s.created_at = jget(attr, "created_at");
            // Node name dari relationships jika ada
            auto rel_p = obj.find("\"relationships\"");
            if (rel_p != std::string::npos) {
                auto node_p = obj.find("\"node\"", rel_p);
                if (node_p != std::string::npos) {
                    auto nb = obj.find('{', node_p);
                    if (nb != std::string::npos) {
                        auto na = get_attributes(obj.substr(nb, 500));
                        s.node = jget(na, "name");
                    }
                }
            }
            if (s.node.empty()) s.node = "node#" + jget(attr, "node");
            if (!s.id.empty()) result.push_back(s);
        }

        auto meta_p = r.find("\"meta\"");
        if (meta_p == std::string::npos) break;
        std::string total_s = jget(r.substr(meta_p), "total");
        if (total_s.empty()) break;
        int total = std::stoi(total_s);
        if ((int)result.size() >= total) break;
        page++;
        if (page > 100) break;
    }
    return result;
}

// ── Query activity_logs untuk user creation events ────────────
// Return map: subject_id → {actor_username, actor_type, ip, timestamp}
struct ActivityEntry {
    std::string actor_name;  // username atau "PLTA" atau "ROOT"
    std::string actor_type;  // "User" / "ApiKey" / "PLTA"
    std::string ip;
    std::string timestamp;
};

static std::map<std::string, ActivityEntry> fetch_user_creation_logs() {
    std::map<std::string, ActivityEntry> result;
    if (!db_ready) return result;

    // Query activity_logs join ke users untuk nama actor
    std::string sql =
        "SELECT al.subject_id, "
        "COALESCE(u.username, 'PLTA'), "
        "COALESCE(al.actor_type, 'PLTA'), "
        "COALESCE(al.ip, '-'), "
        "al.timestamp "
        "FROM activity_logs al "
        "LEFT JOIN users u ON al.actor_type='Pterodactyl\\\\Models\\\\User' AND al.actor_id=u.id "
        "WHERE al.event='user:create' "
        "   OR al.event='admin:user.create' "
        "ORDER BY al.timestamp ASC";

    auto rows = parse_tsv(mysql_query(sql));
    for (auto &row : rows) {
        if (row.size() < 5) continue;
        ActivityEntry e;
        e.actor_name = row[1];
        e.actor_type = row[2];
        e.ip         = row[3];
        e.timestamp  = row[4];
        // Simplify actor type
        if (e.actor_type.find("User") != std::string::npos)
            e.actor_type = "Panel";
        else if (e.actor_type == "PLTA" || e.actor_type.empty())
            e.actor_type = "PLTA";
        result[row[0]] = e;
    }
    return result;
}

static std::map<std::string, ActivityEntry> fetch_server_creation_logs() {
    std::map<std::string, ActivityEntry> result;
    if (!db_ready) return result;

    std::string sql =
        "SELECT al.subject_id, "
        "COALESCE(u.username, 'PLTA'), "
        "COALESCE(al.actor_type, 'PLTA'), "
        "COALESCE(al.ip, '-'), "
        "al.timestamp "
        "FROM activity_logs al "
        "LEFT JOIN users u ON al.actor_type='Pterodactyl\\\\Models\\\\User' AND al.actor_id=u.id "
        "WHERE al.event='server:create' "
        "   OR al.event='admin:server.create' "
        "ORDER BY al.timestamp ASC";

    auto rows = parse_tsv(mysql_query(sql));
    for (auto &row : rows) {
        if (row.size() < 5) continue;
        ActivityEntry e;
        e.actor_name = row[1];
        e.actor_type = row[2];
        e.ip         = row[3];
        e.timestamp  = row[4];
        if (e.actor_type.find("User") != std::string::npos)
            e.actor_type = "Panel";
        else
            e.actor_type = "PLTA";
        result[row[0]] = e;
    }
    return result;
}

// ── Refresh semua data ────────────────────────────────────────
void tracking_refresh() {
    wlog("[TRACKING] Refresh data lineage...");

    auto all_users   = fetch_all_users();
    auto all_servers = fetch_all_servers();
    auto user_logs   = fetch_user_creation_logs();
    auto server_logs = fetch_server_creation_logs();

    // Gabungkan activity log ke user data
    for (auto &[id, user] : all_users) {
        auto it = user_logs.find(id);
        if (it != user_logs.end()) {
            auto &e = it->second;
            if (e.actor_type == "PLTA")
                user.created_by = "PLTA";
            else
                user.created_by = "Panel:" + e.actor_name;
            user.created_by_ip = e.ip;
        } else {
            // Tidak ada log — kemungkinan admin asli / user lama
            user.created_by = (id == "1") ? "ROOT" : "PLTA";
        }
    }

    // Assign server ke user yang sesuai
    for (auto &srv : all_servers) {
        auto it = server_logs.find(srv.id);
        if (it != server_logs.end()) {
            auto &e = it->second;
            srv.created_by = (e.actor_type == "PLTA") ? "PLTA" : "Panel:" + e.actor_name;
            srv.created_by_ip = e.ip;
        } else {
            srv.created_by = "PLTA";
        }

        if (!srv.owner_id.empty() && all_users.count(srv.owner_id))
            all_users[srv.owner_id].servers.push_back(srv);
    }

    std::lock_guard<std::mutex> lk(tracking_mutex);
    users_map.clear();
    servers_map.clear();
    for (auto &[id, u] : all_users) users_map[id] = u;
    for (auto &s : all_servers)     servers_map[s.id] = s;

    wlog("[TRACKING] Selesai: " + std::to_string(users_map.size())
         + " user, " + std::to_string(servers_map.size()) + " server.");
}

// ── Escape HTML untuk Telegram ────────────────────────────────
static std::string esc(const std::string &s) {
    std::string o;
    for (char c : s) {
        if      (c == '<') o += "&lt;";
        else if (c == '>') o += "&gt;";
        else if (c == '&') o += "&amp;";
        else               o += c;
    }
    return o;
}

// ── Format tanggal singkat ────────────────────────────────────
static std::string short_date(const std::string &dt) {
    if (dt.size() >= 10) return dt.substr(0, 10);
    return dt;
}

// ── Render tree rekursif (BFS per level) ─────────────────────
static std::string render_user_branch(
        const TrackUser &u,
        const std::string &prefix,
        bool is_last,
        int depth,
        int max_depth,
        std::map<std::string, TrackUser> &all) {

    if (depth > max_depth) return "";
    std::string line;
    std::string connector = is_last ? "└── " : "├── ";

    // Label icon
    std::string icon = (u.id == "1") ? "👑" : "👤";
    std::string by_label = "";
    if (u.created_by == "ROOT")        by_label = " [root]";
    else if (u.created_by == "PLTA")   by_label = " [via PLTA]";
    else                                by_label = " [via " + esc(u.created_by) + "]";

    line += prefix + connector
         + icon + " <b>" + esc(u.username) + "</b> <code>[ID:" + u.id + "]</code>"
         + by_label + " — " + short_date(u.created_at) + "\n";

    std::string child_prefix = prefix + (is_last ? "    " : "│   ");

    // Server milik user ini
    for (int i = 0; i < (int)u.servers.size(); i++) {
        bool srv_last = (i == (int)u.servers.size() - 1);
        // cek apakah ada child user setelahnya
        // (untuk koneksi prefix yang benar, kita tahu setelah server mungkin ada user lain)
        // simplified: gunakan is_last berdasarkan posisi saja
        std::string sc = srv_last ? "└── " : "├── ";
        line += child_prefix + sc
             + "🖥 <i>\"" + esc(u.servers[i].name) + "\"</i>"
             + " [" + esc(u.servers[i].node) + "]"
             + " — " + u.servers[i].created_by
             + " — " + short_date(u.servers[i].created_at) + "\n";
    }

    // Cari child user (user yang dibuat oleh user ini via Panel)
    std::string my_label = "Panel:" + u.username;
    std::vector<TrackUser> children;
    for (auto &[id, cu] : all)
        if (cu.created_by == my_label) children.push_back(cu);
    // Sort by created_at
    std::sort(children.begin(), children.end(),
              [](const TrackUser &a, const TrackUser &b){ return a.created_at < b.created_at; });

    for (int i = 0; i < (int)children.size(); i++) {
        bool child_last = (i == (int)children.size() - 1);
        line += render_user_branch(children[i], child_prefix, child_last,
                                   depth + 1, max_depth, all);
    }
    return line;
}

// ── Get lineage tree ──────────────────────────────────────────
std::string tracking_get_tree(int max_depth) {
    std::lock_guard<std::mutex> lk(tracking_mutex);

    if (users_map.empty())
        return "⏳ Data belum tersedia. Coba lagi sebentar.";

    std::ostringstream out;
    out << "🌳 <b>LINEAGE TREE</b>\n"
        << "━━━━━━━━━━━━━━━━━━━━\n"
        << "👥 " << users_map.size() << " user  |  "
        << "🖥 " << servers_map.size() << " server\n"
        << "━━━━━━━━━━━━━━━━━━━━\n\n";

    // Root: user yang dibuat via PLTA atau ROOT (bukan dari user lain)
    // Biasanya admin (ID=1) dan user yang dibuat via PLTA langsung
    std::vector<TrackUser> roots;
    for (auto &[id, u] : users_map) {
        if (u.created_by == "ROOT" || u.created_by == "PLTA")
            roots.push_back(u);
    }
    std::sort(roots.begin(), roots.end(),
              [](const TrackUser &a, const TrackUser &b){
                  return std::stoll(a.id.empty()?"0":a.id)
                       < std::stoll(b.id.empty()?"0":b.id);
              });

    for (int i = 0; i < (int)roots.size(); i++) {
        bool last = (i == (int)roots.size() - 1);
        out << render_user_branch(roots[i], "", last, 0, max_depth,
                                   const_cast<std::map<std::string,TrackUser>&>(users_map));
    }

    // User yang tidak terdeteksi asal-usulnya
    std::vector<TrackUser> unknown;
    std::set<std::string> shown;
    for (auto &[id, u] : users_map)
        if (u.created_by == "ROOT" || u.created_by == "PLTA"
            || u.created_by.rfind("Panel:", 0) == 0)
            shown.insert(id);

    // Cek siapa yang sudah di-render sebagai child
    // Simplified: tampilkan semua root saja, yang lain sudah masuk branch

    out << "\n💡 <i>PLTA = dibuat via Application API\n"
        << "Panel:X = dibuat oleh user X via panel admin</i>";

    return out.str();
}

// ── Get info satu server ──────────────────────────────────────
std::string tracking_get_server_info(const std::string &query) {
    std::lock_guard<std::mutex> lk(tracking_mutex);

    // Cari server yang cocok (partial match nama)
    std::string ql = query;
    std::transform(ql.begin(), ql.end(), ql.begin(), ::tolower);

    std::vector<const TrackServer*> found;
    for (auto &[id, s] : servers_map) {
        std::string nl = s.name;
        std::transform(nl.begin(), nl.end(), nl.begin(), ::tolower);
        if (nl.find(ql) != std::string::npos) found.push_back(&s);
    }

    if (found.empty())
        return "❌ Server tidak ditemukan: <code>" + esc(query) + "</code>";

    std::ostringstream out;
    out << "🔍 <b>Server Lineage</b> — " << found.size() << " hasil\n"
        << "━━━━━━━━━━━━━━━━━━━━\n";

    for (auto *s : found) {
        out << "\n🖥 <b>\"" << esc(s->name) << "\"</b>\n"
            << "   📌 UUID    : <code>" << esc(s->uuid) << "</code>\n"
            << "   🌐 Node    : " << esc(s->node) << "\n"
            << "   📅 Dibuat  : " << esc(s->created_at) << "\n"
            << "   🔧 Metode  : " << esc(s->created_by) << "\n";
        if (!s->created_by_ip.empty() && s->created_by_ip != "-")
            out << "   🌍 IP      : <code>" << esc(s->created_by_ip) << "</code>\n";

        // Owner user
        auto uit = users_map.find(s->owner_id);
        if (uit != users_map.end()) {
            auto &owner = uit->second;
            out << "\n   👤 <b>Pemilik:</b> " << esc(owner.username)
                << " <code>[ID:" << owner.id << "]</code>\n"
                << "   📧 Email   : " << esc(owner.email) << "\n"
                << "   📅 Reg.    : " << short_date(owner.created_at) << "\n"
                << "   🔧 Asal    : " << esc(owner.created_by) << "\n";
            if (!owner.created_by_ip.empty() && owner.created_by_ip != "-")
                out << "   🌍 IP Reg. : <code>" << esc(owner.created_by_ip) << "</code>\n";
        }
        out << "━━━━━━━━━━━━━━━━━━━━\n";
    }
    return out.str();
}

// ── Get info satu user ────────────────────────────────────────
std::string tracking_get_user_info(const std::string &query) {
    std::lock_guard<std::mutex> lk(tracking_mutex);

    std::string ql = query;
    std::transform(ql.begin(), ql.end(), ql.begin(), ::tolower);

    const TrackUser *found = nullptr;
    for (auto &[id, u] : users_map) {
        std::string ul = u.username;
        std::transform(ul.begin(), ul.end(), ul.begin(), ::tolower);
        std::string el = u.email;
        std::transform(el.begin(), el.end(), el.begin(), ::tolower);
        if (ul == ql || el == ql || id == query) { found = &u; break; }
    }
    if (!found) {
        // partial match
        for (auto &[id, u] : users_map) {
            std::string ul = u.username;
            std::transform(ul.begin(), ul.end(), ul.begin(), ::tolower);
            if (ul.find(ql) != std::string::npos) { found = &u; break; }
        }
    }
    if (!found)
        return "❌ User tidak ditemukan: <code>" + esc(query) + "</code>";

    std::ostringstream out;
    out << "👤 <b>User Lineage: " << esc(found->username) << "</b>\n"
        << "━━━━━━━━━━━━━━━━━━━━\n"
        << "🆔 ID      : <code>" << found->id << "</code>\n"
        << "📧 Email   : " << esc(found->email) << "\n"
        << "📅 Dibuat  : " << esc(found->created_at) << "\n"
        << "🔧 Asal    : " << esc(found->created_by) << "\n";

    if (!found->created_by_ip.empty() && found->created_by_ip != "-")
        out << "🌍 IP Reg. : <code>" << esc(found->created_by_ip) << "</code>\n";

    // Server milik user ini
    if (!found->servers.empty()) {
        out << "\n🖥 <b>Server (" << found->servers.size() << "):</b>\n";
        for (auto &s : found->servers) {
            out << "  • <b>\"" << esc(s.name) << "\"</b>"
                << " [" << esc(s.node) << "]"
                << " — " << short_date(s.created_at)
                << " — " << esc(s.created_by) << "\n";
        }
    } else {
        out << "\n🖥 Tidak ada server.\n";
    }

    // User yang dibuat oleh user ini
    std::string my_label = "Panel:" + found->username;
    std::vector<const TrackUser*> children;
    for (auto &[id, cu] : users_map)
        if (cu.created_by == my_label) children.push_back(&cu);

    if (!children.empty()) {
        out << "\n👥 <b>User yang dibuat (" << children.size() << "):</b>\n";
        for (auto *cu : children)
            out << "  • " << esc(cu->username)
                << " <code>[ID:" << cu->id << "]</code>"
                << " — " << short_date(cu->created_at) << "\n";
    }

    return out.str();
}

// ── Stats ringkas ─────────────────────────────────────────────
std::string tracking_get_stats() {
    std::lock_guard<std::mutex> lk(tracking_mutex);

    int via_plta_user = 0, via_panel_user = 0;
    int via_plta_srv  = 0, via_panel_srv  = 0;

    for (auto &[id, u] : users_map) {
        if (u.created_by == "PLTA") via_plta_user++;
        else                        via_panel_user++;
    }
    for (auto &[id, s] : servers_map) {
        if (s.created_by == "PLTA") via_plta_srv++;
        else                        via_panel_srv++;
    }

    std::ostringstream out;
    out << "📊 <b>Tracking Stats</b>\n"
        << "━━━━━━━━━━━━━━━━━━━━\n"
        << "👥 Total User  : " << users_map.size()   << "\n"
        << "   ├ Via PLTA  : " << via_plta_user       << "\n"
        << "   └ Via Panel : " << via_panel_user       << "\n\n"
        << "🖥 Total Server: " << servers_map.size()  << "\n"
        << "   ├ Via PLTA  : " << via_plta_srv        << "\n"
        << "   └ Via Panel : " << via_panel_srv        << "\n";
    return out.str();
}
