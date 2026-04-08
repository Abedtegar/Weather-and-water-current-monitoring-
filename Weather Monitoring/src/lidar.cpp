#include "Sensor.h"

#include <Wire.h>

namespace {

constexpr uint8_t kLidarAddress = 0x62;
constexpr long kOffsetKoreksiCm = 5;

long readLidarRaw() {
  Wire.beginTransmission(kLidarAddress);
  Wire.write(0x00);
  Wire.write(0x04);
  Wire.endTransmission();
  delay(10);

  Wire.beginTransmission(kLidarAddress);
  Wire.write(0x8F);
  if (Wire.endTransmission(false) != 0) {
    return -1;
  }

  if (Wire.requestFrom(kLidarAddress, static_cast<uint8_t>(2)) < 2) {
    return -1;
  }

  const uint8_t highByte = Wire.read();
  const uint8_t lowByte = Wire.read();

  long distance = (static_cast<long>(highByte) << 8) | lowByte;
  distance -= kOffsetKoreksiCm;

  if (distance < 0) {
    distance = 0;
  }

  return distance;
}

} // namespace

void lidarInitSimple() {
  Wire.begin(LIDAR_SDA, LIDAR_SCL);

  Wire.beginTransmission(kLidarAddress);
  Wire.write(0x00);
  Wire.write(0x04);
  Wire.endTransmission();

  delay(50);
}

bool lidarReadSimple(LidarReading &reading, int sampleCount) {
  if (sampleCount <= 0) {
    sampleCount = 1;
  }

  long total = 0;
  int validSamples = 0;

  for (int i = 0; i < sampleCount; ++i) {
    const long raw = readLidarRaw();
    if (raw > 0) {
      total += raw;
      validSamples++;
    }
    delay(10);
  }

  reading.validSamples = validSamples;
  reading.valid = false;

  if (validSamples == 0) {
    reading.distanceCm = -1;
    reading.waterHeightCm = -1;
    reading.waveHeightCm = -1;
    return false;
  }

  const long distance = total / validSamples;

  reading.distanceCm = distance;
  // Height calculations moved to server side.
  reading.waterHeightCm = -1;
  reading.waveHeightCm = -1;
  reading.valid = true;

  return true;
}

void printLidarDebug(const LidarReading &reading) {
  Serial.println("[LIDAR] ----------");
  Serial.print("valid: ");
  Serial.println(reading.valid ? "yes" : "no");
  Serial.print("validSamples: ");
  Serial.println(reading.validSamples);

  if (!reading.valid) {
    return;
  }

  Serial.print("distance(cm): ");
  Serial.println(reading.distanceCm);
}
