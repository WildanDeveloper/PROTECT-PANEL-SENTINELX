#pragma once
#include <string>
#include <vector>

// ── Keyword berbahaya di NAMA file ────────────────────────────
const std::vector<std::string> DANGER_KEYWORDS = {
    // Kudeta / wipe panel
    "kudet", "kudeta", "kudetatotal",
    "killpanel", "kill_panel", "kill-panel", "kilpanel", "panelkill",
    "wipepanel", "wipe_panel", "panelwipe",
    "killserver", "kill_server", "kill-server",
    // Tools dari screenshot
    "ranggacloud", "hackvps", "loginvps", "logoutvps",
    "phpbridge", "php-bridge", "php_bridge",
    "kudetaTotal", "masswipe",
    // DDoS / flood
    "ddos", "d-dos", "flood", "flooder", "stresser", "udpflood", "tcpflood",
    // Malware
    "botnet", "bypass",
    "exploit", "payload",
    "backdoor", "back_door",
    "webshell", "rootkit",
    "cryptominer", "miner", "xmrig", "stratum",
    "attack", "crasher", "nuker",
    "deface", "defacer",
    // Ransomware
    "ransomware", "encryptor", "cryptolocker",
    "decrypt_files", "how_to_decrypt", "readme_decrypt",
    "ransom_note", "your_files",
    // Credential stealer
    "stealer", "credential", "keylog", "passlog",
    // Reverse shell / tunnel
    "revshell", "reverse_shell", "bindshell",
    "ngrok", "frpc", "chisel",
    // Scanner
    "portscan", "netscan", "masscan",
    // Proxy abuse
    "socks5", "proxychains",
};

// ── Ekstensi ransomware ───────────────────────────────────────
const std::vector<std::string> RANSOM_EXTENSIONS = {
    ".enc", ".locked", ".encrypted", ".crypted",
    ".crypt", ".ransom", ".wncry", ".wcry",
    ".RANSOM", ".LOCKED", ".ENC",
};

// ── Nama file ransom note ─────────────────────────────────────
const std::vector<std::string> RANSOM_NOTE_NAMES = {
    "how_to_decrypt", "readme_decrypt", "decrypt_files",
    "ransom_note", "your_files_are_encrypted",
    "help_decrypt", "recovery_key", "read_me_now",
    "!readme!", "_readme_", "!!!_recover_",
    "how_to_restore", "restore_files",
};

// ── Pattern berbahaya di ISI file (PHP) ───────────────────────
const std::vector<std::string> PHP_DANGEROUS_PATTERNS = {
    // eval obfuscation
    "eval(base64_decode",
    "eval(gzinflate",
    "eval(str_rot13",
    "eval(gzuncompress",
    "eval(gzdecode",
    "eval(hex2bin",
    // assert exec
    "assert($_",
    "assert(base64",
    "assert(str_rot13",
    // preg_replace /e modifier (code exec)
    "preg_replace(\"/./e",
    "preg_replace('/.*/e",
    // create_function (deprecated code exec)
    "create_function(",
    // Reflection (bypass)
    "ReflectionFunction",
    // call_user_func with input
    "call_user_func($_",
    "call_user_func_array($_",
    // upload
    "$_FILES[",
    "move_uploaded_file",
    // system exec from input
    "passthru(",
    "shell_exec(",
    "popen(",
    "proc_open(",
    "pcntl_exec(",
    "system($_",  "exec($_",
    "system($_POST", "exec($_POST",
    "system($_GET",  "exec($_GET",
    "system($_REQUEST", "exec($_REQUEST",
    "system($_COOKIE", "exec($_COOKIE",
    // PHP stream wrappers (PHP-bridge)
    "php://input",
    "expect://",
    "data://text/plain;base64",
    // DB manipulation langsung
    "DELETE FROM users",
    "DELETE FROM servers",
    "UPDATE users SET password",
    "UPDATE users SET root_admin",
    "DROP DATABASE",
    "TRUNCATE TABLE",
    // Well-known webshells
    "FilesMan",
    "WSO Shell",
    "b374k",
    "r57shell",
    "c99shell",
    "PHP Shell",
    "IndoXploit",
    "Alfa Shell",
    "GhostShell",
    "RanggaCloud",
    "ranggacloud",
    "Hackable",
    // Hacking tool patterns (dari screenshot image 3)
    "/hackvps",
    "/loginvps",
    "/kudetatotal",
    "/deface",
    "php-bridge",
    "kudetatotal",
};

