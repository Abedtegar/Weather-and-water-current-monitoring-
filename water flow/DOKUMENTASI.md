# Dokumentasi Program Pembacaan Encoder 3-Channel dengan PCNT & Upload HTTP

## Daftar Isi
1. [Ringkasan Program](#ringkasan-program)
2. [Arsitektur Sistem](#arsitektur-sistem)
3. [Konfigurasi Hardware](#konfigurasi-hardware)
4. [Alur Kerja Detail](#alur-kerja-detail)
5. [Parameter Konfigurasi](#parameter-konfigurasi)
6. [Contoh Output Serial](#contoh-output-serial)
7. [Format URL Upload](#format-url-upload)
8. [Troubleshooting](#troubleshooting)
9. [API Fungsi](#api-fungsi)

---

## Ringkasan Program

Program ini menggunakan **PCNT (Pulse Counter)** hardware ESP32-S3 untuk membaca encoder rotary 3-channel dengan presisi tinggi (non-blocking), kemudian menghitung kecepatan putaran dalam berbagai satuan (CPS, RPS, RPM), mendeteksi arah putar (CW/CCW), dan mengirimkan data ke server via HTTP GET setiap interval tertentu.

**Fitur utama:**
- Pembacaan 3 channel encoder (A, B, Z)
- Deteksi arah putar otomatis
- Perhitungan kecepatan realtime (CPS, RPS, RPM)
- Akumulasi count total dan index pulse
- WiFi reconnect otomatis
- HTTP upload periodik dengan basic auth
- Retry gagal dengan restart WiFi setelah 3x kegagalan
- Serial debug verbose untuk monitoring

---

## Arsitektur Sistem

```
┌─────────────────────────────────────────────────────────────────┐
│                      MAIN (main.cpp)                             │
│  - setup(): Inisialisasi Serial, Pin, Encoder, Server/WiFi      │
│  - loop(): Baca encoder → Debug serial → Upload HTTP             │
└────────────────────┬────────────────────────┬────────────────────┘
                     │                        │
         ┌───────────▼──────────────┐   ┌────▼─────────────────────┐
         │   ENCODER (encoder.cpp)  │   │   SERVER (server.cpp)     │
         ├──────────────────────────┤   ├───────────────────────────┤
         │ encoderInit()            │   │ serverInit()              │
         │ - Setup PCNT unit 0 (A/B)│   │ - Koneksi WiFi awal      │
         │ - Setup PCNT unit 1 (Z)  │   │                           │
         │ - Clear counter          │   │ serverHandleUpload()      │
         │                          │   │ - Throttle 1s             │
         │ encoderRead()            │   │ - Ensure WiFi connected   │
         │ - Baca counter PCNT      │   │ - Bentuk URL GET          │
         │ - Clear counter          │   │ - HTTP request            │
         │ - Kalkulasi speed/dir    │   │ - Retry logic             │
         │ - Return struct reading  │   │                           │
         │                          │   │ printEncoderDebug()       │
         │ printEncoderDebug()      │   │ (di encoder.cpp)          │
         │ - Serial log detail      │   │ - Serial output read data │
         └──────────────────────────┘   └───────────────────────────┘
```

**Modul Utama:**
1. **main.cpp** - Kontroller utama, mengorkestra encoder & server
2. **encoder.cpp** - Driver PCNT untuk pembacaan encoder
3. **server.cpp** - Modul WiFi dan HTTP upload
4. **Sensor.h** - Header definitions dan struct EncoderReading

---

## Konfigurasi Hardware

### Pin Encoder

| Signal | Pin | Mode | Fungsi |
|--------|-----|------|--------|
| A (Clock) | 10 | INPUT_PULLUP | Pulse input PCNT unit 0 |
| B (Direction) | 11 | INPUT_PULLUP | Control input PCNT unit 0 |
| Z (Index) | 12 | INPUT_PULLUP | Pulse input PCNT unit 1 |

**Definisi di Sensor.h:**
```cpp
#define ENCODER_A 10
#define ENCODER_B 11
#define ENCODER_Z 12
```

### PCNT Configuration

**Channel A/B (Unit 0):**
```
Pulse GPIO:     Pin 10 (ENCODER_A)
Control GPIO:   Pin 11 (ENCODER_B)
Pulse Mode:     PCNT_COUNT_INC (positif edge increment)
Neg Mode:       PCNT_COUNT_DIS (negatif diabaikan)
Ctrl Low Mode:  PCNT_MODE_KEEP (keep jika B=0)
Ctrl High Mode: PCNT_MODE_REVERSE (reverse jika B=1) → Deteksi arah
Counter Range:  -30000 hingga +30000
Filter:         100 (glitch filter)
```

**Channel Z (Unit 1):**
```
Pulse GPIO:     Pin 12 (ENCODER_Z)
Control GPIO:   Not used
Pulse Mode:     PCNT_COUNT_INC (positif edge increment)
Counter Range:  0 hingga +30000
Filter:         100
Fungsi:         Index pulse counter (1x per putaran)
```

### WiFi Configuration

| Param | Value |
|-------|-------|
| SSID | YOUR_WIFI_SSID |
| Password | YOUR_WIFI_PASSWORD |
| Server IP | 31.97.66.191 |
| Endpoint | /Wemon_BauBau/wemonbaubau.php |
| Username | YOUR_HTTP_USER |
| Password | YOUR_HTTP_PASSWORD |

Catatan backend:
- URL tersebut mengarah ke file: /var/www/html/Wemon_BauBau/wemonbaubau.php
- Data tersimpan ke MySQL database Wemon_BauBau tabel water_flow (kolom enc_rpm + created_at)

---

## Parameter Konfigurasi

### Sampling Interval (main.cpp)
```cpp
constexpr uint32_t kSampleIntervalMs = 200;  // Ambil snapshot PCNT tiap 200 ms
```

### Pulses Per Revolution (main.cpp)
```cpp
constexpr float kPulsesPerRevolution = 600.0f;  // Encoder menghasilkan 600 pulsa per putaran
```

### Upload Interval (server.cpp)
```cpp
const uint32_t kUploadIntervalMs = 1000;  // Kirim ke server tiap 1000 ms
```

### Kontrol Upload & Debug (main.cpp)
```cpp
constexpr bool kEnableServerUpload = true;  // true=upload ke server, false=tanpa WiFi/HTTP
constexpr bool kEnableSerialDebug = true;   // true=print debug serial, false=senyap
```

---

## Alur Kerja Detail

### Fase 1: Boot (t=0 ms)

```
[WAKTU]     [EVENT]
0 ms        ESP32 power-on
5 ms        Serial mulai 115200 baud
310 ms      setup() dijalankan
310 ms      Serial.println("Encoder debug starting...")
312 ms      Pin mode set INPUT_PULLUP
315 ms      encoderInit() dipanggil
             ├─ PCNT unit 0 (A/B) diconfig
             ├─ PCNT unit 1 (Z) diconfig
             └─ Counter dimulai
330 ms      serverInit() dipanggil
             ├─ WiFi.mode(WIFI_STA)
             ├─ WiFi.begin(...) → cari network
             └─ Serial.println("Connecting to WiFi...")
500-6000 ms WiFi scanning & connecting (retries < 20)
6000 ms     WiFi connected atau timeout
             ├─ Jika connected: Serial.println("Connected to WiFi")
             └─ Jika timeout: Serial.println("Initial WiFi connection failed...")
             
loop() mulai berjalan
```

**Serial Output saat boot (WiFi berhasil):**
```
Encoder debug starting...
Connecting to WiFi.....................
Connected to WiFi
IP Address: 192.168.1.100
```

**Serial Output saat boot (WiFi timeout):**
```
Encoder debug starting...
Connecting to WiFi.....................
Initial WiFi connection failed. Will retry in loop.
```

---

### Fase 2: Sampling Encoder (200 ms interval)

**Situasi 1: Encoder berputar CW (arah A→B naik, delta positif)**

```
[WAKTU]     [PCNT EVENT]                [SOFTWARE]
6200 ms     ─────────────                encoderRead() cek waktu
            │A║B║Z║                     elapsedMs = 200 ≥ gSampleIntervalMs ✓
            ├─┼─┼─┤
            │1║0║0║ (start state)
6201 ms     │1║1║0║ (B naik, phase change)
6202 ms     │0║1║0║ (A turun)            PCNT: abCountRaw++;
6203 ms     │0║0║0║ (B turun)            PCNT: abCountRaw-- (tapi B=0, KEEP mode)
6204 ms     │1║0║0║ (A naik lagi)        PCNT: abCountRaw++;
            ...repeat...
6200 ms     (setelah 200ms)              ✓ Gate terpenuhi
                                         pcnt_get_counter_value(unit0) → abCountRaw = +120
                                         pcnt_get_counter_value(unit1) → zCountRaw = 0
                                         pcnt_counter_clear(unit0, unit1)
                                         
                                         abCountRaw = +120 (positif = CW)
                                         elapsedSeconds = 0.2
                                         speedCps = 120 / 0.2 = 600
                                         speedRps = 600 / 600 = 1.0
                                         speedRpm = 1.0 * 60 = 60
                                         direction = +1 (CW)
                                         
                                         gTotalCount += 120
                                         return true dengan struct terisi
```

**Serial Output Fase 2.1 (CW):**
```
[ENC] tMs=6200 | dir=CW | dCount=120 | total=120 | idx=0 | cps=600.00 | rps=1.000 | rpm=60.00 | dtMs=200 | A=1 B=0 Z=0
dir=CW | dCount=120 | total=120 | idx=0 | cps=600.00 | rps=1.000 | rpm=60.00 | dtMs=200
HTTP response: 200
Server response:
OK
```

---

**Situasi 2: Encoder berputar CCW (delta negatif)**

```
[WAKTU]     [PCNT EVENT]                [SOFTWARE]
6400 ms     (setelah 200ms)              ✓ Gate terpenuhi
                                         pcnt_get_counter_value(unit0) → abCountRaw = -150
                                         pcnt_get_counter_value(unit1) → zCountRaw = 0
                                         pcnt_counter_clear(...)
                                         
                                         abCountRaw = -150 (negatif = CCW)
                                         elapsedSeconds = 0.2
                                         speedCps = -150 / 0.2 = -750
                                         speedRps = -750 / 600 = -1.25
                                         speedRpm = -1.25 * 60 = -75
                                         direction = -1 (CCW)
                                         
                                         gTotalCount += (-150)
                                         return true dengan struct terisi
```

**Serial Output Fase 2.2 (CCW):**
```
[ENC] tMs=6400 | dir=CCW | dCount=-150 | total=-30 | idx=0 | cps=-750.00 | rps=-1.250 | rpm=-75.00 | dtMs=200 | A=0 B=1 Z=0
dir=CCW | dCount=-150 | total=-30 | idx=0 | cps=-750.00 | rps=-1.250 | rpm=-75.00 | dtMs=200
HTTP response: 200
Server response:
OK
```

---

**Situasi 3: Encoder berhenti (delta = 0)**

```
[WAKTU]     [PCNT EVENT]                [SOFTWARE]
6600 ms     (setelah 200ms)              ✓ Gate terpenuhi
                                         pcnt_get_counter_value(unit0) → abCountRaw = 0
                                         pcnt_get_counter_value(unit1) → zCountRaw = 0
                                         pcnt_counter_clear(...)
                                         
                                         abCountRaw = 0 (tidak ada pulsa)
                                         elapsedSeconds = 0.2
                                         speedCps = 0 / 0.2 = 0
                                         speedRps = 0 / 600 = 0
                                         speedRpm = 0 * 60 = 0
                                         direction = 0 (STOP)
                                         
                                         gTotalCount += 0
                                         return true dengan struct terisi
```

**Serial Output Fase 2.3 (STOP):**
```
[ENC] tMs=6600 | dir=STOP | dCount=0 | total=-30 | idx=0 | cps=0.00 | rps=0.000 | rpm=0.00 | dtMs=200 | A=1 B=0 Z=0
dir=STOP | dCount=0 | total=-30 | idx=0 | cps=0.00 | rps=0.000 | rpm=0.00 | dtMs=200
HTTP response: 200
Server response:
OK
```

---

**Situasi 4: Index pulse terbaca (Z naik sekali per putaran)**

```
[WAKTU]     [PCNT EVENT]                [SOFTWARE]
6800 ms     (setelah 200ms)              ✓ Gate terpenuhi
            │A║B║Z║                     pcnt_get_counter_value(unit0) → abCountRaw = +240
            ├─┼─┼─┤                     pcnt_get_counter_value(unit1) → zCountRaw = 1
            │...(normal pulsa)           pcnt_counter_clear(...)
            │...
            │0║0║1║ (Z naik 1x)          gZIndexCount += 1
            │0║0║0║ (Z turun)            
                                         abCountRaw = +240
                                         speedCps = 1200
                                         speedRps = 2.0
                                         speedRpm = 120
                                         direction = +1 (CW)
                                         
                                         return true dengan zIndexCount = 1
```

**Serial Output Fase 2.4 (CW dengan Index Pulse):**
```
[ENC] tMs=6800 | dir=CW | dCount=240 | total=210 | idx=1 | cps=1200.00 | rps=2.000 | rpm=120.00 | dtMs=200 | A=1 B=0 Z=0
dir=CW | dCount=240 | total=210 | idx=1 | cps=1200.00 | rps=2.000 | rpm=120.00 | dtMs=200
HTTP response: 200
Server response:
OK
```

---

### Fase 3: Upload ke Server (1000 ms throttle)

**Situasi A: WiFi Connected, Upload Sukses**

```
[WAKTU]     [EVENT]
6000 ms     WiFi.status() = WL_CONNECTED ✓
7000 ms     (throttle 1000 ms terpenuhi)
            serverHandleUpload() dipanggil
            ├─ now = 7000, gLastUploadTime = 6000
            ├─ elapsedUpload = 1000 ≥ 1000 ✓
            ├─ gLastUploadTime = 7000
            ├─ Serial log data
            ├─ ensureWiFiConnection() → connected ✓
            ├─ URL dibangun:
            │   http://31.97.66.191/Wemon_BauBau/wemonbaubau.php?enc_rpm=60.00
            │
            ├─ HTTPClient begin URL
            ├─ setAuthorization("YOUR_HTTP_USER", "YOUR_HTTP_PASSWORD")
            ├─ timeout(5000 ms)
            ├─ GET() → httpResponseCode = 200
            ├─ getString() → response body
            ├─ gFailedUploadCount = 0 (reset)
            └─ http.end()
```

**Serial Output Fase 3.A (Sukses Upload):**
```
[ENC] tMs=7000 | dir=CW | dCount=95 | total=215 | idx=0 | cps=475.00 | rps=0.792 | rpm=47.50 | dtMs=200 | A=1 B=0 Z=0
dir=CW | dCount=95 | total=215 | idx=0 | cps=475.00 | rps=0.792 | rpm=47.50 | dtMs=200
HTTP response: 200
Server response:
OK
```

---

**Situasi B: WiFi Disconnected, Auto-Reconnect**

```
[WAKTIME]   [EVENT]
7100 ms     encoderRead() return true (ada sample baru)
            serverHandleUpload() dipanggil
            ├─ gLastUploadTime update
            ├─ Serial log data
            ├─ ensureWiFiConnection()
            │   ├─ WiFi.status() = WL_DISCONNECTED ✗
            │   ├─ Serial.println("WiFi disconnected. Reconnecting...")
            │   ├─ WiFi.begin(ssid, pass)
            │   ├─ wait loop 10 retries
            │      ├─ retry 0: delay(500), WiFi.status() = WL_CONNECTING
            │      ├─ retry 1: delay(500), WiFi.status() = WL_CONNECTING
            │      ├─ retry 2: delay(500), WiFi.status() = WL_CONNECTED ✓
            │   ├─ return true
            ├─ URL dibangun dan GET
            ├─ HTTP response: 200
            └─ gFailedUploadCount = 0
```

**Serial Output Fase 3.B (WiFi Reconnect):**
```
[ENC] tMs=7100 | dir=CW | dCount=100 | total=315 | idx=0 | cps=500.00 | rps=0.833 | rpm=50.00 | dtMs=200 | A=1 B=0 Z=0
dir=CW | dCount=100 | total=315 | idx=0 | cps=500.00 | rps=0.833 | rpm=50.00 | dtMs=200
WiFi disconnected. Reconnecting...
..
WiFi reconnected
HTTP response: 200
Server response:
OK
```

---

**Situasi C: WiFi Available tapi Upload Fail (3x retry → Restart WiFi)**

```
[WAKTIME]   [EVENT]
7200 ms     ├─ WiFi connected ✓
            ├─ GET() → httpResponseCode = -1 (connection refused)
            ├─ gFailedUploadCount++ → 1
            ├─ Serial.println("Upload failed. Consecutive failures: 1")
            └─ Skip upload

7300 ms     ├─ WiFi connected ✓
            ├─ GET() → httpResponseCode = -4 (not connected)
            ├─ gFailedUploadCount++ → 2
            ├─ Serial.println("Upload failed. Consecutive failures: 2")
            └─ Skip upload

7400 ms     ├─ WiFi connected ✓
            ├─ GET() → httpResponseCode = -11 (read timeout)
            ├─ gFailedUploadCount++ → 3
            ├─ Serial.println("Upload failed. Consecutive failures: 3")
            ├─ gFailedUploadCount ≥ 3 → Trigger restart WiFi
            ├─ Serial.println("Too many failures, restarting WiFi connection...")
            ├─ WiFi.disconnect()
            ├─ delay(1000)
            ├─ WiFi.begin(ssid, pass)
            ├─ gFailedUploadCount = 0
            └─ http.end()
```

**Serial Output Fase 3.C (3x Fail → WiFi Restart):**
```
[ENC] tMs=7200 | dir=CW | dCount=110 | total=425 | idx=0 | cps=550.00 | rps=0.917 | rpm=55.00 | dtMs=200 | A=1 B=0 Z=0
dir=CW | dCount=110 | total=425 | idx=0 | cps=550.00 | rps=0.917 | rpm=55.00 | dtMs=200
HTTP response: -1
Upload failed. Consecutive failures: 1

[ENC] tMs=7300 | dir=CW | dCount=105 | total=530 | idx=0 | cps=525.00 | rps=0.875 | rpm=52.50 | dtMs=200 | A=1 B=0 Z=0
dir=CW | dCount=105 | total=530 | idx=0 | cps=525.00 | rps=0.875 | rpm=52.50 | dtMs=200
HTTP response: -4
Upload failed. Consecutive failures: 2

[ENC] tMs=7400 | dir=CW | dCount=115 | total=645 | idx=0 | cps=575.00 | rps=0.958 | rpm=57.50 | dtMs=200 | A=1 B=0 Z=0
dir=CW | dCount=115 | total=645 | idx=0 | cps=575.00 | rps=0.958 | rpm=57.50 | dtMs=200
HTTP response: -11
Upload failed. Consecutive failures: 3
Too many failures, restarting WiFi connection...
```

---

**Situasi D: Server Respond non-200 tapi tetap simpan data**

```
[WAKTIME]   [EVENT]
7500 ms     ├─ GET() → httpResponseCode = 500 (server error)
            ├─ response = "ERROR: Database connection failed"
            ├─ gFailedUploadCount++ → 1
            ├─ Serial.println("Upload failed. Consecutive failures: 1")
            └─ Coba lagi 1000 ms kemudian
```

**Serial Output Fase 3.D (Server Error):**
```
[ENC] tMs=7500 | dir=CW | dCount=120 | total=765 | idx=0 | cps=600.00 | rps=1.000 | rpm=60.00 | dtMs=200 | A=1 B=0 Z=0
dir=CW | dCount=120 | total=765 | idx=0 | cps=600.00 | rps=1.000 | rpm=60.00 | dtMs=200
HTTP response: 500
Upload failed. Consecutive failures: 1
```

---

## Contoh Output Serial

### Normal Operation (Encoder CW, WiFi connected, upload sukses)

```
Encoder debug starting...
Connecting to WiFi..................
Connected to WiFi
IP Address: 192.168.1.100
[ENC] tMs=310 | dir=STOP | dCount=0 | total=0 | idx=0 | cps=0.00 | rps=0.000 | rpm=0.00 | dtMs=200 | A=1 B=0 Z=0
dir=STOP | dCount=0 | total=0 | idx=0 | cps=0.00 | rps=0.000 | rpm=0.00 | dtMs=200
[ENC] tMs=510 | dir=CW | dCount=200 | total=200 | idx=0 | cps=1000.00 | rps=1.667 | rpm=100.00 | dtMs=200 | A=1 B=1 Z=0
dir=CW | dCount=200 | total=200 | idx=0 | cps=1000.00 | rps=1.667 | rpm=100.00 | dtMs=200
HTTP response: 200
Server response:
OK
[ENC] tMs=710 | dir=CW | dCount=190 | total=390 | idx=0 | cps=950.00 | rps=1.583 | rpm=95.00 | dtMs=200 | A=1 B=0 Z=0
dir=CW | dCount=190 | total=390 | idx=0 | cps=950.00 | rps=1.583 | rpm=95.00 | dtMs=200
[ENC] tMs=910 | dir=CW | dCount=185 | total=575 | idx=0 | cps=925.00 | rps=1.542 | rpm=92.50 | dtMs=200 | A=0 B=1 Z=0
dir=CW | dCount=185 | total=575 | idx=0 | cps=925.00 | rps=1.542 | rpm=92.50 | dtMs=200
HTTP response: 200
Server response:
OK
```

---

### Edge Case: WiFi Reconnect During Operation

```
...
[ENC] tMs=2810 | dir=CW | dCount=150 | total=1575 | idx=0 | cps=750.00 | rps=1.250 | rpm=75.00 | dtMs=200 | A=1 B=1 Z=0
dir=CW | dCount=150 | total=1575 | idx=0 | cps=750.00 | rps=1.250 | rpm=75.00 | dtMs=200
WiFi disconnected. Reconnecting...
....
WiFi reconnected
HTTP response: 200
Server response:
OK
[ENC] tMs=3010 | dir=CW | dCount=145 | total=1720 | idx=0 | cps=725.00 | rps=1.208 | rpm=72.50 | dtMs=200 | A=0 B=0 Z=0
dir=CW | dCount=145 | total=1720 | idx=0 | cps=725.00 | rps=1.208 | rpm=72.50 | dtMs=200
...
```

---

### Edge Case: Index Pulse Detected

```
...
[ENC] tMs=5600 | dir=CW | dCount=240 | total=3840 | idx=1 | cps=1200.00 | rps=2.000 | rpm=120.00 | dtMs=200 | A=1 B=0 Z=0
dir=CW | dCount=240 | total=3840 | idx=1 | cps=1200.00 | rps=2.000 | rpm=120.00 | dtMs=200
HTTP response: 200
Server response:
OK
[ENC] tMs=5800 | dir=CW | dCount=235 | total=4075 | idx=1 | cps=1175.00 | rps=1.958 | rpm=117.50 | dtMs=200 | A=0 B=1 Z=0
dir=CW | dCount=235 | total=4075 | idx=1 | cps=1175.00 | rps=1.958 | rpm=117.50 | dtMs=200
...
```

---

## Format URL Upload

Untuk mengurangi ukuran pengiriman data, firmware **hanya mengirim** parameter `enc_rpm`.

Perhitungan lain (CPS/RPS/arah/count) tetap ada untuk debug Serial, tetapi tidak dikirim ke server.

### Struktur URL
```
http://31.97.66.191/Wemon_BauBau/wemonbaubau.php?enc_rpm=<float>
```

### Contoh URL Lengkap
```
http://31.97.66.191/Wemon_BauBau/wemonbaubau.php?enc_rpm=60.00
```

### Parameter Dekripsi

| Parameter | Format | Range | Deskripsi |
|-----------|--------|-------|-----------|
| enc_rpm | float 2 decimal | -3000~+3000 | Revolution Per Minute |

---

## Troubleshooting

### Masalah 1: Serial tidak menampilkan output
**Gejala:** Monitor serial kosong atau garbage text
**Penyebab:** 
- Baud rate tidak 115200
- Pin TX/RX terbalik/terputus
- Board tidak terpilih benar

**Solusi:**
1. Cek baud rate di Serial Monitor: pilih 115200
2. Verify board di PlatformIO: `board = esp32s3usbotg`
3. Reconnect kabel USB

---

### Masalah 2: Encoder tidak terbaca (tMs=X | dCount=0 terus)
**Gejala:** Delta count selalu 0, RPM=0 meski encoder berputar
**Penyebab:**
- Pin A/B/Z tidak terhubung atau short
- Pull-up lemah (hambatan tinggi)
- Frekuensi pulsa terlalu tinggi (glitch filter terlalu ketat)

**Solusi:**
1. Verifikasi pin A=10, B=11, Z=12 dengan multimeter
2. Cek pembacaan logika di serial: `A=X B=Y Z=Z` di akhir output
3. Naikkan filter value dari 100 menjadi 200 di encoder.cpp:
   ```cpp
   constexpr uint16_t kFilterValue = 200;  // Tarik dari 100
   ```

---

### Masalah 3: Arah selalu detect CW meskipun putar CCW
**Gejala:** Dir=CW saat pemutar putaran berlawanan arah
**Penyebab:**
- Pin A & B terbalik
- Hardware mode A/B tidak sesuai standar quadrature

**Solusi:**
1. Tukar define di Sensor.h:
   ```cpp
   #define ENCODER_A 11  // Tukar dari 10
   #define ENCODER_B 10  // Tukar dari 11
   ```
2. Atau terbalik mode di encoder.cpp configureAbCounter:
   ```cpp
   abConfig.hctrl_mode = PCNT_MODE_KEEP;  // Ganti dari PCNT_MODE_REVERSE
   ```

---

### Masalah 4: WiFi tidak connect saat setup
**Gejala:** "Initial WiFi connection failed" saat boot
**Penyebab:**
- SSID/password salah
- Router tidak dalam jangkauan
- Frekuensi WiFi 6 GHz (ESP32-S3 support 2.4 GHz saja)

**Solusi:**
1. Cek SSID & password di server.cpp:
   ```cpp
   const char *kSsid = "YOUR_WIFI_SSID";      // Verifikasi
   const char *kPass = "YOUR_WIFI_PASSWORD";  // Verifikasi
   ```
2. Pastikan router broadcast SSID
3. Ubah WiFi ke 2.4 GHz band

---

### Masalah 5: Upload selalu gagal (3x error → WiFi restart loop)
**Gejala:** "Upload failed. Consecutive failures: 3" terus berulang
**Penyebab:**
- Server endpoint tidak valid
- Basic auth username/password salah
- Server mati/tidak response

**Solusi:**
1. Test endpoint dengan curl atau Postman:
   ```bash
   curl -u <user>:<pass> \
     "http://31.97.66.191/Wemon_BauBau/wemonbaubau.php?enc_rpm=12.34"
   ```
2. Verifikasi basic auth di server.cpp:
   ```cpp
   const char *kHttpUser = "YOUR_HTTP_USER";
   const char *kHttpPass = "YOUR_HTTP_PASSWORD";
   ```
3. Cek status server dengan `ping 31.97.66.191`

---

### Masalah 6: Serial debug terlalu verbose / lag
**Gejala:** Output serial melambat, ada karakter hilang
**Penyebab:**
- Debug output terlalu banyak → buffer overflow serial
- PCNT counter overflow karena interval terlalu panjang

**Solusi:**
1. Matikan debug di encoder.cpp:
   ```cpp
   constexpr bool kEnableSerialDebug = false;  // Change true → false
   ```
2. Atau naikkan sample interval dari 200ms menjadi 1000ms di main.cpp:
   ```cpp
   constexpr uint32_t kSampleIntervalMs = 1000;  // Naikkan dari 200
   ```

---

## API Fungsi

### Sensor.h - Struktur Data

```cpp
struct EncoderReading {
  int32_t deltaCount;        // Pulsa dalam window sampling terakhir
  int32_t totalCount;        // Akumulasi pulsa sejak boot
  int32_t zIndexCount;       // Counter pulse index (Z)
  float speedCps;            // Kecepatan count/sec
  float speedRps;            // Kecepatan rev/sec
  float speedRpm;            // Kecepatan rev/min
  int8_t direction;          // Arah: -1=CCW, 0=STOP, 1=CW
  uint32_t sampleIntervalMs; // Actual sampling window (ms)
};
```

---

### encoder.cpp - API Fungsi

#### `void encoderInit(uint32_t sampleIntervalMs, float pulsesPerRevolution)`
Inisialisasi modul encoder dan PCNT hardware.

| Param | Tipe | Default | Deskripsi |
|-------|------|---------|-----------|
| sampleIntervalMs | uint32_t | 200 | Interval pengambilan snapshot (ms) |
| pulsesPerRevolution | float | 600 | Jumlah pulsa A+B per 1 putaran mekanik |

**Return:** void

**Contoh:**
```cpp
encoderInit(200, 600);  // Sampling 200ms, PPR 600
```

---

#### `bool encoderRead(EncoderReading &reading)`
Baca snapshot PCNT dan hitung speed/direction (non-blocking).

| Param | Tipe | Deskripsi |
|-------|------|-----------|
| reading | EncoderReading& | Struct output untuk hasil pembacaan |

**Return:** 
- `true` jika snapshot baru tersedia (gate waktu terpenuhi)
- `false` jika belum waktunya (gate belum terpenuhi)

**Contoh:**
```cpp
EncoderReading reading = {};
if (encoderRead(reading)) {
  Serial.println(reading.speedRpm);
}
```

---

#### `void printEncoderDebug(const EncoderReading &reading)`
Cetak ke serial seluruh data pembacaan encoder untuk debugging.

| Param | Tipe | Deskripsi |
|-------|------|-----------|
| reading | const EncoderReading& | Data untuk dicetak |

**Return:** void

**Catatan:** Output ditentukan oleh `kEnableSerialDebug`

---

### server.cpp - API Fungsi

#### `void serverInit()`
Inisialisasi WiFi dan coba koneksi awal.

**Return:** void

**Timing:** Blocking ~6 second (retry 20x dengan delay 300ms)

---

#### `void serverHandleUpload(const EncoderReading &reading)`
Upload data encoder ke server via HTTP GET dengan throttle 1 detik.

| Param | Tipe | Deskripsi |
|-------|------|-----------|
| reading | const EncoderReading& | Data encoder untuk dikirim |

**Return:** void

**Timing:** 
- Throttle: 1000 ms antar upload
- HTTP timeout: 5000 ms
- WiFi reconnect timeout: 5000 ms (10 retry x 500ms)

**Kondisi Retry:**
- Jika gagal, `gFailedUploadCount++`
- Jika >= 3x gagal: WiFi disconnect dan reconnect

---

## Modifikasi & Customization

### Mengubah Interval Sampling
File: main.cpp

```cpp
constexpr uint32_t kSampleIntervalMs = 200;  // Ubah ke nilai lain
```

**Rekomendasi:** 100-1000 ms tergantung akurasi yang diinginkan

---

### Mengubah PPR (Pulses Per Revolution)
File: main.cpp

```cpp
constexpr float kPulsesPerRevolution = 600.0f;  // Ubah sesuai encoder Anda
```

**Catatan:** Cek datasheet encoder untuk nilai yang tepat

---

### Mengubah Upload Interval
File: server.cpp

```cpp
const uint32_t kUploadIntervalMs = 1000;  // Ubah ke nilai lain
```

**Rekomendasi:** >= 1000 ms untuk menjaga bandwidth & server

---

### Mengubah WiFi Credentials
File: server.cpp

```cpp
const char *kSsid = "YourSSID";
const char *kPass = "YourPassword";
```

---

### Mengubah Server Endpoint
File: server.cpp

```cpp
const char *kServer = "your.server.com";
const char *kPath = "/your/endpoint.php";
```

---

## Kesimpulan

Program ini menyediakan:
1. ✓ Pembacaan encoder presisi tinggi via PCNT hardware
2. ✓ Kalkulasi kecepatan realtime dalam berbagai satuan
3. ✓ Deteksi arah otomatis
4. ✓ WiFi reconnect dan HTTP retry robust
5. ✓ Serial debug verbose untuk troubleshooting

Semua fitur dapat dikustomisasi via konstanta di code tanpa perlu memodifikasi logic utama.

