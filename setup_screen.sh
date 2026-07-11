#!/bin/bash
# ─────────────────────────────────────────────────────────────────────────────
#  TetraMonitor — LUCKFOX 3.5" capacitief scherm (CTP) op de Pi instellen
#
#  Voor de LUCKFOX "3.5inch RPi LCD (CTP)": ST7796S-display (SPI) + GT911
#  5-punts capacitieve touch (I2C). Dit scherm gebruikt een 26-pins female
#  header, dus het bezet alleen de fysieke pinnen 1–26 van de Pi; pinnen 27–40
#  blijven vrij (daar hangt de buzzer).
#
#  Wat dit script doet:
#    1. SPI + I2C aanzetten
#    2. LUCKFOX-driver (st7796s.ko) + overlay (Luckfox35CTP.dtbo) downloaden
#       en installeren  ← officiële LUCKFOX-methode, zie wiki
#    3. De juiste regels in config.txt zetten (met back-up)
#    4. gpiozero/lgpio installeren voor de buzzer
#    5. Een systemd-service maken die de COMPACTE GUI fullscreen op het scherm
#       draait (Qt op de framebuffer, GT911-touch via evdev) mét actieve buzzer
#
#  Bron: https://wiki.luckfox.com/Display/3.5inch-RPi-LCD-CTP/
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
KREL="$(uname -r)"

DL_KO="https://files.luckfox.com/wiki/Luckfox/Display/3inch5-RPi-LCD-CTP/Luckfox-st7796s.zip"
DL_DTBO="https://files.luckfox.com/wiki/Luckfox/Display/3inch5-RPi-LCD-CTP/Luckfox35CTP.dtbo"

# config.txt-pad: Bookworm = /boot/firmware, ouder = /boot
CFG="/boot/firmware/config.txt"
[ -f "$CFG" ] || CFG="/boot/config.txt"
OVERLAYS_DIR="$(dirname "$CFG")/overlays"

echo "🔌 1/6  SPI + I2C aanzetten…"
sudo raspi-config nonint do_spi 0
sudo raspi-config nonint do_i2c 0

echo "📥 2/6  LUCKFOX-driver + overlay downloaden…"
TMP="$(mktemp -d)"
cd "$TMP"
wget -q "$DL_KO"   -O Luckfox-st7796s.zip
wget -q "$DL_DTBO" -O Luckfox35CTP.dtbo
unzip -o -q Luckfox-st7796s.zip

# De zip bevat st7796s.ko per kernelversie. Zoek er één die bij DEZE kernel past;
# val anders terug op de nieuwste .ko die we vinden (met waarschuwing).
KO="$(find . -name 'st7796s.ko' -path "*${KREL}*" | head -n1)"
if [ -z "$KO" ]; then
    KO="$(find . -name 'st7796s.ko' | head -n1)"
    if [ -n "$KO" ]; then
        echo "   ⚠️  Geen st7796s.ko exact voor kernel $KREL gevonden."
        echo "      Gebruik '$KO' — werkt dit niet, pak dan handmatig de juiste map"
        echo "      uit $TMP (blijft staan) voor jouw kernelversie."
    fi
fi
if [ -z "$KO" ]; then
    echo "❌ Geen st7796s.ko in de download gevonden. Check $DL_KO handmatig."
    exit 1
fi

echo "📦 3/6  Driver-module + overlay installeren…"
DRV_DIR="/lib/modules/${KREL}/kernel/drivers"
sudo mkdir -p "$DRV_DIR"
sudo cp "$KO" "$DRV_DIR/st7796s.ko"
sudo cp Luckfox35CTP.dtbo "$OVERLAYS_DIR/"
grep -q '^st7796s$' /etc/modules 2>/dev/null || echo 'st7796s' | sudo tee -a /etc/modules >/dev/null
sudo depmod -a
cd "$REPO"

