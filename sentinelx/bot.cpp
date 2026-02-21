#include "bot.h"
#include "config.h"
#include "http.h"
#include "logger.h"
#include "tracking.h"
#include "disk_protect.h"
#include "api.h"

#include <sstream>
#include <functional>
#include <thread>
#include <chrono>
#include <deque>
#include <set>
#include <mutex>
#include <ctime>

// ── Track action_id yang sudah diproses (hindari double-tap) ─
static std::set<std::string> handled_actions;
static std::mutex handled_mutex;

// ── Thread-safe recent threats log (max 20) ───────────────────
static std::deque<std::string> threat_log;
static std::mutex threat_log_mutex;

void bot_log_threat(const std::string &msg) {
    std::lock_guard<std::mutex> lk(threat_log_mutex);
    std::time_t t = std::time(nullptr);
    char ts[20]; std::strftime(ts, sizeof(ts), "%m-%d %H:%M:%S", std::localtime(&t));
    threat_log.push_front(std::string(ts) + " | " + msg);
    if (threat_log.size() > 20) threat_log.pop_back();
}

// ── JSON helper ───────────────────────────────────────────────
static std::string jget(const std::string &json, const std::string &key) {
    std::string needle = "\"" + key + "\":";
    auto p = json.find(needle);
    if (p == std::string::npos) return "";
    p += needle.size();
    while (p < json.size() && (json[p]==' '||json[p]=='\t')) p++;
    if (p >= json.size()) return "";
    if (json[p] == '"') {
        p++;
        std::string out;
        while (p < json.size() && json[p] != '"') {
            if (json[p]=='\\' && p+1<json.size()) { p++; out+=json[p]; }
            else out += json[p];
            p++;
        }
        return out;
    }
    auto e = json.find_first_of(",}\n", p);
    return json.substr(p, e==std::string::npos ? std::string::npos : e-p);
}

// ── Kirim pesan ke owner ──────────────────────────────────────
static void tg_send(const std::string &text) {
    if (cfg.telegram_token.empty() || cfg.telegram_owner_id.empty()) return;
    std::string safe;
    for (char c : text) {
        if      (c=='"')  safe += "\\\"";
        else if (c=='\\') safe += "\\\\";
        else if (c=='\n') safe += "\\n";
        else              safe += c;
    }
    http_post(
        "https://api.telegram.org/bot" + cfg.telegram_token + "/sendMessage",
        "{\"chat_id\":\""  + cfg.telegram_owner_id + "\","
        "\"text\":\""      + safe + "\","
        "\"parse_mode\":\"HTML\"}",
        {"Content-Type: application/json"});
}

