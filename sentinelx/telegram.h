#pragma once
#include <string>
#include <vector>

struct SpamEvent {
    std::string              type;
    int                      count;
    int                      window_sec;
    int                      deleted;
    std::vector<std::string> ids;
    std::vector<std::string> names;
    std::vector<std::string> emails;
};

void send_tg(const std::string &msg);

// Disk bomb (.bin besar)
void send_tg_disk_bomb(const std::string &fpath, long long mb, long long kb,
                       const std::string &del_status, bool suspended,
                       const std::string &uuid, const std::string &name,
                       const std::string &node, const std::string &username,
                       const std::string &email, const std::string &created_at);

// Script berbahaya (nama file atau isi file)
void send_tg_danger_script(const std::string &fpath, const std::string &reason,
                            const std::string &del_status, bool suspended,
                            const std::string &uuid, const std::string &name,
                            const std::string &node, const std::string &username,
                            const std::string &email, const std::string &created_at);

// Spam akun/server / mass delete
void send_tg_spam_alert(const SpamEvent &ev);

// [BARU] Integrity: file PHP baru di panel
void send_tg_integrity_new(const std::string &fpath, const std::string &del_status);

// [BARU] Integrity: file PHP core panel dimodifikasi
void send_tg_integrity_modified(const std::string &fpath,
                                  const std::string &old_hash,
                                  const std::string &new_hash);

// [BARU] Self protection alert
void send_tg_selfguard_alert(const std::string &event_type,
                               const std::string &detail);

// Startup notif
void send_tg_startup();

// Kirim alert script berbahaya + tombol inline Suspend/Biarkan
// action_id dipakai untuk matching callback dari bot
void send_tg_danger_confirm(const std::string &fpath, const std::string &reason,
                             const std::string &del_status,
                             const std::string &uuid, const std::string &name,
                             const std::string &node, const std::string &username,
                             const std::string &email, const std::string &created_at,
                             const std::string &action_id);

// Ransomware detection alert
void send_tg_ransomware(const std::string &fpath, const std::string &reason,
                        bool suspended,
                        const std::string &uuid, const std::string &name,
                        const std::string &node, const std::string &username,
                        const std::string &email, const std::string &created_at);
