# Dokumentasi Program Weather Monitoring

## 1. Tujuan Program
Program ini menjalankan monitoring 3 sumber data utama pada ESP32-S3:

1. Anemometer (arah angin, kecepatan, hujan, kelembaban, tekanan)
2. THM30MD via RS485 (suhu dan kelembaban, protokol Modbus RTU)
3. Lidar Lite V3 (jarak, tinggi air, tinggi gelombang)

Setiap data dibaca periodik, dicetak ke Serial untuk debugging, lalu dikirim ke server melalui HTTP (mode utama saat ini: HTTP POST JSON untuk batch Lidar).

## 2. File Inti yang Dipakai
Berdasarkan konfigurasi build, file yang dikompilasi adalah:

1. src/main.cpp
2. src/Anemometer.cpp
3. src/thm30md.cpp
4. src/lidar.cpp
5. src/server.cpp

File example tidak ikut build.

## 3. Konfigurasi Build
Di platformio.ini:

- Board: esp32s3usbotg
- Framework: arduino
- Source filter hanya memasukkan file inti program

## 4. Pin dan Interface
Definisi pin ada di include/Sensor.h:

1. Anemometer TX: GPIO 7
2. Anemometer RX: GPIO 6
3. Lidar SDA: GPIO 8
4. Lidar SCL: GPIO 9
5. THM30MD TX: GPIO 18
6. THM30MD RX: GPIO 17
7. LED indikator: GPIO 5 (`led_indicator`)

Catatan:
1. Penamaan `Anemometer_TX` dan `Anemometer_RX` mengikuti definisi pin ESP32 (TX/RX ESP32).
2. Implementasi membuka UART dengan urutan argumen `(RX, TX)` sesuai API `HardwareSerial.begin(...)`.

Catatan interface:

1. Anemometer: UART (HardwareSerial 1)
2. THM30MD: UART (HardwareSerial 2) + converter TTL to RS485 (Waveshare)
3. Lidar: I2C

## 5. Alur Program Keseluruhan
### 5.1 Setup
Urutan setup di main.cpp:

1. Serial.begin(115200)
2. pinMode(led_indicator, OUTPUT) + LED default LOW
3. Print banner: Weather Monitoring Simple Mode
4. anemometerInit(2400)
5. thm30mdInit(9600)
6. lidarInitSimple()
7. (opsional) serverInit() untuk koneksi WiFi awal (dikontrol oleh `kEnableServerUpload` di main.cpp)

### 5.2 Loop
Setiap loop:

1. anemometerRead(gAnemometer) (non-blocking)
2. thm30mdRead(gThm30md) (polling Modbus tiap 1 detik)
3. Lidar dibaca oleh task terpisah tiap 0.1 detik (10 Hz) dan dibuffer 10 sampel (distance saja)
4. (opsional) print debug sensor tiap 1 detik (dikontrol oleh `kEnableSerialDebug` di main.cpp)
5. (opsional) upload batch tiap 1 detik via HTTP POST JSON (dikontrol oleh `kEnableServerUpload` di main.cpp)

## 6. Detail Pembacaan Per Sensor
### 6.1 Anemometer (src/Anemometer.cpp)
Alur pengambilan data:

1. UART anemometer dibuka pada 2400 bps, format SERIAL_8N1 (HardwareSerial 1).
2. Data dibaca byte-per-byte secara non-blocking.
3. Sinkronisasi frame memakai karakter awal `'A'`.
4. Setelah sinkron, karakter dikumpulkan sampai terminator (`\r`, `\n`, atau `'*'`).
5. Parser membaca nilai numerik setelah *key* huruf di dalam frame.

Format frame (mengikuti sketch yang sudah tervalidasi di lapangan):
1. Awal frame: `'A'`
2. Di dalam frame terdapat pasangan key/value berupa angka ASCII setelah huruf key.
3. Akhir frame: `CR/LF` atau `'*'`

Pemetaan key yang dipakai firmware:
1. `B` -> arah angin (derajat)
2. `C` -> kecepatan angin rata-rata (skala /10)
3. `D` -> kecepatan angin maksimum (skala /10)
4. `E` -> curah hujan 1 jam (skala /10)
5. `F` -> curah hujan 24 jam (skala /10)
6. `M` -> kelembaban dari unit anemometer (skala /10, dibulatkan ke int)
7. `N` -> tekanan udara (skala /10)

Proses validasi:

