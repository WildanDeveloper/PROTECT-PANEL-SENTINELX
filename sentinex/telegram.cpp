#include "telegram.h"
#include "config.h"
#include "http.h"
#include "logger.h"
#include <sstream>
#include <algorithm>

// ── Escape HTML untuk Telegram ────────────────────────────────
static std::string esc(const std::string &s) {
    std::string out;
    for (char c : s) {
        if      (c == '<') out += "&lt;";
        else if (c == '>') out += "&gt;";
        else if (c == '&') out += "&amp;";
        else               out += c;
    }
    return out;
}

// ── Escape JSON string ────────────────────────────────────────
static std::string jesc(const std::string &s) {
    std::string out;
    for (char c : s) {
        if      (c == '"')  out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else                out += c;
    }
    return out;
}

void send_tg(const std::string &msg) {
    if (cfg.telegram_token.empty()) return;
    http_post(
        "https://api.telegram.org/bot" + cfg.telegram_token + "/sendMessage",
        "{\"chat_id\":\"" + cfg.telegram_owner_id + "\","
        "\"text\":\"" + jesc(msg) + "\","
        "\"parse_mode\":\"HTML\"}",
        {"Content-Type: application/json"});
}


static std::string srv_block(const std::string &uuid, const std::string &name,
                              const std::string &node, const std::string &username,
                              const std::string &email, const std::string &created_at) {
    std::ostringstream o;
    o << "━━━━━━━━━━━━━━━━━━━━\n"
      << "🖥 <b>UUID    :</b> <code>" << esc(uuid)       << "</code>\n"
      << "📌 <b>Server  :</b> "        << esc(name)        << "\n"
      << "🌐 <b>Node    :</b> "        << esc(node)        << "\n\n"
      << "━━━━━━━━━━━━━━━━━━━━\n"
      << "👤 <b>User    :</b> "        << esc(username)    << "\n"
      << "📧 <b>Email   :</b> "        << esc(email)       << "\n"
      << "📅 <b>Dibuat  :</b> "        << esc(created_at)  << "\n\n"
      << "⏰  <b>Waktu   :</b> "       << now_str()        << "\n"
      << "🔗 " << cfg.panel_domain;
    return o.str();
}

// ── Disk bomb ─────────────────────────────────────────────────
void send_tg_disk_bomb(const std::string &fpath, long long mb, long long kb,
                       const std::string &del_status, bool suspended,
                       const std::string &uuid, const std::string &name,
                       const std::string &node, const std::string &username,
                       const std::string &email, const std::string &created_at) {
    std::ostringstream msg;
    msg << "🚨 <b>[SENTINELX] DISK BOMB TERDETEKSI!</b>\n"
        << "━━━━━━━━━━━━━━━━━━━━\n"
        << "📄 <b>File    :</b> <code>" << esc(fpath)      << "</code>\n"
        << "💾 <b>Ukuran  :</b> "        << mb << " MB " << kb << " KB\n"
        << "🗑 <b>Hapus   :</b> "        << esc(del_status) << "\n"
        << "🔒 <b>Suspend :</b> "        << (suspended ? "Ya (otomatis)" : "Tidak") << "\n\n"
        << srv_block(uuid, name, node, username, email, created_at);
    send_tg(msg.str());
}

// ── Script berbahaya ──────────────────────────────────────────
void send_tg_danger_script(const std::string &fpath, const std::string &reason,
                            const std::string &del_status, bool suspended,
                            const std::string &uuid, const std::string &name,
                            const std::string &node, const std::string &username,
                            const std::string &email, const std::string &created_at) {
    std::ostringstream msg;
    msg << "⚠️ <b>[SENTINELX] SCRIPT BERBAHAYA TERDETEKSI!</b>\n"
        << "━━━━━━━━━━━━━━━━━━━━\n"
        << "🔍 <b>Alasan  :</b> <code>" << esc(reason)     << "</code>\n"
        << "📄 <b>File    :</b> <code>" << esc(fpath)       << "</code>\n"
        << "🗑 <b>Aksi    :</b> "        << esc(del_status)  << "\n"
        << "🔒 <b>Suspend :</b> "        << (suspended ? "Ya (otomatis)" : "Tidak") << "\n\n"
        << srv_block(uuid, name, node, username, email, created_at);
    send_tg(msg.str());
}

