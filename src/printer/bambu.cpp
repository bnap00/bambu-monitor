#include "bambu.h"
#include "bambu_parse.h"
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <NetworkClientSecure.h>

// Bambu printers expose an MQTT broker on port 8883 (TLS, self-signed cert) for LAN
// clients: user "bblp", password = LAN access code. Status arrives on
// device/<serial>/report; commands go to device/<serial>/request. This works while
// the printer stays bound to Bambu Cloud; P1/A1 send deltas, so state is merged.

static const uint16_t MQTT_PORT = 8883;
static const size_t MQTT_BUFFER = 48 * 1024; // full X1/H2 reports are large
static const uint32_t STALE_PUSHALL_MS = 120000;
static const uint32_t STALE_RECONNECT_MS = 300000;

struct Slot
{
    PrinterConfig cfg;
    NetworkClientSecure tls;
    PubSubClient mqtt{tls};
    PrinterState st;
    uint32_t nextAttemptMs = 0;
    uint32_t backoffMs = 2000;
    uint32_t seq = 0;
    bool pushallSent = false;
};

struct CmdMsg
{
    uint8_t idx;
    PrinterCmd cmd;
    int arg;
};

static Slot *slots[MAX_PRINTERS];
static SemaphoreHandle_t lock;
static QueueHandle_t cmdQueue;
static volatile bool reconfigure = false;

// ------------------------------------------------------------ MQTT

static void onMessage(int idx, char *topic, uint8_t *payload, unsigned int len)
{
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload, len);
    if (err)
    {
        Serial.printf("[bambu%d] json error: %s (%u bytes)\n", idx, err.c_str(), len);
        return;
    }
    JsonObjectConst p = doc["print"];
    xSemaphoreTake(lock, portMAX_DELAY);
    PrinterState &s = slots[idx]->st;
    s.lastMsgMs = millis();
    s.updateCount++;
    if (!p.isNull())
        bambuParsePrint(p, s);
    xSemaphoreGive(lock);
}

static void publish(Slot &sl, JsonDocument &doc)
{
    char buf[512];
    size_t n = serializeJson(doc, buf, sizeof(buf));
    String topic = "device/" + sl.cfg.serial + "/request";
    sl.mqtt.publish(topic.c_str(), (const uint8_t *)buf, n, false);
}

static void sendPushAll(Slot &sl)
{
    JsonDocument doc;
    doc["pushing"]["sequence_id"] = String(sl.seq++);
    doc["pushing"]["command"] = "pushall";
    doc["pushing"]["version"] = 1;
    doc["pushing"]["push_target"] = 1;
    publish(sl, doc);
    sl.pushallSent = true;
}

static void sendCommand(Slot &sl, PrinterCmd cmd, int arg)
{
    JsonDocument doc;
    switch (cmd)
    {
    case PrinterCmd::Pause:
    case PrinterCmd::Resume:
    case PrinterCmd::Stop:
    {
        JsonObject p = doc["print"].to<JsonObject>();
        p["sequence_id"] = String(sl.seq++);
        p["command"] = cmd == PrinterCmd::Pause ? "pause" : cmd == PrinterCmd::Resume ? "resume" : "stop";
        p["param"] = "";
        break;
    }
    case PrinterCmd::Speed:
    {
        JsonObject p = doc["print"].to<JsonObject>();
        p["sequence_id"] = String(sl.seq++);
        p["command"] = "print_speed";
        p["param"] = String(constrain(arg, 1, 4));
        break;
    }
    case PrinterCmd::LightOn:
    case PrinterCmd::LightOff:
    {
        JsonObject s = doc["system"].to<JsonObject>();
        s["sequence_id"] = String(sl.seq++);
        s["command"] = "ledctrl";
        s["led_node"] = "chamber_light";
        s["led_mode"] = cmd == PrinterCmd::LightOn ? "on" : "off";
        s["led_on_time"] = 500;
        s["led_off_time"] = 500;
        s["loop_times"] = 0;
        s["interval_time"] = 0;
        break;
    }
    case PrinterCmd::PushAll:
        sendPushAll(sl);
        return;
    }
    publish(sl, doc);
}

static void setLink(Slot &sl, Link l)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    sl.st.link = l;
    xSemaphoreGive(lock);
}

static void tryConnect(int idx)
{
    Slot &sl = *slots[idx];
    setLink(sl, Link::Connecting);

    sl.tls.stop();
    sl.tls.setInsecure(); // printers use a self-signed certificate
    sl.tls.setHandshakeTimeout(12);
    sl.tls.setTimeout(8000);

    String clientId = "bambumon-" + String((uint32_t)ESP.getEfuseMac(), HEX) + "-" + idx;
    Serial.printf("[bambu%d] connecting to %s (%s)\n", idx, sl.cfg.host.c_str(), sl.cfg.serial.c_str());
    if (sl.mqtt.connect(clientId.c_str(), "bblp", sl.cfg.code.c_str()))
    {
        String topic = "device/" + sl.cfg.serial + "/report";
        sl.mqtt.subscribe(topic.c_str());
        sendPushAll(sl);
        sl.backoffMs = 2000;
        xSemaphoreTake(lock, portMAX_DELAY);
        sl.st.link = Link::Online;
        sl.st.lastMsgMs = millis();
        xSemaphoreGive(lock);
        Serial.printf("[bambu%d] online\n", idx);
        return;
    }

    int rc = sl.mqtt.state();
    bool auth = rc == MQTT_CONNECT_BAD_CREDENTIALS || rc == MQTT_CONNECT_UNAUTHORIZED;
    Serial.printf("[bambu%d] connect failed rc=%d%s\n", idx, rc, auth ? " (check access code)" : "");
    setLink(sl, auth ? Link::AuthFailed : Link::Offline);
    sl.nextAttemptMs = millis() + (auth ? 60000 : sl.backoffMs);
    sl.backoffMs = min<uint32_t>(sl.backoffMs * 2, 60000);
}

