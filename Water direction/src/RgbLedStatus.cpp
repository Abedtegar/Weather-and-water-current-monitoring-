#include "RgbLedStatus.h"

#include "Sensor.h"

#include <Adafruit_NeoPixel.h>
#include <WiFi.h>

namespace {

#ifdef RGB_BUILTIN
constexpr uint8_t kRgbLedPin = RGB_BUILTIN;
#else
constexpr uint8_t kRgbLedPin = RGB_PIN;
#endif

constexpr uint8_t kRgbLedCount = 1;
constexpr uint8_t kRgbBrightness = 40;
constexpr uint32_t kRgbUpdateIntervalMs = 250;

Adafruit_NeoPixel gRgb(kRgbLedCount, kRgbLedPin, NEO_GRB + NEO_KHZ800);
uint32_t gLastUpdateMs = 0;
int gLastWiFiStatus = -999;

void setRgbColor(uint8_t r, uint8_t g, uint8_t b) {
  gRgb.setPixelColor(0, gRgb.Color(r, g, b));
  gRgb.show();
}

void updateWiFiIndicator() {
  const int status = WiFi.status();
  if (status == gLastWiFiStatus) {
    return;
  }
  gLastWiFiStatus = status;

  if (status == WL_CONNECTED) {
    setRgbColor(0, 255, 0);
  } else {
    setRgbColor(255, 0, 0);
  }
}

} // namespace

void rgbLedStatusInit() {
#ifdef NEOPIXEL_POWER
  pinMode(NEOPIXEL_POWER, OUTPUT);
  digitalWrite(NEOPIXEL_POWER, HIGH);
#endif

  gRgb.begin();
  gRgb.setBrightness(kRgbBrightness);
  gRgb.clear();
  gRgb.show();

  // Default to disconnected
  setRgbColor(255, 0, 0);
}

void rgbLedStatusUpdate(uint32_t nowMs) {
  if (nowMs - gLastUpdateMs < kRgbUpdateIntervalMs) {
    return;
  }

  updateWiFiIndicator();
  gLastUpdateMs = nowMs;
}