// ── Spam / mass delete ────────────────────────────────────────
void send_tg_spam_alert(const SpamEvent &ev) {
    std::ostringstream msg;
    msg << "🚨 <b>[SENTINELX] SERANGAN TERDETEKSI!</b>\n"
        << "━━━━━━━━━━━━━━━━━━━━\n"
        << "📋 <b>Tipe    :</b> " << esc(ev.type)     << "\n"
        << "📊 <b>Jumlah  :</b> " << ev.count << " dalam " << ev.window_sec << " detik\n"
        << "🗑 <b>Dihapus :</b> " << ev.deleted << " dari " << ev.count << "\n\n"
        << "━━━━━━━━━━━━━━━━━━━━\n"
        << "📄 <b>Detail:</b>\n";
    int show = std::min((int)ev.ids.size(), 10);
    for (int i = 0; i < show; i++) {
        msg << "  <code>" << esc(ev.ids[i]) << "</code>";
        if (i < (int)ev.names.size()  && !ev.names[i].empty())  msg << " | " << esc(ev.names[i]);
        if (i < (int)ev.emails.size() && !ev.emails[i].empty()) msg << " | " << esc(ev.emails[i]);
        msg << "\n";
    }
    if ((int)ev.ids.size() > 10)
        msg << "  ... dan " << ev.ids.size() - 10 << " lainnya\n";
    msg << "\n⏰ <b>Waktu   :</b> " << now_str() << "\n"
        << "🔗 " << cfg.panel_domain;
    send_tg(msg.str());
}

// ── [BARU] Integrity: file PHP baru ──────────────────────────
void send_tg_integrity_new(const std::string &fpath, const std::string &del_status) {
    std::ostringstream msg;
    msg << "🚨 <b>[SENTINELX] FILE PHP BARU DI PANEL!</b>\n"
        << "━━━━━━━━━━━━━━━━━━━━\n"
        << "⚠️ <b>Status  :</b> Kemungkinan WEBSHELL!\n"
        << "📄 <b>File    :</b> <code>" << esc(fpath)       << "</code>\n"
        << "🗑 <b>Aksi    :</b> "        << esc(del_status)  << "\n\n"
        << "⏰ <b>Waktu   :</b> " << now_str() << "\n"
        << "🔗 " << cfg.panel_domain << "\n\n"
        << "❗ Segera periksa server Anda!";
    send_tg(msg.str());
}

// ── [BARU] Integrity: file PHP core dimodifikasi ──────────────
void send_tg_integrity_modified(const std::string &fpath,
                                  const std::string &old_hash,
                                  const std::string &new_hash) {
    std::ostringstream msg;
    msg << "🚨 <b>[SENTINELX] FILE PANEL DIMODIFIKASI!</b>\n"
        << "━━━━━━━━━━━━━━━━━━━━\n"
        << "⚠️ <b>Status  :</b> File PHP core berubah!\n"
        << "📄 <b>File    :</b> <code>" << esc(fpath) << "</code>\n"
        << "🔑 <b>Hash Lama:</b> <code>" << esc(old_hash.substr(0, 16)) << "...</code>\n"
        << "🔑 <b>Hash Baru:</b> <code>" << esc(new_hash.substr(0, 16)) << "...</code>\n\n"
        << "⏰ <b>Waktu   :</b> " << now_str() << "\n"
        << "🔗 " << cfg.panel_domain << "\n\n"
        << "❗ Kemungkinan PHP-bridge atau backdoor ditanam!";
    send_tg(msg.str());
}

// ── [BARU] Self protection alert ─────────────────────────────
void send_tg_selfguard_alert(const std::string &event_type,
                               const std::string &detail) {
    std::ostringstream msg;
    msg << "🔴 <b>[SENTINELX] PERINGATAN KRITIS!</b>\n"
        << "━━━━━━━━━━━━━━━━━━━━\n"
        << "🛡 <b>Event   :</b> " << esc(event_type) << "\n"
        << "📋 <b>Detail  :</b>\n" << esc(detail)    << "\n\n"
        << "⏰ <b>Waktu   :</b> " << now_str() << "\n"
        << "🔗 " << cfg.panel_domain << "\n\n"
        << "❗ Attacker mungkin mencoba menonaktifkan SentinelX!";
    send_tg(msg.str());
}

// ── Startup notification ──────────────────────────────────────
void send_tg_startup() {
    std::string kw;
    for (int i = 0; i < (int)DANGER_KEYWORDS.size() && i < 6; i++)
        kw += DANGER_KEYWORDS[i] + (i < 5 ? ", " : "...");

    send_tg(
        "✅ <b>[SENTINELX] DAEMON AKTIF</b>\n"
        "━━━━━━━━━━━━━━━━━━━━\n"
        "📁 Volumes   : <code>" + cfg.volumes_path + "</code>\n"
        "🏠 Panel     : <code>" + cfg.panel_path   + "</code>\n"
        "🚨 Disk bomb : file &gt; <b>" + std::to_string(cfg.max_size_mb) + " MB</b>\n"
        "🔍 Content scan: <b>" + std::string(cfg.scan_file_content ? "Aktif" : "Nonaktif") + "</b>\n"
        "🛡 Integrity : <b>" + std::string(cfg.integrity_enabled ? "Aktif" : "Nonaktif") + "</b>\n"
        "📊 Rate limit: <b>" + std::to_string(cfg.threshold_accounts) + " akun / "
        + std::to_string(cfg.threshold_servers) + " server dalam "
        + std::to_string(cfg.window_seconds) + " detik</b>\n"
        "💣 Mass delete: threshold <b>" + std::to_string(cfg.mass_delete_threshold) + "</b>\n"
        "🦠 Ransomware detect: <b>" + std::string(cfg.ransomware_detection ? "Aktif" : "Nonaktif") + "</b>\n"
        "🗄 DB Guard: <b>" + std::string(cfg.db_guard_enabled ? "Aktif" : "Nonaktif") + "</b>\n"
        "🔒 Self guard: <b>" + std::string(cfg.self_protect_enabled ? "Aktif" : "Nonaktif") + "</b>\n"
        "⏰ " + now_str());
}

