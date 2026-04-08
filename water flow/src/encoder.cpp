#include "Sensor.h"

#include "driver/pcnt.h"
#include <Arduino.h>

namespace {

constexpr pcnt_unit_t kAbUnit = PCNT_UNIT_0;
constexpr pcnt_channel_t kAbChannel = PCNT_CHANNEL_0;
constexpr pcnt_unit_t kZUnit = PCNT_UNIT_1;
constexpr pcnt_channel_t kZChannel = PCNT_CHANNEL_0;

constexpr int16_t kCounterHighLimit = 30000;
constexpr int16_t kCounterLowLimit = -30000;
constexpr uint16_t kFilterValue = 100;

uint32_t gSampleIntervalMs = 200;
float gPulsesPerRevolution = 600.0f;
uint32_t gLastSampleMs = 0;
int32_t gTotalCount = 0;
int32_t gZIndexCount = 0;

void configureAbCounter() {
  pcnt_config_t abConfig = {};
  abConfig.pulse_gpio_num = ENCODER_A;
  abConfig.ctrl_gpio_num = ENCODER_B;
  abConfig.lctrl_mode = PCNT_MODE_KEEP;
  abConfig.hctrl_mode = PCNT_MODE_REVERSE;
  abConfig.pos_mode = PCNT_COUNT_INC;
  abConfig.neg_mode = PCNT_COUNT_DIS;
  abConfig.counter_h_lim = kCounterHighLimit;
  abConfig.counter_l_lim = kCounterLowLimit;
  abConfig.unit = kAbUnit;
  abConfig.channel = kAbChannel;

  pcnt_unit_config(&abConfig);
  pcnt_set_filter_value(kAbUnit, kFilterValue);
  pcnt_filter_enable(kAbUnit);
  pcnt_counter_pause(kAbUnit);
  pcnt_counter_clear(kAbUnit);
  pcnt_counter_resume(kAbUnit);
}

void configureZCounter() {
  pcnt_config_t zConfig = {};
  zConfig.pulse_gpio_num = ENCODER_Z;
  zConfig.ctrl_gpio_num = PCNT_PIN_NOT_USED;
  zConfig.lctrl_mode = PCNT_MODE_KEEP;
  zConfig.hctrl_mode = PCNT_MODE_KEEP;
  zConfig.pos_mode = PCNT_COUNT_INC;
  zConfig.neg_mode = PCNT_COUNT_DIS;
  zConfig.counter_h_lim = kCounterHighLimit;
  zConfig.counter_l_lim = 0;
  zConfig.unit = kZUnit;
  zConfig.channel = kZChannel;

  pcnt_unit_config(&zConfig);
  pcnt_set_filter_value(kZUnit, kFilterValue);
  pcnt_filter_enable(kZUnit);
  pcnt_counter_pause(kZUnit);
  pcnt_counter_clear(kZUnit);
  pcnt_counter_resume(kZUnit);
}

} // namespace

void encoderInit(uint32_t sampleIntervalMs, float pulsesPerRevolution) {
  gSampleIntervalMs = sampleIntervalMs == 0 ? 200 : sampleIntervalMs;
  gPulsesPerRevolution =
      pulsesPerRevolution <= 0.0f ? 600.0f : pulsesPerRevolution;
  gLastSampleMs = millis();
  gTotalCount = 0;
  gZIndexCount = 0;

  configureAbCounter();
  configureZCounter();
}

bool encoderRead(EncoderReading &reading) {
  const uint32_t nowMs = millis();
  const uint32_t elapsedMs = nowMs - gLastSampleMs;

  if (elapsedMs < gSampleIntervalMs) {
    return false;
  }

  int16_t abCountRaw = 0;
  int16_t zCountRaw = 0;

  pcnt_get_counter_value(kAbUnit, &abCountRaw);
  pcnt_get_counter_value(kZUnit, &zCountRaw);

  pcnt_counter_clear(kAbUnit);
  pcnt_counter_clear(kZUnit);

  gLastSampleMs = nowMs;
  gTotalCount += static_cast<int32_t>(abCountRaw);
  gZIndexCount += static_cast<int32_t>(zCountRaw);

  const float elapsedSeconds = static_cast<float>(elapsedMs) / 1000.0f;
  const float speedCps = static_cast<float>(abCountRaw) / elapsedSeconds;

  reading.deltaCount = static_cast<int32_t>(abCountRaw);
  reading.totalCount = gTotalCount;
  reading.zIndexCount = gZIndexCount;
  reading.speedCps = speedCps;
  reading.speedRps = speedCps / gPulsesPerRevolution;
  reading.speedRpm = reading.speedRps * 60.0f;
  reading.sampleIntervalMs = elapsedMs;
  reading.direction = 0;

  if (abCountRaw > 0) {
    reading.direction = 1;
  } else if (abCountRaw < 0) {
    reading.direction = -1;
  }

  return true;
}

void testencoder() {
  int a = digitalRead(ENCODER_A), b = digitalRead(ENCODER_B),
      z = digitalRead(ENCODER_Z);
  Serial.print("A: ");
  Serial.print(a);
  Serial.print(" | B: ");
  Serial.print(b);
  Serial.print(" | Z: ");
  Serial.println(z);
  delay(5);
}

void printEncoderDebug(const EncoderReading &reading) {
  const char *directionText = "STOP";
  if (reading.direction > 0) {
    directionText = "CW";
  } else if (reading.direction < 0) {
    directionText = "CCW";
  }

  Serial.print("[ENC] tMs=");
  Serial.print(millis());
  Serial.print(" | dir=");
  Serial.print(directionText);
  Serial.print(" | dCount=");
  Serial.print(reading.deltaCount);
  Serial.print(" | total=");
  Serial.print(reading.totalCount);
  Serial.print(" | idx=");
  Serial.print(reading.zIndexCount);
  Serial.print(" | cps=");
  Serial.print(reading.speedCps, 2);
  Serial.print(" | rps=");
  Serial.print(reading.speedRps, 3);
  Serial.print(" | rpm=");
  Serial.print(reading.speedRpm, 2);
  Serial.print(" | dtMs=");
  Serial.print(reading.sampleIntervalMs);
  Serial.print(" | A=");
  Serial.print(digitalRead(ENCODER_A));
  Serial.print(" B=");
  Serial.print(digitalRead(ENCODER_B));
  Serial.print(" Z=");
  Serial.println(digitalRead(ENCODER_Z));
}
