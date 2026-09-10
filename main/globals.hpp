#pragma once

#include "jsvarStore.hpp"
#include "cell_minmax.hpp"

// Shared parser/store for the SBMS "var name=...;" stream.
// Defined in main.cpp, written by the UART task, read by the web server and the
// main loop.
extern JsvarStore varStore;

// Rolling ~24 h per-cell voltage min/max. Defined in main.cpp; only ever touched
// from the main loop task.
extern CellMinMax g_cellMinMax;

// Set from the web OTA handler; the main loop reboots the chip when it is true.
extern volatile bool g_should_reboot;

// Set from the web handler for POST /cells/reset; the main loop clears the
// per-cell 24 h min/max history on the next SBMS frame and clears the flag.
extern volatile bool g_cell_minmax_reset;