// ── Handle perintah masuk ─────────────────────────────────────
static void handle(const std::string &chat_id, const std::string &text,
                   std::atomic<bool> &running) {
    if (chat_id != cfg.telegram_owner_id) return;

    // /start  /help
    if (text == "/start" || text == "/help") {
        tg_send(
            "🛡 <b>SentinelX — Pterodactyl Guard</b>\n"
            "━━━━━━━━━━━━━━━━━━━━\n\n"
            "🔍 <b>Proteksi aktif:</b>\n"
            "• Disk bomb (inotify real-time)\n"
            "• Content scan (PHP/bash — webshell, php-bridge)\n"
            "• Ransomware detection (enkripsi massal, ransom note)\n"
            "• Panel integrity (modifikasi .env / file PHP)\n"
            "• DB Guard (admin pw change langsung di MySQL)\n"
            "• Rate limit (spam akun/server)\n"
            "• Mass delete detection\n"
            "• Self guard (immutable binary + watchdog)\n\n"
            "━━━━━━━━━━━━━━━━━━━━\n"
            "📋 <b>Perintah:</b>\n\n"
            "<code>/status</code>    — konfigurasi aktif\n"
            "<code>/threats</code>   — 20 ancaman terakhir\n"
            "<code>/scan</code>      — disk scan manual\n"
            "<code>/lockdown</code>  — 🔴 suspend SEMUA server (darurat)\n\n"
            "<code>/tracking</code>         — pohon semua user & server\n"
            "<code>/tracking stats</code>   — statistik ringkas\n"
            "<code>/tracking user &lt;nama&gt;</code>\n"
            "<code>/tracking server &lt;nama&gt;</code>");
        return;
    }

    // /status
    if (text == "/status") {
        tg_send(
            "⚙️ <b>Status SentinelX</b>\n"
            "━━━━━━━━━━━━━━━━━━━━\n"
            "📁 Volumes   : <code>" + cfg.volumes_path + "</code>\n"
            "🏠 Panel     : <code>" + cfg.panel_path + "</code>\n"
            "🌐 Domain    : <code>" + cfg.panel_domain + "</code>\n\n"
            "🗑 Auto-delete berbahaya  : " + (cfg.delete_dangerous ? "✅" : "❌") + "\n"
            "⚡ Suspend script otomatis : " + (cfg.suspend_on_danger
                ? "✅ Langsung" : "❓ Tanya via Telegram") + "\n"
            "🔴 Suspend file besar           : ✅ Selalu langsung\n"
            "🔍 Content scan           : " + (cfg.scan_file_content ? "✅" : "❌") + "\n"
            "🦠 Ransomware detection   : " + (cfg.ransomware_detection ? "✅" : "❌") + "\n"
            "🧬 Integrity monitor      : " + (cfg.integrity_enabled ? "✅" : "❌") + "\n"
            "🗄 DB Guard               : " + (cfg.db_guard_enabled ? "✅" : "❌") + "\n"
            "🔒 Self guard             : " + (cfg.self_protect_enabled ? "✅" : "❌") + "\n\n"
            "⚡ Rate limit: " +
                std::to_string(cfg.threshold_accounts) + " akun / " +
                std::to_string(cfg.threshold_servers)  + " server per " +
                std::to_string(cfg.window_seconds) + "s\n"
            "🗑 Mass delete threshold  : " + std::to_string(cfg.mass_delete_threshold) + "\n"
            "🦠 Ransom mod threshold   : " + std::to_string(cfg.ransomware_mod_threshold)
                + " file/" + std::to_string(cfg.ransomware_window_sec) + "s\n"
            "🗄 DB Guard poll          : " + std::to_string(cfg.db_guard_poll_sec) + "s");
        return;
    }

    // /threats — tampilkan 20 ancaman terakhir
    if (text == "/threats") {
        std::lock_guard<std::mutex> lk(threat_log_mutex);
        if (threat_log.empty()) {
            tg_send("✅ Tidak ada ancaman yang tercatat sejak daemon aktif.");
            return;
        }
        std::string msg = "⚠️ <b>20 Ancaman Terakhir</b>\n━━━━━━━━━━━━━━━━━━━━\n";
        for (auto &t : threat_log)
            msg += "<code>" + t + "</code>\n";
        tg_send(msg);
        return;
    }

    // /scan — trigger disk scan manual
    if (text == "/scan") {
        tg_send("⏳ Disk scan manual dimulai...");
        std::thread([]() {
            disk_protect_scan();
            wlog("[BOT] Disk scan manual selesai.");
        }).detach();
        tg_send("✅ Disk scan berjalan di background. Ancaman baru akan dikirim via alert.");
        return;
    }

    // /lockdown — suspend SEMUA server (mode darurat)
    if (text == "/lockdown") {
        tg_send(
            "🔴 <b>LOCKDOWN MODE</b>\n"
            "━━━━━━━━━━━━━━━━━━━━\n"
            "Perintah ini akan <b>SUSPEND SEMUA SERVER</b> di panel.\n\n"
            "Ketik <code>/lockdown confirm</code> untuk lanjutkan.");
        return;
    }

    if (text == "/lockdown confirm") {
        if (cfg.api_application.empty()) {
            tg_send("❌ API key tidak dikonfigurasi. Lockdown tidak bisa dilakukan.");
            return;
        }
        tg_send("⏳ Memulai lockdown — suspend semua server...");
        std::thread([]() {
            // Gunakan API untuk suspend semua server
            auto servers = api_get_recent_servers(500);
            int count = 0;
            for (auto &s : servers) {
                api_suspend(s.uuid); count++;
            }
            // Kirim notif selesai
            std::string safe = "🔴 <b>LOCKDOWN SELESAI</b>\\n"
                               + std::to_string(count) + " server di-suspend.";
            http_post(
                "https://api.telegram.org/bot" + cfg.telegram_token + "/sendMessage",
                "{\"chat_id\":\"" + cfg.telegram_owner_id + "\","
                "\"text\":\"" + safe + "\","
                "\"parse_mode\":\"HTML\"}",
                {"Content-Type: application/json"});
            wlog("[LOCKDOWN] " + std::to_string(count) + " server di-suspend.");
        }).detach();
        return;
    }

    // /tracking
    if (text == "/tracking" || text == "/tracking userpanel") {
        tg_send("⏳ Mengambil data lineage...");
        tracking_refresh();
        std::string tree = tracking_get_tree();
        if (tree.size() > 3800)
            tree = tree.substr(0, 3800) +
                   "\n\n⚠️ <i>Output dipotong. Gunakan /tracking user &lt;nama&gt; untuk detail.</i>";
        tg_send(tree);
        return;
    }

    if (text == "/tracking stats") {
        tracking_refresh();
        tg_send(tracking_get_stats());
        return;
    }

    if (text.rfind("/tracking user ", 0) == 0) {
        std::string q = text.substr(15);
        if (q.empty()) { tg_send("❌ <code>/tracking user &lt;username/email/id&gt;</code>"); return; }
        tg_send("⏳ Mencari user...");
        tracking_refresh();
        tg_send(tracking_get_user_info(q));
        return;
    }

    if (text.rfind("/tracking server ", 0) == 0) {
        std::string q = text.substr(17);
        if (q.empty()) { tg_send("❌ <code>/tracking server &lt;nama server&gt;</code>"); return; }
        tg_send("⏳ Mencari server...");
        tracking_refresh();
        tg_send(tracking_get_server_info(q));
        return;
    }

    tg_send("❓ Perintah tidak dikenal. Ketik /help");
}

