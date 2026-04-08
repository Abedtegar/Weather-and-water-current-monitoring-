#include "Sensor.h"

#include <HTTPClient.h>
#include <WiFi.h>

namespace {

// NOTE: Do not commit real credentials to git.
const char *kSsid = "YOUR_WIFI_SSID";
const char *kPass = "YOUR_WIFI_PASSWORD";
const char *kServer = "31.97.66.191";
const char *kPath = "/Wemon_BauBau/wemonbaubau.php";
const char *kHttpUser = "YOUR_HTTP_USER";
const char *kHttpPass = "YOUR_HTTP_PASSWORD";

uint32_t gLastUploadTime = 0;
const uint32_t kUploadIntervalMs = 1000;
int gFailedUploadCount = 0;

String toFixedOrDefault(bool valid, float value, float fallback, int decimals) {
  if (!valid) {
    return String(fallback, decimals);
  }
  return String(value, decimals);
}

bool ensureWiFiConnection() {
  if (WiFi.status() == WL_CONNECTED) {
    return true;
  }

  Serial.println("[SRV] WiFi disconnected. Reconnecting...");
  WiFi.disconnect();
  WiFi.begin(kSsid, kPass);

  int retries = 0;
  while (WiFi.status() != WL_CONNECTED && retries < 10) {
    delay(500);
    Serial.print('.');
    retries++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[SRV] WiFi reconnected");
    // digitalWrite(led_indicator,
    //              HIGH); // Turn on LED to indicate WiFi is connected
    return true;
  }

  Serial.println("\n[SRV] WiFi reconnection failed");
  // digitalWrite(led_indicator,
  //              LOW); // Turn off LED to indicate WiFi is disconnected
  return false;
}

} // namespace

bool serverUploadLidarDistanceBatch(const AnemometerReading &anemometer,
                                    const THM30MDReading &thm30md,
                                    const long *distanceSamplesCm,
                                    size_t sampleCount,
                                    uint32_t sampleIntervalMs) {
  if (distanceSamplesCm == nullptr || sampleCount == 0) {
    Serial.println("[SRV] Upload skipped: empty lidar batch");
    digitalWrite(led_indicator, LOW);
    return false;
  }

  if (!ensureWiFiConnection()) {
    Serial.println("[SRV] Upload skipped: WiFi unavailable");
    digitalWrite(led_indicator, LOW);
    return false;
  }

  const int windDirection =
      anemometer.anemometerValid ? anemometer.windDirectionDeg : 0;
  const float windAvg =
      anemometer.anemometerValid ? anemometer.windSpeedAvgMs : 0.0f;
  const float windMax =
      anemometer.anemometerValid ? anemometer.windSpeedMaxMs : 0.0f;
  const float rain1h =
      anemometer.anemometerValid ? anemometer.rainfall1hMm : 0.0f;
  const float rain24h =
      anemometer.anemometerValid ? anemometer.rainfall24hMm : 0.0f;
  float pressure =
      anemometer.anemometerValid ? anemometer.pressureHpa : 1013.25f;
  if (pressure < 800.0f || pressure > 1100.0f) {
    pressure = 1013.25f;
  }

  const float suhu = thm30md.valid ? thm30md.temperatureC : 99.0f;
  const float humidity = thm30md.valid ? thm30md.humidityPct : 99.0f;

  String url = String("http://") + kServer + kPath;

  String json;
  json.reserve(512);
  json += "{\"type\":\"weather_lidar_distance_batch\",";
  json += "\"sample_interval_ms\":" + String(sampleIntervalMs) + ",";
  json += "\"windir\":" + String(windDirection) + ",";
  json += "\"windavg\":" + String(windAvg, 2) + ",";
  json += "\"windmax\":" + String(windMax, 2) + ",";
  json += "\"rain1h\":" + String(rain1h, 2) + ",";
  json += "\"rain24h\":" + String(rain24h, 2) + ",";
  json += "\"suhu\":" + String(suhu, 2) + ",";
  json += "\"humidity\":" + String(humidity, 2) + ",";
  json += "\"pressure\":" + String(pressure, 2) + ",";
  json += "\"lidar_distance_cm\":[";
  for (size_t i = 0; i < sampleCount; ++i) {
    if (i != 0) {
      json += ',';
    }
    json += String(distanceSamplesCm[i]);
  }
  json += "]}";

  Serial.println("[SRV] POST URL:");
  Serial.println(url);
  Serial.println("[SRV] POST JSON:");
  Serial.println(json);

  digitalWrite(led_indicator, HIGH);

  HTTPClient http;
  http.begin(url);
  http.setAuthorization(kHttpUser, kHttpPass);
  http.setTimeout(5000);
  http.addHeader("User-Agent", "ESP32");
  http.addHeader("Content-Type", "application/json");

  const int httpResponseCode = http.POST(json);
  Serial.print("[SRV] HTTP response: ");
  Serial.println(httpResponseCode);

  bool ok = false;
  if (httpResponseCode > 0) {
    const String response = http.getString();
    Serial.println("[SRV] Server response:");
    Serial.println(response);
    gFailedUploadCount = 0;
    ok = true;
  } else {
    Serial.print("[SRV] HTTP error: ");
    Serial.println(http.errorToString(httpResponseCode));
    Serial.print("[SRV] WiFi status: ");
    Serial.print(WiFi.status());
    Serial.print(" | IP: ");
    Serial.print(WiFi.localIP());
    Serial.print(" | RSSI: ");
    Serial.println(WiFi.RSSI());

    gFailedUploadCount++;
    Serial.print("[SRV] Upload failed. Consecutive failures: ");
    Serial.println(gFailedUploadCount);

    if (gFailedUploadCount == 3 || gFailedUploadCount == 6 ||
        gFailedUploadCount == 9) {
      Serial.println("[SRV] Too many failures, restarting WiFi");
      WiFi.disconnect();
      delay(1000);
      WiFi.begin(kSsid, kPass);
    }
    if (gFailedUploadCount >= 5) {
      Serial.println("[SRV] Too many failures, restarting ESP32");
      ESP.restart();
    }
  }

  http.end();
  digitalWrite(led_indicator, LOW);
  return ok;
}

