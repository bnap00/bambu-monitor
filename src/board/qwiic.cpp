#include "qwiic.h"
#include "board_pins.h"
#include <Wire.h>

static TwoWire &bus = Wire1;
static QwiicReadings data;
static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

enum class Climate : uint8_t { None, AHT20, SHT4x, SHT3x, BME280, BMP280 };
enum class Light : uint8_t { None, BH1750, VEML7700 };

static Climate climate = Climate::None;
static uint8_t climateAddr = 0;
static Light light = Light::None;
static uint8_t lightAddr = 0;

// ------------------------------------------------------------ helpers

static bool probe(uint8_t a)
{
    bus.beginTransmission(a);
    return bus.endTransmission() == 0;
}

static bool writeBytes(uint8_t a, const uint8_t *b, size_t n)
{
    bus.beginTransmission(a);
    bus.write(b, n);
    return bus.endTransmission() == 0;
}

static bool readBytes(uint8_t a, uint8_t *b, size_t n)
{
    if (bus.requestFrom(a, (uint8_t)n) != n)
        return false;
    for (size_t i = 0; i < n; i++)
        b[i] = bus.read();
    return true;
}

static bool readReg(uint8_t a, uint8_t reg, uint8_t *b, size_t n)
{
    bus.beginTransmission(a);
    bus.write(reg);
    if (bus.endTransmission(false) != 0)
        return false;
    return readBytes(a, b, n);
}

static uint8_t sensirionCrc(const uint8_t *d, size_t n)
{
    uint8_t crc = 0xFF;
    for (size_t i = 0; i < n; i++)
    {
        crc ^= d[i];
        for (int b = 0; b < 8; b++)
            crc = (crc & 0x80) ? (crc << 1) ^ 0x31 : crc << 1;
    }
    return crc;
}

// ------------------------------------------------------------ BME280 / BMP280

static struct
{
    uint16_t T1;
    int16_t T2, T3;
    uint16_t P1;
    int16_t P2, P3, P4, P5, P6, P7, P8, P9;
    uint8_t H1, H3;
    int16_t H2, H4, H5;
    int8_t H6;
} bmeCal;

static bool bmeInit(uint8_t a, bool humidity)
{
    uint8_t c[26];
    if (!readReg(a, 0x88, c, 26))
        return false;
    bmeCal.T1 = c[0] | c[1] << 8;
    bmeCal.T2 = c[2] | c[3] << 8;
    bmeCal.T3 = c[4] | c[5] << 8;
    bmeCal.P1 = c[6] | c[7] << 8;
    bmeCal.P2 = c[8] | c[9] << 8;
    bmeCal.P3 = c[10] | c[11] << 8;
    bmeCal.P4 = c[12] | c[13] << 8;
    bmeCal.P5 = c[14] | c[15] << 8;
    bmeCal.P6 = c[16] | c[17] << 8;
    bmeCal.P7 = c[18] | c[19] << 8;
    bmeCal.P8 = c[20] | c[21] << 8;
    bmeCal.P9 = c[22] | c[23] << 8;
    bmeCal.H1 = c[25];
    if (humidity)
    {
        uint8_t h[7];
        if (!readReg(a, 0xE1, h, 7))
            return false;
        bmeCal.H2 = h[0] | h[1] << 8;
        bmeCal.H3 = h[2];
        bmeCal.H4 = (int16_t)((int8_t)h[3] << 4) | (h[4] & 0x0F);
        bmeCal.H5 = (int16_t)((int8_t)h[5] << 4) | (h[4] >> 4);
        bmeCal.H6 = (int8_t)h[6];
        const uint8_t ctrlHum[] = {0xF2, 0x01};
        writeBytes(a, ctrlHum, 2);
    }
    const uint8_t config[] = {0xF5, 0xA0}; // 1 s standby, filter off
    const uint8_t ctrlMeas[] = {0xF4, 0x27}; // T x1, P x1, normal mode
    return writeBytes(a, config, 2) && writeBytes(a, ctrlMeas, 2);
}

