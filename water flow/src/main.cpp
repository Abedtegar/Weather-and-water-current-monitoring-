#include "Sensor.h"
#include "RgbLedStatus.h"
#include <Arduino.h>

constexpr uint32_t kSampleIntervalMs = 200;
constexpr float kPulsesPerRevolution = 600.0f;

namespace {

constexpr bool kEnableServerUpload = true;
constexpr bool kEnableSerialDebug = true;

} // namespace

void setup() {
  pinMode(led_indicator, OUTPUT);
  Serial.begin(115200);
  delay(300);
  Serial.println("Encoder debug starting...");

  rgbLedStatusInit();

  pinMode(ENCODER_A, INPUT_PULLUP);
  pinMode(ENCODER_B, INPUT_PULLUP);
  pinMode(ENCODER_Z, INPUT_PULLUP);

  encoderInit(kSampleIntervalMs, kPulsesPerRevolution);

  if (kEnableServerUpload) {
    serverInit();
  }
}

void loop() {
  rgbLedStatusUpdate(millis());

  EncoderReading reading = {};
  if (encoderRead(reading)) {
    if (kEnableSerialDebug) {
      printEncoderDebug(reading);
    }

    if (kEnableServerUpload) {
      serverHandleUpload(reading);
    }
  }
}
