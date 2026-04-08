#ifndef SENSOR_H
#define SENSOR_H

#include <Arduino.h>

#define Angular_0 17
#define Angular_1 15
#define Angular_2 10
#define Angular_3 12
#define Angular_4 18
#define Angular_5 16
#define Angular_6 9
#define Angular_7 11

#define led_indicator 5
#define RGB_PIN 48
#define NUM_PIXELS 1

// Encoder Configuration
#define ENC_INVERT_DIRECTION false // Set true to invert CW/CCW
#define ENC_MEDIAN_SAMPLE_COUNT 1  // 1=no filter, 3/5/7=median filter
#define ENC_ANGLE_OFFSET_DEG 0.0f  // North offset (0-359.9)

// E6CP-A output is typically 8-bit Gray code (G0..G7).
// Set to false if your encoder outputs straight binary.
#define ENC_CODE_IS_GRAY true

// If your encoder outputs are active-low (LOW means logical 1), set true.
// Keep false for normal logic where HIGH means 1.
#define ENC_ACTIVE_LOW true

// Wiring/bit order helper:
// - false: Angular_0 is bit0 (LSB) ... Angular_7 is bit7 (MSB)
// - true : Angular_0 is bit7 (MSB) ... Angular_7 is bit0 (LSB)
#define ENC_BIT_ORDER_MSB_FIRST false

struct EncoderReading {
  uint8_t rawCode;
  uint8_t count;
  float angleDeg;
  int32_t deltaCount;
  int32_t totalCount;
  int32_t zIndexCount;
  float speedCps;
  float speedRps;
  float speedRpm;
  int8_t direction;
  uint32_t sampleIntervalMs;
};

void encoderInit(uint32_t sampleIntervalMs);
bool encoderRead(EncoderReading &reading);
void printEncoderDebug(const EncoderReading &reading);

void serverInit();
void serverHandleUpload(const EncoderReading &reading);

#endif

// Created by Abednego Tegar Imanto_DTEO ITS_2024 on 18 March 2026.