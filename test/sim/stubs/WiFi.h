#pragma once
#include "Arduino.h"
struct IPAddressStub { String toString() const { return "192.168.4.1"; } };
struct WiFiStub { int RSSI() { return -52; } IPAddressStub softAPIP() { return {}; } };
extern WiFiStub WiFi;
