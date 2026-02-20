#!/bin/bash
# ============================================================
#  SentinelX — One-Command Installer
#  Jalankan sebagai root:  bash install.sh
# ============================================================

set -e

RED='\033[1;31m'
GRN='\033[1;32m'
YLW='\033[1;33m'
CYN='\033[1;36m'
BLD='\033[1m'
RST='\033[0m'

INSTALL_DIR="/opt/sentinelx"
BINARY="/usr/local/bin/sentinelx"
SERVICE="/etc/systemd/system/sentinelx.service"
LOG_FILE="/var/log/sentinelx_install.log"

# ── Banner ────────────────────────────────────────────────────
clear
echo -e "${CYN}"
cat << 'BANNER'
  ███████╗███████╗███╗  ██╗████████╗██╗███╗  ██╗███████╗██╗     ██╗  ██╗
  ██╔════╝██╔════╝████╗ ██║╚══██╔══╝██║████╗ ██║██╔════╝██║     ╚██╗██╔╝
  ███████╗█████╗  ██╔██╗██║   ██║   ██║██╔██╗██║█████╗  ██║      ╚███╔╝
  ╚════██║██╔══╝  ██║╚████║   ██║   ██║██║╚████║██╔══╝  ██║      ██╔██╗
  ███████║███████╗██║ ╚███║   ██║   ██║██║ ╚███║███████╗███████╗██╔╝╚██╗
  ╚══════╝╚══════╝╚═╝  ╚══╝   ╚═╝   ╚═╝╚═╝  ╚══╝╚══════╝╚══════╝╚═╝  ╚═╝
BANNER
echo -e "${RST}"
echo -e "${BLD}       Pterodactyl All-in-One Protection Daemon${RST}"
echo -e "${YLW}       Installer v2.6${RST}"
echo -e "  $(printf '─%.0s' {1..70})\n"

# ── Pastikan root ─────────────────────────────────────────────
if [[ $EUID -ne 0 ]]; then
    echo -e "${RED}[!!]${RST} Script ini harus dijalankan sebagai root."
    echo -e "     Gunakan: ${BLD}sudo bash install.sh${RST}"
    exit 1
fi

# ── Pastikan source ada di direktori yang sama ────────────────
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [[ ! -f "$SCRIPT_DIR/main.cpp" ]]; then
    echo -e "${RED}[!!]${RST} File source tidak ditemukan di $SCRIPT_DIR"
    echo -e "     Pastikan install.sh berada satu folder dengan file .cpp"
    exit 1
fi

log() { echo "[$(date '+%H:%M:%S')] $*" >> "$LOG_FILE"; }
ok()  { echo -e "${GRN}[OK]${RST}  $*"; log "OK: $*"; }
inf() { echo -e "${CYN}[..]${RST}  $*"; log "INFO: $*"; }
err() { echo -e "${RED}[!!]${RST}  $*"; log "ERR: $*"; }
warn(){ echo -e "${YLW}[!!]${RST}  $*"; log "WARN: $*"; }

echo "" > "$LOG_FILE"
inf "Log tersimpan di: $LOG_FILE"
echo ""

# ── 1. Deteksi distro ─────────────────────────────────────────
inf "Mendeteksi sistem operasi..."
if command -v apt-get &>/dev/null; then
    PKG_MGR="apt"
elif command -v yum &>/dev/null; then
    PKG_MGR="yum"
elif command -v dnf &>/dev/null; then
    PKG_MGR="dnf"
else
    err "Package manager tidak dikenali (bukan apt/yum/dnf)"
    exit 1
fi
ok "Package manager: $PKG_MGR"

# ── 2. Install dependencies ───────────────────────────────────
echo ""
inf "Menginstall dependencies..."

install_pkg() {
    local pkg="$1"
    local check="${2:-$1}"
    if command -v "$check" &>/dev/null || dpkg -l "$pkg" &>/dev/null 2>&1; then
        ok "  $pkg — sudah ada"
    else
        inf "  Installing $pkg..."
        if [[ "$PKG_MGR" == "apt" ]]; then
            apt-get install -y "$pkg" >> "$LOG_FILE" 2>&1
        elif [[ "$PKG_MGR" == "yum" ]]; then
            yum install -y "$pkg" >> "$LOG_FILE" 2>&1
        else
            dnf install -y "$pkg" >> "$LOG_FILE" 2>&1
        fi
        ok "  $pkg — terinstall"
    fi
}

if [[ "$PKG_MGR" == "apt" ]]; then
    apt-get update -qq >> "$LOG_FILE" 2>&1
    install_pkg "build-essential"  "g++"
    install_pkg "g++"              "g++"
    install_pkg "libcurl4-openssl-dev"
    install_pkg "libssl-dev"
    install_pkg "mysql-client"     "mysql"
    install_pkg "unzip"            "unzip"
else
    install_pkg "gcc-c++"          "g++"
    install_pkg "libcurl-devel"
    install_pkg "openssl-devel"
    install_pkg "mysql"            "mysql"
    install_pkg "unzip"            "unzip"
fi

# ── 3. Cek versi g++ (butuh >= 9 untuk C++17) ────────────────
echo ""
inf "Cek versi compiler..."
GXX_VER=$(g++ -dumpversion 2>/dev/null | cut -d. -f1)
if [[ -z "$GXX_VER" ]]; then
    err "g++ tidak ditemukan setelah install!"
    exit 1
