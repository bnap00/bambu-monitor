// PC unit test for the Bambu report parser (src/printer/bambu_parse.cpp).
//   test/parse/run.sh
#include <cstdio>
#include "printer/bambu_parse.h"

extern "C" uint32_t millis() { return 0; }

static int failures = 0;
#define CHECK(cond)                                                     \
    do                                                                  \
    {                                                                   \
        if (!(cond))                                                    \
        {                                                               \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);      \
            failures++;                                                 \
        }                                                               \
    } while (0)

static void feed(PrinterState &s, const char *json)
{
    JsonDocument doc;
    DeserializationError e = deserializeJson(doc, json);
    CHECK(!e);
    bambuParsePrint(doc["print"], s);
}

static bool near(float a, float b) { return fabsf(a - b) < 0.01f; }

static void a1Deltas()
{
    PrinterState s;
    feed(s, R"({"print":{"gcode_state":"RUNNING","mc_percent":9,"mc_remaining_time":240,
        "layer_num":24,"total_layer_num":325,"subtask_name":"clips","nozzle_temper":209.97,
        "nozzle_target_temper":210,"bed_temper":54.9,"bed_target_temper":55,"chamber_temper":5,
        "cooling_fan_speed":"15","spd_lvl":2,"wifi_signal":"-48dBm","stg_cur":0,
        "ams":{"tray_now":"2","ams":[{"id":"0","humidity":"4","temp":"27.5","tray":[
          {"id":"0","tray_type":"PLA","tray_color":"F2F2F2FF","remain":80},
          {"id":"1"},
          {"id":"2","tray_type":"PETG","tray_color":"161616FF","remain":35}]}]}}})");
    CHECK(s.state == GState::Running);
    CHECK(s.percent == 9 && s.remainingMin == 240 && s.layer == 24 && s.totalLayers == 325);
    CHECK(near(s.nozzle, 209.97f) && near(s.nozzleTarget, 210));
    CHECK(isnan(s.chamber)); // A1 reports a fake 5 degC
    CHECK(s.partFan == 100 && s.wifiDbm == -48 && s.speedLevel == 2);
    CHECK(s.amsCount == 1 && s.ams[0].present && s.ams[0].trayCount == 4);
    CHECK(s.ams[0].trays[0].present && !s.ams[0].trays[1].present && s.ams[0].trays[2].remain == 35);
    CHECK(s.ams[0].trays[0].rgba == 0xF2F2F2FF);
    CHECK(s.trayNow == 2);

    // delta: only a couple of fields change, the rest must be kept
    feed(s, R"({"print":{"mc_percent":10,"layer_num":25}})");
    CHECK(s.percent == 10 && s.layer == 25 && s.remainingMin == 240 && near(s.bed, 54.9f));
    CHECK(strcmp(s.job, "clips") == 0 && s.ams[0].trays[2].present);

    feed(s, R"({"print":{"gcode_state":"PAUSE","stg_cur":6,"print_error":117473297,
        "hms":[{"attr":83886592,"code":196610}]}})");
    CHECK(s.state == GState::Pause && strcmp(stageName(s.stage), "Filament runout") == 0);
    CHECK(s.printError == 117473297u && s.hmsCount == 1 && (s.hms[0].code >> 16) == 3);

    feed(s, R"({"print":{"hms":[]}})");
    CHECK(s.hmsCount == 0);
}

static void x2dDualNozzle()
{
    PrinterState s;
    // left nozzle (id 1) active, loaded from AMS 0 slot 2; right idle
    feed(s, R"({"print":{"gcode_state":"RUNNING","nozzle_temper":999,"bed_temper":1,
        "device":{
          "extruder":{"state":18,"info":[
            {"id":0,"temp":38,"snow":65535},
            {"id":1,"temp":14418140,"snow":2}]},
          "bed":{"info":{"temp":3932215}},
          "ctc":{"info":{"temp":35}}},
        "ams":{"tray_now":"255","ams":[
          {"id":"0","tray":[{"id":"2","tray_type":"PLA","tray_color":"E5372BFF"}]},
          {"id":"128","humidity_raw":"18","tray":[{"id":"0","tray_type":"PA-CF","tray_color":"222222FF"}]}]}}})");
    CHECK(s.nozzleCount == 2 && s.activeNozzle == 1);
    CHECK(near(s.nozzleL, 220) && near(s.nozzleLTarget, 220)); // 14418140 = 220<<16 | 220
    CHECK(near(s.nozzleR, 38) && near(s.nozzleRTarget, 0));
    CHECK(near(s.nozzle, 220)); // active nozzle, legacy nozzle_temper ignored
    CHECK(near(s.bed, 55) && near(s.bedTarget, 60));  // 3932215 = 60<<16 | 55
    CHECK(near(s.chamber, 35));
    CHECK(s.trayNow == 2); // from snow, not tray_now
    int ht = -1;
    for (int i = 0; i < MAX_AMS; i++)
        if (s.ams[i].present && s.ams[i].hwId == 128)
            ht = i;
    CHECK(ht >= 0 && ht != 0 && s.ams[ht].trayCount == 1 && s.ams[ht].humidityPct == 18);
    CHECK(s.ams[0].hwId == 0 && s.ams[0].trays[2].present);

    // switch to the right nozzle feeding from the AMS HT
    feed(s, R"({"print":{"device":{"extruder":{"state":2,"info":[{"id":0,"temp":15990990,"snow":32768}]}}}})");
    CHECK(s.activeNozzle == 0 && near(s.nozzle, 206) && s.trayNow == ht * 4); // 15990990 = 244<<16 | 206
    feed(s, R"({"print":{"device":{"extruder":{"state":2,"info":[{"id":0,"snow":65535}]}}}})");
    CHECK(s.trayNow == -1);
    feed(s, R"({"print":{"device":{"extruder":{"state":2,"info":[{"id":0,"snow":65024}]}}}})");
    CHECK(s.trayNow == 254); // external spool (ams id 254)
}

int main()
{
    a1Deltas();
    x2dDualNozzle();
    printf(failures ? "%d check(s) failed\n" : "all parser checks passed\n", failures);
    return failures ? 1 : 0;
}
