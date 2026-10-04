#include "bambu_parse.h"

// ------------------------------------------------------------ parsing helpers

static int asInt(JsonVariantConst v, int def = 0)
{
    if (v.is<const char *>())
        return atoi(v.as<const char *>());
    if (v.is<int>() || v.is<float>())
        return v.as<int>();
    return def;
}

static float asFloat(JsonVariantConst v, float def = NAN)
{
    if (v.is<const char *>())
        return atof(v.as<const char *>());
    if (v.is<float>() || v.is<int>())
        return v.as<float>();
    return def;
}

static GState parseState(const char *s)
{
    if (!s)
        return GState::Unknown;
    if (!strcmp(s, "IDLE"))
        return GState::Idle;
    if (!strcmp(s, "PREPARE"))
        return GState::Prepare;
    if (!strcmp(s, "RUNNING"))
        return GState::Running;
    if (!strcmp(s, "PAUSE"))
        return GState::Pause;
    if (!strcmp(s, "FINISH"))
        return GState::Finish;
    if (!strcmp(s, "FAILED"))
        return GState::Failed;
    if (!strcmp(s, "SLICING"))
        return GState::Slicing;
    return GState::Unknown;
}

static int fanPercent(JsonVariantConst v)
{
    int raw = asInt(v, -1); // 0..15 scale
    return raw < 0 ? -1 : (raw * 100 + 7) / 15;
}

static void parseTray(JsonObjectConst t, AmsTray &tray)
{
    if (t.size() <= 1) // only "id": slot is empty
    {
        tray = AmsTray();
        return;
    }
    if (t["tray_type"].is<const char *>())
    {
        const char *type = t["tray_type"];
        strlcpy(tray.type, type, sizeof(tray.type));
        tray.present = type[0] != 0;
    }
    if (t["tray_color"].is<const char *>())
        tray.rgba = strtoul(t["tray_color"].as<const char *>(), nullptr, 16);
    if (!t["remain"].isNull())
        tray.remain = asInt(t["remain"], -1);
}

// Newer firmware (H2*, X2D, some X1/P2) packs temperatures as (target << 16) | current
static void unpackTemp(JsonVariantConst v, float &cur, float *target)
{
    long t = (long)v.as<long long>();
    cur = t & 0xFFFF;
    if (target)
        *target = (t >> 16) & 0xFFFF;
}

// AMS units are stored by slot; regular AMS ids 0..3 keep their slot, AMS HT (128+)
// fills free slots from the end.
static int amsSlot(PrinterState &s, int hwId)
{
    for (int i = 0; i < MAX_AMS; i++)
        if (s.ams[i].present && s.ams[i].hwId == hwId)
            return i;
    if (hwId >= 0 && hwId < MAX_AMS && !s.ams[hwId].present)
        return hwId;
    for (int i = MAX_AMS - 1; i >= 0; i--)
        if (!s.ams[i].present)
            return i;
    return -1;
}

static void parseDevice(JsonObjectConst d, PrinterState &s)
{
    JsonObjectConst ex = d["extruder"];
    if (!ex.isNull())
    {
        if (!ex["state"].isNull())
        {
            int st = asInt(ex["state"]);
            s.nozzleCount = max(1, st & 0xF);
            s.activeNozzle = (st >> 4) & 0xF;
        }
        int activeSnow = -1;
        if (ex["info"].is<JsonArrayConst>())
        {
            for (JsonObjectConst e : ex["info"].as<JsonArrayConst>())
            {
                int id = asInt(e["id"], -1);
                if (!e["temp"].isNull())
                {
                    if (id == 0)
                        unpackTemp(e["temp"], s.nozzleR, &s.nozzleRTarget);
                    else if (id == 1)
                        unpackTemp(e["temp"], s.nozzleL, &s.nozzleLTarget);
                }
                if (id == s.activeNozzle && !e["snow"].isNull())
                    activeSnow = asInt(e["snow"], -1);
            }
        }
        bool left = s.nozzleCount > 1 && s.activeNozzle == 1;
        s.nozzle = left ? s.nozzleL : s.nozzleR;
        s.nozzleTarget = left ? s.nozzleLTarget : s.nozzleRTarget;

        // snow = (ams id << 8) | tray for the filament loaded in the active nozzle
        if (activeSnow >= 0)
        {
            int amsId = activeSnow >> 8, tray = activeSnow & 0x3;
            if (activeSnow == 0xFFFF)
                s.trayNow = -1;
            else if (amsId == 254 || amsId == 255)
                s.trayNow = 254;
            else
            {
                int slot = amsSlot(s, amsId);
                s.trayNow = slot >= 0 ? slot * 4 + tray : -1;
            }
        }
    }
    if (!d["bed"]["info"]["temp"].isNull())
        unpackTemp(d["bed"]["info"]["temp"], s.bed, &s.bedTarget);
    if (!d["ctc"]["info"]["temp"].isNull())
    {
        float c;
        unpackTemp(d["ctc"]["info"]["temp"], c, nullptr);
        s.chamber = c > 6 ? c : NAN;
    }
}

