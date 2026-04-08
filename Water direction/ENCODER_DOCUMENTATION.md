# E6CP-A 8-bit Angular Encoder - Program Documentation

## 📋 Daftar Isi
1. [Pengenalan Program](#pengenalan-program)
2. [Hardware Configuration](#hardware-configuration)
3. [Fitur Utama](#fitur-utama)
4. [Konfigurasi Program](#konfigurasi-program)
5. [Output Serial](#output-serial)
6. [Server Upload](#server-upload)
7. [Contoh Kasus Penggunaan](#contoh-kasus-penggunaan)
8. [Troubleshooting](#troubleshooting)

---

## Pengenalan Program

Program ini membaca sensor encoder absolut E6CP-A dengan resolusi 8-bit (256 step) yang terhubung ke ESP32-S3-USB-OTG. Sensor menghasilkan 8 output paralel (A0-A7) yang masing-masing merepresentasikan satu bit posisi mutlak.

**Fitur Program:**
- ✅ Pembacaan 8-bit paralel dengan stabilisasi 2-sampel
- ✅ Filter median noise (opsional 1/3/5/7 sampel)
- ✅ Konversi ke sudut absolut (0-359.9°) dengan offset
- ✅ Estimasi arah rotasi (CW/CCW) dan kecepatan (RPM)
- ✅ Debug serial real-time dengan bit pattern display
- ✅ Upload ke server HTTP (payload minimal: `ang_dir`)

**Spesifikasi Encoder:**
| Parameter | Nilai |
|-----------|-------|
| Resolusi | 8-bit (256 step per putaran) |
| Sudut per step | 360/256 = 1.40625° |
| Output | 8 pin digital (A0-A7) open-collector |
| Tipe output | Aktif LOW (perlu pull-up) |
| Format data | 8-bit Gray code (umum pada E6CP-A), didecode ke biner 0-255 |

---

## Hardware Configuration

### Pin Mapping

ESP32-S3 → E6CP-A Sensor

| ESP32 Pin | Fungsi | E6CP-A Output |
|-----------|--------|---------------|
| GPIO 17 | Angular_0 | A0 (LSB) |
| GPIO 15 | Angular_1 | A1 |
| GPIO 10 | Angular_2 | A2 |
| GPIO 12 | Angular_3 | A3 |
| GPIO 18 | Angular_4 | A4 |
| GPIO 16 | Angular_5 | A5 |
| GPIO 9  | Angular_6 | A6 |
| GPIO 11 | Angular_7 | A7 (MSB) |
| GND      | Common Ground | -GND |

**Catatan Penting:**
- ⚠️ E6CP-A output adalah open-collector dan aktif LOW (LOW=logika 1), jadi **wajib ada pull-up ke 3.3V**.
- Program menggunakan `INPUT_PULLUP` internal ESP32 (sekitar 40kΩ) dan membalik logika di software (`ENC_ACTIVE_LOW=true`).
- Untuk kabel panjang (>50cm), tambahkan pull-up eksternal 4.7kΩ ke 3.3V

### Wiring Diagram

```
E6CP-A Sensor                ESP32-S3-USB-OTG
├─ A0 ──┬──> GPIO 17 (INPUT_PULLUP)
├─ A1 ──┬──> GPIO 15
├─ A2 ──┬──> GPIO 10
├─ A3 ──┬──> GPIO 12
├─ A4 ──┬──> GPIO 18
├─ A5 ──┬──> GPIO 16
├─ A6 ──┬──> GPIO 9
├─ A7 ──┬──> GPIO 11
└─ GND ─┴──> GND (Common Ground)

Power Supply (5V untuk sensor E6CP-A, 3.3V untuk ESP32)
```

---

## Fitur Utama

### 1. Pembacaan 8-Bit Paralel

Setiap saat sampling, program membaca 8 pin paralel.

Pada implementasi final, output dibedakan menjadi:
- `raw` = nilai 8-bit hasil gabungan pin A0-A7 (setelah koreksi aktif-LOW dan bit-order). Pada E6CP-A umumnya masih berupa Gray code.
- `count` = hasil decode `raw` (Gray → Binary) dalam rentang 0-255. Nilai ini dipakai untuk `angle`, `dCount`, dan perhitungan kecepatan.

**Proses (ilustrasi):**
```
GPIO Raw: 15=1, 16=0, 17=1, 18=0, 9=1, 10=0, 11=1, 12=0
          ↓
raw: 0b10101010 = 0xAA
           ↓ Gray → Binary
count: 0xCC (decimal 204)
```

**Stabilisasi Pembacaan (Anti Glitch):**
- Baca sampel 1
- Tunggu 80µs
- Baca sampel 2
- Jika sama → gunakan nilai tersebut
- Jika beda, ulangi hingga 4 kali atau gunakan sampel terakhir

Tujuan: menghindari glitch saat transisi multiple bit.

### 2. Konversi Sudut

**Rumus:**
```
angle_raw = (count / 256) × 360°
angle_final = (angle_raw + offset) mod 360°
```

**Contoh:**
- Raw code = 0 → angle = 0°
- Raw code = 64 → angle = 90°
- Raw code = 128 → angle = 180°
- Raw code = 192 → angle = 270°
- Raw code = 256 (wraparound ke 0) → angle = 0°

### 3. Filter Median (Noise Reduction)

**Opsi:** 1 (off), 3, 5, atau 7 sampel

**Cara Kerja:**
```
Buffer pada sampling 1,2,3 dengan ENC_MEDIAN_SAMPLE_COUNT=3:

Sampling 1: buffer=[0xAA, ?, ?] → median=0xAA → gunakan 0xAA
Sampling 2: buffer=[0xAC, 0xAA, ?] → median=0xAA (tengah dari sorted)
Sampling 3: buffer=[0xAC, 0xAA, 0xA8] → sorted=[0xA8, 0xAA, 0xAC] → median=0xAA

Manfaat: smoothing untuk data noisy
Tradeoff: response time lebih lambat
```

### 4. Offset Sudut (North Reference)

Mengizinkan rotasi referensi arah. Contoh: jika arah utara Anda saat raw=64, set offset=90.

**Contoh offset=45°:**
```
Raw code 0 → angle = 0 + 45 = 45°
Raw code 64 → angle = 90 + 45 = 135°
Raw code 128 → angle = 180 + 45 = 225°
Raw code 192 → angle = 270 + 45 = 315°
```

### 5. Arah Rotasi & Kecepatan

**Arah:**
- Delta_code > 0 → CW (Clockwise)
- Delta_code < 0 → CCW (Counter-Clockwise)
- Delta_code = 0 → STOP

**Kecepatan:**
```
speedCps = delta_code / elapsed_time_seconds
speedRps = speedCps / 256 (revolution per second)
speedRpm = speedRps × 60
```

**Invert Direction:**
Jika `ENC_INVERT_DIRECTION = true`, CW akan ditampilkan sebagai CCW dan sebaliknya.

---

## Konfigurasi Program

### File Konfigurasi Utama: `include/Sensor.h`

```cpp
// Encoder Configuration Options
#define ENC_INVERT_DIRECTION false      // true = invert CW/CCW
#define ENC_MEDIAN_SAMPLE_COUNT 1       // 1=no filter, 3/5/7=median
#define ENC_ANGLE_OFFSET_DEG 0.0f       // Offset 0-359.9 degrees

// E6CP-A umumnya output Gray code & aktif-LOW (open-collector)
#define ENC_CODE_IS_GRAY true           // true=decode Gray→Binary, false=anggap sudah Binary
#define ENC_ACTIVE_LOW true             // true=LOW dianggap '1' (aktif-LOW)

// Helper jika urutan bit terbalik karena wiring
#define ENC_BIT_ORDER_MSB_FIRST false   // false=A0->bit0, true=A0->bit7
```

### File Lain yang Dapat Dikonfigurasi

**`src/main.cpp`** - Kontrol upload server dan interval sampling:
```cpp
constexpr uint32_t kSampleIntervalMs = 50;      // Sampling interval (ms)
constexpr bool kEnableServerUpload = false;     // true = upload ke server
constexpr bool kEnableSerialDebug = true;       // true = print debug serial
```

**`src/server.cpp`** - Kredensial dan endpoint server:
```cpp
const char *kSsid = "YOUR_WIFI_SSID";
const char *kPass = "YOUR_WIFI_PASSWORD";
const char *kServer = "31.97.66.191";
const char *kPath = "/Wemon_BauBau/wemonbaubau.php";
const char *kHttpUser = "YOUR_HTTP_USER";
const char *kHttpPass = "YOUR_HTTP_PASSWORD";
```

### Build & Upload

```bash
# Compile firmware
platformio run

# Upload ke board
platformio run --target upload

# Monitor serial output
platformio device monitor --baud 115200
```

---

## Output Serial

### Format Output Debug

```
[ANG] tMs=12345 | dir=CW | raw=0x60 (01100000) | count=64 (0x40) | angle=90.00 deg | dCount=1 | total=256 | cps=2.50 | rpm=0.63 | dtMs=50 | median=1
```

**Field Penjelasan:**

| Field | Artinya | Contoh |
|-------|---------|--------|
| `tMs` | Waktu milisecond sejak startup | 12345 |
| `dir` | Arah rotasi (CW/CCW/STOP) | CW |
| `raw` | Raw 8-bit dari pin (umumnya Gray) | 0x60 (01100000) |
| `count` | Hasil decode (biner 0-255) | 64 (0x40) |
| `angle` | Sudut absolut dengan offset (0-359.9°) | 90.00 deg |
| `dCount` | Delta count sejak pembacaan terakhir | 1 |
| `total` | Akumulasi delta count sejak startup | 256 |
| `cps` | Count per second (raw speed) | 2.50 |
| `rpm` | Revolutions per minute | 0.63 |
| `dtMs` | Interval waktu pembacaan aktual | 50 |
| `median` | Ukuran median filter yang aktif | 1 |

### Startup Message

```
========================================
E6CP-A 8-bit Angular Encoder Debug Start
========================================
Configuration:
  Sample Interval: 50 ms
  Median Filter: 1
  Invert Direction: NO
  Angle Offset (North): 0.00 deg
  Server Upload: DISABLED
Rotate shaft slowly for initial validation.
========================================
```

---

## Contoh Output Berdasarkan Kondisi

### 📌 Kondisi 1: STATIONARY (Sensor Diam)

```
=========== STATIONARY (50ms interval) ===========
[ANG] tMs=1050 | dir=STOP | raw=0x60 (01100000) | count=64 (0x40) | angle=90.00 deg | dCount=0 | total=0 | cps=0.00 | rpm=0.00 | dtMs=50 | median=1
[ANG] tMs=1100 | dir=STOP | raw=0x60 (01100000) | count=64 (0x40) | angle=90.00 deg | dCount=0 | total=0 | cps=0.00 | rpm=0.00 | dtMs=50 | median=1
[ANG] tMs=1150 | dir=STOP | raw=0x60 (01100000) | count=64 (0x40) | angle=90.00 deg | dCount=0 | total=0 | cps=0.00 | rpm=0.00 | dtMs=50 | median=1
```

**Karakteristik:**
- `dCount` selalu 0 (tidak ada perubahan)
- `cps` dan `rpm` = 0.00
- `dir` = STOP
- `angle` tetap konstan

---

### 📌 Kondisi 2: SLOW ROTATION CW (Putaran lambat searah jarum jam)

```
=========== SLOW ROTATION CW (1 step per sampling) ===========
[ANG] tMs=2050 | dir=CW | raw=0x61 (01100001) | count=65 (0x41) | angle=91.41 deg | dCount=1 | total=1 | cps=20.0 | rpm=0.07 | dtMs=50 | median=1
[ANG] tMs=2100 | dir=CW | raw=0x63 (01100011) | count=66 (0x42) | angle=92.81 deg | dCount=1 | total=2 | cps=20.0 | rpm=0.07 | dtMs=50 | median=1
[ANG] tMs=2150 | dir=CW | raw=0x62 (01100010) | count=67 (0x43) | angle=94.22 deg | dCount=1 | total=3 | cps=20.0 | rpm=0.07 | dtMs=50 | median=1
[ANG] tMs=2200 | dir=CW | raw=0x66 (01100110) | count=68 (0x44) | angle=95.63 deg | dCount=1 | total=4 | cps=20.0 | rpm=0.07 | dtMs=50 | median=1
```

**Karakteristik:**
- `count` meningkat 1 per sampling (0x41 → 0x42 → 0x43)
- `dCount` = 1 (1 step per interval)
- `angle` meningkat ~1.4° per sampling (360/256)
- `cps` = 20 counts/sec (1 count / 0.05 sec)
- `rpm` = 0.07 (20/256 × 60)
- `dir` = CW

---

### 📌 Kondisi 3: FAST ROTATION CW (Putaran cepat searah jarum jam)

```
=========== FAST ROTATION CW (16 steps per sampling) ===========
[ANG] tMs=3050 | dir=CW | raw=0x78 (01111000) | count=80 (0x50) | angle=112.50 deg | dCount=16 | total=16 | cps=320.0 | rpm=1.17 | dtMs=50 | median=1
[ANG] tMs=3100 | dir=CW | raw=0x50 (01010000) | count=96 (0x60) | angle=135.00 deg | dCount=16 | total=32 | cps=320.0 | rpm=1.17 | dtMs=50 | median=1
[ANG] tMs=3150 | dir=CW | raw=0x48 (01001000) | count=112 (0x70) | angle=157.50 deg | dCount=16 | total=48 | cps=320.0 | rpm=1.17 | dtMs=50 | median=1
[ANG] tMs=3200 | dir=CW | raw=0xC0 (11000000) | count=128 (0x80) | angle=180.00 deg | dCount=16 | total=64 | cps=320.0 | rpm=1.17 | dtMs=50 | median=1
```

**Karakteristik:**
- `code` naik 16 per sampling (0x50 → 0x60 → 0x70 → 0x80)
- `dCount` = 16 (16 steps per interval)
- `angle` naik ~22.5° per sampling (16 × 1.40625°)
- `cps` = 320 (16 counts / 0.05 sec)
- `rpm` = 1.17 revolution per minute
- `dir` = CW

---

### 📌 Kondisi 4: CCW ROTATION (Putaran berlawanan arah jarum jam)

```
=========== CCW ROTATION (8 steps per sampling, backward) ===========
[ANG] tMs=4050 | dir=CCW | raw=0x44 (01000100) | count=120 (0x78) | angle=168.75 deg | dCount=-8 | total=56 | cps=-160.0 | rpm=-0.59 | dtMs=50 | median=1
[ANG] tMs=4100 | dir=CCW | raw=0x48 (01001000) | count=112 (0x70) | angle=157.50 deg | dCount=-8 | total=48 | cps=-160.0 | rpm=-0.59 | dtMs=50 | median=1
[ANG] tMs=4150 | dir=CCW | raw=0x5C (01011100) | count=104 (0x68) | angle=146.25 deg | dCount=-8 | total=40 | cps=-160.0 | rpm=-0.59 | dtMs=50 | median=1
```

**Karakteristik:**
- `code` menurun (wraparound dihandle: 0x78 → 0x70 → 0x68)
- `dCount` negatif (-8)
- `angle` turun ~11.25° per sampling
- `cps` negatif (-160)
- `rpm` negatif (-0.59)
- `dir` = CCW

---

### 📌 Kondisi 5: WRAPAROUND (Sensor dari 255→0)

```
=========== WRAPAROUND 255→0 ===========
[ANG] tMs=5050 | dir=CW | raw=0x81 (10000001) | count=254 (0xFE) | angle=359.63 deg | dCount=1 | total=256 | cps=20.0 | rpm=0.07 | dtMs=50 | median=1
[ANG] tMs=5100 | dir=CW | raw=0x80 (10000000) | count=255 (0xFF) | angle=359.84 deg | dCount=1 | total=257 | cps=20.0 | rpm=0.07 | dtMs=50 | median=1
[ANG] tMs=5150 | dir=CW | raw=0x00 (00000000) | count=0 (0x00) | angle=0.00 deg | dCount=1 | total=258 | cps=20.0 | rpm=0.07 | dtMs=50 | median=1
[ANG] tMs=5200 | dir=CW | raw=0x01 (00000001) | count=1 (0x01) | angle=1.41 deg | dCount=1 | total=259 | cps=20.0 | rpm=0.07 | dtMs=50 | median=1
```

**Karakteristik:**
- Transisi 255→0 ditangani dengan wrapped delta (256→1, bukan -255)
- `total` terus meningkat (tidak reset)
- Kontinuitas terjaga

---

### 📌 Kondisi 6: DENGAN MEDIAN FILTER (ENC_MEDIAN_SAMPLE_COUNT = 3)

```
=========== WITH MEDIAN FILTER (3 samples) ===========

Raw sensor bising: 0x40 → 0x41 → 0x3F → 0x40 → 0x41 → 0x42

Output:
[ANG] tMs=6050 | dir=CW | raw=0x60 (01100000) | count=64 (0x40) | angle=90.00 deg | dCount=0 | total=0 | cps=0.00 | rpm=0.00 | dtMs=50 | median=3
[ANG] tMs=6100 | dir=CW | raw=0x60 (01100000) | count=64 (0x40) | angle=90.00 deg | dCount=0 | total=0 | cps=0.00 | rpm=0.00 | dtMs=50 | median=3
[ANG] tMs=6150 | dir=CW | raw=0x60 (01100000) | count=64 (0x40) | angle=90.00 deg | dCount=0 | total=0 | cps=0.00 | rpm=0.00 | dtMs=50 | median=3
[ANG] tMs=6200 | dir=CW | raw=0x60 (01100000) | count=64 (0x40) | angle=90.00 deg | dCount=0 | total=0 | cps=0.00 | rpm=0.00 | dtMs=50 | median=3
[ANG] tMs=6250 | dir=CW | raw=0x61 (01100001) | count=65 (0x41) | angle=91.41 deg | dCount=1 | total=1 | cps=20.0 | rpm=0.07 | dtMs=50 | median=3
```

**Karakteristik:**
- Noise (0x3F) diabaikan oleh median filter
- Output lebih stabil, berubah hanya jika benar-benar ada perubahan nyata
- `median=3` menunjukkan filter aktif

---

### 📌 Kondisi 7: DENGAN ANGLE OFFSET (ENC_ANGLE_OFFSET_DEG = 90.0)

```
=========== WITH ANGLE OFFSET = 90 deg ===========
[ANG] tMs=7050 | dir=CW | raw=0x00 (00000000) | count=0 (0x00) | angle=90.00 deg | dCount=0 | total=0 | cps=0.00 | rpm=0.00 | dtMs=50 | median=1
                                                       ↑ tanpa offset akan 0.00, dengan offset=90.0f menjadi 90.00
[ANG] tMs=7100 | dir=CW | raw=0x60 (01100000) | count=64 (0x40) | angle=180.00 deg | dCount=64 | total=64 | cps=1280.0 | rpm=4.69 | dtMs=50 | median=1
                                                       ↑ tanpa offset akan 90.00, dengan offset=90.0f menjadi 180.00
[ANG] tMs=7150 | dir=CW | raw=0xC0 (11000000) | count=128 (0x80) | angle=270.00 deg | dCount=64 | total=128 | cps=1280.0 | rpm=4.69 | dtMs=50 | median=1
                                                       ↑ tanpa offset akan 180.00, dengan offset=90.0f menjadi 270.00
[ANG] tMs=7200 | dir=CW | raw=0xA0 (10100000) | count=192 (0xC0) | angle=0.00 deg | dCount=64 | total=192 | cps=1280.0 | rpm=4.69 | dtMs=50 | median=1
                                                       ↑ tanpa offset akan 270.00, dengan offset=90.0f menjadi 0.00 (wraparound)
```

**Karakteristik:**
- Setiap angle value digeser +90°
- Wraparound otomatis (270+90=360→0)
- Berguna untuk kalibrasi arah utara/referensi

---

### 📌 Kondisi 8: DENGAN INVERT DIRECTION (ENC_INVERT_DIRECTION = TRUE)

```
=========== WITH INVERT DIRECTION ENABLED ===========

Saat putaran CW (code meningkat):
[ANG] tMs=8050 | dir=CCW | raw=0x61 (01100001) | count=65 (0x41) | angle=91.41 deg | dCount=1 | total=1 | cps=20.0 | rpm=0.07 | dtMs=50 | median=1
                    ↑ normalnya "CW", tapi karena invert, ditampilkan "CCW"

Saat putaran CCW (code menurun):
[ANG] tMs=8100 | dir=CW | raw=0x60 (01100000) | count=64 (0x40) | angle=90.00 deg | dCount=-1 | total=0 | cps=-20.0 | rpm=-0.07 | dtMs=50 | median=1
                    ↑ normalnya "CCW", tapi karena invert, ditampilkan "CW"
```

**Karakteristik:**
- Arah terbalik namun angular value tetap sama
- Berguna jika motor dipasang dengan wiring terbalik

---

## Server Upload

Untuk mengurangi ukuran pengiriman data, firmware **hanya mengirim** parameter `ang_dir`.

Perhitungan lain (angle, rpm, raw code, dll) tetap ada untuk debug Serial, tetapi tidak dikirim ke server.

Ketika `kEnableServerUpload = true`, data dikirim sebagai parameter GET:

```
GET /Wemon_BauBau/wemonbaubau.php?ang_dir=1

HTTP/1.1 200 OK
```

**Parameter Penjelasan:**

| Parameter | Tipe | Range | Contoh |
|-----------|------|-------|--------|
| `ang_dir` | int | -1, 0, 1 | Arah (CCW, STOP, CW) |

Catatan backend:
- URL tersebut mengarah ke file: /var/www/html/Wemon_BauBau/wemonbaubau.php
- Data tersimpan ke MySQL database Wemon_BauBau tabel water_direction (kolom ang_dir + created_at)

### Serial Log Saat Upload

```
[SRV] dir=CW | angle=90.00 deg | dCount=1 | total=256 | rpm=0.63 | dtMs=50
HTTP response: 200
Server response:
OK
```

---

## Contoh Kasus Penggunaan

### 🔧 Kasus 1: Water Direction Sensor untuk Stasiun Cuaca

**Tujuan:** Mendeteksi arah angin 0-360° dengan referensi utara = 0°

**Konfigurasi:**
```cpp
#define ENC_INVERT_DIRECTION false
#define ENC_MEDIAN_SAMPLE_COUNT 5      // Filter wind noise
#define ENC_ANGLE_OFFSET_DEG 0.0f      // North = 0°
```

**Kalibrasi:**
1. Arahkan sensor ke utara (kompas)
2. Baca raw code: misal 0x64
3. Hitung offset = -(0x64 * 360/256) = -90.0
4. Set `#define ENC_ANGLE_OFFSET_DEG -90.0f`
5. Verifikasi: raw 0x64 → angle = 0° ✓

**Output:**
```
Wind from West (270°): [ANG] angle=270.00 deg | dir=CW | rpm=0.15
Wind from South (180°): [ANG] angle=180.00 deg | dir=CCW | rpm=0.12
Wind from North (0°): [ANG] angle=0.00 deg | dir=STOP | rpm=0.00
```

---

### 🔧 Kasus 2: Motor Posisi dengan Monitoring Kecepatan

**Tujuan:** Monitor putaran motor dan detect stall/jam

**Konfigurasi:**
```cpp
#define ENC_INVERT_DIRECTION false
#define ENC_MEDIAN_SAMPLE_COUNT 1      // Real-time (no smoothing)
#define ENC_ANGLE_OFFSET_DEG 0.0f
```

**Logic Alert:**
```cpp
// Di dalam main loop, tambahkan:
if (reading.speedRpm < 0.5 && reading.direction != 0) {
    Serial.println("⚠️ LOW SPEED ALERT!");
}
if (reading.direction == 0 && reading.deltaCount == 0) {
    Serial.println("⚠️ MOTOR STALLED!");
}
```

**Contoh Output:**
```
[ANG] angle=0.00 deg | dir=CW | rpm=5.42 | total=1280    // Normal
[ANG] angle=2.81 deg | dir=CW | rpm=0.22 | total=1281    // Slow
⚠️ LOW SPEED ALERT!
[ANG] angle=2.81 deg | dir=STOP | rpm=0.00 | total=1281  // Stalled
⚠️ MOTOR STALLED!
```

---

### 🔧 Kasus 3: Bypass Lintasan dengan Invert Direction

**Tujuan:** Dua encoder mendeteksi aliran dari dua arah berbeda

**Sensor 1 (Normal):**
```cpp
#define ENC_INVERT_DIRECTION false
```

**Sensor 2 (Terbalik/Mirrored):**
```cpp
#define ENC_INVERT_DIRECTION true
```

**Output:**
```
Aliran dari kiri → Sensor 1: CW, Sensor 2: CW
Aliran dari kanan → Sensor 1: CCW, Sensor 2: CCW
```

---

## Troubleshooting

### ❌ Masalah 0: Lonjakan `dCount` Besar Saat Putar Pelan

**Gejala:**
```
[ANG] ... | dCount=80
[ANG] ... | dCount=-41
```

**Penyebab paling umum:** konfigurasi format data / logika input belum sesuai (Gray code atau aktif-LOW), atau urutan bit terbalik.

**Checklist Solusi (urutkan dari yang paling sering):**
1. ✅ Pastikan output aktif-LOW:
  ```cpp
  #define ENC_ACTIVE_LOW true
  ```
2. ✅ Pastikan encoder dibaca sebagai Gray code (E6CP-A umumnya Gray):
  ```cpp
  #define ENC_CODE_IS_GRAY true
  ```
3. ✅ Jika pola masih meloncat, coba balik urutan bit (indikasi wiring A0..A7 terbalik):
  ```cpp
  #define ENC_BIT_ORDER_MSB_FIRST true
  ```
4. ✅ Tambahkan pull-up eksternal 4.7kΩ dan rapikan kabel untuk mengurangi glitch.

### ❌ Masalah 1: Output Sembur/Jitter

**Gejala:**
```
[ANG] angle=90.00 deg | dCount=1 | ...
[ANG] angle=91.41 deg | dCount=-1 | ...  ← Tiba-tiba mundur
[ANG] angle=90.00 deg | dCount=1 | ...
```

**Penyebab:** Noise pada input pin, koneksi loose, atau pull-up insufficient

**Solusi:**
1. ✅ Aktifkan median filter:
   ```cpp
   #define ENC_MEDIAN_SAMPLE_COUNT 5
   ```
2. ✅ Periksa kabel koneksi (tidak terlalu panjang)
3. ✅ Tambahkan pull-up eksternal 4.7kΩ ke 3.3V
4. ✅ Pastikan common ground terhubung

---

### ❌ Masalah 2: Arah Terbalik

**Gejala:**
```
Putar searah jarum jam (CW) → ditampilkan CCW
```

**Solusi:**
```cpp
#define ENC_INVERT_DIRECTION true
```

Atau periksa wiring pin A0-A7, mungkin terbalik.

---

### ❌ Masalah 3: Sudut Tidak Mulai dari 0°

**Gejala:**
```
Seharusnya pointing North (0°), tapi showing 45°
```

**Solusi - Kalibrasi Offset:**

1. Arahkan sensor ke referensi (utara/home)
2. Baca `count` dari serial output
3. Hitung offset: `offset_deg = -(count × 360 / 256)`
4. Update:
   ```cpp
   #define ENC_ANGLE_OFFSET_DEG <hasil_perhitungan>
   ```

**Contoh:**
- `count` saat pointing north = 64 (0x40)
- offset = -(64 × 360 / 256) = -90.0°
- Set: `#define ENC_ANGLE_OFFSET_DEG -90.0f`

---

### ❌ Masalah 4: Server Upload Gagal

**Gejala:**
```
HTTP response: -1
Upload failed. Consecutive failures: 1
```

**Solusi:**
1. ✅ Periksa WiFi SSID/Password di `server.cpp`
2. ✅ Verifikasi server IP dan endpoint accessible
3. ✅ Aktifkan serial debug untuk response detail
4. ✅ Timeout terlalu pendek? Tambah di `server.cpp`:
   ```cpp
   http.setTimeout(10000);  // 10 detik
   ```

---

### ❌ Masalah 5: Response Time Lambat

**Gejala:**
```
Sensor rotate tapi output update (median=5) terasa lag
```

**Solusi:**
- ✅ Kurangi sample count filter:
  ```cpp
  #define ENC_MEDIAN_SAMPLE_COUNT 3  // dari 5 → 3
  ```
- ✅ Perkecil interval sampling di `main.cpp`:
  ```cpp
  constexpr uint32_t kSampleIntervalMs = 20;  // dari 50 → 20
  ```

---

## Referensi Cepat

### Build & Deploy Commands

```bash
# Compile
cd "path/to/Water direction"
platformio run

# Upload firmware
platformio run --target upload

# Monitor serial
platformio device monitor --baud 115200

# Clean build
platformio run --target clean
```

### File Struktur

```
Water direction/
├── platformio.ini                    # PlatformIO config
├── include/
│   └── Sensor.h                     # Header dengan konfigurasi
├── src/
│   ├── main.cpp                     # Setup & loop utama
│   ├── angular.cpp                  # Logika pembacaan encoder
│   └── server.cpp                   # HTTP upload & WiFi
├── lib/
│   └── README
├── test/
│   └── README
└── ENCODER_DOCUMENTATION.md         # File ini
```

### Quick Config Sheet

| Fitur | File | Default | Range |
|-------|------|---------|-------|
| Invert Dir | include/Sensor.h | false | true/false |
| Median | include/Sensor.h | 1 | 1,3,5,7 |
| Offset | include/Sensor.h | 0.0 | 0.0-359.9 |
| Gray Decode | include/Sensor.h | true | true/false |
| Active Low | include/Sensor.h | true | true/false |
| Bit Order MSB First | include/Sensor.h | false | true/false |
| Sample Interval | src/main.cpp | 50ms | 10-500ms |
| Server Upload | src/main.cpp | false | true/false |

---

## Kesimpulan

Program E6CP-A Encoder memberikan solusi komprehensif untuk pembacaan posisi absolut dengan fitur-fitur fleksibel untuk berbagai aplikasi. Sesuaikan konfigurasi sesuai kebutuhan spesifik dan monitor serial output untuk validasi real-time.

**Support & Debug:**
- Serial monitor: 115200 baud
- Format: `[ANG]` untuk debug lokal, `[SRV]` untuk upload
- Dokumentasi konfigurasi: lihat `include/Sensor.h`

---

*Last Updated: March 31, 2026*
*Mikrokontroller Program - E6CP-A Angular Encoder System*
