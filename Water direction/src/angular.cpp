#include "Sensor.h"

#include <Arduino.h>

namespace {

constexpr uint8_t kAngularPins[8] = {Angular_0, Angular_1, Angular_2,
                                     Angular_3, Angular_4, Angular_5,
                                     Angular_6, Angular_7};

constexpr uint16_t kEncoderResolution = 256;
constexpr float kDegreePerCount =
    360.0f / static_cast<float>(kEncoderResolution);
constexpr uint8_t kMaxStableReadTry = 4;

// Use configuration from Sensor.h
constexpr bool kInvertDirection = ENC_INVERT_DIRECTION;
constexpr uint8_t kMedianSampleCount = ENC_MEDIAN_SAMPLE_COUNT;
constexpr float kAngleOffsetDeg = ENC_ANGLE_OFFSET_DEG;
constexpr bool kCodeIsGray = ENC_CODE_IS_GRAY;
constexpr bool kActiveLow = ENC_ACTIVE_LOW;
constexpr bool kBitOrderMsbFirst = ENC_BIT_ORDER_MSB_FIRST;

// Median filter buffer
uint8_t gMedianBuffer[7] = {0}; // Max 7 samples for median
constexpr uint8_t kMaxMedianSize =
    sizeof(gMedianBuffer) / sizeof(gMedianBuffer[0]);

uint32_t gSampleIntervalMs = 50;
uint32_t gLastSampleMs = 0;
int32_t gTotalCount = 0;
uint8_t gLastRawCode = 0;
uint8_t gLastCount = 0;

uint8_t grayToBinary8(uint8_t gray) {
  // Standard Gray->Binary conversion: b ^= b>>1; b ^= b>>2; b ^= b>>4;
  uint8_t b = gray;
  b ^= (b >> 1);
  b ^= (b >> 2);
  b ^= (b >> 4);
  return b;
}

uint8_t readRawCodeOnce() {
  uint8_t code = 0;

  for (uint8_t bit = 0; bit < 8; ++bit) {
    uint8_t pinState = static_cast<uint8_t>(digitalRead(kAngularPins[bit]));
    pinState = (pinState & 0x01U);
    if (kActiveLow) {
      pinState ^= 0x01U;
    }

    const uint8_t shift =
        kBitOrderMsbFirst ? static_cast<uint8_t>(7U - bit) : bit;
    code |= (pinState << shift);
  }

  return code;
}

uint8_t readRawCodeStable() {
  uint8_t previous = readRawCodeOnce();

  for (uint8_t i = 0; i < kMaxStableReadTry; ++i) {
    delayMicroseconds(80);
    const uint8_t current = readRawCodeOnce();
    if (current == previous) {
      return current;
    }
    previous = current;
  }

  return previous;
}

int16_t wrappedDelta(uint8_t current, uint8_t previous) {
  int16_t delta =
      static_cast<int16_t>(current) - static_cast<int16_t>(previous);

  if (delta > 127) {
    delta -= 256;
  } else if (delta < -128) {
    delta += 256;
  }

  return delta;
}

const char *directionText(int8_t direction) {
  if (direction > 0) {
    return "CW";
  }
  if (direction < 0) {
    return "CCW";
  }
  return "STOP";
}

uint8_t getMedianValue(const uint8_t *buffer, uint8_t size) {
  if (size == 1) {
    return buffer[0];
  }

  uint8_t sorted[7] = {0};
  for (uint8_t i = 0; i < size; ++i) {
    sorted[i] = buffer[i];
  }

  // Simple bubble sort
  for (uint8_t i = 0; i < size - 1; ++i) {
    for (uint8_t j = 0; j < size - i - 1; ++j) {
      if (sorted[j] > sorted[j + 1]) {
        uint8_t temp = sorted[j];
        sorted[j] = sorted[j + 1];
        sorted[j + 1] = temp;
      }
    }
  }

  return sorted[size / 2];
}

float normalizeAngle(float angle) {
  while (angle < 0.0f) {
    angle += 360.0f;
  }
  while (angle >= 360.0f) {
    angle -= 360.0f;
  }
  return angle;
}

} // namespace