static bool bmeRead(uint8_t a, bool humidity, float &t, float &h, float &p)
{
    uint8_t d[8];
    if (!readReg(a, 0xF7, d, humidity ? 8 : 6))
        return false;
    int32_t adcP = (d[0] << 12) | (d[1] << 4) | (d[2] >> 4);
    int32_t adcT = (d[3] << 12) | (d[4] << 4) | (d[5] >> 4);

    int32_t v1 = ((((adcT >> 3) - ((int32_t)bmeCal.T1 << 1))) * bmeCal.T2) >> 11;
    int32_t v2 = (((((adcT >> 4) - bmeCal.T1) * ((adcT >> 4) - bmeCal.T1)) >> 12) * bmeCal.T3) >> 14;
    int32_t tFine = v1 + v2;
    t = ((tFine * 5 + 128) >> 8) / 100.0f;

    int64_t q1 = (int64_t)tFine - 128000;
    int64_t q2 = q1 * q1 * bmeCal.P6;
    q2 += (q1 * bmeCal.P5) << 17;
    q2 += ((int64_t)bmeCal.P4) << 35;
    q1 = ((q1 * q1 * bmeCal.P3) >> 8) + ((q1 * bmeCal.P2) << 12);
    q1 = ((((int64_t)1) << 47) + q1) * bmeCal.P1 >> 33;
    if (q1 != 0)
    {
        int64_t pr = 1048576 - adcP;
        pr = (((pr << 31) - q2) * 3125) / q1;
        q1 = (((int64_t)bmeCal.P9) * (pr >> 13) * (pr >> 13)) >> 25;
        q2 = (((int64_t)bmeCal.P8) * pr) >> 19;
        pr = ((pr + q1 + q2) >> 8) + (((int64_t)bmeCal.P7) << 4);
        p = pr / 25600.0f;
    }

    if (humidity)
    {
        int32_t adcH = (d[6] << 8) | d[7];
        int32_t x = tFine - 76800;
        x = (((((adcH << 14) - (((int32_t)bmeCal.H4) << 20) - (((int32_t)bmeCal.H5) * x)) + 16384) >> 15) *
             (((((((x * ((int32_t)bmeCal.H6)) >> 10) * (((x * ((int32_t)bmeCal.H3)) >> 11) + 32768)) >> 10) + 2097152) *
                   ((int32_t)bmeCal.H2) + 8192) >> 14));
        x = x - (((((x >> 15) * (x >> 15)) >> 7) * ((int32_t)bmeCal.H1)) >> 4);
        x = constrain(x, 0, 419430400);
        h = (x >> 12) / 1024.0f;
    }
    return true;
}

// ------------------------------------------------------------ detection

static void detect()
{
    QwiicReadings r;
    for (uint8_t a = 0x08; a < 0x78 && r.foundCount < sizeof(r.found); a++)
        if (probe(a))
            r.found[r.foundCount++] = a;

    climate = Climate::None;
    light = Light::None;
    auto seen = [&](uint8_t a) {
        for (uint8_t i = 0; i < r.foundCount; i++)
            if (r.found[i] == a)
                return true;
        return false;
    };

    if (seen(0x38))
    {
        const uint8_t initCmd[] = {0xBE, 0x08, 0x00};
        writeBytes(0x38, initCmd, 3);
        delay(10);
        climate = Climate::AHT20;
        climateAddr = 0x38;
    }
    for (uint8_t a : {0x44, 0x45})
    {
        if (climate != Climate::None || !seen(a))
            continue;
        // SHT4x answers single-byte 0xFD with a valid CRC'd measurement; SHT3x does not
        const uint8_t cmd = 0xFD;
        uint8_t d[6];
        if (writeBytes(a, &cmd, 1) && (delay(12), readBytes(a, d, 6)) && sensirionCrc(d, 2) == d[2])
            climate = Climate::SHT4x;
        else
            climate = Climate::SHT3x;
        climateAddr = a;
    }
    for (uint8_t a : {0x76, 0x77})
    {
        if (climate != Climate::None || !seen(a))
            continue;
        uint8_t id = 0;
        if (!readReg(a, 0xD0, &id, 1))
            continue;
        if (id == 0x60 && bmeInit(a, true))
            climate = Climate::BME280;
        else if (id == 0x58 && bmeInit(a, false))
            climate = Climate::BMP280;
        else
            continue;
        climateAddr = a;
    }

    for (uint8_t a : {0x23, 0x5C})
    {
        if (light != Light::None || !seen(a))
            continue;
        const uint8_t on = 0x01, contHigh = 0x10;
        if (writeBytes(a, &on, 1) && writeBytes(a, &contHigh, 1))
        {
            light = Light::BH1750;
            lightAddr = a;
        }
    }
    if (light == Light::None && seen(0x10))
    {
        const uint8_t cfg[] = {0x00, 0x00, 0x00}; // gain x1, 100 ms, power on
        if (writeBytes(0x10, cfg, 3))
        {
            light = Light::VEML7700;
            lightAddr = 0x10;
        }
    }

    static const char *climateNames[] = {"", "AHT20", "SHT4x", "SHT3x", "BME280", "BMP280"};
    static const char *lightNames[] = {"", "BH1750", "VEML7700"};
    r.hasClimate = climate != Climate::None;
    r.climateSensor = climateNames[(int)climate];
    r.hasLight = light != Light::None;
    r.lightSensor = lightNames[(int)light];

    portENTER_CRITICAL(&mux);
    data = r;
    portEXIT_CRITICAL(&mux);

    if (r.foundCount)
    {
        Serial.printf("[qwiic] %u device(s):", r.foundCount);
        for (uint8_t i = 0; i < r.foundCount; i++)
            Serial.printf(" 0x%02X", r.found[i]);
        Serial.printf("  climate=%s light=%s\n", r.climateSensor, r.lightSensor);
    }
}

