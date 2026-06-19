#!/bin/bash
# ─────────────────────────────────────────────────────────────────────────────
#  TetraMonitor — klein SPI-touchscreen (XPT2046) op de Pi instellen
#
#  Zet een goedkoop SPI-TFT-schermpje (ILI9341/ILI9486 + XPT2046-touch) aan en
#  laat de COMPACTE GUI (alleen de 3 balken) fullscreen op dat scherm draaien —
#  rechtstreeks op de framebuffer (linuxfb), dus zónder zware desktop.
#
#  ⚠️  LET OP: het juiste device-tree-overlay is MODEL-AFHANKELIJK. Een verkeerd
#      overlay kan de boot blokkeren. Daarom geef JIJ het overlay van jóuw scherm
#      mee als argument. Veelvoorkomende overlays:
#         mhs35        → 3.5" MHS-3.5 (rode print, veelverkocht)
#         piscreen     → 3.5" ILI9486 (PiScreen-kloon)
#         waveshare35a → Waveshare 3.5" (A)
#         tft35a       → generieke 3.5" (LCD-show "tft35a")
#         pitft28-resistive → Adafruit 2.8" ILI9341
#      Weet je het model niet zeker? Zoek "<jouw scherm> raspberry pi overlay".
#
#  Gebruik (op de Pi, in de projectmap):
#       chmod +x setup_screen.sh
#       ./setup_screen.sh mhs35            # <- vervang door JOUW overlay
#
#  Daarna:  sudo reboot
# ─────────────────────────────────────────────────────────────────────────────
set -e

OVERLAY="$1"
if [ -z "$OVERLAY" ]; then
    echo "❌ Geef het overlay van je scherm mee, bv:  ./setup_screen.sh mhs35"
    echo "   (zie de lijst boven in dit script of zoek het op voor jouw model)"
    exit 1
fi

REPO="$(cd "$(dirname "$0")" && pwd)"
RUN_USER="$(whoami)"
PY="$(command -v python3)"
FB="${FB:-/dev/fb1}"             # SPI-scherm is meestal fb1 (fb0 = HDMI)

# config.txt-pad: Bookworm = /boot/firmware, ouder = /boot
CFG="/boot/firmware/config.txt"
[ -f "$CFG" ] || CFG="/boot/config.txt"

echo "📺 1/4  SPI aanzetten + overlay '$OVERLAY' toevoegen aan $CFG…"
sudo cp "$CFG" "$CFG.bak.$(date +%s)"          # back-up, voor het geval dat
grep -q '^dtparam=spi=on' "$CFG" || echo 'dtparam=spi=on' | sudo tee -a "$CFG" >/dev/null
grep -q "^dtoverlay=$OVERLAY" "$CFG" || echo "dtoverlay=$OVERLAY" | sudo tee -a "$CFG" >/dev/null

echo "📦 2/4  Touch-bibliotheek (tslib) installeren…"
sudo apt-get update -qq
sudo apt-get install -y libts-bin libinput-tools 2>/dev/null || sudo apt-get install -y libts-bin

echo "⚙️  3/4  GUI-autostart-service aanmaken (compacte modus op het scherm)…"
sudo tee /etc/systemd/system/tetramonitor-gui.service >/dev/null <<EOF
[Unit]
Description=TetraMonitor compacte GUI op SPI-scherm
After=multi-user.target

[Service]
Type=simple
User=$RUN_USER
WorkingDirectory=$REPO
# Qt rechtstreeks op de SPI-framebuffer, touch via tslib.
Environment=QT_QPA_PLATFORM=linuxfb:fb=$FB
Environment=QT_QPA_FB_TSLIB=1
Environment=TSLIB_FBDEVICE=$FB
Environment=TSLIB_TSDEVICE=/dev/input/event0
Environment=QT_QPA_FB_HIDECURSOR=1
ExecStart=$PY $REPO/tetra_monitor.py --compact --fullscreen
Restart=on-failure
RestartSec=5
TimeoutStopSec=5

[Install]
WantedBy=multi-user.target
EOF

sudo systemctl daemon-reload
sudo systemctl enable tetramonitor-gui.service

echo "🚀 4/4  Klaar met instellen."
echo
echo "   Doe nu:   sudo reboot"
echo
echo "   Na de reboot:"
echo "   • Komt er beeld op het schermpje? ✅"
echo "   • Touch scheef? Kalibreer eenmalig:   sudo ts_calibrate"
echo "     (en check het juiste touch-device met:  cat /proc/bus/input/devices"
echo "      → pas zonodig TSLIB_TSDEVICE=/dev/input/eventN aan in de service)"
echo "   • Geen beeld? Dan klopt het overlay '$OVERLAY' niet voor jouw scherm."
echo "     Herstel met de back-up:  sudo cp $CFG.bak.* $CFG   en probeer een ander overlay."
echo
echo "   Service beheren:"
echo "     sudo systemctl status  tetramonitor-gui"
echo "     sudo systemctl restart tetramonitor-gui"
echo "     journalctl -u tetramonitor-gui -f"
