#pragma once
// Bambu report parsing, kept free of networking so it can be unit-tested on a PC
// (see test/parse). Merges a report's "print" object into the existing state, since
// P1/A1 printers only send changed fields.
#include <ArduinoJson.h>
#include "bambu.h"

void bambuParsePrint(JsonObjectConst print, PrinterState &state);
