#pragma once
#include <Arduino.h>
#include "net/settings.h"

enum class GState : uint8_t
{
    Unknown,
    Idle,
    Prepare,
    Running,
    Pause,
    Finish,
    Failed,
    Slicing,
};

enum class Link : uint8_t
{
    Disabled,
    Connecting,
    Online,
    AuthFailed,
    Offline,
};

struct AmsTray
{
    bool present = false;
    char type[12] = "";
    uint32_t rgba = 0; // 0xRRGGBBAA
    int8_t remain = -1; // %, -1 unknown
};

struct AmsUnit
{
    bool present = false;
    int16_t hwId = -1;     // Bambu's AMS id: 0..3 for AMS / AMS lite, 128+ for AMS HT
    uint8_t trayCount = 4; // AMS HT has a single slot
    int8_t humidityLevel = -1; // Bambu's 1..5 humidity index, -1 unknown
    int8_t humidityPct = -1;   // newer firmware reports raw %RH
    float tempC = NAN;
    AmsTray trays[4];
};

#define MAX_AMS 4
#define MAX_HMS 6

struct HmsEntry
{
    uint32_t attr;
    uint32_t code;
};

struct PrinterState
{
    Link link = Link::Disabled;
    uint32_t lastMsgMs = 0;
    uint32_t updateCount = 0;

    GState state = GState::Unknown;
    int stage = -1; // stg_cur
    int percent = 0;
    int remainingMin = 0;
    int layer = 0;
    int totalLayers = 0;
    char job[64] = "";

    float nozzle = NAN, nozzleTarget = NAN; // active nozzle on dual-nozzle printers
    // Dual-nozzle printers (H2D, X2D): extruder 0 = right, 1 = left
    uint8_t nozzleCount = 1;
    int8_t activeNozzle = 0;
    float nozzleR = NAN, nozzleRTarget = NAN;
    float nozzleL = NAN, nozzleLTarget = NAN;
    float bed = NAN, bedTarget = NAN;
    float chamber = NAN;

    int partFan = -1, auxFan = -1, chamberFan = -1; // percent
    int speedLevel = 0; // 1 silent, 2 standard, 3 sport, 4 ludicrous
    int wifiDbm = 0;
    bool chamberLight = false;
    bool hasChamberLight = false;

    uint32_t printError = 0;
    HmsEntry hms[MAX_HMS];
    uint8_t hmsCount = 0;

    AmsUnit ams[MAX_AMS];
    uint8_t amsCount = 0;
    int trayNow = -1; // ams slot*4 + tray, 254 = external, -1 none
    AmsTray external;
};

enum class PrinterCmd : uint8_t
{
    Pause,
    Resume,
    Stop,
    LightOn,
    LightOff,
    Speed,   // arg: 1..4
    PushAll, // request a full status refresh
};

// Starts the network task that keeps one TLS MQTT session per configured printer.
void bambuBegin();
// Re-read printer configs (after the web UI saved new settings).
void bambuReconfigure();

// Thread-safe snapshot for the UI.
PrinterState bambuGet(int idx);
bool bambuEnabled(int idx);
String bambuName(int idx);

// Queue a command for the printer (sent from the network task).
void bambuCommand(int idx, PrinterCmd cmd, int arg = 0);

const char *gstateName(GState s);
const char *stageName(int stage);
bool gstateBusy(GState s);