void encoderInit(uint32_t sampleIntervalMs) {
  gSampleIntervalMs = sampleIntervalMs == 0 ? 50 : sampleIntervalMs;
  gLastSampleMs = millis();
  gTotalCount = 0;

  for (uint8_t i = 0; i < 8; ++i) {
    pinMode(kAngularPins[i], INPUT_PULLUP);
  }

  gLastRawCode = readRawCodeStable();
  gLastCount = kCodeIsGray ? grayToBinary8(gLastRawCode) : gLastRawCode;
}

bool encoderRead(EncoderReading &reading) {
  const uint32_t nowMs = millis();
  const uint32_t elapsedMs = nowMs - gLastSampleMs;

  if (elapsedMs < gSampleIntervalMs) {
    return false;
  }

  const uint8_t currentCodeRaw = readRawCodeStable();

  // Apply median filter
  uint8_t currentCode = currentCodeRaw;
  if (kMedianSampleCount > 1) {
    static uint8_t bufferIndex = 0;
    const uint8_t medianCount = (kMedianSampleCount <= kMaxMedianSize)
                                    ? kMedianSampleCount
                                    : kMaxMedianSize;

    gMedianBuffer[bufferIndex] = currentCodeRaw;
    bufferIndex = (bufferIndex + 1) % medianCount;

    currentCode = getMedianValue(gMedianBuffer, medianCount);
  }

  const uint8_t currentCount =
      kCodeIsGray ? grayToBinary8(currentCode) : currentCode;
  const int16_t delta = wrappedDelta(currentCount, gLastCount);

  gLastRawCode = currentCode;
  gLastCount = currentCount;
  gLastSampleMs = nowMs;
  gTotalCount += static_cast<int32_t>(delta);

  const float elapsedSeconds = static_cast<float>(elapsedMs) / 1000.0f;
  const float speedCps = static_cast<float>(delta) / elapsedSeconds;

  // Calculate angle with offset
  float angle = static_cast<float>(currentCount) * kDegreePerCount;
  angle = normalizeAngle(angle + kAngleOffsetDeg);

  reading.rawCode = currentCode;
  reading.count = currentCount;
  reading.angleDeg = angle;
  reading.deltaCount = static_cast<int32_t>(delta);
  reading.totalCount = gTotalCount;
  reading.zIndexCount = 0;
  reading.speedCps = speedCps;
  reading.speedRps = speedCps / static_cast<float>(kEncoderResolution);
  reading.speedRpm = reading.speedRps * 60.0f;
  reading.direction = 0;
  reading.sampleIntervalMs = elapsedMs;

  if (delta > 0) {
    reading.direction = kInvertDirection ? -1 : 1;
  } else if (delta < 0) {
    reading.direction = kInvertDirection ? 1 : -1;
  }

  return true;
}

void printEncoderDebug(const EncoderReading &reading) {
  char bits[9] = {0};
  for (uint8_t i = 0; i < 8; ++i) {
    const uint8_t mask = static_cast<uint8_t>(1U << (7U - i));
    bits[i] = (reading.rawCode & mask) ? '1' : '0';
  }

  Serial.print("[ANG] tMs=");
  Serial.print(millis());
  Serial.print(" | dir=");
  Serial.print(directionText(reading.direction));
  Serial.print(" | raw=0x");
  if (reading.rawCode < 16) {
    Serial.print('0');
  }
  Serial.print(reading.rawCode, HEX);
  Serial.print(" (");
  Serial.print(bits);
  Serial.print(")");
  Serial.print(" | count=");
  Serial.print(reading.count);
  Serial.print(" (0x");
  if (reading.count < 16) {
    Serial.print('0');
  }
  Serial.print(reading.count, HEX);
  Serial.print(")");
  Serial.print(" | angle=");
  Serial.print(reading.angleDeg, 2);
  Serial.print(" deg");
  Serial.print(" | dCount=");
  Serial.print(reading.deltaCount);
  Serial.print(" | total=");
  Serial.print(reading.totalCount);
  Serial.print(" | cps=");
  Serial.print(reading.speedCps, 2);
  Serial.print(" | rpm=");
  Serial.print(reading.speedRpm, 2);
  Serial.print(" | dtMs=");
  Serial.print(reading.sampleIntervalMs);
  Serial.print(" | median=");
  Serial.println(kMedianSampleCount);
}
