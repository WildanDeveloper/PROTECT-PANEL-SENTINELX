#include "config.h"
#include "http.h"
#include <iostream>
#include <fstream>
#include <algorithm>
#include <termios.h>
#include <unistd.h>

Config cfg;
const std::string CONFIG_FILE = "/etc/sentinelx.conf";

#define RST "\033[0m"
#define RED "\033[1;31m"
#define GRN "\033[1;32m"
#define YLW "\033[1;33m"
#define CYN "\033[1;36m"
#define BLD "\033[1m"
#define BLU "\033[1;34m"

static void okk(const std::string &m) { std::cout << "  " GRN "[OK] " RST << m << "\n"; }
static void err(const std::string &m) { std::cout << "  " RED "[!!] " RST << m << "\n"; }
static void inf(const std::string &m) { std::cout << "  " YLW "[>>] " RST << m << "\n"; }
static void sec(const std::string &t) {
    std::cout << "\n" BLU "  +-- " << t << " "
              << std::string(std::max(0, 38 - (int)t.size()), '-') << "+\n\n" RST;
}

static std::string ask(const std::string &label, const std::string &def = "", bool hide = false) {
    std::cout << BLD "  >> " RST << label;
    if (!def.empty()) std::cout << YLW " [default: " << def << "]" RST;
    std::cout << "\n     " GRN "-> " RST; std::cout.flush();
    std::string val;
    if (hide) {
        struct termios o, n; tcgetattr(STDIN_FILENO, &o);
        n = o; n.c_lflag &= ~ECHO; tcsetattr(STDIN_FILENO, TCSANOW, &n);
        std::getline(std::cin, val); tcsetattr(STDIN_FILENO, TCSANOW, &o);
        std::cout << "\n";
    } else { std::getline(std::cin, val); }
    if (val.empty() && !def.empty()) val = def;
    return val;
}

static bool ask_bool(const std::string &label, bool def) {
    std::string s = ask(label, def ? "y" : "n");
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    return s == "y" || s == "yes" || s == "1" || s == "true";
}

static bool test_tg() {
    return http_get("https://api.telegram.org/bot" + cfg.telegram_token + "/getMe")
               .find("\"ok\":true") != std::string::npos;
}
static bool test_plta() {
    if (cfg.api_application.empty() || cfg.panel_domain.empty()) return false;
    return http_get(cfg.panel_domain + "/api/application/users?per_page=1",
        {"Authorization: Bearer " + cfg.api_application, "Accept: application/json"})
               .find("\"data\"") != std::string::npos;
}
static bool test_pltc() {
    if (cfg.api_client.empty() || cfg.panel_domain.empty()) return false;
    return http_get(cfg.panel_domain + "/api/client",
        {"Authorization: Bearer " + cfg.api_client, "Accept: application/json"})
               .find("\"object\"") != std::string::npos;
}

bool load_cfg() {
    std::ifstream f(CONFIG_FILE); if (!f) return false;
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        auto eq = line.find('='); if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        if      (k == "telegram_token")          cfg.telegram_token          = v;
        else if (k == "telegram_owner_id")        cfg.telegram_owner_id       = v;
        else if (k == "panel_domain")             cfg.panel_domain            = v;
        else if (k == "panel_path")               cfg.panel_path              = v;
        else if (k == "api_application")          cfg.api_application         = v;
        else if (k == "api_client")               cfg.api_client              = v;
        else if (k == "volumes_path")             cfg.volumes_path            = v;
        else if (k == "log_file")                 cfg.log_file                = v;
        else if (k == "max_size_mb")              try{cfg.max_size_mb=std::stoll(v);}catch(...){}
        else if (k == "delete_dangerous")         cfg.delete_dangerous        = (v == "1");
        else if (k == "suspend_on_danger")        cfg.suspend_on_danger       = (v == "1");
        else if (k == "scan_file_content")        cfg.scan_file_content       = (v == "1");
        else if (k == "content_scan_limit_kb")    try{cfg.content_scan_limit_kb=std::stoi(v);}catch(...){}
        else if (k == "integrity_enabled")        cfg.integrity_enabled       = (v == "1");
        else if (k == "integrity_delete_new_php") cfg.integrity_delete_new_php= (v == "1");
        else if (k == "threshold_accounts")       try{cfg.threshold_accounts=std::stoi(v);}catch(...){}
        else if (k == "threshold_servers")        try{cfg.threshold_servers=std::stoi(v);}catch(...){}
        else if (k == "window_seconds")           try{cfg.window_seconds=std::stoi(v);}catch(...){}
        else if (k == "poll_interval_sec")        try{cfg.poll_interval_sec=std::stoi(v);}catch(...){}
        else if (k == "mass_delete_threshold")    try{cfg.mass_delete_threshold=std::stoi(v);}catch(...){}
        else if (k == "mass_delete_enabled")      cfg.mass_delete_enabled     = (v == "1");
        else if (k == "self_protect_enabled")     cfg.self_protect_enabled    = (v == "1");
        else if (k == "self_binary_path")         cfg.self_binary_path        = v;
    }
    return !cfg.telegram_token.empty();
}

