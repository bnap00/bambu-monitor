#pragma once
#include <Arduino.h>

enum class KnobEvent : uint8_t
{
    None,
    Click,
    DoubleClick,
    LongPress,     // held ~0.7 s (fires while still held)
    VeryLongPress, // held ~8 s (fires while still held)
};

void encoderInit();

// Detent steps since last call (+ clockwise, - counter-clockwise).
int encoderTakeSteps();

// Next button event, or None.
KnobEvent encoderTakeEvent();

bool encoderButtonHeld();
uint32_t encoderLastActivityMs();
