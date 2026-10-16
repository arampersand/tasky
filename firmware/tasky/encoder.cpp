#include "encoder.h"
#include "config_hardware.h"
#include <Arduino.h>
#include <soc/gpio_reg.h>

// Quadrature decoder on interrupts: every edge of both pins is counted, so a
// quick turn isn't lost while the screen draws, and one detent is one step
// (ENCODER_TICKS_PER_DETENT edges). Pins are read straight from the GPIO
// register because the ISR must stay in IRAM.
static volatile int16_t encTicks = 0;
static volatile uint8_t encState = 0;

static inline __attribute__((always_inline)) uint8_t encPins() {
  uint32_t in = REG_READ(GPIO_IN_REG);
  return (((in >> ENCODER_CLK) & 1) << 1) | ((in >> ENCODER_DT) & 1);
}

// index = previous AB << 2 | current AB; invalid jumps (bounce) count 0.
// In DRAM: the ISR can run while flash is busy (NVS writes).
static const int8_t DRAM_ATTR ENC_DIR[16] = {0, 1, -1, 0, -1, 0, 0, 1,
                                             1, 0, 0, -1, 0, -1, 1, 0};

static void IRAM_ATTR encoderISR() {
  encState = ((encState << 2) | encPins()) & 0x0F;
  encTicks += ENC_DIR[encState];
}

void encoderBegin() {
  encState = encPins();
  attachInterrupt(ENCODER_CLK, encoderISR, CHANGE);
  attachInterrupt(ENCODER_DT, encoderISR, CHANGE);
}

int encoderStep() {
  noInterrupts();
  int steps = encTicks / ENCODER_TICKS_PER_DETENT;
  encTicks -= steps * ENCODER_TICKS_PER_DETENT;  // keep a half-turned detent
  interrupts();
  return steps;
}