static bool readClimate(float &t, float &h, float &p)
{
    uint8_t d[7];
    switch (climate)
    {
    case Climate::AHT20:
    {
        const uint8_t cmd[] = {0xAC, 0x33, 0x00};
        if (!writeBytes(climateAddr, cmd, 3))
            return false;
        delay(85);
        if (!readBytes(climateAddr, d, 6) || (d[0] & 0x80))
            return false;
        uint32_t rh = ((uint32_t)d[1] << 12) | ((uint32_t)d[2] << 4) | (d[3] >> 4);
        uint32_t rt = (((uint32_t)d[3] & 0x0F) << 16) | ((uint32_t)d[4] << 8) | d[5];
        h = rh * 100.0f / 1048576.0f;
        t = rt * 200.0f / 1048576.0f - 50.0f;
        return true;
    }
    case Climate::SHT4x:
    case Climate::SHT3x:
    {
        bool sht4 = climate == Climate::SHT4x;
        const uint8_t cmd4[] = {0xFD};
        const uint8_t cmd3[] = {0x24, 0x00};
        if (!(sht4 ? writeBytes(climateAddr, cmd4, 1) : writeBytes(climateAddr, cmd3, 2)))
            return false;
        delay(sht4 ? 12 : 18);
        if (!readBytes(climateAddr, d, 6) || sensirionCrc(d, 2) != d[2] || sensirionCrc(d + 3, 2) != d[5])
            return false;
        uint16_t st = d[0] << 8 | d[1], srh = d[3] << 8 | d[4];
        t = -45.0f + 175.0f * st / 65535.0f;
        h = sht4 ? -6.0f + 125.0f * srh / 65535.0f : 100.0f * srh / 65535.0f;
        h = constrain(h, 0.0f, 100.0f);
        return true;
    }
    case Climate::BME280:
        return bmeRead(climateAddr, true, t, h, p);
    case Climate::BMP280:
        return bmeRead(climateAddr, false, t, h, p);
    default:
        return false;
    }
}

static bool readLight(float &lux)
{
    uint8_t d[2];
    if (light == Light::BH1750)
    {
        if (!readBytes(lightAddr, d, 2))
            return false;
        lux = ((d[0] << 8) | d[1]) / 1.2f;
        return true;
    }
    if (light == Light::VEML7700)
    {
        if (!readReg(lightAddr, 0x04, d, 2))
            return false;
        lux = ((d[1] << 8) | d[0]) * 0.0576f; // gain x1, IT 100 ms
        return true;
    }
    return false;
}

static void qwiicTask(void *)
{
    uint32_t lastScan = 0;
    uint8_t failures = 0;
    detect();
    lastScan = millis();
    for (;;)
    {
        bool anything = climate != Climate::None || light != Light::None;
        if (!anything && millis() - lastScan > 15000)
        {
            detect(); // hot-plug: look again for new sensors
            lastScan = millis();
        }

        float t = NAN, h = NAN, p = NAN, lux = NAN;
        bool okC = climate != Climate::None && readClimate(t, h, p);
        bool okL = light != Light::None && readLight(lux);

        if ((climate != Climate::None && !okC) || (light != Light::None && !okL))
        {
            if (++failures >= 5) // sensor unplugged
            {
                failures = 0;
                detect();
                lastScan = millis();
            }
        }
        else
        {
            failures = 0;
        }

        portENTER_CRITICAL(&mux);
        if (okC)
        {
            data.tempC = t;
            data.humidity = h;
            data.pressureHpa = p;
        }
        if (okL)
            data.lux = lux;
        portEXIT_CRITICAL(&mux);

        vTaskDelay(pdMS_TO_TICKS(light != Light::None ? 1000 : 5000));
    }
}

void qwiicInit()
{
    bus.begin(PIN_QWIIC_SDA, PIN_QWIIC_SCL, 100000);
    bus.setTimeOut(50);
    xTaskCreatePinnedToCore(qwiicTask, "qwiic", 4096, nullptr, 1, nullptr, 0);
}

QwiicReadings qwiicGet()
{
    portENTER_CRITICAL(&mux);
    QwiicReadings r = data;
    portEXIT_CRITICAL(&mux);
    return r;
}
