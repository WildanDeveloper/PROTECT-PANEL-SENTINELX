# 🛡️ SentinelX v2.6

**Security daemon untuk Pterodactyl Panel** — proteksi real-time dari ancaman internal maupun eksternal, dikendalikan lewat Telegram.

![C++17](https://img.shields.io/badge/C%2B%2B-17-blue?style=flat-square&logo=c%2B%2B)
![Platform](https://img.shields.io/badge/Platform-Linux-informational?style=flat-square&logo=linux)
![License](https://img.shields.io/badge/License-Private-red?style=flat-square)
![Status](https://img.shields.io/badge/Status-Active-brightgreen?style=flat-square)

---

## 🌟 Fitur Utama

### 🔒 Disk Protection
- Monitoring real-time via **inotify** pada direktori volumes Pterodactyl
- Deteksi file berbahaya berdasarkan **nama** (ddos, botnet, exploit, webshell, miner, dll.)
- Scanning **isi file** PHP & Bash untuk mendeteksi pola berbahaya (eval obfuscation, reverse shell, webshell populer, dll.)
- Auto-delete file berbahaya (configurable)
- Alert Telegram instan saat ancaman terdeteksi

### 🧬 Panel File Integrity
- Membangun **baseline hash** seluruh file PHP di direktori panel (`/var/www/pterodactyl`)
- Deteksi file PHP **baru** atau **termodifikasi** yang tidak seharusnya ada
- Auto-delete file PHP mencurigakan baru (configurable)
- Proteksi dari serangan **webshell injection** ke panel

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

### 🤖 Dual Bot System (Owner + Buyer)
- **Owner Bot** — bot utama untuk owner panel, akses penuh ke semua fitur dan konfigurasi
- **Buyer Bot** — bot terpisah per buyer, akses terbatas hanya ke VPS & server milik buyer tersebut
- Multi-buyer support dengan konfigurasi per `chat_id`

### 🖥️ VPS Manager (per Buyer)
- Buyer bisa daftarkan VPS mereka sendiri via Telegram
- **Auto-deploy agent** ke VPS via SSH
- Test koneksi SSH, monitoring status (`verified` / `pending` / `failed`)
- Menjalankan command di VPS remote via bot

### 📊 Tracking & Lineage
- Lacak **siapa yang membuat** akun/server (PLTA, Panel admin, user tertentu)
- Tampilkan **pohon lineage** user dan server
- Info lengkap per-user dan per-server
- Summary statistik panel

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
├── disk_protect.cpp/h  # Disk protection (inotify + scan)
├── integrity.cpp/h     # Panel file integrity monitor
├── selfguard.cpp/h     # Self-protection & watchdog
├── tracking.cpp/h      # User/server lineage tracking (MySQL)
├── vps_manager.cpp/h   # VPS management via SSH
├── buyer_bot.cpp/h     # Buyer Telegram bot
├── logger.cpp/h        # Logging
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
| `sshpass` | Deploy agent ke VPS buyer (auto-install) |
| `MySQL` | Database Pterodactyl (untuk tracking) |

Install dependencies:

```bash
# Ubuntu / Debian
apt install -y build-essential libcurl4-openssl-dev libssl-dev

# CentOS / AlmaLinux
yum install -y gcc-c++ libcurl-devel openssl-devel
```

---

## 🚀 Instalasi

### 1. Clone & Build

```bash
git clone https://github.com/your-username/sentinelx.git
cd sentinelx
make -j$(nproc)
```

### 2. Install sebagai Systemd Service

```bash
sudo make install
```

Perintah ini akan:
- Menyalin binary ke `/usr/local/bin/sentinelx`
- Membuat service file di `/etc/systemd/system/sentinelx.service`
- Mengaktifkan dan menjalankan service secara otomatis

### 3. Setup Konfigurasi

Jalankan setup wizard saat pertama kali:

```bash
sentinelx --setup
```

Atau edit konfigurasi langsung di `/etc/sentinelx.conf`.

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
scan_interval_sec=30                  # Interval scan berkala
delete_dangerous=false                # Auto-delete file berbahaya
scan_file_content=true                # Scan isi file
content_scan_limit_kb=512             # Batas baca isi file

# ── Panel Integrity ───────────────────────────────
panel_path=/var/www/pterodactyl
integrity_enabled=true
integrity_delete_new_php=true         # Hapus PHP baru mencurigakan

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

---

## 🎮 Perintah Bot Telegram

### Owner Bot

| Perintah | Fungsi |
|---|---|
| `/status` | Status semua modul protection |
| `/stats` | Statistik panel (user, server, dll.) |
| `/tree` | Pohon lineage semua user & server |
| `/user <username>` | Info & lineage user tertentu |
| `/server <nama>` | Info & lineage server tertentu |
| `/scan` | Trigger disk scan manual |
| `/buyers` | Daftar buyer bot yang aktif |
| `/addbuyer <token> <label>` | Tambah buyer bot baru |
| `/removebuyer <token>` | Hapus buyer bot |
| `/config` | Lihat konfigurasi aktif |
| `/set <key> <value>` | Ubah nilai konfigurasi |

### Buyer Bot

| Perintah | Fungsi |
|---|---|
| `/start` | Setup domain & API key buyer |
| `/vps` | Daftar VPS yang terdaftar |
| `/addvps <ip> <password>` | Tambah VPS baru |
| `/delvps <ip>` | Hapus VPS |
| `/testvps <ip>` | Test koneksi SSH ke VPS |
| `/deployvps <ip>` | Deploy agent ke VPS |
| `/cmd <ip> <command>` | Jalankan command di VPS |
| `/servers` | Daftar server milik buyer |

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
cd sentinelx
git pull
make -j$(nproc)
sudo make install
```

---

## 🗑️ Uninstall

```bash
sudo make uninstall
```

---

## 🏗️ Build dari Source

```bash
# Build saja (tanpa install)
make

# Clean build artifacts
make clean

# Build + install sekaligus
sudo make install
```

Binary output: `./sentinelx`

---

## ⚠️ Disclaimer

SentinelX dirancang untuk digunakan oleh **owner/admin panel Pterodactyl yang sah**. Penggunaan untuk kegiatan ilegal atau penyalahgunaan sistem milik orang lain adalah tanggung jawab pengguna sepenuhnya.

---

## 📄 License

Private — All Rights Reserved. Dilarang mendistribusikan ulang tanpa izin.

---

<div align="center">
  <b>SentinelX v2.6</b> — Built with ❤️ for Pterodactyl Security
</div>
