#include "RgbLedTest.h"
#include "Sensor.h"
#include <Arduino.h>

namespace {

constexpr bool kEnableServerUpload = true;
constexpr bool kEnableSerialDebug = true;

constexpr uint32_t kLidarSampleIntervalMs = 100;
constexpr uint32_t kLidarSamplesPerBatch = 10;

// Loop/task-local readings.
AnemometerReading gAnemometerLocal = {};
THM30MDReading gThm30mdLocal = {};

// Snapshots for other tasks (upload).
portMUX_TYPE gWeatherMux = portMUX_INITIALIZER_UNLOCKED;
AnemometerReading gAnemometerShared = {};
THM30MDReading gThm30mdShared = {};

// Latest lidar reading for debug (updated by lidar task).
LidarReading gLidarLatest = {};

// Lidar 10-sample batch buffer (0.1..1.0s). Only distance is meaningful.
portMUX_TYPE gLidarMux = portMUX_INITIALIZER_UNLOCKED;
long gLidarAccum[kLidarSamplesPerBatch] = {};
size_t gLidarAccumIndex = 0;
long gLidarLastBatch[kLidarSamplesPerBatch] = {};
uint32_t gLidarBatchSeq = 0;
uint32_t gLidarUploadedSeq = 0;

TaskHandle_t gUploadTaskHandle = nullptr;
TaskHandle_t gLidarTaskHandle = nullptr;

uint32_t gLastDebugPrintMs = 0;
constexpr uint32_t kDebugIntervalMs = 1000;

void lidarSampleTask(void *param) {
  (void)param;
  TickType_t lastWake = xTaskGetTickCount();

  for (;;) {
    LidarReading reading = {};
    lidarReadSimple(reading, 1);

    const long distance = reading.valid ? reading.distanceCm : -1;

    portENTER_CRITICAL(&gLidarMux);
    if (gLidarAccumIndex < kLidarSamplesPerBatch) {
      gLidarAccum[gLidarAccumIndex] = distance;
      gLidarAccumIndex++;
    }

    // Keep latest lidar reading for debug.
    gLidarLatest = reading;

    if (gLidarAccumIndex >= kLidarSamplesPerBatch) {
      memcpy(gLidarLastBatch, gLidarAccum, sizeof(gLidarLastBatch));
      gLidarBatchSeq++;
      gLidarAccumIndex = 0;
      if (gUploadTaskHandle != nullptr) {
        xTaskNotifyGive(gUploadTaskHandle);
      }
    }
    portEXIT_CRITICAL(&gLidarMux);

    vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(kLidarSampleIntervalMs));
  }
}

void uploadTask(void *param) {
  (void)param;

  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    long batch[kLidarSamplesPerBatch] = {};
    uint32_t seq = 0;

    portENTER_CRITICAL(&gLidarMux);
    if (gLidarBatchSeq == gLidarUploadedSeq) {
      portEXIT_CRITICAL(&gLidarMux);
      continue;
    }
    memcpy(batch, gLidarLastBatch, sizeof(batch));
    seq = gLidarBatchSeq;
    portEXIT_CRITICAL(&gLidarMux);

    AnemometerReading anemometer = {};
    THM30MDReading thm30md = {};
    portENTER_CRITICAL(&gWeatherMux);
    anemometer = gAnemometerShared;
    thm30md = gThm30mdShared;
    portEXIT_CRITICAL(&gWeatherMux);

    // Upload using the latest weather readings.
    const bool ok = serverUploadLidarDistanceBatch(anemometer, thm30md, batch,
                                                   kLidarSamplesPerBatch,
                                                   kLidarSampleIntervalMs);

    if (ok) {
      portENTER_CRITICAL(&gLidarMux);
      gLidarUploadedSeq = seq;
      portEXIT_CRITICAL(&gLidarMux);
    }
  }
}

} // namespace

void setup() {
  Serial.begin(115200);
  pinMode(led_indicator, OUTPUT);
  digitalWrite(led_indicator, LOW);

  delay(500);
  Serial.println("=== Weather Monitoring Simple Mode ===");

  anemometerInit(2400);
  thm30mdInit(9600);
  lidarInitSimple();

  // Start lidar sampling task (100ms). Keep it independent from upload/Modbus.
  xTaskCreatePinnedToCore(lidarSampleTask, "LidarSample", 4096, nullptr, 2,
                          &gLidarTaskHandle, 0);

  if (kEnableServerUpload) {
    serverInit();
    xTaskCreatePinnedToCore(uploadTask, "Upload", 6144, nullptr, 1,
                            &gUploadTaskHandle, 1);
    if (gUploadTaskHandle != nullptr) {
      // Kick once in case a batch was completed before the task existed.
      xTaskNotifyGive(gUploadTaskHandle);
    }
  }
  rgbLedTestInit();
}

void loop() {
  const uint32_t now = millis();

  // Non-blocking read for serial sensors.
  anemometerRead(gAnemometerLocal);
  thm30mdRead(gThm30mdLocal);

  portENTER_CRITICAL(&gWeatherMux);
  gAnemometerShared = gAnemometerLocal;
  gThm30mdShared = gThm30mdLocal;
  portEXIT_CRITICAL(&gWeatherMux);

  if (kEnableSerialDebug && now - gLastDebugPrintMs >= kDebugIntervalMs) {
    LidarReading lidarCopy = {};
    portENTER_CRITICAL(&gLidarMux);
    lidarCopy = gLidarLatest;
    portEXIT_CRITICAL(&gLidarMux);

    Serial.println();
    Serial.print("t(ms): ");
    Serial.println(now);
    printAnemometerDebug(gAnemometerLocal);
    printTHM30MDDebug(gThm30mdLocal);
    printLidarDebug(lidarCopy);
    gLastDebugPrintMs = now;
  }

  rgbLedTestUpdate(now);
}
