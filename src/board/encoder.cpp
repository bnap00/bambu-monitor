#include "encoder.h"
#include "board_pins.h"
#include <soc/gpio_reg.h>

// The knob rests at both 00 and 11 (one detent per half quadrature cycle), so a
// step is counted each time the state settles at 00 or 11, in the direction of
// the intermediate state passed through. Same scheme as LilyGO's example, but
// interrupt-driven so no detents are lost while the UI or TLS is busy.

static volatile int32_t steps = 0;
static volatile uint8_t lastStable = 0; // last settled state: 0b00 or 0b11
static volatile int8_t pendingDir = 0;
static volatile uint32_t activityMs = 0;

static void IRAM_ATTR knobIsr()
{
    uint32_t in = REG_READ(GPIO_IN_REG); // IRAM-safe, unlike digitalRead()
    uint8_t s = (((in >> PIN_KNOB_A) & 1) << 1) | ((in >> PIN_KNOB_B) & 1);
    if (s == 0b00 || s == 0b11)
    {
        if (s != lastStable && pendingDir != 0)
        {
            steps += pendingDir;
            activityMs = millis();
        }
        lastStable = s;
        pendingDir = 0;
    }
    else if (s == 0b10)
    {
        pendingDir = (lastStable == 0b00) ? 1 : -1;
    }
    else // 0b01
    {
        pendingDir = (lastStable == 0b00) ? -1 : 1;
    }
}

// ---- button state machine (polled from encoderTakeEvent)

static const uint32_t DEBOUNCE_MS = 25;
static const uint32_t LONG_MS = 700;
static const uint32_t VERY_LONG_MS = 8000;
static const uint32_t DOUBLE_GAP_MS = 250;

static bool rawLast = false, stable = false;
static uint32_t rawChangeMs = 0, pressMs = 0, releaseMs = 0;
static uint8_t clickCount = 0;
static bool longFired = false, veryLongFired = false;

void encoderInit()
{
    pinMode(PIN_KNOB_A, INPUT_PULLUP);
    pinMode(PIN_KNOB_B, INPUT_PULLUP);
    pinMode(PIN_KNOB_KEY, INPUT_PULLUP);
    lastStable = (digitalRead(PIN_KNOB_A) << 1) | digitalRead(PIN_KNOB_B);
    if (lastStable != 0b00 && lastStable != 0b11)
        lastStable = 0b00;
    attachInterrupt(PIN_KNOB_A, knobIsr, CHANGE);
    attachInterrupt(PIN_KNOB_B, knobIsr, CHANGE);
}

int encoderTakeSteps()
{
    noInterrupts();
    int s = steps;
    steps = 0;
    interrupts();
    return s;
}

bool encoderButtonHeld()
{
    return stable;
}

uint32_t encoderLastActivityMs()
{
    return activityMs;
}

KnobEvent encoderTakeEvent()
{
    uint32_t now = millis();
    bool raw = digitalRead(PIN_KNOB_KEY) == LOW;
    if (raw != rawLast)
    {
        rawLast = raw;
        rawChangeMs = now;
    }

    if (raw != stable && now - rawChangeMs >= DEBOUNCE_MS)
    {
        stable = raw;
        activityMs = now;
        if (stable)
        {
            pressMs = now;
            longFired = veryLongFired = false;
        }
        else
        {
            releaseMs = now;
            if (!longFired)
                clickCount++;
        }
    }

    if (stable)
    {
        if (!veryLongFired && now - pressMs >= VERY_LONG_MS)
        {
            veryLongFired = true;
            return KnobEvent::VeryLongPress;
        }
        if (!longFired && now - pressMs >= LONG_MS)
        {
            longFired = true;
            clickCount = 0;
            return KnobEvent::LongPress;
        }
        return KnobEvent::None;
    }

    if (clickCount >= 2)
    {
        clickCount = 0;
        return KnobEvent::DoubleClick;
    }
    if (clickCount == 1 && now - releaseMs > DOUBLE_GAP_MS)
    {
        clickCount = 0;
        return KnobEvent::Click;
    }
    return KnobEvent::None;
}