void send_tg_ransomware(const std::string &fpath, const std::string &reason,
                        bool suspended,
                        const std::string &uuid, const std::string &name,
                        const std::string &node, const std::string &username,
                        const std::string &email, const std::string &created_at) {
    std::string msg =
        "🔴 <b>⚠️ RANSOMWARE TERDETEKSI ⚠️</b>\n"
        "━━━━━━━━━━━━━━━━━━━━\n"
        "📋 <b>Alasan :</b> " + esc(reason) + "\n"
        "📄 <b>File   :</b> <code>" + esc(fpath) + "</code>\n"
        "━━━━━━━━━━━━━━━━━━━━\n"
        "🖥 <b>Server :</b> " + esc(name.empty() ? "unknown" : name) + "\n"
        "🔑 <b>UUID   :</b> <code>" + esc(uuid) + "</code>\n"
        "📡 <b>Node   :</b> " + esc(node) + "\n"
        "👤 <b>User   :</b> " + esc(username) + "\n"
        "📧 <b>Email  :</b> " + esc(email) + "\n"
        "📅 <b>Dibuat :</b> " + esc(created_at) + "\n"
        "━━━━━━━━━━━━━━━━━━━━\n"
        "🔒 <b>Suspend:</b> " + std::string(suspended ? "✅ YA" : "❌ TIDAK") + "\n"
        "⏰ " + now_str();
    send_tg(msg);
}

void send_tg_danger_confirm(const std::string &fpath, const std::string &reason,
                             const std::string &del_status,
                             const std::string &uuid, const std::string &name,
                             const std::string &node, const std::string &username,
                             const std::string &email, const std::string &created_at,
                             const std::string &action_id) {
    // Escape semua field
    auto e_ = [](const std::string &s) {
        std::string out;
        for (char c : s) {
            if      (c == '<') out += "&lt;";
            else if (c == '>') out += "&gt;";
            else if (c == '&') out += "&amp;";
            else               out += c;
        }
        return out;
    };
    auto j_ = [](const std::string &s) {
        std::string out;
        for (char c : s) {
            if      (c == '"')  out += "\\\"";
            else if (c == '\\') out += "\\\\";
            else if (c == '\n') out += "\\n";
            else                out += c;
        }
        return out;
    };

    std::string text =
        "⚠️ <b>SCRIPT BERBAHAYA DITEMUKAN</b>\\n"
        "━━━━━━━━━━━━━━━━━━━━\\n"
        "📋 <b>Alasan :</b> " + j_(e_(reason)) + "\\n"
        "📄 <b>File   :</b> <code>" + j_(e_(fpath)) + "</code>\\n"
        "🗑 <b>Hapus  :</b> " + j_(e_(del_status)) + "\\n"
        "━━━━━━━━━━━━━━━━━━━━\\n"
        "🖥 <b>Server :</b> " + j_(e_(name.empty() ? "unknown" : name)) + "\\n"
        "🔑 <b>UUID   :</b> <code>" + j_(e_(uuid)) + "</code>\\n"
        "📡 <b>Node   :</b> " + j_(e_(node)) + "\\n"
        "👤 <b>User   :</b> " + j_(e_(username)) + "\\n"
        "📧 <b>Email  :</b> " + j_(e_(email)) + "\\n"
        "📅 <b>Dibuat :</b> " + j_(e_(created_at)) + "\\n"
        "━━━━━━━━━━━━━━━━━━━━\\n"
        "❓ <b>Suspend server ini?</b>\\n"
        "⏰ " + j_(now_str());

    // Inline keyboard: tombol suspend & biarkan
    // callback_data format: "suspend:<uuid>:<action_id>" atau "ignore:<uuid>:<action_id>"
    std::string keyboard =
        "[[{\"text\":\"✅ Suspend\","
        "\"callback_data\":\"suspend:" + uuid + ":" + action_id + "\"},"
        "{\"text\":\"❌ Biarkan\","
        "\"callback_data\":\"ignore:" + uuid + ":" + action_id + "\"}]]";

    std::string body =
        "{\"chat_id\":\"" + cfg.telegram_owner_id + "\","
        "\"text\":\"" + text + "\","
        "\"parse_mode\":\"HTML\","
        "\"reply_markup\":{\"inline_keyboard\":" + keyboard + "}}";

    http_post(
        "https://api.telegram.org/bot" + cfg.telegram_token + "/sendMessage",
        body,
        {"Content-Type: application/json"});
}
