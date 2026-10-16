// Welk scherm: de echte SC01 Plus, of een venster op de Mac (sim/).
#pragma once
#if defined(PRIOSENSE_SIM)
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <LGFX_AUTODETECT.hpp>
using Display = LGFX;
#else
#include "board_sc01plus.h"
using Display = LGFX_SC01Plus;
#endif