void bambuParsePrint(JsonObjectConst p, PrinterState &s)
{
    bool newTemps = !p["device"]["extruder"]["info"].isNull();
    if (p["gcode_state"].is<const char *>())
        s.state = parseState(p["gcode_state"]);
    if (!p["mc_percent"].isNull())
        s.percent = constrain(asInt(p["mc_percent"]), 0, 100);
    if (!p["mc_remaining_time"].isNull())
        s.remainingMin = max(0, asInt(p["mc_remaining_time"]));
    if (!p["layer_num"].isNull())
        s.layer = asInt(p["layer_num"]);
    if (!p["total_layer_num"].isNull())
        s.totalLayers = asInt(p["total_layer_num"]);
    if (!p["stg_cur"].isNull())
        s.stage = asInt(p["stg_cur"], -1);
    if (p["subtask_name"].is<const char *>())
        strlcpy(s.job, p["subtask_name"], sizeof(s.job));

    // Legacy single-nozzle fields; dual-nozzle printers use device.extruder instead
    if (!newTemps && s.nozzleCount < 2)
    {
        if (!p["nozzle_temper"].isNull())
            s.nozzle = s.nozzleR = asFloat(p["nozzle_temper"]);
        if (!p["nozzle_target_temper"].isNull())
            s.nozzleTarget = s.nozzleRTarget = asFloat(p["nozzle_target_temper"]);
    }
    if (!p["bed_temper"].isNull())
        s.bed = asFloat(p["bed_temper"]);
    if (!p["bed_target_temper"].isNull())
        s.bedTarget = asFloat(p["bed_target_temper"]);
    if (!p["chamber_temper"].isNull())
    {
        // Printers without a chamber sensor (A1/P1) report a constant ~5
        float c = asFloat(p["chamber_temper"]);
        s.chamber = c > 6 ? c : NAN;
    }

    if (!p["cooling_fan_speed"].isNull())
        s.partFan = fanPercent(p["cooling_fan_speed"]);
    if (!p["big_fan1_speed"].isNull())
        s.auxFan = fanPercent(p["big_fan1_speed"]);
    if (!p["big_fan2_speed"].isNull())
        s.chamberFan = fanPercent(p["big_fan2_speed"]);
    if (!p["spd_lvl"].isNull())
        s.speedLevel = asInt(p["spd_lvl"]);
    if (!p["wifi_signal"].isNull())
        s.wifiDbm = asInt(p["wifi_signal"]);
    if (!p["print_error"].isNull())
        s.printError = (uint32_t)p["print_error"].as<long long>();

    if (p["hms"].is<JsonArrayConst>())
    {
        s.hmsCount = 0;
        for (JsonObjectConst h : p["hms"].as<JsonArrayConst>())
        {
            if (s.hmsCount >= MAX_HMS)
                break;
            s.hms[s.hmsCount++] = {(uint32_t)h["attr"].as<long long>(), (uint32_t)h["code"].as<long long>()};
        }
    }

    if (p["lights_report"].is<JsonArrayConst>())
    {
        for (JsonObjectConst l : p["lights_report"].as<JsonArrayConst>())
        {
            if (l["node"] == "chamber_light")
            {
                s.hasChamberLight = true;
                s.chamberLight = l["mode"] != "off";
            }
        }
    }

    if (p["vt_tray"].is<JsonObjectConst>())
        parseTray(p["vt_tray"], s.external);

    JsonObjectConst ams = p["ams"];
    if (!ams.isNull())
    {
        if (!ams["tray_now"].isNull() && s.nozzleCount < 2) // dual nozzle: from extruder snow
        {
            int t = asInt(ams["tray_now"], 255);
            s.trayNow = (t == 255) ? -1 : t;
        }
        if (ams["ams"].is<JsonArrayConst>())
        {
            JsonArrayConst units = ams["ams"];
            if (units.size() == 0)
                s.amsCount = 0;
            for (JsonObjectConst u : units)
            {
                int id = asInt(u["id"], -1);
                int slot = id < 0 ? -1 : amsSlot(s, id);
                if (slot < 0)
                    continue;
                AmsUnit &unit = s.ams[slot];
                unit.present = true;
                unit.hwId = id;
                unit.trayCount = id >= 128 ? 1 : 4;
                if (!u["humidity"].isNull())
                    unit.humidityLevel = asInt(u["humidity"], -1);
                if (!u["humidity_raw"].isNull())
                    unit.humidityPct = asInt(u["humidity_raw"], -1);
                if (!u["temp"].isNull())
                    unit.tempC = asFloat(u["temp"]);
                if (u["tray"].is<JsonArrayConst>())
                {
                    for (JsonObjectConst t : u["tray"].as<JsonArrayConst>())
                    {
                        int tid = asInt(t["id"], -1);
                        if (tid >= 0 && tid < 4)
                            parseTray(t, unit.trays[tid]);
                    }
                }
                s.amsCount = max<uint8_t>(s.amsCount, slot + 1);
            }
        }
    }

    // After AMS parsing so the active tray can be mapped to an AMS slot
    if (p["device"].is<JsonObjectConst>())
        parseDevice(p["device"], s);
}