// ── Answer callback query (hilangkan loading di tombol) ─────
static void answer_callback(const std::string &callback_query_id, const std::string &text = "") {
    std::string safe;
    for (char c : text) {
        if (c=='"') safe += "\\\"";
        else if (c=='\\') safe += "\\\\";
        else safe += c;
    }
    http_post(
        "https://api.telegram.org/bot" + cfg.telegram_token + "/answerCallbackQuery",
        "{\"callback_query_id\":\"" + callback_query_id + "\""
        + (safe.empty() ? "" : ",\"text\":\"" + safe + "\"") + "}",
        {"Content-Type: application/json"});
}

// ── Edit teks pesan setelah tombol dipencet ───────────────────
static void edit_message(const std::string &chat_id, const std::string &msg_id,
                          const std::string &new_text) {
    std::string safe;
    for (char c : new_text) {
        if      (c=='"')  safe += "\\\"";
        else if (c=='\\') safe += "\\\\";
        else if (c=='\n') safe += "\\n";
        else               safe += c;
    }
    http_post(
        "https://api.telegram.org/bot" + cfg.telegram_token + "/editMessageText",
        "{\"chat_id\":\"" + chat_id + "\","
        "\"message_id\":" + msg_id + ","
        "\"text\":\"" + safe + "\","
        "\"parse_mode\":\"HTML\"}" ,
        {"Content-Type: application/json"});
}

// ── Handle callback query (tombol inline Suspend/Biarkan) ─────
static void handle_callback(const std::string &callback_query_id,
                              const std::string &from_id,
                              const std::string &chat_id,
                              const std::string &msg_id,
                              const std::string &data) {
    // Hanya owner yang bisa pencet tombol
    if (from_id != cfg.telegram_owner_id) {
        answer_callback(callback_query_id, "❌ Bukan owner!");
        return;
    }

    // Parse data: "suspend:<uuid>:<action_id>" atau "ignore:<uuid>:<action_id>"
    auto p1 = data.find(':');
    auto p2 = data.find(':', p1 + 1);
    if (p1 == std::string::npos || p2 == std::string::npos) return;

    std::string action    = data.substr(0, p1);
    std::string uuid      = data.substr(p1 + 1, p2 - p1 - 1);
    std::string action_id = data.substr(p2 + 1);

    // Hindari double-process (kalau owner pencet dua kali)
    {
        std::lock_guard<std::mutex> lk(handled_mutex);
        if (handled_actions.count(action_id)) {
            answer_callback(callback_query_id, "⚠️ Sudah diproses.");
            return;
        }
        handled_actions.insert(action_id);
        // Bersihkan set kalau sudah terlalu besar
        if (handled_actions.size() > 500) handled_actions.clear();
    }

    if (action == "suspend") {
        answer_callback(callback_query_id, "⏳ Menyuspend server...");
        bool ok = !cfg.api_application.empty();
        if (ok) api_suspend(uuid);
        std::string result = ok
            ? "✅ <b>Server di-SUSPEND!</b>\nUUID: <code>" + uuid + "</code>"
            : "❌ Gagal suspend — cek API key.\nUUID: <code>" + uuid + "</code>";
        edit_message(chat_id, msg_id, result);
        wlog("[BOT] Suspend via tombol: uuid=" + uuid + " ok=" + std::to_string(ok));

    } else if (action == "ignore") {
        answer_callback(callback_query_id, "Diabaikan.");
        edit_message(chat_id, msg_id,
            "❎ <b>Diabaikan oleh owner.</b>\n"
            "UUID: <code>" + uuid + "</code>\n"
            "<i>Server tidak di-suspend.</i>");
        wlog("[BOT] Ignore via tombol: uuid=" + uuid);
    }
}