void serverInit() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(kSsid, kPass);

  Serial.println("[SRV] Connecting to WiFi...");
  int retries = 0;
  while (WiFi.status() != WL_CONNECTED && retries < 20) {
    delay(300);
    Serial.print('.');
    retries++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[SRV] Connected to WiFi");
    Serial.print("[SRV] IP Address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println(
        "\n[SRV] Initial WiFi connection failed. Will retry in loop.");
  }
}

void serverHandleUpload(const AnemometerReading &anemometer,
                        const THM30MDReading &thm30md,
                        const LidarReading &lidar) {
  const uint32_t now = millis();

  if (now - gLastUploadTime < kUploadIntervalMs) {
    digitalWrite(led_indicator,
                 LOW); // Briefly turn on LED to indicate upload attempt
    return;
  }
  digitalWrite(led_indicator,
               HIGH); // Briefly turn on LED to indicate upload attempt
  gLastUploadTime = now;

  Serial.println("[SRV] Preparing upload from latest sensor readings");
  Serial.print("[SRV] anemometerValid=");
  Serial.print(anemometer.anemometerValid ? "yes" : "no");
  Serial.print(" | thmValid=");
  Serial.print(thm30md.valid ? "yes" : "no");
  Serial.print(" | lidarValid=");
  Serial.println(lidar.valid ? "yes" : "no");

  if (!ensureWiFiConnection()) {
    Serial.println("[SRV] Upload skipped: WiFi unavailable");
    return;
  }

  const int windDirection =
      anemometer.anemometerValid ? anemometer.windDirectionDeg : 0;
  const float windAvg =
      anemometer.anemometerValid ? anemometer.windSpeedAvgMs : 0.0f;
  const float windMax =
      anemometer.anemometerValid ? anemometer.windSpeedMaxMs : 0.0f;
  const float rain1h =
      anemometer.anemometerValid ? anemometer.rainfall1hMm : 0.0f;
  const float rain24h =
      anemometer.anemometerValid ? anemometer.rainfall24hMm : 0.0f;
  float pressure =
      anemometer.anemometerValid ? anemometer.pressureHpa : 1013.25f;
  // Guard against missing/invalid barometer values (some variants report 0).
  if (pressure < 800.0f || pressure > 1100.0f) {
    pressure = 1013.25f;
  }
  const float suhu = thm30md.valid ? thm30md.temperatureC : 99.0f;
  const float humidity = thm30md.valid ? thm30md.humidityPct : 99.0f;
  const long distance = lidar.valid ? lidar.distanceCm : -1;
  const long waterHeight = lidar.valid ? lidar.waterHeightCm : -1;
  const long waveHeight = lidar.valid ? lidar.waveHeightCm : -1;

  String url =
      String("http://") + kServer + kPath + "?" +
      "windir=" + String(windDirection) + "&windavg=" + String(windAvg, 2) +
      "&windmax=" + String(windMax, 2) + "&rain1h=" + String(rain1h, 2) +
      "&rain24h=" + String(rain24h, 2) + "&suhu=" + String(suhu, 2) +
      "&humidity=" + String(humidity, 2) + "&pressure=" + String(pressure, 2) +
      "&distance=" + String(distance) + "&waterheight=" + String(waterHeight) +
      "&waveheight=" + String(waveHeight);

  Serial.println("[SRV] URL:");
  Serial.println(url);

  HTTPClient http;
  http.begin(url);
  http.setAuthorization(kHttpUser, kHttpPass);
  http.setTimeout(5000);
  http.addHeader("User-Agent", "ESP32");

  int httpResponseCode = http.GET();
  Serial.print("[SRV] HTTP response: ");
  Serial.println(httpResponseCode);

  if (httpResponseCode <= 0) {
    Serial.print("[SRV] HTTP error: ");
    Serial.println(http.errorToString(httpResponseCode));
    Serial.print("[SRV] WiFi status: ");
    Serial.print(WiFi.status());
    Serial.print(" | IP: ");
    Serial.print(WiFi.localIP());
    Serial.print(" | RSSI: ");
    Serial.println(WiFi.RSSI());
  }

  if (httpResponseCode > 0) {
    String response = http.getString();
    Serial.println("[SRV] Server response:");
    Serial.println(response);
    gFailedUploadCount = 0;
  } else {
    gFailedUploadCount++;
    Serial.print("[SRV] Upload failed. Consecutive failures: ");
    Serial.println(gFailedUploadCount);

    if (gFailedUploadCount == 3 || gFailedUploadCount == 6 ||
        gFailedUploadCount == 9) {
      Serial.println("[SRV] Too many failures, restarting WiFi");
      WiFi.disconnect();
      delay(1000);
      WiFi.begin(kSsid, kPass);
    }
    if (gFailedUploadCount >= 5) {
      Serial.println("[SRV] Too many failures, restarting ESP32");
      ESP.restart();
    }
  }

  http.end();
}
