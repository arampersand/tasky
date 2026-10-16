#pragma once

// Rotary encoder (knob) decoded on interrupts. Call encoderBegin() once after
// the pins are set up; encoderStep() returns the detents turned since the
// last call (+ clockwise, - counter-clockwise).
void encoderBegin();
int encoderStep();
