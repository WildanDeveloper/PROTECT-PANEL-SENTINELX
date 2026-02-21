#pragma once
#include <string>
#include <vector>
#include <map>

// ── Struktur data lineage ─────────────────────────────────────

struct TrackServer {
    std::string id;
    std::string uuid;
    std::string name;
    std::string node;
    std::string owner_id;       // user ID pemilik server
    std::string created_at;
    std::string created_by;     // "PLTA" / "Panel:admin" / "Panel:userX"
    std::string created_by_ip;
};

struct TrackUser {
    std::string id;
    std::string username;
    std::string email;
    std::string created_at;
    std::string created_by;     // "PLTA" / "Panel:admin" / "Panel:userX" / "ROOT"
    std::string created_by_ip;
    std::vector<TrackServer> servers;   // server milik user ini
};

// ── Inisialisasi: baca .env, test koneksi MySQL ───────────────
bool tracking_init();

// ── Refresh data dari MySQL + API ────────────────────────────
void tracking_refresh();

// ── Get formatted tree untuk Telegram ────────────────────────
std::string tracking_get_tree(int max_depth = 10);

// ── Get lineage satu server berdasarkan nama (partial match) ─
std::string tracking_get_server_info(const std::string &server_name);

// ── Get lineage satu user berdasarkan username ────────────────
std::string tracking_get_user_info(const std::string &username);

// ── Get summary stats ─────────────────────────────────────────
std::string tracking_get_stats();
