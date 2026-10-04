#pragma once
#include <Arduino.h>

// Optional sensors on the Qwiic/STEMMA QT port, auto-detected and hot-pluggable:
//   temp/humidity: AHT20/AHT21 (0x38), SHT3x/SHT4x (0x44/0x45), BME280/BMP280 (0x76/0x77)
//   ambient light: BH1750 (0x23/0x5C), VEML7700 (0x10) -> drives auto-brightness
struct QwiicReadings
{
    bool hasClimate = false;
    float tempC = NAN;
    float humidity = NAN; // %RH, NAN when the sensor has none (BMP280)
    float pressureHpa = NAN;
    const char *climateSensor = "";

    bool hasLight = false;
    float lux = NAN;
    const char *lightSensor = "";

    uint8_t found[16]; // raw I2C addresses seen on the last scan
    uint8_t foundCount = 0;
};

void qwiicInit(); // starts a background polling task
QwiicReadings qwiicGet();
