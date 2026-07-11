#!/bin/bash
# ─────────────────────────────────────────────────────────────────────────────
#  TetraMonitor — LUCKFOX 3.5" capacitief scherm (CTP) op de Pi instellen
#
#  Voor de LUCKFOX "3.5inch RPi LCD (CTP)": ST7796S-display (SPI) + GT911
#  5-punts capacitieve touch (I2C). Dit scherm gebruikt een 26-pins female
#  header, dus het bezet alleen de fysieke pinnen 1–26 van de Pi; pinnen 27–40
#  blijven vrij (daar hangt de buzzer).
#
#  Aanpak: de MAINLINE kerneldriver `panel-mipi-dbi` (dtoverlay=mipi-dbi-spi)
#  met een eigen init-firmware voor de ST7796S, plus de standaard `goodix`-
#  overlay voor de GT911-touch. Dit is kernel-onafhankelijk — geen kant-en-
#  klare .ko van de fabrikant nodig die na elke kernelupdate stukgaat.
#
#  Let op: het paneel (ST7796S-kloon) kan alleen portret-adressering aan;
#  liggend beeld wordt in software gedaan met `tetra_monitor.py --rotate 90`
#  (touch draait automatisch mee).
#
#  Wat dit script doet:
#    1. SPI + I2C aanzetten
#    2. ST7796S init-firmware installeren (/lib/firmware/st7796s.bin)
#    3. De juiste regels in config.txt zetten (met back-up) + console-cursor uit
#    4. gpiozero/lgpio installeren voor de buzzer
#    5. Een systemd-service maken die de COMPACTE GUI fullscreen liggend op het
#       scherm draait (Qt op de framebuffer, GT911-touch via evdev) mét buzzer
#
#  Gebruik (op de Pi, in de projectmap):
#       chmod +x setup_screen.sh
#       ./setup_screen.sh              # buzzer op BCM26 (fysieke pin 37)
#       ./setup_screen.sh 26           # of geef zelf de buzzer-BCM-pin mee
#
#  Daarna:  sudo reboot
# ─────────────────────────────────────────────────────────────────────────────
set -e

BUZZER_BCM="${1:-26}"    # KY-012 actieve buzzer. Het scherm bezet fysieke pin
                         # 1–26, dus BCM26 (= fysieke pin 37) is standaard vrij.
                         # Andere pin? Geef 'm mee als 1e argument.

REPO="$(cd "$(dirname "$0")" && pwd)"
RUN_USER="$(whoami)"
PY="$(command -v python3)"

# config.txt-pad: Bookworm = /boot/firmware, ouder = /boot
CFG="/boot/firmware/config.txt"
[ -f "$CFG" ] || CFG="/boot/config.txt"
CMDLINE="$(dirname "$CFG")/cmdline.txt"

echo "🔌 1/6  SPI + I2C aanzetten…"
sudo raspi-config nonint do_spi 0
sudo raspi-config nonint do_i2c 0

echo "📥 2/6  ST7796S init-firmware installeren (/lib/firmware/st7796s.bin)…"
# Init-sequence voor de ST7796S in panel-mipi-dbi-formaat (portret, MADCTL
# 0x48). Bevat o.a. SWRESET, MADCTL, COLMOD 16-bit, gamma en DISPON.
base64 -d <<'FWEOF' | sudo tee /lib/firmware/st7796s.bin >/dev/null
TUlQSSBEQkkAAAAAAAAAAQEAAAGWNgFIOgEF8AHD8AGWtAEBtwHGwAKARcEBE8IBp8UBCugIQIoAACkZpTPgDtAIDwYGMzAzRxcTEysx4Q7QChELCQcvM0c4FRYsMvABPPABaSEAEQAAAWQpAAABZBMAAAEU
FWEOF