// ── Pattern berbahaya di ISI file (Bash/Script/Binary text) ───
const std::vector<std::string> BASH_DANGEROUS_PATTERNS = {
    // Wipe sistem
    "rm -rf /var/www",
    "rm -rf /etc/pterodactyl",
    "rm -rf /var/lib/pterodactyl",
    "rm -rf /root",
    "rm -rf /*",
    // DB wipe
    "DROP DATABASE",
    "DROP TABLE",
    "TRUNCATE TABLE",
    "DELETE FROM users",
    "DELETE FROM servers",
    "UPDATE users SET password",
    // Chmod berbahaya
    "chmod 777 /",
    // Pipe exec
    "curl|bash", "curl | bash",
    "wget|bash", "wget | bash",
    "curl|sh",   "curl | sh",
    "wget|sh",   "wget | sh",
    "|bash -s",  "| bash -s",
    "|sh -s",    "| sh -s",
    "base64 -d|bash", "base64 -d | bash",
    "base64 -d|sh",   "base64 -d | sh",
    // Reverse shells
    "python -c 'import socket",
    "python3 -c 'import socket",
    "bash -i >& /dev/tcp",
    "0>&1",
    "nc -e /bin/bash",
    "nc -e /bin/sh",
    "nc -lvp",
    // SSH backdoor
    "echo >> /root/.ssh/authorized_keys",
    ">> ~/.ssh/authorized_keys",
    // Ransomware patterns
    "openssl enc -aes",
    "openssl enc -des",
    "gpg --symmetric",
    "gpg -c ",
    "find / -type f -name",    // ransomware biasa lakukan ini sebelum enkripsi
    // Attacker-specific (dari screenshot)
    "/kudetatotal",
    "/deface",
    "php-bridge",
    "ranggacloud",
    "/hackvps",
    "/loginvps",
    // Firewall flush
    "iptables -F",
    "ufw disable",
    // Remove immutable
    "chattr -i /usr/local/bin",
    // cron backdoor
    "crontab -",
    // Data exfil
    "mysqldump",
    // Network pivot
    "proxychains",
    // ── Anti-Exploit: Disk Bomb / Kudeta / Metadata Scan ─────
    "dd if=/dev/zero",              // disk bomb
    "dd if=/dev/urandom",           // disk bomb variant
    "| chpasswd",                   // ganti password root
    "echo \"root:",               // ganti password root via echo
    "169.254.169.254",              // scan metadata cloud DO/AWS/GCP
    "password_hash(",               // generate hash password baru
    "DB_USERNAME",                  // baca kredensial .env
    "DB_PASSWORD",                  // baca kredensial .env
    "SAMPEL_SAMPAH",                // folder disk bomb
    "trash_",                       // file disk bomb pattern
    "final_crash",                  // file disk bomb
    "runKillPanel",                 // disk bomb function
    "runKudeta",                    // kudeta total
    "kamikaze",                     // known exploit tool
};

struct Config {
    // ── Telegram ──────────────────────────────────────────────
    std::string telegram_token;
    std::string telegram_owner_id;

    // ── Panel ─────────────────────────────────────────────────
    std::string panel_domain;
    std::string api_application;
    std::string api_client;

    // ── Disk Protect ──────────────────────────────────────────
    std::string volumes_path          = "/var/lib/pterodactyl/volumes";
    long long   max_size_mb           = 1024;  // 1GB default
    bool        delete_dangerous      = false;
    bool        suspend_on_danger     = false;  // suspend server saat script berbahaya ditemukan
                                                // false = kirim alert + tombol Y/N ke Telegram
    bool        scan_file_content     = true;
    int         content_scan_limit_kb = 512;

    // ── Ransomware Detection ──────────────────────────────────
    bool ransomware_detection         = true;
    int  ransomware_mod_threshold     = 15;  // X file termodifikasi dalam window = alert
    int  ransomware_window_sec        = 30;

    // ── Panel File Integrity ──────────────────────────────────
    std::string panel_path              = "/var/www/pterodactyl";
    bool        integrity_enabled       = true;
    bool        integrity_delete_new_php = true;

    // ── DB Guard ──────────────────────────────────────────────
    bool db_guard_enabled             = true;
    int  db_guard_poll_sec            = 10;  // cek admin pw tiap N detik

    // ── Rate Limit Protect ────────────────────────────────────
    int threshold_accounts  = 5;
    int threshold_servers   = 5;
    int window_seconds      = 10;
    int poll_interval_sec   = 3;

    // ── Mass Delete Detection ─────────────────────────────────
    int  mass_delete_threshold = 5;
    bool mass_delete_enabled   = true;

    // ── Self Protection ───────────────────────────────────────
    bool        self_protect_enabled  = true;
    std::string self_binary_path      = "/usr/local/bin/sentinelx";

    // ── Log ───────────────────────────────────────────────────
    std::string log_file = "/var/log/sentinelx.log";
};

extern Config cfg;
extern const std::string CONFIG_FILE;

bool load_cfg();
void save_cfg();
void setup_wizard();
