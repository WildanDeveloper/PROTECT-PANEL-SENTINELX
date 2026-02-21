#include "rate_protect.h"
#include "config.h"
#include "api.h"
#include "telegram.h"
#include "logger.h"

#include <chrono>
#include <algorithm>
#include <set>
#include <mutex>

static long long unix_now() {
    return (long long)std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

// ── Snapshot server/user sebelumnya untuk deteksi mass delete ─
static std::set<std::string> prev_server_ids;
static std::set<std::string> prev_user_ids;
static bool snapshot_initialized = false;
static std::mutex snapshot_mutex;

// ── Deteksi spam buat akun baru ───────────────────────────────
static void check_users_create() {
    auto users = api_get_recent_users(100);
    if (users.empty()) return;

    long long now = unix_now();
    std::vector<PteroUser> fresh;
    for (auto &u : users) {
        long long ts = parse_iso8601(u.created_at);
        if (ts > 0 && (now - ts) <= cfg.window_seconds) fresh.push_back(u);
    }
    if ((int)fresh.size() < cfg.threshold_accounts) return;

    wlog("[RATE] " + std::to_string(fresh.size()) + " akun baru dalam "
         + std::to_string(cfg.window_seconds) + "s! Threshold="
         + std::to_string(cfg.threshold_accounts) + " -> HAPUS SEMUA");

    SpamEvent ev;
    ev.type    = "AKUN BARU (User Spam)";
    ev.count   = (int)fresh.size();
    ev.window_sec = cfg.window_seconds;
    ev.deleted = 0;
    for (auto &u : fresh) {
        ev.ids.push_back(u.id);
        ev.names.push_back(u.username);
        ev.emails.push_back(u.email);
        if (api_delete_user(u.id)) ev.deleted++;
    }
    send_tg_spam_alert(ev);
}

// ── Deteksi spam buat server baru ─────────────────────────────
static void check_servers_create() {
    auto servers = api_get_recent_servers(100);
    if (servers.empty()) return;

    long long now = unix_now();
    std::vector<PteroServer> fresh;
    for (auto &s : servers) {
        long long ts = parse_iso8601(s.created_at);
        if (ts > 0 && (now - ts) <= cfg.window_seconds) fresh.push_back(s);
    }
    if ((int)fresh.size() < cfg.threshold_servers) return;

    wlog("[RATE] " + std::to_string(fresh.size()) + " server baru dalam "
         + std::to_string(cfg.window_seconds) + "s!");

    SpamEvent ev;
    ev.type    = "SERVER BARU (Server Spam)";
    ev.count   = (int)fresh.size();
    ev.window_sec = cfg.window_seconds;
    ev.deleted = 0;
    for (auto &s : fresh) {
        ev.ids.push_back(s.id);
        ev.names.push_back(s.name);
        ev.emails.push_back(s.uuid);
        if (api_delete_server(s.id)) ev.deleted++;
    }
    send_tg_spam_alert(ev);
}

// ── [BARU] Deteksi mass delete server/user ───────────────────
static void check_mass_delete() {
    if (!cfg.mass_delete_enabled) return;

    // Ambil server dan user yang ada sekarang
    auto servers = api_get_recent_servers(500);
    auto users   = api_get_recent_users(500);

    std::set<std::string> cur_server_ids, cur_user_ids;
    for (auto &s : servers) cur_server_ids.insert(s.id);
    for (auto &u : users)   cur_user_ids.insert(u.id);

    std::lock_guard<std::mutex> lk(snapshot_mutex);

    if (!snapshot_initialized) {
        prev_server_ids   = cur_server_ids;
        prev_user_ids     = cur_user_ids;
        snapshot_initialized = true;
        wlog("[MASS_DEL] Snapshot awal: " + std::to_string(cur_server_ids.size())
             + " server, " + std::to_string(cur_user_ids.size()) + " user.");
        return;
    }

    // Cari ID yang ada di snapshot lama tapi tidak ada sekarang = dihapus
    std::vector<std::string> deleted_servers, deleted_users;
    for (auto &id : prev_server_ids)
        if (!cur_server_ids.count(id)) deleted_servers.push_back(id);
    for (auto &id : prev_user_ids)
        if (!cur_user_ids.count(id)) deleted_users.push_back(id);

    if ((int)deleted_servers.size() >= cfg.mass_delete_threshold) {
        wlog("[MASS_DEL] ⚠️ MASS DELETE SERVER! " + std::to_string(deleted_servers.size())
             + " server dihapus dalam 1 polling cycle!");
        SpamEvent ev;
        ev.type       = "MASS DELETE SERVER ⚠️";
        ev.count      = (int)deleted_servers.size();
        ev.window_sec = cfg.poll_interval_sec;
        ev.deleted    = 0; // sudah terlanjur dihapus oleh attacker
        for (auto &id : deleted_servers) {
            ev.ids.push_back(id);
            ev.names.push_back("-");
            ev.emails.push_back("-");
        }
        send_tg_spam_alert(ev);
    }

    if ((int)deleted_users.size() >= cfg.mass_delete_threshold) {
        wlog("[MASS_DEL] ⚠️ MASS DELETE USER! " + std::to_string(deleted_users.size())
             + " user dihapus dalam 1 polling cycle!");
        SpamEvent ev;
        ev.type       = "MASS DELETE USER ⚠️";
        ev.count      = (int)deleted_users.size();
        ev.window_sec = cfg.poll_interval_sec;
        ev.deleted    = 0;
        for (auto &id : deleted_users) {
            ev.ids.push_back(id);
            ev.names.push_back("-");
            ev.emails.push_back("-");
        }
        send_tg_spam_alert(ev);
    }

    // Update snapshot
    prev_server_ids = cur_server_ids;
    prev_user_ids   = cur_user_ids;
}

// ── Public entry point ────────────────────────────────────────
void rate_protect_run() {
    check_users_create();
    check_servers_create();
    check_mass_delete();
}
