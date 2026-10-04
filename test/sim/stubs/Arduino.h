// Minimal Arduino shim so the real UI code can be rendered on a PC.
#pragma once
#ifndef __cplusplus
#include <stdint.h>
uint32_t millis(void); // LVGL's C sources only need the tick
#else
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <algorithm>
#include <math.h>
using std::isnan;

extern "C" uint32_t millis();
inline size_t strlcpy(char *d, const char *s, size_t n)
{
    size_t l = strlen(s);
    if (n)
    {
        size_t c = l < n - 1 ? l : n - 1;
        memcpy(d, s, c);
        d[c] = 0;
    }
    return l;
}
inline void delay(uint32_t) {}
using std::max;
using std::min;
template <class T, class L, class H> T constrain(T v, L lo, H hi) { return v < lo ? lo : v > hi ? hi : v; }

class String
{
  public:
    std::string s;
    String() {}
    String(const char *c) : s(c ? c : "") {}
    String(const std::string &x) : s(x) {}
    String(char c) : s(1, c) {}
    String(int v) : s(std::to_string(v)) {}
    String(unsigned v) : s(std::to_string(v)) {}
    String(long v) : s(std::to_string(v)) {}
    String(unsigned long v) : s(std::to_string(v)) {}
    String(float v, int dec = 2) { char b[32]; snprintf(b, sizeof b, "%.*f", dec, v); s = b; }
    String(double v, int dec = 2) { char b[32]; snprintf(b, sizeof b, "%.*f", dec, v); s = b; }
    const char *c_str() const { return s.c_str(); }
    size_t length() const { return s.size(); }
    bool isEmpty() const { return s.empty(); }
    String &operator+=(const String &o) { s += o.s; return *this; }
    String &operator+=(const char *o) { s += o; return *this; }
    String &operator+=(char c) { s += c; return *this; }
    String &operator+=(int v) { s += std::to_string(v); return *this; }
    bool operator==(const String &o) const { return s == o.s; }
    bool operator!=(const String &o) const { return s != o.s; }
    bool operator==(const char *o) const { return s == o; }
};
inline String operator+(const String &a, const String &b) { return String(a.s + b.s); }
inline String operator+(const String &a, const char *b) { return String(a.s + b); }
inline String operator+(const char *a, const String &b) { return String(std::string(a) + b.s); }
inline String operator+(const String &a, int b) { return String(a.s + std::to_string(b)); }

struct EspClass { void restart() {} };
extern EspClass ESP;
#endif