echo "⚙️  3/6  config.txt aanpassen ($CFG)…"
sudo cp "$CFG" "$CFG.bak.$(date +%s)"          # back-up, voor het geval dat
add_cfg () { grep -qF "$1" "$CFG" || echo "$1" | sudo tee -a "$CFG" >/dev/null; }
add_cfg 'dtparam=i2c_arm=on'
add_cfg 'dtparam=i2c_arm_baudrate=50000'
add_cfg 'dtparam=spi=on'
# Display: mainline panel-mipi-dbi op spi0.0 (DC=BCM22, RST=BCM27, BL=BCM18)
add_cfg 'dtoverlay=mipi-dbi-spi,speed=48000000'
add_cfg 'dtparam=compatible=st7796s\0panel-mipi-dbi-spi'
add_cfg 'dtparam=width=320,height=480,width-mm=49,height-mm=79'
add_cfg 'dtparam=reset-gpio=27,dc-gpio=22,backlight-gpio=18'
# Touch: GT911 via de standaard goodix-overlay (INT=BCM4, RST=BCM17)
add_cfg 'dtoverlay=goodix'
# Knipperende console-cursor uit (schijnt anders door de GUI heen)
grep -q 'vt.global_cursor_default=0' "$CMDLINE" || \
    sudo sed -i 's/$/ vt.global_cursor_default=0/' "$CMDLINE"

echo "🔔 4/6  Buzzer-bibliotheek (gpiozero/lgpio) installeren…"
"$PY" -m pip show gpiozero >/dev/null 2>&1 || \
    "$PY" -m pip install --break-system-packages gpiozero lgpio 2>/dev/null || \
    "$PY" -m pip install gpiozero lgpio

echo "🧹 5/6  Restanten van de oude LUCKFOX-driver opruimen (indien aanwezig)…"
sudo sed -i '/^st7796s$/d' /etc/modules 2>/dev/null || true
sudo sed -i '/^dtoverlay=Luckfox35CTP/d;/^dtoverlay=ads7846/d' "$CFG"
echo 'blacklist st7796s' | sudo tee /etc/modprobe.d/blacklist-st7796s.conf >/dev/null

echo "🖥️  6/6  Autostart-service maken (compacte GUI + buzzer op BCM$BUZZER_BCM)…"
# Zonder HDMI-kabel is het paneel /dev/fb0. Het paneel kan alleen portret,
# dus de GUI draait zichzelf met --rotate 90 (touch draait mee via Qt).
sudo tee /etc/systemd/system/tetramonitor-gui.service >/dev/null <<EOF
[Unit]
Description=TetraMonitor compacte GUI op LUCKFOX 3.5 inch CTP-scherm
After=multi-user.target

[Service]
Type=simple
User=$RUN_USER
WorkingDirectory=$REPO
Environment=QT_QPA_PLATFORM=linuxfb:fb=/dev/fb0
Environment=QT_QPA_GENERIC_PLUGINS=evdevtouch
Environment=QT_QPA_FB_HIDECURSOR=1
ExecStart=$PY $REPO/tetra_monitor.py --compact --fullscreen --rotate 90 --buzzer $BUZZER_BCM
Restart=on-failure
RestartSec=5
TimeoutStopSec=5

[Install]
WantedBy=multi-user.target
EOF

sudo systemctl daemon-reload
sudo systemctl enable tetramonitor-gui.service

echo
echo "🚀  Klaar met instellen."
echo
echo "   Doe nu:   sudo reboot"
echo
echo "   Na de reboot:"
echo "   • Beeld op het schermpje? ✅  Zo niet, check de driver:"
echo "       dmesg | grep -i mipi     → 'fb0: panel-mipi-dbid frame buffer device'"
echo "       cat /sys/class/graphics/fb0/virtual_size   → 320,480"
echo "   • Beeld verkeerd om gedraaid? Zet --rotate 270 i.p.v. 90 in de service:"
echo "       sudo systemctl edit --full tetramonitor-gui"
echo "   • Touch werkt meteen (capacitief, GT911) — geen ts_calibrate nodig."
echo "     Reageert touch niet? Check:  i2cdetect -y 1   (GT911 op 0x5d of 0x14)"
echo "     en  grep -i goodix /proc/bus/input/devices"
echo "   • Buzzer (KY-012) op BCM$BUZZER_BCM = fysieke pin $( [ "$BUZZER_BCM" = 26 ] && echo 37 || echo '?' ):"
echo "       S → GPIO-pin,  − → een GND-pin (bv. fysieke pin 39, naast pin 37)."
echo "       Test 'm met de 🔔 Test-knop: piept steeds sneller, als een geigerteller."
echo "       Geen piep? journalctl -u tetramonitor-gui -f  → zoek '[buzzer]'."
echo "   • Config stuk / geen boot? Herstel:  sudo cp $CFG.bak.* $CFG"
echo
echo "   Service beheren:"
echo "     sudo systemctl status  tetramonitor-gui"
echo "     sudo systemctl restart tetramonitor-gui"
echo "     journalctl -u tetramonitor-gui -f"