1. Frame dianggap kandidat valid jika diawali `'A'` dan panjang minimal terpenuhi.
2. Firmware mewajibkan key `B,C,D,E,F` ada dan diikuti digit (opsional ada tanda `-` untuk nilai negatif).
3. Jika ada key wajib yang tidak ditemukan / tidak punya digit, parser mengembalikan gagal.
4. Key `M` (humidity) dan `N` (pressure) bersifat opsional.

Rumus konversi:

1. windDirectionDeg = B (dibulatkan)
2. windSpeedAvgMs = C / 10.0
3. windSpeedMaxMs = D / 10.0
4. rainfall1hMm = E / 10.0
5. rainfall24hMm = F / 10.0
6. humidityAnemometerPct = round((M/10.0)) jika key `M` ada
7. pressureHpa = N / 10.0 jika key `N` ada

Contoh kalkulasi:

1. Jika `C = 26`, maka windSpeedAvgMs = 26 / 10.0 = 2.6.
2. Jika `F = 13`, maka rainfall24hMm = 13 / 10.0 = 1.3.
3. Jika `N = 10096`, maka pressureHpa = 10096 / 10.0 = 1009.6.

Kondisi akhir pembacaan:

1. anemometerValid = true jika seluruh field berhasil diparse.
2. Nilai pada struct diperbarui hanya saat frame valid.

Variabel debug yang dicetak (`printAnemometerDebug`):
1. `baud`
2. `bytesRxLastSec`
3. `framesTotal`, `parseOk`, `parseFail`
4. `lastGoodFrame` dan `lastBadFrame` (untuk verifikasi format mentah)

### 6.2 THM30MD Modbus RTU (src/thm30md.cpp)
Alur pengambilan data:

1. UART THM30MD dibuka 9600 bps, SERIAL_8N1.
2. Polling dilakukan periodik setiap 1000 ms.
3. Program membuat request Modbus RTU FC04 (Read Input Register).
4. Sebelum kirim request, buffer RX UART dibersihkan agar frame lama tidak tercampur.
5. Program menunggu tepat 9 byte response dengan timeout total 250 ms.

Struktur request yang dikirim:

1. Byte 0: Slave ID = 0x01
2. Byte 1: Function Code = 0x04
3. Byte 2-3: Start Register = 0x0000
4. Byte 4-5: Register Count = 0x0002
5. Byte 6-7: CRC16 Modbus (low byte, high byte)

Request final:

01 04 00 00 00 02 71 CB

Struktur response yang diharapkan:

1. Byte 0: Slave ID (harus 0x01)
2. Byte 1: Function Code (harus 0x04)
3. Byte 2: Byte Count (harus 0x04)
4. Byte 3-4: Register 0x0000 (Temperature raw, signed 16-bit)
5. Byte 5-6: Register 0x0001 (Humidity raw, signed 16-bit)
6. Byte 7-8: CRC16 Modbus response

Proses validasi response:

1. Validasi panjang data (harus lengkap 9 byte dalam timeout).
2. Validasi header frame (slave id, function code, byte count).
3. Hitung ulang CRC16 dari byte 0..6 lalu bandingkan dengan byte CRC response.

Rumus konversi:

1. temperatureC = int16(rawTemp) / 10.0
2. humidityPct = int16(rawHum) / 10.0

Contoh kalkulasi:

1. Jika rawTemp = 296, maka temperatureC = 296 / 10.0 = 29.6 C.
2. Jika rawHum = 781, maka humidityPct = 781 / 10.0 = 78.1 %.
3. Jika rawTemp bernilai negatif, misal -52, maka temperatureC = -5.2 C.

Kode error THM30MD:

1. 0: tidak ada error
2. 1: timeout (response tidak lengkap)
3. 2: format frame salah (slave/fc/byte count tidak cocok)
4. 3: CRC response salah

Perilaku saat error:

1. reading.valid diset false.
2. lastErrorCode diisi sesuai jenis error.
3. Nilai suhu/kelembaban terbaru yang valid tidak dipakai untuk update baru pada siklus gagal.

Variabel debug:

1. rawTemperatureDeci
2. rawHumidityDeci
3. lastErrorCode

### 6.3 Lidar (src/lidar.cpp)
Alur pengambilan data:

1. I2C diinisialisasi pada pin SDA/SCL sesuai Sensor.h.
2. Untuk tiap sampel, sensor dipicu dengan menulis 0x04 ke register 0x00 (alamat 0x62).
3. Setelah jeda 10 ms, program membaca 2 byte jarak dari register 0x8F.
4. Nilai jarak mentah dibentuk dari high byte dan low byte.
5. Offset kalibrasi dikurangi sebesar 5 cm.
6. Nilai negatif setelah offset dipotong menjadi 0.

