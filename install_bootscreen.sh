#!/bin/bash
# ─────────────────────────────────────────────────────────────────────────────
#  priosense — opstartscherm op de Pi
#
#  Na stroom-erop zie je alleen nog het priosense-logo tot de app start:
#    • geen Raspberry Pi-logo (logo.nologo, disable_splash=1) en geen
#      "Welcome to the Raspberry Pi Desktop" (Plymouth uit: 'splash' weg)
#    • geen terminaltekst: kernel- en systemd-meldingen gaan naar tty3 i.p.v.
#      het scherm (console=tty3, quiet), knipperende cursor uit, geen
#      login-prompt op het scherm (getty@tty1 uit — inloggen gaat via SSH)
#    • het logo zelf: service priosense-bootlogo zet bootlogo.raw op /dev/fb0
#      zodra het schermpje er is
#
#  Back-ups van cmdline.txt en config.txt komen naast het origineel
#  (*.bak-priosense). Alles terugdraaien:  ./install_bootscreen.sh --undo
#
#  Gebruik (op de Pi, in de projectmap):
#       chmod +x install_bootscreen.sh
#       ./install_bootscreen.sh
# ─────────────────────────────────────────────────────────────────────────────
set -e

REPO="$(cd "$(dirname "$0")" && pwd)"
RAW="$REPO/bootlogo.raw"
UNIT=/etc/systemd/system/priosense-bootlogo.service
GUI_UNIT=/etc/systemd/system/tetramonitor-gui.service

BOOT=/boot/firmware; [ -d "$BOOT" ] || BOOT=/boot
CMD="$BOOT/cmdline.txt"
CFG="$BOOT/config.txt"

if [ "$1" = "--undo" ]; then
    echo "↩️  Opstartscherm terugdraaien…"
    [ -f "$CMD.bak-priosense" ] && sudo cp "$CMD.bak-priosense" "$CMD" && echo "   cmdline.txt hersteld"
    [ -f "$CFG.bak-priosense" ] && sudo cp "$CFG.bak-priosense" "$CFG" && echo "   config.txt hersteld"
    sudo systemctl disable priosense-bootlogo.service 2>/dev/null || true
    sudo rm -f "$UNIT"
    sudo systemctl enable getty@tty1.service
    sudo systemctl daemon-reload
    echo "✅ Klaar. Doe nu:  sudo reboot"
    exit 0
fi

# Zelfde draaiing als de app: het getal achter --rotate in de GUI-service
# (setup_screen.sh zet daar 90), anders 90.
ROT=$(grep -o -- '--rotate [0-9]\+' "$GUI_UNIT" 2>/dev/null | head -n1 | awk '{print $2}')
ROT=${ROT:-90}

echo "🖼️  1/4  Boot-logo maken (rotatie ${ROT}°)…"
python3 "$REPO/make_bootlogo.py" --rotation "$ROT" --out "$RAW"

echo "⚙️  2/4  Service die het logo bij het opstarten toont…"
sudo tee "$UNIT" >/dev/null <<EOF
[Unit]
Description=priosense boot-logo op het schermpje
DefaultDependencies=no
Before=tetramonitor-gui.service

[Service]
Type=oneshot
# Wachten tot het schermpje (fb0) er is, dan het logo erop zetten.
ExecStart=/bin/sh -c 'for i in \$(seq 1 150); do [ -e /dev/fb0 ] && break; sleep 0.1; done; cat "$RAW" > /dev/fb0'

[Install]
WantedBy=sysinit.target
EOF
sudo systemctl daemon-reload
sudo systemctl enable priosense-bootlogo.service

echo "🔇 3/4  Pi-logo's en terminaltekst uitzetten…"
[ -f "$CMD.bak-priosense" ] || sudo cp "$CMD" "$CMD.bak-priosense"
[ -f "$CFG.bak-priosense" ] || sudo cp "$CFG" "$CFG.bak-priosense"
# cmdline.txt is één regel: console naar tty3 + opties toevoegen die er nog niet staan.
LINE=$(head -n1 "$CMD")
LINE=${LINE//console=tty1/console=tty3}
# Plymouth uit ('splash'): op Raspberry Pi OS Desktop toont dat "Welcome to the
# Raspberry Pi Desktop" vóór ons logo.
NEW=""
for T in $LINE; do [ "$T" = "splash" ] || NEW="$NEW $T"; done
LINE="${NEW# }"
for OPT in quiet loglevel=3 logo.nologo vt.global_cursor_default=0 consoleblank=0; do
    KEY=${OPT%%=*}
    case " $LINE " in
        *" $OPT "*|*" $KEY="*) ;;
        *) LINE="$LINE $OPT" ;;
    esac
done
echo "$LINE" | sudo tee "$CMD" >/dev/null
grep -q '^disable_splash=1' "$CFG" || echo 'disable_splash=1' | sudo tee -a "$CFG" >/dev/null
sudo systemctl disable getty@tty1.service

echo "👀 4/4  Voorproefje: het logo staat nu 6 seconden op het scherm…"
sudo systemctl stop tetramonitor-gui.service 2>/dev/null || true
sudo sh -c "cat '$RAW' > /dev/fb0"
sleep 6
sudo systemctl start tetramonitor-gui.service 2>/dev/null || true

echo
echo "✅ Klaar. Doe nu:  sudo reboot"
echo
echo "   Het logo staat net zo gedraaid als de app (--rotate $ROT). Pas je later"
echo "   --rotate in de service aan, draai dit script dan opnieuw."
echo
echo "   Alles terugdraaien:  ./install_bootscreen.sh --undo"
