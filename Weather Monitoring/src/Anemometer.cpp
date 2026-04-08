#include "Sensor.h"

namespace {

HardwareSerial gAnemometerSerial(1);

uint32_t gAnemometerBaud = 2400;

// Frame format (proven sketch):
// - Starts with 'A' (sync)
// - Ends with CR/LF or '*'
// - Values are ASCII digits after letter keys (B,C,D,E,F,L,M,N, ...)
// Example: A...BxxxxCxxxxDxxxxE....

constexpr size_t kFrameMaxLen = 128;
char gFrame[kFrameMaxLen] = {0};
size_t gFrameLen = 0;

char gLastGoodFrame[kFrameMaxLen] = {0};
size_t gLastGoodLen = 0;

char gLastBadFrame[kFrameMaxLen] = {0};
size_t gLastBadLen = 0;

uint32_t gBytesTotal = 0;
uint32_t gBytesSinceReport = 0;
uint32_t gFramesTotal = 0;
uint32_t gParseOk = 0;
uint32_t gParseFail = 0;
uint8_t gLastByte = 0;
uint32_t gLastByteMs = 0;

bool getValueAfterKey(const char *data, size_t len, char key, float &out) {
  for (size_t i = 0; i < len; ++i) {
    if (data[i] != key) {
      continue;
    }

    size_t j = i + 1;
    bool neg = false;
    if (j < len && data[j] == '-') {
      neg = true;
      ++j;
    }

    if (j >= len || data[j] < '0' || data[j] > '9') {
      return false;
    }

    long value = 0;
    while (j < len && data[j] >= '0' && data[j] <= '9') {
      value = (value * 10) + (data[j] - '0');
      ++j;
    }

    out = neg ? -static_cast<float>(value) : static_cast<float>(value);
    return true;
  }

  return false;
}

bool parseFrameAN(const char *data, size_t len, AnemometerReading &reading) {
  if (len < 10 || data[0] != 'A') {
    return false;
  }

  float windDirRaw = 0.0f;
  float windSpeedRaw = 0.0f;
  float windMaxRaw = 0.0f;
  float rain1hRaw = 0.0f;
  float rain24hRaw = 0.0f;

  // Required fields based on the proven sketch.
  const bool hasB = getValueAfterKey(data, len, 'B', windDirRaw);
  const bool hasC = getValueAfterKey(data, len, 'C', windSpeedRaw);
  const bool hasD = getValueAfterKey(data, len, 'D', windMaxRaw);
  const bool hasE = getValueAfterKey(data, len, 'E', rain1hRaw);
  const bool hasF = getValueAfterKey(data, len, 'F', rain24hRaw);

  if (!hasB || !hasC || !hasD || !hasE || !hasF) {
    return false;
  }

  // Mapping & scaling exactly like the proven sketch:
  // B: direction degrees (no /10)
  // C,D: wind speed (/10)
  // E,F: rainfall (/10)
  reading.windDirectionDeg = static_cast<int>(windDirRaw + 0.5f);
  reading.windSpeedAvgMs = windSpeedRaw / 10.0f;
  reading.windSpeedMaxMs = windMaxRaw / 10.0f;
  reading.rainfall1hMm = rain1hRaw / 10.0f;
  reading.rainfall24hMm = rain24hRaw / 10.0f;

  float humidityRaw = 0.0f;
  if (getValueAfterKey(data, len, 'M', humidityRaw)) {
    const float humidityPct = humidityRaw / 10.0f;
    reading.humidityAnemometerPct = static_cast<int>(humidityPct + 0.5f);
  }

  float pressureRaw = 0.0f;
  if (getValueAfterKey(data, len, 'N', pressureRaw)) {
    reading.pressureHpa = pressureRaw / 10.0f;
  }

  reading.anemometerValid = true;
  return true;
}

void resetFrame() {
  gFrameLen = 0;
  gFrame[0] = '\0';
}

} // namespace

void anemometerInit(uint32_t anemometerBaud) {
  gAnemometerBaud = anemometerBaud;
  gBytesTotal = 0;
  gBytesSinceReport = 0;
  gFramesTotal = 0;
  gParseOk = 0;
  gParseFail = 0;
  gLastGoodLen = 0;
  gLastBadLen = 0;
  resetFrame();

  gAnemometerSerial.begin(anemometerBaud, SERIAL_8N1, Anemometer_RX,
                          Anemometer_TX);
}

