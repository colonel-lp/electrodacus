#pragma once

#include <ArduinoJson.h>

#include "sbmsData.hpp"
#include "cell_minmax.hpp"

// Serialise a decoded SBMS frame into the JSON shape consumed by the web UI and
// the MQTT publisher. Honours g_settings.data_sbms_diff for the optional
// cell-voltage delta field. `hist` supplies the rolling 24 h per-cell min/max
// emitted as cellsMinMV / cellsMaxMV (0 mV entries mean "no data yet").
void toJsonSBMS(const SbmsData &sbms, const CellMinMax &hist, JsonDocument &doc);