// ------------------------------------------------------------ names

const char *gstateName(GState s)
{
    switch (s)
    {
    case GState::Idle: return "Idle";
    case GState::Prepare: return "Preparing";
    case GState::Running: return "Printing";
    case GState::Pause: return "Paused";
    case GState::Finish: return "Finished";
    case GState::Failed: return "Failed";
    case GState::Slicing: return "Slicing";
    default: return "Unknown";
    }
}

bool gstateBusy(GState s)
{
    return s == GState::Prepare || s == GState::Running || s == GState::Pause || s == GState::Slicing;
}

const char *stageName(int stage)
{
    // stg_cur values as reported by Bambu firmware
    static const char *names[] = {
        "Printing",                    // 0
        "Auto bed leveling",           // 1
        "Heatbed preheating",          // 2
        "Vibration compensation",      // 3
        "Changing filament",           // 4
        "M400 pause",                  // 5
        "Filament runout",             // 6
        "Heating hotend",              // 7
        "Calibrating extrusion",       // 8
        "Scanning bed surface",        // 9
        "Inspecting first layer",      // 10
        "Identifying build plate",     // 11
        "Calibrating Micro Lidar",     // 12
        "Homing toolhead",             // 13
        "Cleaning nozzle tip",         // 14
        "Checking extruder temp",      // 15
        "Paused by user",              // 16
        "Front cover fell off",        // 17
        "Calibrating Micro Lidar",     // 18
        "Calibrating extrusion flow",  // 19
        "Nozzle temp malfunction",     // 20
        "Heatbed temp malfunction",    // 21
        "Unloading filament",          // 22
        "Skip step pause",             // 23
        "Loading filament",            // 24
        "Calibrating motor noise",     // 25
        "AMS lost",                    // 26
        "Heatbreak fan too slow",      // 27
        "Chamber temp control error",  // 28
        "Cooling chamber",             // 29
        "Paused by G-code",            // 30
        "Motor noise showoff",         // 31
        "Nozzle filament covered",     // 32
        "Cutter error",                // 33
        "First layer error",           // 34
        "Nozzle clog",                 // 35
    };
    if (stage < 0 || stage >= (int)(sizeof(names) / sizeof(names[0])))
        return "";
    return names[stage];
}
