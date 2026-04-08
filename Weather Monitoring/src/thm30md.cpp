#include "Sensor.h"

namespace {

HardwareSerial gThm30mdSerial(2);

constexpr uint8_t kSlaveId = 0x01;
constexpr uint8_t kFunctionReadInputRegister = 0x04;
constexpr uint16_t kRegStart = 0x0000;
constexpr uint16_t kRegCount = 0x0002;

constexpr uint32_t kPollIntervalMs = 1000;
constexpr uint32_t kResponseTimeoutMs = 250;

constexpr uint8_t kErrorNone = 0;
constexpr uint8_t kErrorTimeout = 1;
constexpr uint8_t kErrorFrame = 2;
constexpr uint8_t kErrorCrc = 3;

uint32_t gLastPollMs = 0;
uint8_t gLastError = kErrorNone;
int16_t gLastRawTemp = 0;
int16_t gLastRawHum = 0;

uint16_t modbusCrc16(const uint8_t *data, size_t len) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (uint8_t b = 0; b < 8; ++b) {
      if ((crc & 0x0001U) != 0U) {
        crc >>= 1;
        crc ^= 0xA001;
      } else {
        crc >>= 1;
      }
    }
  }
  return crc;
}

void clearRxBuffer() {
  while (gThm30mdSerial.available() > 0) {
    gThm30mdSerial.read();
  }
}

bool readExactBytes(uint8_t *buffer, size_t length, uint32_t timeoutMs) {
  const uint32_t startMs = millis();
  size_t index = 0;

  while (index < length) {
    if (gThm30mdSerial.available() > 0) {
      buffer[index++] = static_cast<uint8_t>(gThm30mdSerial.read());
      continue;
    }

    if (millis() - startMs >= timeoutMs) {
      return false;
    }

    delay(1);
  }

  return true;
}

} // namespace

void thm30mdInit(uint32_t thm30mdBaud) {
  gThm30mdSerial.begin(thm30mdBaud, SERIAL_8N1, Temp_RX, Temp_TX);
  gLastPollMs = 0;
  gLastError = kErrorNone;
}

bool thm30mdRead(THM30MDReading &reading) {
  const uint32_t now = millis();
  if (now - gLastPollMs < kPollIntervalMs) {
    return false;
  }

  gLastPollMs = now;

  uint8_t request[8] = {
      kSlaveId,
      kFunctionReadInputRegister,
      static_cast<uint8_t>((kRegStart >> 8) & 0xFF),
      static_cast<uint8_t>(kRegStart & 0xFF),
      static_cast<uint8_t>((kRegCount >> 8) & 0xFF),
      static_cast<uint8_t>(kRegCount & 0xFF),
      0,
      0,
  };

  const uint16_t requestCrc = modbusCrc16(request, 6);
  request[6] = static_cast<uint8_t>(requestCrc & 0xFF);
  request[7] = static_cast<uint8_t>((requestCrc >> 8) & 0xFF);

  clearRxBuffer();
  gThm30mdSerial.write(request, sizeof(request));
  gThm30mdSerial.flush();
  delay(5);

  uint8_t response[9] = {0};
  if (!readExactBytes(response, sizeof(response), kResponseTimeoutMs)) {
    gLastError = kErrorTimeout;
    reading.lastErrorCode = gLastError;
    reading.valid = false;
    return false;
  }

  if (response[0] != kSlaveId || response[1] != kFunctionReadInputRegister ||
      response[2] != 4) {
    gLastError = kErrorFrame;
    reading.lastErrorCode = gLastError;
    reading.valid = false;
    return false;
  }

  const uint16_t responseCrc = static_cast<uint16_t>(response[7]) |
                               (static_cast<uint16_t>(response[8]) << 8);
  const uint16_t computedCrc = modbusCrc16(response, 7);
  if (responseCrc != computedCrc) {
    gLastError = kErrorCrc;
    reading.lastErrorCode = gLastError;
    reading.valid = false;
    return false;
  }

  const int16_t rawTemp = static_cast<int16_t>(
      (static_cast<uint16_t>(response[3]) << 8) | response[4]);
  const int16_t rawHum = static_cast<int16_t>(
      (static_cast<uint16_t>(response[5]) << 8) | response[6]);

  const float temperature = static_cast<float>(rawTemp) / 10.0f;
  const float humidity = static_cast<float>(rawHum) / 10.0f;

  gLastRawTemp = rawTemp;
  gLastRawHum = rawHum;
  gLastError = kErrorNone;

  reading.temperatureC = temperature;
  reading.humidityPct = humidity;
  reading.rawTemperatureDeci = rawTemp;
  reading.rawHumidityDeci = rawHum;
  reading.lastErrorCode = gLastError;
  reading.valid = true;

  return true;
}

void printTHM30MDDebug(const THM30MDReading &reading) {
  Serial.println("[THM30MD] ----------");
  Serial.print("valid: ");
  Serial.println(reading.valid ? "yes" : "no");

  Serial.print("slave: ");
  Serial.println(kSlaveId);
  Serial.print("fc: ");
  Serial.println(kFunctionReadInputRegister);
  Serial.print("lastError: ");
  Serial.println(reading.lastErrorCode);
  Serial.print("rawTemp(deci): ");
  Serial.println(reading.rawTemperatureDeci);
  Serial.print("rawHum(deci): ");
  Serial.println(reading.rawHumidityDeci);

  if (!reading.valid) {
    return;
  }

  Serial.print("temperature(C): ");
  Serial.println(reading.temperatureC, 2);
  Serial.print("humidity(%): ");
  Serial.println(reading.humidityPct, 2);
}