Metode filtering yang dipakai:

1. Jumlah sampel default = 5.
2. Hanya sampel > 0 yang dianggap valid.
3. Jika validSamples > 0, distance = rata-rata aritmatika sampel valid.
4. Jika validSamples = 0, pembacaan dianggap gagal.

Rumus kalkulasi level air:

1. distanceCm = avg(valid raw distance)
2. waterHeightCm = sensorToBottomCm - distanceCm
3. waveHeightCm = sensorToCalmWaterCm - distanceCm

Dengan konstanta:

1. sensorToBottomCm = 1100
2. sensorToCalmWaterCm = 400
3. offset = 5

Contoh kalkulasi:

1. Sampel valid setelah offset: 397, 399, 398, 398, 400
2. distanceCm = (397 + 399 + 398 + 398 + 400) / 5 = 398
3. waterHeightCm = 1100 - 398 = 702
4. waveHeightCm = 400 - 398 = 2

Perilaku batas:

1. Jika waterHeightCm hasilnya negatif, dipaksa menjadi 0.
2. Jika semua sampel gagal, distance/waterHeight/waveHeight diset -1 dan valid=false.

## 7. Alur Pengiriman ke Server
Lokasi: src/server.cpp

Catatan penting:
1. Ada 2 cara upload di kode:
	- `serverHandleUpload(...)`: HTTP GET (mode lama / legacy).
	- `serverUploadLidarDistanceBatch(...)`: HTTP POST JSON batch (mode yang dipakai `main.cpp` saat ini).
2. Pada Simple Mode terbaru, upload dipicu oleh task `uploadTask` ketika 1 batch lidar (10 sampel) sudah lengkap.

### 7.1 WiFi
1. serverInit() mencoba koneksi saat setup
2. Saat upload, ensureWiFiConnection() memastikan WiFi tetap terhubung
3. Jika putus, akan reconnect dengan retry

### 7.2 Interval Upload
- Upload tiap 1000 ms

### 7.3 Endpoint dan Auth
1. Server: 31.97.66.191
2. Path: /Wemon_BauBau/wemonbaubau.php
3. HTTP Basic Auth: <user> / <pass>

Catatan backend:
1. Endpoint PHP menyimpan data ke MySQL.
2. Database: Wemon_BauBau
3. Tabel: weather_monitoring
4. Lokasi file di server (Apache DocumentRoot): /var/www/html/Wemon_BauBau/wemonbaubau.php
5. Skema MySQL dapat di-import via phpMyAdmin menggunakan file wemonbaubau_schema.sql

### 7.4 Parameter yang Dikirim
Payload yang dikirim menggunakan HTTP POST JSON:

1. type = weather_lidar_distance_batch
2. sample_interval_ms = 100
3. windir
4. windavg
5. windmax
6. rain1h
7. rain24h
8. suhu
9. humidity
10. pressure
11. lidar_distance_cm = array 10 nilai (distance_01..distance_10 untuk 0.1..1.0 detik)

Catatan:
1. Firmware tidak mengirim waterheight/waveheight.
2. Kolom lama distance/waterheight/waveheight tetap terisi di database dari sampel terakhir (distance_10) dan hasil kalkulasi server.

Fallback saat data invalid:

1. Anemometer invalid -> wind/rain = 0, pressure = 1013.25
2. THM30MD invalid -> suhu = 99, humidity = 99
3. Lidar invalid -> distance/waterheight/waveheight = -1

Catatan Lidar:
1. Untuk mode batch, jika sampel Lidar invalid maka elemen array `lidar_distance_cm` akan bernilai -1.
2. Server menghitung waterheight/waveheight dari distance_10; jika distance_10 < 0 maka waterheight/waveheight disimpan -1.

Catatan pressure:
1. Jika nilai pressure dari anemometer di luar rentang wajar (mis. <800 atau >1100 hPa), firmware akan paksa fallback ke 1013.25.

## 8. Contoh Output Serial per Kondisi
Berikut contoh output yang merepresentasikan kondisi-kondisi utama saat runtime.

### 8.1 Startup normal + WiFi terkoneksi
```text
=== Weather Monitoring Simple Mode ===
Connecting to WiFi...
........
Connected to WiFi
IP Address: 192.168.1.23
```

### 8.2 Anemometer belum menerima frame valid
```text
[ANEMO] ----------
baud: 2400
frameValid: no
bytesRxLastSec: 0
framesTotal: 0
parseOk: 0 | parseFail: 0
lastByte: 0x--
lastByteAgeMs: -1
```

