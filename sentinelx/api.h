#pragma once
#include <string>
#include <vector>

struct SrvInfo {
    std::string uuid, name="?", username="?",
                email="?", created_at="?", node="?";
};

struct PteroUser {
    std::string id, username, email, created_at;
};

struct PteroServer {
    std::string id, uuid, name, created_at;
};

// Ambil info server berdasarkan UUID
SrvInfo      api_get_server_info(const std::string &uuid);

// Suspend server
void         api_suspend(const std::string &uuid);

// Ambil user/server terbaru
std::vector<PteroUser>   api_get_recent_users(int limit=100);
std::vector<PteroServer> api_get_recent_servers(int limit=100);

// Hapus user/server
bool api_delete_user(const std::string &user_id);
bool api_delete_server(const std::string &server_id);

// Parse ISO8601 ke unix timestamp
long long parse_iso8601(const std::string &s);