void save_cfg() {
    std::ofstream f(CONFIG_FILE); if (!f) { err("Gagal simpan: " + CONFIG_FILE); return; }
    f << "telegram_token="          << cfg.telegram_token           << "\n"
      << "telegram_owner_id="       << cfg.telegram_owner_id        << "\n"
      << "panel_domain="            << cfg.panel_domain             << "\n"
      << "panel_path="              << cfg.panel_path               << "\n"
      << "api_application="         << cfg.api_application          << "\n"
      << "api_client="              << cfg.api_client               << "\n"
      << "volumes_path="            << cfg.volumes_path             << "\n"
      << "log_file="                << cfg.log_file                 << "\n"
      << "max_size_mb="             << cfg.max_size_mb              << "\n"
      << "delete_dangerous="        << (cfg.delete_dangerous?"1":"0") << "\n"
      << "suspend_on_danger="       << (cfg.suspend_on_danger?"1":"0") << "\n"
      << "scan_file_content="       << (cfg.scan_file_content?"1":"0") << "\n"
      << "content_scan_limit_kb="   << cfg.content_scan_limit_kb    << "\n"
      << "integrity_enabled="       << (cfg.integrity_enabled?"1":"0") << "\n"
      << "integrity_delete_new_php="<< (cfg.integrity_delete_new_php?"1":"0") << "\n"
      << "threshold_accounts="      << cfg.threshold_accounts       << "\n"
      << "threshold_servers="       << cfg.threshold_servers        << "\n"
      << "window_seconds="          << cfg.window_seconds           << "\n"
      << "poll_interval_sec="       << cfg.poll_interval_sec        << "\n"
      << "mass_delete_threshold="   << cfg.mass_delete_threshold    << "\n"
      << "mass_delete_enabled="     << (cfg.mass_delete_enabled?"1":"0") << "\n"
      << "self_protect_enabled="    << (cfg.self_protect_enabled?"1":"0") << "\n"
      << "self_binary_path="        << cfg.self_binary_path         << "\n";
}

void setup_wizard() {
    std::cout << CYN "  ╔══════════════════════════════════════╗\n" RST
              << CYN "  ║    SentinelX Setup Wizard v2.0       ║\n" RST
              << CYN "  ╚══════════════════════════════════════╝\n\n" RST;

    // ── Telegram ──────────────────────────────────────────────
    sec("1. Telegram Bot");
    cfg.telegram_token    = ask("Bot Token", "", true);
    cfg.telegram_owner_id = ask("Owner Chat ID");

    if (test_tg()) okk("Telegram OK!");
    else           err("Token invalid atau tidak bisa konek.");

    // ── Panel ─────────────────────────────────────────────────
    sec("2. Pterodactyl Panel");
    cfg.panel_domain      = ask("Panel URL (e.g. https://panel.example.com)");
    cfg.panel_path        = ask("Panel PHP path", "/var/www/pterodactyl");
    cfg.api_application   = ask("PLTA Token (Application API)", "", true);
    cfg.api_client        = ask("PLTC Token (Client API, opsional)", "", true);

    if (test_plta()) okk("Application API OK!");
    else             err("PLTA gagal. Cek token & URL.");
    if (!cfg.api_client.empty()) {
        if (test_pltc()) okk("Client API OK!");
        else             err("PLTC gagal.");
    }

    // ── Disk & Content ────────────────────────────────────────
    sec("3. Disk & Content Protection");
    cfg.volumes_path         = ask("Path volumes", "/var/lib/pterodactyl/volumes");
    cfg.max_size_mb          = std::stoll(ask("Max file size (MB)", "1024"));
    cfg.delete_dangerous  = ask_bool("Hapus script berbahaya otomatis?", false);
    cfg.suspend_on_danger = ask_bool(
        "Suspend server otomatis saat ada script berbahaya (PHP/bash)?\n"
        "  (N = kirim alert + tombol Suspend ke Telegram)", false);
    cfg.scan_file_content    = ask_bool("Scan ISI file (PHP webshell, reverse shell)?", true);
    cfg.content_scan_limit_kb = std::stoi(ask("Batas baca isi file (KB)", "512"));

    // ── Integrity ─────────────────────────────────────────────
    sec("4. Panel File Integrity");
    cfg.integrity_enabled        = ask_bool("Aktifkan integrity monitor panel?", true);
    cfg.integrity_delete_new_php = ask_bool("Auto-hapus file PHP baru di panel?", true);

    // ── Rate Limit & Mass Delete ──────────────────────────────
    sec("5. Rate Limit & Mass Delete Detection");
    cfg.threshold_accounts   = std::stoi(ask("Max akun baru dalam window", "5"));
    cfg.threshold_servers    = std::stoi(ask("Max server baru dalam window", "5"));
    cfg.window_seconds       = std::stoi(ask("Window waktu (detik)", "10"));
    cfg.poll_interval_sec    = std::stoi(ask("Polling interval (detik)", "3"));
    cfg.mass_delete_enabled  = ask_bool("Aktifkan deteksi mass delete?", true);
    cfg.mass_delete_threshold = std::stoi(ask("Threshold mass delete (server/user)", "5"));

    // ── Self Protection ───────────────────────────────────────
    sec("6. Self Protection");
    cfg.self_protect_enabled = ask_bool("Aktifkan self protection (chattr+i + watchdog)?", true);
    cfg.self_binary_path     = ask("Path binary SentinelX", "/usr/local/bin/sentinelx");

    save_cfg();
    okk("Config disimpan ke " + CONFIG_FILE);
    std::cout << "\n" GRN "  Setup selesai! Jalankan ulang: sudo sentinelx\n\n" RST;
}