### 8.3 Anemometer valid
```text
[ANEMO] ----------
baud: 2400
frameValid: yes
bytesRxLastSec: 62
framesTotal: 1
parseOk: 1 | parseFail: 0
lastGoodFrame: A...B0274C0026D0044E0000F0013M0790N10096
lastByte: 0x0A
lastByteAgeMs: 15
windDir(deg): 274
windAvg(m/s): 2.60
windMax(m/s): 4.40
rain1h(mm): 0.00
rain24h(mm): 1.30
humidityAnemometer(%): 79
pressure(hPa): 1009.6
```

### 8.4 THM30MD valid (Modbus sukses)
```text
[THM30MD] ----------
valid: yes
slave: 1
fc: 4
lastError: 0
rawTemp(deci): 296
rawHum(deci): 781
temperature(C): 29.60
humidity(%): 78.10
```

### 8.5 THM30MD timeout (misal kabel A/B terbalik atau sensor tidak merespons)
```text
[THM30MD] ----------
valid: no
slave: 1
fc: 4
lastError: 1
rawTemp(deci): 0
rawHum(deci): 0
```

### 8.6 THM30MD frame tidak sesuai
```text
[THM30MD] ----------
valid: no
slave: 1
fc: 4
lastError: 2
rawTemp(deci): 0
rawHum(deci): 0
```

### 8.7 THM30MD CRC error
```text
[THM30MD] ----------
valid: no
slave: 1
fc: 4
lastError: 3
rawTemp(deci): 0
rawHum(deci): 0
```

### 8.8 Lidar valid
```text
[LIDAR] ----------
valid: yes
validSamples: 5
distance(cm): 398
waterHeight(cm): 702
waveHeight(cm): 2
```

### 8.9 Lidar gagal baca
```text
[LIDAR] ----------
valid: no
validSamples: 0
```

### 8.10 Upload sukses
```text
[SRV] POST URL:
http://31.97.66.191/Wemon_BauBau/wemonbaubau.php
[SRV] POST JSON:
{"type":"weather_lidar_distance_batch","sample_interval_ms":100,"windir":274,"windavg":2.60,"windmax":4.40,"rain1h":0.00,"rain24h":1.30,"suhu":29.60,"humidity":78.10,"pressure":1009.60,"lidar_distance_cm":[398,398,399,398,398,398,398,398,398,398]}
[SRV] HTTP response: 200
[SRV] Server response:
OK
```

### 8.11 WiFi terputus saat upload
```text
[SRV] WiFi disconnected. Reconnecting...
.....
[SRV] WiFi reconnection failed
[SRV] Upload skipped: WiFi unavailable
```

### 8.12 HTTP gagal berulang
```text

[SRV] HTTP response: -1
[SRV] HTTP error: <lihat http.errorToString(...)>
[SRV] Upload failed. Consecutive failures: 1

[SRV] HTTP response: -1
[SRV] HTTP error: <lihat http.errorToString(...)>
[SRV] Upload failed. Consecutive failures: 2

[SRV] HTTP response: -1
[SRV] HTTP error: <lihat http.errorToString(...)>
[SRV] Upload failed. Consecutive failures: 3
[SRV] Too many failures, restarting WiFi
```

Catatan LED indikator:
1. LED indikator dipulse HIGH saat request HTTP dimulai, lalu kembali LOW setelah request selesai.
2. Jika WiFi tidak tersambung atau batch kosong, LED dipaksa LOW.

## 9. Checklist Uji Lapangan
### 9.1 THM30MD RS485
1. Pastikan A ke A, B ke B
2. GND referensi sama antara ESP32, converter, sensor
3. Pastikan slave ID sensor = 1
4. Pastikan baudrate sensor = 9600
5. Cek lastError pada debug THM30MD

### 9.2 Anemometer
1. Cek frame valid menjadi yes
2. Bandingkan arah/kecepatan dengan alat referensi

### 9.3 Lidar
1. Cek validSamples > 0
2. Verifikasi offset dan tinggi referensi (1100 dan 400) sesuai instalasi fisik

### 9.4 Server
1. Cek HTTP response > 0
2. Cek response body dari server
3. Verifikasi data masuk database sesuai parameter query

## 10. Catatan Pengembangan Lanjutan
1. Tambahkan mode baca register float 32-bit (0x02/0x04) untuk THM30MD bila diperlukan.
2. Pindahkan kredensial WiFi dan server ke file konfigurasi terpisah.
3. Tambahkan retry khusus Modbus sebelum menandai data THM invalid.
4. Tambahkan stempel waktu RTC/NTP dalam payload ke server.
