# 🛡️ SentinelX v2.0

**Security daemon untuk Pterodactyl Panel** — proteksi real-time dari ancaman internal maupun eksternal, dikendalikan lewat Telegram.

![C++17](https://img.shields.io/badge/C%2B%2B-17-blue?style=flat-square&logo=c%2B%2B)
![Platform](https://img.shields.io/badge/Platform-Linux-informational?style=flat-square&logo=linux)
![License](https://img.shields.io/badge/License-Private-red?style=flat-square)
![Status](https://img.shields.io/badge/Status-Active-brightgreen?style=flat-square)

---

## ⚡ Quick Install (1 Perintah)

> **Jalankan sebagai root di VPS / server Pterodactyl kamu:**

```bash
bash <(curl -sSL https://raw.githubusercontent.com/WildanDeveloper/PROTECT-PANEL-SENTINELX/main/install.sh)
```

Script ini akan otomatis:
- Install semua dependency yang dibutuhkan
- Download & compile source code
- Setup systemd service
- Menjalankan setup wizard

---

## 📥 Manual Install

Kalau prefer install manual:

```bash
# 1. Install dependency
apt install -y build-essential libcurl4-openssl-dev libssl-dev git

# 2. Clone repo
git clone https://github.com/WildanDeveloper/PROTECT-PANEL-SENTINELX.git
cd PROTECT-PANEL-SENTINELX

# 3. Build
make -j$(nproc)

# 4. Install & jalankan sebagai service
sudo make install

# 5. Setup konfigurasi (wizard otomatis muncul saat pertama kali)
sudo sentinelx
```

---

## 🌟 Fitur Utama

### 🔒 Disk Protection
- Monitoring real-time via **inotify** pada direktori volumes Pterodactyl
- Deteksi file berbahaya berdasarkan **nama** (ddos, botnet, exploit, webshell, miner, dll.)
- Scanning **isi file** PHP & Bash untuk mendeteksi pola berbahaya (eval obfuscation, reverse shell, webshell populer, dll.)
- Auto-delete file berbahaya (configurable) — atau mode **alert + tombol konfirmasi Y/N** via Telegram
- Auto-suspend server saat file berbahaya ditemukan (configurable)
- Alert Telegram instan saat ancaman terdeteksi

### 🦠 Ransomware Detection
- Deteksi ekstensi file terenkripsi khas ransomware (`.enc`, `.locked`, `.wncry`, dll.)
- Deteksi ransom note berdasarkan nama file (`how_to_decrypt`, `readme_decrypt`, dll.)
- Alert saat jumlah file termodifikasi melebihi threshold dalam window waktu tertentu

### 🧬 Panel File Integrity
- Membangun **baseline hash** seluruh file PHP di direktori panel (`/var/www/pterodactyl`)
- Deteksi file PHP **baru** atau **termodifikasi** yang tidak seharusnya ada
- Auto-delete file PHP mencurigakan baru (configurable)
- Proteksi dari serangan **webshell injection** ke panel

### 🗄️ DB Guard
- Monitor langsung ke **MySQL Pterodactyl** — bypass API
- Deteksi **perubahan password** admin secara diam-diam
- Deteksi **privilege escalation**: user biasa yang di-upgrade jadi `root_admin`
- Deteksi **penambahan admin baru** langsung ke database
- Polling configurable (default setiap 10 detik)

### 📡 Rate Limit Protection
- Deteksi pembuatan **akun massal** dalam window waktu tertentu
- Deteksi pembuatan **server massal** secara tidak wajar
- Alert real-time ke owner via Telegram
- Parameter threshold & window sepenuhnya configurable

### 🗑️ Mass Delete Detection
- Deteksi penghapusan massal **server** atau **user** dalam satu polling cycle
- Proteksi dari sabotase internal atau aksi berlebihan admin nakal
- Configurable threshold

### 🏠 Self Protection
- Binary dikunci dengan `chattr +i` agar tidak bisa dihapus/dimodifikasi
- **Watchdog thread** internal — jika binary terhapus/termodifikasi, daemon mati sendiri secara aman
- Pencatatan PID di `/var/run/sentinelx.pid`

### 🤖 Telegram Bot
- Bot untuk owner panel dengan akses penuh ke semua fitur dan kontrol daemon
- Dukungan **inline keyboard** untuk aksi cepat (konfirmasi suspend, lockdown, dll.)

### 📊 Tracking & Lineage
- Lacak **siapa yang membuat** akun/server via MySQL Pterodactyl
- Tampilkan **pohon lineage** seluruh user dan server panel
- Info lengkap per-user dan per-server
- Summary statistik panel

### 🔴 Lockdown Mode
- Suspend **semua server** aktif sekaligus dalam satu perintah
- Dilengkapi konfirmasi dua langkah untuk mencegah eksekusi tidak sengaja

### 📝 Logging
- Log semua aktivitas ke file (`/var/log/sentinelx.log`)
- Timestamp otomatis

---

## 🏗️ Arsitektur

```
sentinelx/
├── main.cpp            # Entry point, thread orchestrator
├── config.cpp/h        # Konfigurasi & setup wizard
├── api.cpp/h           # Pterodactyl API client
├── http.cpp/h          # HTTP client (libcurl wrapper)
├── telegram.cpp/h      # Telegram sender
├── bot.cpp/h           # Telegram command bot (owner)
├── disk_protect.cpp/h  # Disk protection (inotify + scan)
├── integrity.cpp/h     # Panel file integrity monitor
├── db_guard.cpp/h      # DB Guard (MySQL direct monitor)
├── rate_protect.cpp/h  # Rate limit & mass delete detection
├── selfguard.cpp/h     # Self-protection & watchdog
├── tracking.cpp/h      # User/server lineage tracking (MySQL)
├── logger.cpp/h        # Logging
├── install.sh          # Installer script
└── Makefile
```

---

## ⚙️ Requirements

| Dependency | Keterangan |
|---|---|
| `g++` ≥ 9 | Compiler C++17 |
| `libcurl` | HTTP requests ke Telegram & Panel API |
| `libssl` / `libcrypto` | OpenSSL untuk hashing integrity |
| `libpthread` | Multi-threading |
| `inotify` | Kernel Linux ≥ 2.6.13 (built-in) |
| `MySQL` | Database Pterodactyl (untuk tracking & DB Guard) |

> ✅ Semua dependency di-install otomatis oleh `install.sh`

---

## 📋 Konfigurasi

File konfigurasi: `/etc/sentinelx.conf`

```ini
# ── Telegram ──────────────────────────────────────
telegram_token=123456:ABC...          # Token bot Telegram owner
telegram_owner_id=123456789           # Chat ID owner

# ── Pterodactyl Panel ─────────────────────────────
panel_domain=https://panel.example.com
api_application=YOUR_PLTA_KEY         # Application API key
api_client=YOUR_PLTC_KEY              # Client API key (opsional)

# ── Disk Protection ───────────────────────────────
volumes_path=/var/lib/pterodactyl/volumes
max_size_mb=100                       # Batas ukuran file (MB)
delete_dangerous=false                # Auto-delete file berbahaya
suspend_on_danger=false               # Auto-suspend server (false = alert + tombol konfirmasi)
scan_file_content=true                # Scan isi file
content_scan_limit_kb=512             # Batas baca isi file

# ── Ransomware Detection ──────────────────────────
ransomware_detection=true
ransomware_mod_threshold=15           # Jumlah file dimodif dalam window = alert
ransomware_window_sec=30              # Durasi window (detik)

# ── Panel Integrity ───────────────────────────────
panel_path=/var/www/pterodactyl
integrity_enabled=true
integrity_delete_new_php=true         # Hapus PHP baru mencurigakan

# ── DB Guard ──────────────────────────────────────
db_guard_enabled=true
db_guard_poll_sec=10                  # Interval cek admin DB (detik)

# ── Rate Limit ────────────────────────────────────
threshold_accounts=5                  # Maks akun baru per window
threshold_servers=5                   # Maks server baru per window
window_seconds=10                     # Durasi window (detik)
poll_interval_sec=3                   # Interval polling API

# ── Mass Delete ───────────────────────────────────
mass_delete_threshold=5
mass_delete_enabled=true

# ── Self Protection ───────────────────────────────
self_protect_enabled=true
self_binary_path=/usr/local/bin/sentinelx

# ── Log ───────────────────────────────────────────
log_file=/var/log/sentinelx.log
```

Untuk reset / ubah konfigurasi:

```bash
sudo sentinelx --reset
```

---

## 🎮 Perintah Bot Telegram

| Perintah | Fungsi |
|---|---|
| `/help` | Daftar semua perintah |
| `/status` | Status konfigurasi & modul yang aktif |
| `/threats` | Daftar ancaman yang terdeteksi sejak daemon aktif |
| `/scan` | Trigger disk scan manual |
| `/lockdown` | Tampilkan konfirmasi lockdown mode |
| `/lockdown confirm` | Suspend semua server aktif sekaligus |
| `/tracking` | Pohon lineage semua user & server |
| `/tracking stats` | Summary statistik panel |
| `/tracking user <username/email/id>` | Info & lineage user tertentu |
| `/tracking server <nama server>` | Info & lineage server tertentu |

---

## 🛠️ Manajemen Service

```bash
# Status
systemctl status sentinelx

# Start / Stop / Restart
systemctl start sentinelx
systemctl stop sentinelx
systemctl restart sentinelx

# Lihat log realtime
journalctl -u sentinelx -f

# Log file
tail -f /var/log/sentinelx.log
```

---

## 🔄 Update

```bash
cd PROTECT-PANEL-SENTINELX
git pull
make -j$(nproc)
sudo make install
```

---

## 🗑️ Uninstall

```bash
cd PROTECT-PANEL-SENTINELX
sudo make uninstall
```

---

## ⚠️ Disclaimer

SentinelX dirancang untuk digunakan oleh **owner/admin panel Pterodactyl yang sah**. Penggunaan untuk kegiatan ilegal atau penyalahgunaan sistem milik orang lain adalah tanggung jawab pengguna sepenuhnya.

---

## 📄 License

Private — All Rights Reserved. Dilarang mendistribusikan ulang tanpa izin.

---

<div align="center">
  <b>SentinelX v2.0</b> — Built with ❤️ for Pterodactyl Security
</div>
