#!/bin/sh
# Draait bij het opstarten via priosense-bootlogo.service (zie install_bootscreen.sh).
RAW="$(dirname "$0")/bootlogo.raw"

# Wachten tot het schermpje (fb0) er is.
for i in $(seq 1 150); do [ -e /dev/fb0 ] && break; sleep 0.1; done

# De tekstconsole loskoppelen van het schermpje, zodat er na het logo nooit meer
# terminaltekst overheen komt. Inloggen via SSH werkt gewoon; de app tekent zelf
# rechtstreeks op fb0.
for v in /sys/class/vtconsole/vtcon*; do
    grep -qi "frame buffer" "$v/name" 2>/dev/null && echo 0 > "$v/bind"
done

cat "$RAW" > /dev/fb0