fi
if [[ "$GXX_VER" -lt 9 ]]; then
    warn "g++ versi $GXX_VER terdeteksi. Butuh >= 9 untuk C++17."
    if [[ "$PKG_MGR" == "apt" ]]; then
        inf "Mencoba install g++-10..."
        apt-get install -y g++-10 >> "$LOG_FILE" 2>&1
        update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-10 10 >> "$LOG_FILE" 2>&1
        ok "g++-10 diinstall"
    fi
else
    ok "g++ versi $GXX_VER — OK"
fi

# ── 4. Copy source ke install dir ────────────────────────────
echo ""
inf "Menyiapkan source di $INSTALL_DIR..."
mkdir -p "$INSTALL_DIR"
cp -r "$SCRIPT_DIR/"*.cpp "$SCRIPT_DIR/"*.h "$SCRIPT_DIR/Makefile" "$INSTALL_DIR/" 2>/dev/null
ok "Source disalin ke $INSTALL_DIR"

# ── 5. Compile ────────────────────────────────────────────────
echo ""
inf "Mengcompile SentinelX (ini mungkin 30-60 detik)..."
cd "$INSTALL_DIR"
NPROC=$(nproc 2>/dev/null || echo 1)

if ! make -j"$NPROC" >> "$LOG_FILE" 2>&1; then
    err "Compile GAGAL! Cek log: $LOG_FILE"
    echo ""
    tail -20 "$LOG_FILE"
    exit 1
fi
ok "Compile berhasil — binary: $INSTALL_DIR/sentinelx"

# ── 6. Install binary ─────────────────────────────────────────
echo ""
inf "Menginstall binary ke $BINARY..."

# Stop service dulu kalau sedang jalan (binary busy = tidak bisa di-overwrite)
if systemctl is-active --quiet sentinelx 2>/dev/null; then
    inf "  Menghentikan service lama..."
    systemctl stop sentinelx 2>/dev/null || true
    sleep 1
fi

# Unlock chattr kalau binary lama dikunci selfguard
if [[ -f "$BINARY" ]]; then
    chattr -i "$BINARY" 2>/dev/null || true
fi

cp "$INSTALL_DIR/sentinelx" "$BINARY"
chmod 755 "$BINARY"
ok "Binary terinstall di $BINARY"

# ── 7. Buat systemd service ───────────────────────────────────
echo ""
inf "Membuat systemd service..."
cat > "$SERVICE" << 'SVCEOF'
[Unit]
Description=SentinelX Pterodactyl Protection Daemon
After=network.target mysql.service mariadb.service
Wants=network.target

[Service]
Type=simple
ExecStart=/usr/local/bin/sentinelx
Restart=always
RestartSec=3
User=root
StandardOutput=null
StandardError=journal
LimitNOFILE=65536

[Install]
WantedBy=multi-user.target
SVCEOF

systemctl daemon-reload
ok "Service file dibuat"

# ── 8. Setup wizard (konfigurasi) ────────────────────────────
echo ""
echo -e "${CYN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${RST}"
echo -e "${BLD}  KONFIGURASI SENTINELX${RST}"
echo -e "${CYN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${RST}"
echo ""

# Cek apakah config sudah ada
if [[ -f "/etc/sentinelx.conf" ]]; then
    echo -e "${YLW}[!!]${RST}  Config sudah ada di /etc/sentinelx.conf"
    read -rp "      Buat ulang config? [y/N]: " RESET_CFG
    if [[ "$RESET_CFG" =~ ^[Yy]$ ]]; then
        rm -f /etc/sentinelx.conf
        "$BINARY" --setup
    else
        inf "Menggunakan config lama."
    fi
else
    "$BINARY" --setup
fi

# ── 9. Lock binary (self-protect) ────────────────────────────
echo ""
inf "Mengunci binary dengan chattr +i..."
if chattr +i "$BINARY" 2>/dev/null; then
    ok "Binary dikunci — tidak bisa dihapus/dimodifikasi"
else
    warn "chattr tidak tersedia — binary tidak dikunci (opsional)"
fi

# ── 10. Start service ─────────────────────────────────────────
echo ""
inf "Mengaktifkan dan menjalankan service..."
systemctl enable sentinelx >> "$LOG_FILE" 2>&1
systemctl restart sentinelx

sleep 2

if systemctl is-active --quiet sentinelx; then
    ok "SentinelX berjalan!"
else
    err "Service gagal start. Cek: journalctl -u sentinelx -n 30"
    journalctl -u sentinelx -n 20 --no-pager
    exit 1
fi

# ── 11. Summary ───────────────────────────────────────────────
echo ""
echo -e "${CYN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${RST}"
echo -e "${GRN}${BLD}  ✅  SENTINELX BERHASIL DIINSTALL & BERJALAN!${RST}"
echo -e "${CYN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${RST}"
echo ""
echo -e "  ${BLD}Binary   :${RST} $BINARY"
echo -e "  ${BLD}Config   :${RST} /etc/sentinelx.conf"
echo -e "  ${BLD}Log      :${RST} /var/log/sentinelx.log"
echo -e "  ${BLD}Service  :${RST} systemctl {start|stop|restart|status} sentinelx"
echo ""
echo -e "  ${BLD}Perintah berguna:${RST}"
echo -e "  ${CYN}  tail -f /var/log/sentinelx.log${RST}          # live log"
echo -e "  ${CYN}  journalctl -u sentinelx -f${RST}              # systemd log"
echo -e "  ${CYN}  systemctl status sentinelx${RST}               # status"
echo -e "  ${CYN}  sentinelx --reset${RST}                        # setup ulang config"
echo ""
echo -e "  ${YLW}Install log: $LOG_FILE${RST}"
echo ""
