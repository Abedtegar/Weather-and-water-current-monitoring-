# Sistem Monitoring Wemon Bau Bau — Dokumentasi Lengkap

**Proyek:** Water & Environment Monitoring — Pertamina / ITS  
**Lokasi:** Bau Bau  
**Platform:** ESP32-S3 (firmware) + PHP/MySQL (backend server)

---

## Daftar Isi

1. [Gambaran Umum Sistem](#1-gambaran-umum-sistem)
2. [Infrastruktur & Konektivitas](#2-infrastruktur--konektivitas)
3. [Sub-Proyek 1: Weather Monitoring (Firmware)](#3-sub-proyek-1-weather-monitoring-firmware)
4. [Sub-Proyek 2: Water Direction (Firmware)](#4-sub-proyek-2-water-direction-firmware)
5. [Sub-Proyek 3: Water Flow (Firmware)](#5-sub-proyek-3-water-flow-firmware)
6. [Sub-Proyek 4: Backend Server (PHP)](#6-sub-proyek-4-backend-server-php)
7. [Alur Data End-to-End](#7-alur-data-end-to-end)
8. [Konfigurasi & Deployment](#8-konfigurasi--deployment)
9. [Referensi Cepat](#9-referensi-cepat)

---

## 1. Gambaran Umum Sistem

Sistem Wemon Bau Bau adalah platform monitoring lingkungan dan kondisi air berbasis IoT yang terdiri dari **3 node firmware ESP32-S3** dan **1 backend server PHP/MySQL**. Setiap node mengumpulkan data dari sensor masing-masing dan mengirimkannya ke server pusat melalui jaringan WiFi lokal.

### Tujuan Sistem

| Node | Tugas | Data yang Dimonitor |
|------|-------|---------------------|
| Weather Monitoring | Cuaca & tinggi air | Angin, hujan, suhu, kelembaban, tekanan atmosfer, jarak lidar |
| Water Direction | Arah arus air | Arah putaran encoder (CW/CCW/STOP) |
| Water Flow | Kecepatan aliran air | RPM encoder (kecepatan putaran) |
| Backend Server | Penerimaan & penyimpanan data | Routing semua payload ke tabel MySQL |

### Diagram Arsitektur Sistem

```
┌─────────────────────────────────────────────────────────────────────┐
│                    JARINGAN WiFi "Wemon Bau Bau"                     │
│                                                                       │
│  ┌──────────────────┐  ┌──────────────────┐  ┌───────────────────┐  │
│  │  ESP32-S3 #1     │  │  ESP32-S3 #2     │  │  ESP32-S3 #3     │  │
│  │ Weather Monitor  │  │ Water Direction  │  │  Water Flow      │  │
│  │                  │  │                  │  │                  │  │
│  │ • Anemometer     │  │ • E6CP-A Encoder │  │ • Encoder 3-ch   │  │
│  │ • THM30MD RS485  │  │   (8-bit absolut)│  │   (A/B/Z PCNT)   │  │
│  │ • Lidar Lite V3  │  │                  │  │                  │  │
│  └────────┬─────────┘  └────────┬─────────┘  └────────┬─────────┘  │
│           │ HTTP POST JSON       │ HTTP GET             │ HTTP GET   │
│           └──────────────────────┴──────────────────────┘           │
│                                  │                                   │
└──────────────────────────────────┼───────────────────────────────────┘
                                   │
                    ┌──────────────▼──────────────────┐
                    │   SERVER  31.97.66.191           │
                    │  /Wemon_BauBau/wemonbaubau.php   │
                    │                                  │
                    │  ┌────────────────────────────┐  │
                    │  │     MySQL: Wemon_BauBau     │  │
                    │  │  • weather_monitoring       │  │
                    │  │  • water_flow               │  │
                    │  │  • water_direction          │  │
                    │  └────────────────────────────┘  │
                    └──────────────────────────────────┘
```

---

## 2. Infrastruktur & Konektivitas

### Jaringan WiFi

| Parameter | Nilai |
|-----------|-------|
| SSID | `Wemon Bau Bau` |
| Password | `WemonBauBau2026` |

### Server Backend

| Parameter | Nilai |
|-----------|-------|
| IP Server | `31.97.66.191` |
| Endpoint Utama | `/Wemon_BauBau/wemonbaubau.php` |
| Protokol | HTTP (port 80) |
| Autentikasi | HTTP Basic Auth |
| Username | `pcserver` |
| Password | `dteo2025` |

### Database MySQL

| Parameter | Nilai |
|-----------|-------|
| Host | `localhost` (di server) |
| Database | `Wemon_BauBau` |
| User | `Joko` |
| Password | `Joko12345` |
| Charset | `utf8mb4` |

---

## 3. Sub-Proyek 1: Weather Monitoring (Firmware)

**Lokasi:** `Weather Monitoring/`  
**Fungsi:** Membaca data cuaca dan tinggi air dari 3 sensor, kemudian mengirim batch ke server setiap 1 detik.

### 3.1 Hardware & Sensor

| Sensor | Interface | Baudrate / Protokol | Data yang Dibaca |
|--------|-----------|---------------------|------------------|
| Anemometer | UART (HardwareSerial 1) | 2400 bps, ASCII frame | Arah angin, kec. angin avg & max, curah hujan 1h & 24h, suhu, kelembaban, tekanan |
| THM30MD | UART + RS485 (HardwareSerial 2) | 9600 bps, Modbus RTU | Suhu (°C), kelembaban (%) |
| Lidar Lite V3 | I2C | Standar I2C | Jarak (cm) → tinggi air & tinggi gelombang |

### 3.2 Peta Pin (ESP32-S3)

| GPIO | Fungsi |
|------|--------|
| 6 | Anemometer RX (HardwareSerial 1) |
| 7 | Anemometer TX (HardwareSerial 1) |
| 8 | Lidar SDA (I2C) |
| 9 | Lidar SCL (I2C) |
| 17 | THM30MD RX (HardwareSerial 2) |
| 18 | THM30MD TX (HardwareSerial 2) |
| 5 | LED Indikator (HIGH saat upload HTTP) |
| 48 | RGB NeoPixel (hijau=WiFi OK, merah=tidak) |

### 3.3 Detail Sensor

#### Anemometer (UART ASCII)
- Frame dimulai dengan karakter `'A'`, diakhiri CR/LF atau `'*'`
- Tiap field dalam frame berformat `<Key><Value>`:

| Key | Arti | Konversi |
|-----|------|----------|
| `B` | Arah angin (°) | langsung |
| `C` | Kecepatan angin rata-rata | ÷ 10 → m/s |
| `D` | Kecepatan angin maksimum | ÷ 10 → m/s |
| `E` | Curah hujan 1 jam | ÷ 10 → mm |
| `F` | Curah hujan 24 jam | ÷ 10 → mm |
| `L` | Suhu | ÷ 10 → °C |
| `M` | Kelembaban | ÷ 10 → % |
| `N` | Tekanan atmosfer | ÷ 10 → hPa |

- Validasi frame: harus ada kunci B, C, D, E, F dengan nilai digit yang valid

#### THM30MD (Modbus RTU via RS485)
- Slave ID: `0x01`, Function Code: `0x04` (Read Input Registers)
- Register `0x0000`: suhu (÷10 → °C)
- Register `0x0001`: kelembaban (÷10 → %)
- Timeout respons: 250 ms
- CRC16 dengan polinomial `0xA001`
- Sumber suhu/humidity dipilih via flag kompilasi: `kUseThm30mdForTempHumidity` di `include/Sensor.h`
  - `false` (default): suhu & humidity diambil dari Anemometer, THM30MD tidak diinisialisasi
  - `true`: suhu & humidity diambil dari THM30MD

#### Lidar Lite V3 (I2C)
- Alamat I2C: `0x62`
- Register trigger: `0x00` (tulis `0x04`)
- Register jarak: `0x8F` (2 byte, high byte first)
- Offset kalibrasi: 5 cm
- Dikumpulkan 10 sampel per detik (interval 100 ms) oleh task terpisah
- Konversi di server:
  - `waterHeightCm = 1100 - distanceCm`
  - `waveHeightCm = 400 - distanceCm`

### 3.4 Arsitektur FreeRTOS

Program menggunakan 3 task FreeRTOS yang berjalan paralel:

```
Core 0:
  └── lidarSampleTask (prioritas 2)
        Setiap 100 ms: baca 1 sampel lidar
        Setelah 10 sampel: notifikasi uploadTask

Core 1:
  └── uploadTask (prioritas 1)
        Tunggu notifikasi dari lidarSampleTask
        Ambil data cuaca (mutex gWeatherMux)
        Ambil batch lidar (mutex gLidarMux)
        Kirim HTTP POST JSON ke server

Main Loop (loop()):
  └── Setiap ~1 ms: baca Anemometer (non-blocking)
      Setiap 1 s:   polling THM30MD (Modbus)
      Setiap 1 s:   print debug serial (jika diaktifkan)
```

**Sinkronisasi:**
- `gWeatherMux`: melindungi data Anemometer & THM30MD dari race condition
- `gLidarMux`: melindungi buffer batch lidar

### 3.5 Format Data yang Dikirim ke Server

**Metode:** HTTP POST dengan `Content-Type: application/json`

```json
{
  "type": "weather_lidar_distance_batch",
  "sample_interval_ms": 100,
  "windir": 270,
  "windavg": 1.23,
  "windmax": 2.50,
  "rain1h": 0.00,
  "rain24h": 0.10,
  "suhu": 30.50,
  "humidity": 70.20,
  "pressure": 1012.34,
  "lidar_distance_cm": [123, 124, 125, 126, 127, 128, 129, 130, 131, 132]
}
```

**Nilai fallback saat sensor tidak valid:**
- Anemometer tidak valid: `windir=0`, `windavg=0`, `windmax=0`, `rain=0`, `pressure=1013.25`
- THM30MD tidak valid: `suhu=99`, `humidity=99`
- Lidar tidak valid: elemen array `= -1`

### 3.6 Struktur File

```
Weather Monitoring/
├── platformio.ini
├── DOKUMENTASI_PROGRAM.md          (dokumentasi detail program ini)
├── VALIDASI_LAPANGAN_SENSOR.md     (hasil pengujian lapangan)
├── include/
│   ├── Sensor.h                    (struct data & deklarasi fungsi + pin defines)
│   └── RgbLedTest.h
└── src/
    ├── main.cpp                    (inisialisasi & FreeRTOS tasks)
    ├── Anemometer.cpp              (parsing frame ASCII UART)
    ├── thm30md.cpp                 (Modbus RTU driver)
    ├── lidar.cpp                   (driver I2C Lidar Lite V3)
    ├── server.cpp                  (WiFi + HTTP POST)
    └── RgbLedTest.cpp              (kontrol NeoPixel)
```

---

## 4. Sub-Proyek 2: Water Direction (Firmware)

**Lokasi:** `Water direction/`  
**Fungsi:** Membaca arah putaran encoder absolut untuk mendeteksi arah arus air (CW/CCW/STOP), lalu mengirim hasilnya ke server.

### 4.1 Hardware & Sensor

**Sensor:** Omron E6CP-A — Absolute Rotary Encoder 8-bit
- Resolusi: 256 langkah per putaran penuh
- Presisi sudut: 360° ÷ 256 = **1.40625° per langkah**
- Output: 8-bit Gray code, open-collector aktif-LOW (butuh pull-up)
- Interface: 8 pin paralel ke GPIO ESP32

### 4.2 Peta Pin (ESP32-S3 ↔ E6CP-A)

| GPIO ESP32 | Bit Encoder | Keterangan |
|-----------|-------------|------------|
| 17 | A0 (LSB) | Bit 0 |
| 15 | A1 | Bit 1 |
| 10 | A2 | Bit 2 |
| 12 | A3 | Bit 3 |
| 18 | A4 | Bit 4 |
| 16 | A5 | Bit 5 |
| 9  | A6 | Bit 6 |
| 11 | A7 (MSB) | Bit 7 |
| 5  | — | LED Indikator |
| 48 | — | RGB NeoPixel |

### 4.3 Alur Pembacaan Data

```
1. Baca 8 GPIO secara paralel (active-LOW → diinversikan)
2. Anti-glitch:
   a. Baca sampel pertama
   b. Tunggu 80 µs
   c. Baca sampel kedua
   d. Jika sama → gunakan nilai itu
   e. Jika beda → retry, max 4x percobaan
3. Median filter (opsional): ambil median dari 1/3/5/7 sampel
4. Gray code → decode ke binary (XOR shift method)
5. Hitung sudut: (count / 256) × 360° + offset
6. Deteksi arah: bandingkan count sekarang vs sebelumnya
   - dCount > 0 → CW
   - dCount < 0 → CCW
   - dCount = 0 → STOP
7. Hitung kecepatan: CPS, RPS, RPM
```

**Contoh Gray code decode:**
```
Gray:   0110 0000 (0x60)
Binary: 0100 0000 (64)
Sudut:  (64/256) × 360° = 90.00°
```

### 4.4 Parameter Konfigurasi (include/Sensor.h)

| Konstanta | Default | Keterangan |
|-----------|---------|------------|
| `ENC_INVERT_DIRECTION` | `false` | Balik arah CW/CCW |
| `ENC_MEDIAN_SAMPLE_COUNT` | `1` | Filter median: 1 (off), 3, 5, 7 |
| `ENC_ANGLE_OFFSET_DEG` | `0.0` | Offset referensi sudut (North) |
| `ENC_CODE_IS_GRAY` | `true` | Decode Gray → Binary |
| `ENC_ACTIVE_LOW` | `true` | LOW = logika 1 |
| `kSampleIntervalMs` | `50` | Interval sampling (ms) |

### 4.5 Format Data yang Dikirim ke Server

**Metode:** HTTP GET

```
GET /Wemon_BauBau/wemonbaubau.php?ang_dir=1
Authorization: Basic cGNzZXJ2ZXI6ZHRlbzIwMjU=
```

| Parameter | Nilai | Arti |
|-----------|-------|------|
| `ang_dir` | `-1` | CCW (berlawanan arah jarum jam) |
| `ang_dir` | `0` | STOP (diam) |
| `ang_dir` | `1` | CW (searah jarum jam) |

**Interval upload:** 1000 ms

### 4.6 Struktur File

```
Water direction/
├── platformio.ini
├── ENCODER_DOCUMENTATION.md       (dokumentasi teknis lengkap ~733 baris)
├── include/
│   ├── Sensor.h                   (konfigurasi encoder + deklarasi)
│   └── RgbLedStatus.h
└── src/
    ├── main.cpp                   (inisialisasi & main loop)
    ├── angular.cpp                (pembacaan GPIO, Gray decode, median filter)
    ├── server.cpp                 (WiFi + HTTP GET)
    └── RgbLedStatus.cpp           (kontrol NeoPixel)
```

---

## 5. Sub-Proyek 3: Water Flow (Firmware)

**Lokasi:** `water flow/`  
**Fungsi:** Mengukur kecepatan aliran air menggunakan encoder rotary 3-channel via hardware PCNT ESP32-S3, lalu mengirim nilai RPM ke server.

### 5.1 Hardware & Sensor

**Sensor:** Rotary Encoder 3-channel (A/B/Z)
- Resolusi: 600 pulsa per revolusi (PPR)
- Channel A: sinyal clock utama
- Channel B: sinyal arah (CW/CCW)
- Channel Z: index pulse (referensi posisi)

**Metode pembacaan:** Hardware PCNT (Pulse Counter Unit) — akurasi tinggi, non-blocking

### 5.2 Peta Pin (ESP32-S3)

| GPIO | Channel | Fungsi |
|------|---------|--------|
| 10 | ENCODER_A | Clock signal (A) |
| 11 | ENCODER_B | Direction signal (B) |
| 12 | ENCODER_Z | Index pulse (Z) |
| 5  | — | LED Indikator |
| 48 | — | RGB NeoPixel |

### 5.3 Konfigurasi PCNT

| Unit PCNT | Channel | Fungsi | Range |
|-----------|---------|--------|-------|
| Unit 0 | A + B | Quadrature counter (arah otomatis) | -30.000 ~ +30.000 |
| Unit 1 | Z | Index counter | 0 ~ 30.000 |

- **Glitch filter:** 100 taps (menghilangkan noise)
- **PCNT Mode:** `PCNT_MODE_REVERSE` untuk deteksi arah otomatis

### 5.4 Alur Pembacaan & Kalkulasi

```
1. Hardware PCNT berjalan terus di background (non-blocking)
2. encoderRead() dipanggil setiap loop:
   - Cek apakah gate 200 ms sudah lewat
   - Jika belum → return false (skip)
   - Jika sudah:
     a. Baca counter PCNT Unit 0 (deltaCount)
     b. Baca counter PCNT Unit 1 (zIndexCount)
     c. Reset counter ke 0
     d. Hitung speedCps = deltaCount / (intervalMs / 1000.0)
     e. Hitung speedRps = speedCps / pulsesPerRevolution
     f. Hitung speedRpm = speedRps × 60.0
     g. Tentukan arah: deltaCount > 0 → CW, < 0 → CCW, = 0 → STOP
     h. Akumulasi totalCount
     i. Return true + struct reading
```

**Struct Data:**
```cpp
struct EncoderReading {
  int32_t deltaCount;        // Pulsa dalam window sampling
  int32_t totalCount;        // Kumulatif sejak boot
  int32_t zIndexCount;       // Jumlah index pulse
  float   speedCps;          // Counts per second
  float   speedRps;          // Revolutions per second
  float   speedRpm;          // Revolutions per minute
  int8_t  direction;         // -1=CCW, 0=STOP, +1=CW
  uint32_t sampleIntervalMs; // Window sampling aktual (ms)
};
```

### 5.5 Format Data yang Dikirim ke Server

**Metode:** HTTP GET

```
GET /Wemon_BauBau/wemonbaubau.php?enc_rpm=12.34
Authorization: Basic cGNzZXJ2ZXI6ZHRlbzIwMjU=
```

| Parameter | Tipe | Keterangan |
|-----------|------|------------|
| `enc_rpm` | float | Kecepatan putaran (RPM) |

**Interval upload:** 1000 ms (throttled)

### 5.6 Struktur File

```
water flow/
├── platformio.ini
├── DOKUMENTASI.md              (dokumentasi teknis program ini)
├── include/
│   ├── Sensor.h                (struct EncoderReading + deklarasi fungsi)
│   └── RgbLedStatus.h
└── src/
    ├── main.cpp                (inisialisasi & main loop)
    ├── encoder.cpp             (driver PCNT, kalkulasi speed/direction)
    ├── server.cpp              (WiFi + HTTP GET)
    └── RgbLedStatus.cpp        (kontrol NeoPixel)
```

---

## 6. Sub-Proyek 4: Backend Server (PHP)

**Lokasi:** `multisensoris/`  
**Fungsi:** Menerima data dari semua node ESP32, memvalidasi, dan menyimpannya ke database MySQL.

### 6.1 Endpoint Utama: `wemonbaubau.php`

Endpoint tunggal yang mendeteksi tipe payload secara otomatis berdasarkan parameter yang diterima.

**Autentikasi:** HTTP Basic Auth wajib untuk semua request. Perbandingan password menggunakan `hash_equals()` (constant-time) untuk mencegah timing attack.

#### Deteksi Tipe Payload

```php
// Jika ada parameter cuaca → simpan ke weather_monitoring
if (isset($_GET['windir']) || isset($_GET['windavg']) || isset($_GET['distance']))
    → tipe: 'weather'

// Jika ada parameter encoder flow → simpan ke water_flow
if (isset($_GET['enc_rpm']) || isset($_GET['enc_cps']) || ...)
    → tipe: 'water_flow'

// Jika ada parameter encoder direction → simpan ke water_direction
if (isset($_GET['ang_angle']) || isset($_GET['ang_dir']) || ...)
    → tipe: 'water_direction'
```

**Selain GET query string, endpoint juga menerima HTTP POST dengan body JSON** (untuk payload `weather_lidar_distance_batch` dari Weather Monitoring node).

### 6.2 Pemrosesan Per Tipe Data

#### Tipe: Weather (GET)

Parameter yang diterima:

| Parameter | Tipe | Keterangan |
|-----------|------|------------|
| `windir` | int | Arah angin (°) |
| `windavg` | float | Kecepatan angin rata-rata (m/s) |
| `windmax` | float | Kecepatan angin maksimum (m/s) |
| `rain1h` | float | Curah hujan 1 jam (mm) |
| `rain24h` | float | Curah hujan 24 jam (mm) |
| `suhu` | float | Suhu (°C) |
| `humidity` | float | Kelembaban (%) |
| `pressure` | float | Tekanan atmosfer (hPa) |
| `distance` | int | Jarak lidar (cm) — opsional |

Konversi otomatis dari jarak ke ketinggian:
```php
function computeHeightsFromDistance(int $distance): array {
    return [
        'waterheight' => 1100 - $distance,  // Tinggi air dari dasar (cm)
        'waveheight'  => 400 - $distance,   // Tinggi gelombang dari permukaan tenang (cm)
    ];
}
```

#### Tipe: Weather Lidar Batch (POST JSON)

```json
{
  "type": "weather_lidar_distance_batch",
  "sample_interval_ms": 100,
  "windir": 270,
  "windavg": 1.23,
  "windmax": 2.50,
  "rain1h": 0.00,
  "rain24h": 0.10,
  "suhu": 30.50,
  "humidity": 70.20,
  "pressure": 1012.34,
  "lidar_distance_cm": [123, 124, 125, 126, 127, 128, 129, 130, 131, 132]
}
```

10 sampel lidar disimpan ke kolom `distance_01` s/d `distance_10`.

#### Tipe: Water Flow (GET)

| Parameter | Tipe | Keterangan |
|-----------|------|------------|
| `enc_rpm` | float | Kecepatan putaran encoder (RPM) |

#### Tipe: Water Direction (GET)

| Parameter | Tipe | Keterangan |
|-----------|------|------------|
| `ang_dir` | int | Arah: -1=CCW, 0=STOP, 1=CW |

### 6.3 Skema Database MySQL

#### Tabel `weather_monitoring`

| Kolom | Tipe | Keterangan |
|-------|------|------------|
| `id` | INT AUTO_INCREMENT | Primary key |
| `windir` | INT | Arah angin (°) |
| `windavg` | FLOAT | Kec. angin rata-rata (m/s) |
| `windmax` | FLOAT | Kec. angin maksimum (m/s) |
| `rain1h` | FLOAT | Curah hujan 1 jam (mm) |
| `rain24h` | FLOAT | Curah hujan 24 jam (mm) |
| `suhu` | FLOAT | Suhu (°C) |
| `humidity` | FLOAT | Kelembaban (%) |
| `pressure` | FLOAT | Tekanan atmosfer (hPa) |
| `distance` | INT | Jarak lidar (cm) |
| `waterheight` | INT | Tinggi air dari dasar (cm) |
| `waveheight` | INT | Tinggi gelombang (cm) |
| `lidar_sample_interval_ms` | INT | Interval sampel lidar (ms) |
| `distance_01` ... `distance_10` | INT | 10 sampel lidar batch |
| `created_at` | TIMESTAMP | Auto-filled |

#### Tabel `water_flow`

| Kolom | Tipe | Keterangan |
|-------|------|------------|
| `id` | INT AUTO_INCREMENT | Primary key |
| `enc_rpm` | FLOAT | Kecepatan encoder (RPM) |
| `created_at` | TIMESTAMP | Auto-filled |

#### Tabel `water_direction`

| Kolom | Tipe | Keterangan |
|-------|------|------------|
| `id` | INT AUTO_INCREMENT | Primary key |
| `ang_dir` | INT | Arah: -1/0/+1 |
| `created_at` | TIMESTAMP | Auto-filled |

### 6.4 File-file Backend Lainnya

| File | Fungsi |
|------|--------|
| `wemonbaubau.php` | Endpoint produksi utama — semua tipe data, MySQL |
| `kirimdata3.php` | Endpoint legacy — data cuaca saja, simpan ke CSV |
| `uji.php` | Endpoint testing — semua tipe, simpan ke CSV (tanpa DB) |
| `contoh.php` | Contoh lama — format sensor jarak, database berbeda |
| `test_wemonbaubau_curl.cmd` | Script Windows untuk test semua payload via curl |

#### Penggunaan Script Testing (Windows)

```batch
REM Test payload cuaca
test_wemonbaubau_curl.cmd weather

REM Test payload cuaca + lidar batch (POST JSON)
test_wemonbaubau_curl.cmd weather_lidar_batch

REM Test payload water flow
test_wemonbaubau_curl.cmd flow

REM Test payload water direction
test_wemonbaubau_curl.cmd direction
```

Environment variable yang bisa di-override:

| Variabel | Default |
|----------|---------|
| `WBB_SERVER` | `31.97.66.191` |
| `WBB_KPATH` | `/Wemon_BauBau/wemonbaubau.php` |
| `WBB_AUTH` | `pcserver:dteo2025` |
| `WBB_DEBUG` | `0` |

### 6.5 Fitur Keamanan Backend

- HTTP Basic Auth dengan constant-time comparison (`hash_equals()`)
- Prepared statements MySQL (mencegah SQL injection)
- Strict type checking (`declare(strict_types=1)`)
- Validasi tipe parameter — array dideteksi dan ditolak
- Validasi numerik — float dengan koma/titik keduanya didukung
- UTF-8 charset pada semua query database

---

## 7. Alur Data End-to-End

### 7.1 Node Weather Monitoring

```
[Anemometer UART 2400bps] ──┐
[THM30MD RS485 Modbus RTU] ──┤──► ESP32-S3 (main loop, non-blocking)
[Lidar Lite V3 I2C] ─────────┘       │
                                      │ FreeRTOS
                              lidarSampleTask (100ms × 10)
                                      │
                              uploadTask (setiap 10 sampel selesai)
                                      │
                              HTTP POST JSON
                                      │
                              31.97.66.191/wemonbaubau.php
                                      │
                              INSERT INTO weather_monitoring
```

### 7.2 Node Water Direction

```
[E6CP-A 8 pin paralel] ──► ESP32-S3 (main loop, 50ms)
                                  │
                          Stabilisasi (80µs × max 4 retry)
                                  │
                          Gray code → Binary
                                  │
                          Deteksi arah (CW/CCW/STOP)
                                  │
                          HTTP GET ?ang_dir=X (1000ms)
                                  │
                          INSERT INTO water_direction
```

### 7.3 Node Water Flow

```
[Encoder A/B/Z] ──► PCNT Unit 0+1 (hardware counter, terus menerus)
                          │
                    encoderRead() gate 200ms
                          │
                    Hitung RPM dari deltaCount
                          │
                    HTTP GET ?enc_rpm=X (1000ms)
                          │
                    INSERT INTO water_flow
```

### 7.4 Interval Upload & Recovery

| Node | Interval Upload | Recovery WiFi | Recovery ESP32 |
|------|----------------|----------------|----------------|
| Weather Monitoring | 1000 ms (per 10 lidar sampel) | Restart WiFi setelah 3 gagal berturut | Restart setelah 5 gagal |
| Water Direction | 1000 ms | Restart WiFi setelah 3 gagal | Restart setelah 5 gagal |
| Water Flow | 1000 ms | Restart WiFi setelah 3 gagal | Restart setelah 10 gagal |

### 7.5 LED Status Semua Node

| Kondisi | LED Diskrit (GPIO 5) | RGB NeoPixel (GPIO 48) |
|---------|----------------------|------------------------|
| WiFi terhubung | — | Hijau |
| WiFi tidak terhubung | — | Merah |
| Sedang upload HTTP | HIGH | — |
| Selesai upload | LOW | — |

---

## 8. Konfigurasi & Deployment

### 8.1 PlatformIO Build Config

Semua 3 firmware menggunakan konfigurasi dasar yang sama:

```ini
[env:esp32s3usbotg]
platform  = espressif32
board     = esp32s3usbotg
framework = arduino
monitor_speed = 115200
lib_deps  = adafruit/Adafruit NeoPixel @ ^1.12.0
```

Perbedaan: Weather Monitoring memiliki `build_src_filter` untuk mengecualikan file example dari build.

### 8.2 Flag Compile-Time yang Bisa Diubah

#### Weather Monitoring (`src/main.cpp`)

| Flag | Default | Efek |
|------|---------|------|
| `kEnableServerUpload` | `true` | Aktifkan/matikan HTTP upload |
| `kEnableSerialDebug` | `true` | Aktifkan/matikan debug serial |

#### Weather Monitoring (`include/Sensor.h`)

| Flag | Default | Efek |
|------|---------|------|
| `kUseThm30mdForTempHumidity` | `false` | `true` = pakai THM30MD; `false` = pakai Anemometer untuk suhu/humidity |

#### Water Direction (`include/Sensor.h`)

| Flag | Default | Efek |
|------|---------|------|
| `ENC_INVERT_DIRECTION` | `false` | Balik arah CW/CCW jika sensor terpasang terbalik |
| `ENC_MEDIAN_SAMPLE_COUNT` | `1` | Ukuran filter median (1=off, 3/5/7) |
| `ENC_ANGLE_OFFSET_DEG` | `0.0` | Offset sudut referensi North |

#### Water Direction & Flow (`src/main.cpp`)

| Flag | Default | Efek |
|------|---------|------|
| `kEnableServerUpload` | `true` | Aktifkan/matikan HTTP upload |
| `kEnableSerialDebug` | `true` | Aktifkan/matikan debug serial |
| `kSampleIntervalMs` | `50` / `200` | Interval sampling encoder |

### 8.3 Cara Build & Upload (PlatformIO)

```bash
# Compile
platformio run

# Upload ke ESP32-S3 (USB)
platformio run --target upload

# Monitor serial (115200 baud)
platformio device monitor
```

### 8.4 Dependensi Library

| Library | Versi | Dipakai Oleh |
|---------|-------|--------------|
| Adafruit NeoPixel | ≥ 1.12.0 | Semua 3 firmware |
| ESP32 Arduino Core | built-in | Semua 3 firmware (WiFi, HTTPClient, PCNT, I2C, UART) |
| Arduino JSON | — | Weather Monitoring (parsing response) |

---

## 9. Referensi Cepat

### 9.1 Semua Endpoint & Parameter

| Payload | Metode | Parameter | Tabel DB |
|---------|--------|-----------|----------|
| Weather | GET | `windir`, `windavg`, `windmax`, `rain1h`, `rain24h`, `suhu`, `humidity`, `pressure`, `distance` | `weather_monitoring` |
| Weather Lidar Batch | POST JSON | JSON dengan `type`, `lidar_distance_cm[]`, + field cuaca | `weather_monitoring` |
| Water Flow | GET | `enc_rpm` | `water_flow` |
| Water Direction | GET | `ang_dir` | `water_direction` |

**Base URL:** `http://31.97.66.191/Wemon_BauBau/wemonbaubau.php`  
**Auth Header:** `Authorization: Basic cGNzZXJ2ZXI6ZHRlbzIwMjU=`

### 9.2 Semua Pin GPIO per Node

#### Weather Monitoring

| GPIO | Sensor / Fungsi |
|------|-----------------|
| 6 | Anemometer RX |
| 7 | Anemometer TX |
| 8 | Lidar SDA |
| 9 | Lidar SCL |
| 17 | THM30MD RX |
| 18 | THM30MD TX |
| 5 | LED Indikator |
| 48 | RGB NeoPixel |

#### Water Direction

| GPIO | Bit E6CP-A |
|------|------------|
| 17 | A0 (LSB) |
| 15 | A1 |
| 10 | A2 |
| 12 | A3 |
| 18 | A4 |
| 16 | A5 |
| 9 | A6 |
| 11 | A7 (MSB) |
| 5 | LED Indikator |
| 48 | RGB NeoPixel |

#### Water Flow

| GPIO | Fungsi |
|------|--------|
| 10 | Encoder A (Clock) |
| 11 | Encoder B (Direction) |
| 12 | Encoder Z (Index) |
| 5 | LED Indikator |
| 48 | RGB NeoPixel |

### 9.3 Kredensial Sistem

| Keperluan | Nilai |
|-----------|-------|
| WiFi SSID | `Wemon Bau Bau` |
| WiFi Password | `WemonBauBau2026` |
| HTTP Auth User | `pcserver` |
| HTTP Auth Pass | `dteo2025` |
| MySQL User | `Joko` |
| MySQL Pass | `Joko12345` |
| MySQL DB | `Wemon_BauBau` |

### 9.4 Ringkasan Fungsi Utama Per Module

| Module | Fungsi Utama | File |
|--------|-------------|------|
| Anemometer | `anemometerInit()`, `anemometerRead()` | `Weather Monitoring/src/Anemometer.cpp` |
| THM30MD | `thm30mdInit()`, `thm30mdRead()` | `Weather Monitoring/src/thm30md.cpp` |
| Lidar | `lidarInitSimple()`, `lidarReadSimple()` | `Weather Monitoring/src/lidar.cpp` |
| Weather Server | `serverUploadLidarDistanceBatch()` | `Weather Monitoring/src/server.cpp` |
| Angular Encoder | `encoderInit()`, `encoderRead()` | `Water direction/src/angular.cpp` |
| PCNT Encoder | `encoderInit()`, `encoderRead()` | `water flow/src/encoder.cpp` |
| Flow/Dir Server | `serverInit()`, `serverHandleUpload()` | `*/src/server.cpp` |

---

*Dokumentasi ini dihasilkan dari analisis kode sumber keempat sub-proyek. Terakhir diperbarui: 2026-05-30.*
