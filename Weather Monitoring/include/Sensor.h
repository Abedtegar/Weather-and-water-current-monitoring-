#ifndef SENSOR_H
#define SENSOR_H

#include "HTTPClient.h"
#include <Arduino.h>
#include <WiFi.h>

#define RGB_PIN 48   // Onboard RGB LED pin
#define NUM_PIXELS 1 // Only one LED

#define led_indicator 5

#define Anemometer_TX 7
#define Anemometer_RX 6

#define LIDAR_SDA 8
#define LIDAR_SCL 9

#define Temp_TX 18
#define Temp_RX 17

struct AnemometerReading {
  bool anemometerValid;
  int windDirectionDeg;
  float windSpeedAvgMs;
  float windSpeedMaxMs;
  float rainfall1hMm;
  float rainfall24hMm;
  int humidityAnemometerPct;
  float pressureHpa;
};

struct THM30MDReading {
  bool valid;
  float temperatureC;
  float humidityPct;
  int16_t rawTemperatureDeci;
  int16_t rawHumidityDeci;
  uint8_t lastErrorCode;
};

struct LidarReading {
  bool valid;
  long distanceCm;
  long waterHeightCm;
  long waveHeightCm;
  int validSamples;
};

void anemometerInit(uint32_t anemometerBaud = 9600);
bool anemometerRead(AnemometerReading &reading);
void printAnemometerDebug(const AnemometerReading &reading);

void thm30mdInit(uint32_t thm30mdBaud = 9600);
bool thm30mdRead(THM30MDReading &reading);
void printTHM30MDDebug(const THM30MDReading &reading);

void lidarInitSimple();
bool lidarReadSimple(LidarReading &reading, int sampleCount = 5);
void printLidarDebug(const LidarReading &reading);

void serverInit();
void serverHandleUpload(const AnemometerReading &anemometer,
                        const THM30MDReading &thm30md,
                        const LidarReading &lidar);

// Upload 10 lidar distance samples (0.1..1.0s) in one request.
// Height calculations are performed server-side.
bool serverUploadLidarDistanceBatch(const AnemometerReading &anemometer,
                                    const THM30MDReading &thm30md,
                                    const long *distanceSamplesCm,
                                    size_t sampleCount,
                                    uint32_t sampleIntervalMs);

#endif
// Created by Abednego Tegar Imanto_DTEO ITS_2024 on 18 March 2026.
