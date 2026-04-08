#include "Sensor.h"

#include "RgbLedStatus.h"

#include <Arduino.h>

namespace {

constexpr uint32_t kSampleIntervalMs = 50;
constexpr bool kEnableServerUpload = true;
constexpr bool kEnableSerialDebug = true;

} // namespace

void setup() {
  pinMode(led_indicator, OUTPUT);
  Serial.begin(115200);
  delay(300);
  Serial.println("\n========================================");
  Serial.println("E6CP-A 8-bit Angular Encoder Debug Start");
  Serial.println("========================================");
  Serial.println("Configuration:");
  Serial.print("  Sample Interval: ");
  Serial.print(kSampleIntervalMs);
  Serial.println(" ms");
  Serial.print("  Median Filter: ");
  Serial.println(ENC_MEDIAN_SAMPLE_COUNT);
  Serial.print("  Invert Direction: ");
  Serial.println(ENC_INVERT_DIRECTION ? "YES" : "NO");
  Serial.print("  Angle Offset (North): ");
  Serial.print(ENC_ANGLE_OFFSET_DEG);
  Serial.println(" deg");
  Serial.print("  Server Upload: ");
  Serial.println(kEnableServerUpload ? "ENABLED" : "DISABLED");
  Serial.print("  Serial Debug: ");
  Serial.println(kEnableSerialDebug ? "ENABLED" : "DISABLED");
  Serial.println("Rotate shaft slowly for initial validation.");
  Serial.println("========================================\n");

  encoderInit(kSampleIntervalMs);

  rgbLedStatusInit();

  if (kEnableServerUpload) {
    serverInit();
  }
}

void loop() {
  rgbLedStatusUpdate(millis());

  EncoderReading reading = {};
  if (!encoderRead(reading)) {
    return;
  }

  if (kEnableSerialDebug) {
    printEncoderDebug(reading);
  }

  if (kEnableServerUpload) {
    serverHandleUpload(reading);
  }
}
