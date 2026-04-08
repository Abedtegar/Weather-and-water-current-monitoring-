#ifndef SENSOR_H
#define SENSOR_H

#include "HTTPClient.h"
#include <Arduino.h>
#include <wifi.h>

#define led_indicator 5
#define RGB_PIN 48   // Onboard RGB LED pin
#define NUM_PIXELS 1 // Only one LED

#define ENCODER_A 10
#define ENCODER_B 11
#define ENCODER_Z 12

struct EncoderReading {
  int32_t deltaCount;
  int32_t totalCount;
  int32_t zIndexCount;
  float speedCps;
  float speedRps;
  float speedRpm;
  int8_t direction;
  uint32_t sampleIntervalMs;
};

void encoderInit(uint32_t sampleIntervalMs, float pulsesPerRevolution);
bool encoderRead(EncoderReading &reading);
void serverInit();
void serverHandleUpload(const EncoderReading &reading);
void printEncoderDebug(const EncoderReading &reading);
#endif

// Sensor.h - Header file for encoder sensor reading and server upload
// This file defines the interface for initializing the encoder, reading its
// values, and handling server uploads. It also includes necessary libraries and
// pin definitions. Created by Abednego Tegar Imanto_DTEO ITS_2024 on 18 March
// 2026.