bool anemometerRead(AnemometerReading &reading) {
  bool hasNewData = false;

  while (gAnemometerSerial.available() > 0) {
    const char c = static_cast<char>(gAnemometerSerial.read());

    ++gBytesTotal;
    ++gBytesSinceReport;
    gLastByte = static_cast<uint8_t>(c);
    gLastByteMs = millis();

    // Sync: whenever 'A' appears, treat it as a new frame start.
    if (c == 'A') {
      resetFrame();
      gFrame[gFrameLen++] = 'A';
      gFrame[gFrameLen] = '\0';
      continue;
    }

    // Ignore until we have sync.
    if (gFrameLen == 0) {
      continue;
    }

    // End of frame.
    if (c == '\n' || c == '\r' || c == '*') {
      if (gFrameLen > 20 && gFrame[0] == 'A') {
        ++gFramesTotal;
        if (parseFrameAN(gFrame, gFrameLen, reading)) {
          ++gParseOk;
          hasNewData = true;
          gLastGoodLen =
              (gFrameLen < kFrameMaxLen - 1) ? gFrameLen : (kFrameMaxLen - 1);
          memcpy(gLastGoodFrame, gFrame, gLastGoodLen);
          gLastGoodFrame[gLastGoodLen] = '\0';
        } else {
          ++gParseFail;
          gLastBadLen =
              (gFrameLen < kFrameMaxLen - 1) ? gFrameLen : (kFrameMaxLen - 1);
          memcpy(gLastBadFrame, gFrame, gLastBadLen);
          gLastBadFrame[gLastBadLen] = '\0';
        }
      }

      resetFrame();
      continue;
    }

    // Append.
    if (gFrameLen < kFrameMaxLen - 1) {
      gFrame[gFrameLen++] = c;
      gFrame[gFrameLen] = '\0';
    } else {
      // Overflow protection: drop current partial frame.
      resetFrame();
    }
  }

  return hasNewData;
}

void printAnemometerDebug(const AnemometerReading &reading) {
  Serial.println("[ANEMO] ----------");
  Serial.print("baud: ");
  Serial.println(gAnemometerBaud);
  Serial.print("frameValid: ");
  Serial.println(reading.anemometerValid ? "yes" : "no");

  Serial.print("bytesRxLastSec: ");
  Serial.println(gBytesSinceReport);
  Serial.print("framesTotal: ");
  Serial.println(gFramesTotal);
  Serial.print("parseOk: ");
  Serial.print(gParseOk);
  Serial.print(" | parseFail: ");
  Serial.println(gParseFail);

  if (gLastGoodLen > 0) {
    Serial.print("lastGoodFrame: ");
    Serial.println(gLastGoodFrame);
  }
  if (gLastBadLen > 0) {
    Serial.print("lastBadFrame: ");
    Serial.println(gLastBadFrame);
  }

  Serial.print("lastByte: 0x");
  if (gBytesTotal == 0) {
    Serial.println("--");
  } else {
    if (gLastByte < 0x10) {
      Serial.print('0');
    }
    Serial.println(gLastByte, HEX);
  }
  Serial.print("lastByteAgeMs: ");
  if (gBytesTotal == 0) {
    Serial.println(-1);
  } else {
    Serial.println(static_cast<long>(millis() - gLastByteMs));
  }

  gBytesSinceReport = 0;

  if (reading.anemometerValid) {
    Serial.print("windDir(deg): ");
    Serial.println(reading.windDirectionDeg);
    Serial.print("windAvg(m/s): ");
    Serial.println(reading.windSpeedAvgMs, 2);
    Serial.print("windMax(m/s): ");
    Serial.println(reading.windSpeedMaxMs, 2);
    Serial.print("rain1h(mm): ");
    Serial.println(reading.rainfall1hMm, 2);
    Serial.print("rain24h(mm): ");
    Serial.println(reading.rainfall24hMm, 2);
    Serial.print("humidityAnemometer(%): ");
    Serial.println(reading.humidityAnemometerPct);
    Serial.print("pressure(hPa): ");
    Serial.println(reading.pressureHpa, 1);
  }
}
