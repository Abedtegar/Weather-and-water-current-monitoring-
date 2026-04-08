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
    return true;
  }

  Serial.println("\n[SRV] WiFi reconnection failed");
  return false;
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

} // namespace

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

void serverHandleUpload(const EncoderReading &reading) {
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
  Serial.print("[SRV] encoderValid=");
  Serial.println("yes");

  Serial.print("[SRV] dir=");
  Serial.print(directionText(reading.direction));
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
  Serial.println(reading.sampleIntervalMs);

  if (!ensureWiFiConnection()) {
    Serial.println("[SRV] Upload skipped: WiFi unavailable");
    return;
  }

  String url = String("http://") + kServer + kPath + "?" +
               "enc_rpm=" + String(reading.speedRpm, 2);

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
    if (gFailedUploadCount >= 10) {
      Serial.println("[SRV] Too many failures, restarting ESP32");
      ESP.restart();
    }
  }
  http.end();
}