echo "⚙️  4/6  config.txt aanpassen ($CFG)…"
sudo cp "$CFG" "$CFG.bak.$(date +%s)"          # back-up, voor het geval dat
add_cfg () { grep -qF "$1" "$CFG" || echo "$1" | sudo tee -a "$CFG" >/dev/null; }
add_cfg 'dtparam=i2c_arm=on'
add_cfg 'dtparam=i2c_arm_baudrate=50000'
add_cfg 'dtparam=spi=on'
add_cfg 'dtoverlay=Luckfox35CTP,fps=60,speed=48000000,rotate=90,ts_rotate_90'
add_cfg 'hdmi_force_hotplug=1'
add_cfg 'hdmi_group=2'
add_cfg 'hdmi_mode=87'
add_cfg 'hdmi_cvt 480 320 60 6 0 0 0'

echo "🔔 5/6  Buzzer-bibliotheek (gpiozero/lgpio) installeren…"
"$PY" -m pip show gpiozero >/dev/null 2>&1 || \
    "$PY" -m pip install --break-system-packages gpiozero lgpio 2>/dev/null || \
    "$PY" -m pip install gpiozero lgpio

echo "🖥️  6/6  Autostart-service maken (compacte GUI + buzzer op BCM$BUZZER_BCM)…"
# Het ST7796S-scherm verschijnt als een eigen framebuffer (meestal /dev/fb1;
# fb0 = HDMI). We detecteren 'm op naam; override met FB=/dev/fbN als het mis is.
FB="${FB:-}"
if [ -z "$FB" ]; then
    for f in /sys/class/graphics/fb*; do
        n="$(cat "$f/name" 2>/dev/null || true)"
        case "$n" in *st7796*|*spi*) FB="/dev/$(basename "$f")";; esac
    done
    [ -z "$FB" ] && FB="/dev/fb1"
fi
echo "   → framebuffer: $FB (na reboot controleren; override met FB=/dev/fbN)"

sudo tee /etc/systemd/system/tetramonitor-gui.service >/dev/null <<EOF
[Unit]
Description=TetraMonitor compacte GUI op LUCKFOX 3.5" CTP-scherm
After=multi-user.target

[Service]
Type=simple
User=$RUN_USER
WorkingDirectory=$REPO
# Qt rechtstreeks op de framebuffer; GT911 capacitieve touch via evdev (Qt vindt
# het touch-device zelf; kalibreren hoeft niet zoals bij resistief). De rotatie
# zit al in het dtoverlay (rotate=90,ts_rotate_90), dus Qt hoeft niet te draaien.
Environment=QT_QPA_PLATFORM=linuxfb:fb=$FB
Environment=QT_QPA_GENERIC_PLUGINS=evdevtouch
Environment=QT_QPA_FB_HIDECURSOR=1
ExecStart=$PY $REPO/tetra_monitor.py --compact --fullscreen --buzzer $BUZZER_BCM
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
echo "   • Beeld op het schermpje? ✅  Zo niet: klopt \$FB ($FB)? Kijk met"
echo "       cat /sys/class/graphics/fb*/name   en zet FB=/dev/fbN in de service"
echo "       (sudo systemctl edit --full tetramonitor-gui) of draai dit script"
echo "       opnieuw met  FB=/dev/fbN ./setup_screen.sh $BUZZER_BCM"
echo "   • Touch werkt meteen (capacitief, GT911) — geen ts_calibrate nodig."
echo "     Reageert touch niet? Check:  i2cdetect -y 1   (GT911 op 0x5d of 0x14)"
echo "   • Buzzer (KY-012) op BCM$BUZZER_BCM = fysieke pin $( [ "$BUZZER_BCM" = 26 ] && echo 37 || echo '?' ):"
echo "       S → GPIO-pin,  − → een GND-pin (bv. fysieke pin 39, naast pin 37)."
echo "       Piept steeds sneller naarmate het signaal sterker/dichterbij is."
echo "       Geen piep? journalctl -u tetramonitor-gui -f  → zoek '[buzzer]'."
echo "   • Overlay fout / geen boot? Herstel:  sudo cp $CFG.bak.* $CFG"
echo
echo "   Service beheren:"
echo "     sudo systemctl status  tetramonitor-gui"
echo "     sudo systemctl restart tetramonitor-gui"
echo "     journalctl -u tetramonitor-gui -f"
