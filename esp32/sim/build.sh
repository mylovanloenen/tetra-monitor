#!/bin/sh
# Bouwt de simulator (Mac): brew install sdl2, en één keer `pio run` in esp32/
# zodat LovyanGFX in .pio/libdeps staat.
set -e
cd "$(dirname "$0")"
LGFX=../.pio/libdeps/sc01plus/LovyanGFX/src
mkdir -p obj
SRCS="sim_main.cpp ../src/ui.cpp ../src/detector.cpp \
  $(ls $LGFX/lgfx/v1/*.cpp $LGFX/lgfx/v1/misc/*.cpp $LGFX/lgfx/v1/platforms/sdl/*.cpp) \
  $LGFX/lgfx/v1/panel/Panel_Device.cpp $LGFX/lgfx/v1/panel/Panel_FrameBufferBase.cpp"
CSRCS="$(ls $LGFX/lgfx/utility/*.c $LGFX/lgfx/Fonts/efont/*.c $LGFX/lgfx/Fonts/IPA/*.c)"
FLAGS="-O2 -DLGFX_SDL -DPRIOSENSE_SIM -Ishim -I$LGFX $(sdl2-config --cflags)"
for f in $CSRCS; do cc $FLAGS -c $f -o obj/$(basename $f).o; done
c++ -std=c++17 $FLAGS $SRCS obj/*.o $(sdl2-config --libs) -o priosense_sim
echo "klaar: ./priosense_sim"