// ── Helper: extract JSON object block mulai dari pos ──────────
static std::string extract_obj(const std::string &s, size_t start) {
    auto brace = s.find('{', start);
    if (brace == std::string::npos) return "";
    int depth = 0; size_t end = brace;
    for (; end < s.size(); end++) {
        if (s[end]=='{') depth++;
        else if (s[end]=='}') { depth--; if (depth==0) break; }
    }
    return s.substr(brace, end - brace + 1);
}

// ── Polling loop — handle message & callback_query ────────────
static int tg_get_updates(int offset,
    std::function<void(const std::string&, const std::string&)> on_msg,
    std::function<void(const std::string&, const std::string&,
                       const std::string&, const std::string&,
                       const std::string&)> on_callback) {
    std::string resp = http_get(
        "https://api.telegram.org/bot" + cfg.telegram_token +
        "/getUpdates?timeout=10&offset=" + std::to_string(offset));
    if (resp.find("\"ok\":true") == std::string::npos) return offset;

    size_t pos = 0;
    while (true) {
        auto uid_p = resp.find("\"update_id\":", pos);
        if (uid_p == std::string::npos) break;
        uid_p += 12;
        auto uid_e = resp.find_first_of(",}", uid_p);
        int uid = std::stoi(resp.substr(uid_p, uid_e - uid_p));
        auto nxt_u = resp.find("\"update_id\":", uid_e);

        // ── Proses message biasa ───────────────────────────────
        auto msg_p = resp.find("\"message\":", uid_p);
        if (msg_p != std::string::npos &&
            (nxt_u == std::string::npos || msg_p < nxt_u)) {
            std::string mj = extract_obj(resp, msg_p + 10);
            if (!mj.empty()) {
                std::string text = jget(mj, "text");
                std::string cid;
                auto cp = mj.find("\"chat\":");
                if (cp != std::string::npos) {
                    std::string cj = extract_obj(mj, cp + 7);
                    cid = jget(cj, "id");
                }
                if (!text.empty() && !cid.empty())
                    on_msg(cid, text);
            }
        }

        // ── Proses callback_query (tombol inline) ──────────────
        auto cbq_p = resp.find("\"callback_query\":", uid_p);
        if (cbq_p != std::string::npos &&
            (nxt_u == std::string::npos || cbq_p < nxt_u)) {
            std::string cj = extract_obj(resp, cbq_p + 17);
            if (!cj.empty()) {
                std::string cbq_id  = jget(cj, "id");
                std::string data    = jget(cj, "data");
                // from.id
                std::string from_id;
                auto fp = cj.find("\"from\":");
                if (fp != std::string::npos) {
                    std::string fj = extract_obj(cj, fp + 7);
                    from_id = jget(fj, "id");
                }
                // message.chat.id dan message.message_id
                std::string chat_id, msg_id;
                auto mp = cj.find("\"message\":");
                if (mp != std::string::npos) {
                    std::string mj2 = extract_obj(cj, mp + 10);
                    msg_id = jget(mj2, "message_id");
                    auto cp2 = mj2.find("\"chat\":");
                    if (cp2 != std::string::npos) {
                        std::string chatj = extract_obj(mj2, cp2 + 7);
                        chat_id = jget(chatj, "id");
                    }
                }
                if (!cbq_id.empty() && !data.empty())
                    on_callback(cbq_id, from_id, chat_id, msg_id, data);
            }
        }

        offset = uid + 1;
        pos = uid_e;
    }
    return offset;
}

void bot_run(std::atomic<bool> &running) {
    wlog("[BOT] Polling aktif. Owner chat_id=" + cfg.telegram_owner_id);
    int offset = 0;
    while (running) {
        try {
            offset = tg_get_updates(offset,
                [&](const std::string &cid, const std::string &text) {
                    handle(cid, text, running);
                },
                [&](const std::string &cbq_id, const std::string &from_id,
                    const std::string &chat_id, const std::string &msg_id,
                    const std::string &data) {
                    handle_callback(cbq_id, from_id, chat_id, msg_id, data);
                });
        } catch (std::exception &ex) {
            wlog("[BOT] Error: " + std::string(ex.what()));
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    wlog("[BOT] Berhenti.");
}