static void buildSlots()
{
    for (int i = 0; i < MAX_PRINTERS; i++)
    {
        if (slots[i])
        {
            slots[i]->mqtt.disconnect();
            slots[i]->tls.stop();
        }
        else
        {
            slots[i] = new Slot();
        }
        Slot &sl = *slots[i];
        xSemaphoreTake(lock, portMAX_DELAY);
        sl.cfg = settings.printers[i];
        sl.st = PrinterState();
        bool ok = sl.cfg.enabled && sl.cfg.host.length() && sl.cfg.serial.length() && sl.cfg.code.length();
        sl.st.link = ok ? Link::Offline : Link::Disabled;
        xSemaphoreGive(lock);
        sl.nextAttemptMs = 0;
        sl.backoffMs = 2000;
        sl.mqtt.setServer(sl.cfg.host.c_str(), MQTT_PORT);
        sl.mqtt.setBufferSize(MQTT_BUFFER);
        sl.mqtt.setKeepAlive(30);
        sl.mqtt.setSocketTimeout(10);
        sl.mqtt.setCallback([i](char *t, uint8_t *p, unsigned int l) { onMessage(i, t, p, l); });
    }
}

static void netTask(void *)
{
    buildSlots();
    for (;;)
    {
        if (reconfigure)
        {
            reconfigure = false;
            buildSlots();
        }

        CmdMsg m;
        while (xQueueReceive(cmdQueue, &m, 0) == pdTRUE)
        {
            Slot &sl = *slots[m.idx];
            if (sl.mqtt.connected())
                sendCommand(sl, m.cmd, m.arg);
        }

        bool wifiUp = WiFi.status() == WL_CONNECTED;
        for (int i = 0; i < MAX_PRINTERS; i++)
        {
            Slot &sl = *slots[i];
            if (sl.st.link == Link::Disabled)
                continue;

            if (!wifiUp)
            {
                if (sl.mqtt.connected())
                    sl.mqtt.disconnect();
                if (sl.st.link != Link::Offline)
                    setLink(sl, Link::Offline);
                continue;
            }

            if (sl.mqtt.connected())
            {
                sl.mqtt.loop();
                uint32_t silent = millis() - sl.st.lastMsgMs;
                if (silent > STALE_RECONNECT_MS)
                {
                    Serial.printf("[bambu%d] no data for %lus, reconnecting\n", i, silent / 1000);
                    sl.mqtt.disconnect();
                }
                else if (silent > STALE_PUSHALL_MS && !sl.pushallSent)
                {
                    sendPushAll(sl);
                }
                else if (silent < STALE_PUSHALL_MS)
                {
                    sl.pushallSent = false;
                }
            }
            else
            {
                if (sl.st.link == Link::Online)
                {
                    Serial.printf("[bambu%d] disconnected\n", i);
                    setLink(sl, Link::Offline);
                    sl.nextAttemptMs = millis() + 1000;
                }
                if ((int32_t)(millis() - sl.nextAttemptMs) >= 0)
                    tryConnect(i);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// ------------------------------------------------------------ public API

void bambuBegin()
{
    lock = xSemaphoreCreateMutex();
    cmdQueue = xQueueCreate(8, sizeof(CmdMsg));
    // TLS + JSON parsing need a roomy stack; pinned away from the UI core
    xTaskCreatePinnedToCore(netTask, "bambu", 16384, nullptr, 2, nullptr, 0);
}

void bambuReconfigure()
{
    reconfigure = true;
}

PrinterState bambuGet(int idx)
{
    PrinterState s;
    if (idx < 0 || idx >= MAX_PRINTERS || !slots[idx])
        return s;
    xSemaphoreTake(lock, portMAX_DELAY);
    s = slots[idx]->st;
    xSemaphoreGive(lock);
    return s;
}

bool bambuEnabled(int idx)
{
    const PrinterConfig &p = settings.printers[idx];
    return p.enabled && p.host.length() && p.serial.length() && p.code.length();
}

String bambuName(int idx)
{
    const PrinterConfig &p = settings.printers[idx];
    if (p.name.length())
        return p.name;
    return "Printer " + String(idx + 1);
}

void bambuCommand(int idx, PrinterCmd cmd, int arg)
{
    if (idx < 0 || idx >= MAX_PRINTERS || !cmdQueue)
        return;
    CmdMsg m{(uint8_t)idx, cmd, arg};
    xQueueSend(cmdQueue, &m, 0);
}
