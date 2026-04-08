# Validasi Lapangan Sensor dan Telemetri

## 1. Tujuan
Dokumen ini dipakai untuk uji lapangan berbasis data mentah agar pembacaan sensor dan pengiriman ke server dapat diverifikasi secara terukur.

## 2. Ruang Lingkup Uji
1. Anemometer UART frame `A..N` (key/value) pada 2400 bps
2. THM30MD Modbus RTU melalui TTL to RS485 converter
3. Lidar Lite V3 via I2C
4. Upload HTTP ke server

## 3. Prasyarat Pengujian
1. Firmware terbaru sudah ter-upload ke ESP32-S3.
2. Serial Monitor aktif pada 115200 bps.
3. Konfigurasi pin sesuai Sensor.h.
4. THM30MD memakai default Modbus:
   - Slave ID: 1
   - Baudrate: 9600
   - Data bits: 8
   - Stop bit: 1
   - Parity: none
5. Wiring RS485 benar:
   - A sensor ke A converter
   - B sensor ke B converter
   - GND referensi bersama

## 4. Ringkasan Rumus Acuan
## 4.1 Anemometer
Format frame acuan (sesuai firmware):
1. Start: `'A'`
2. End: `CR/LF` atau `'*'`
3. Field berupa angka ASCII setelah key huruf.

Mapping & scaling acuan:
1. `B` -> windDirectionDeg (derajat)
2. `C` -> windSpeedAvgMs = C / 10.0
3. `D` -> windSpeedMaxMs = D / 10.0
4. `E` -> rainfall1hMm = E / 10.0
5. `F` -> rainfall24hMm = F / 10.0
6. `M` -> humidityAnemometerPct ≈ round((M/10.0)) (opsional)
7. `N` -> pressureHpa = N / 10.0 (opsional)

## 4.2 THM30MD
1. temperatureC = int16(rawTemp) / 10.0
2. humidityPct = int16(rawHum) / 10.0

## 4.3 Lidar
1. distanceCm = rata-rata sampel valid setelah offset
2. waterHeightCm = 1100 - distanceCm
3. waveHeightCm = 400 - distanceCm

## 5. Test Case Anemometer
| ID | Input Mentah | Langkah Uji | Expected Hasil |
|---|---|---|---|
| ANM-01 | Frame valid diawali karakter `A` dan berisi key wajib `B,C,D,E,F` | Hubungkan anemometer, putar baling-baling, cek serial | frameValid: yes, parseOk bertambah, `lastGoodFrame` terisi |
| ANM-02 | Frame tidak punya salah satu key wajib (mis. tidak ada `F`) | Injeksi frame uji tanpa `F` | parseFail bertambah, nilai tidak update |
| ANM-03 | `C = 0026` | Amati windAvg pada debug | windAvg = 2.6 m/s |
| ANM-04 | `D = 0044` | Amati windMax pada debug | windMax = 4.4 m/s |
| ANM-05 | `F = 0013` | Amati rain24h pada debug | rain24h = 1.3 mm |
| ANM-06 | `N = 10096` | Amati pressure pada debug | pressure = 1009.6 hPa |

Contoh verifikasi hitung:
1. `C=26` -> 26 / 10.0 = 2.6
2. `F=13` -> 13 / 10.0 = 1.3
3. `N=10096` -> 10096 / 10.0 = 1009.6

## 6. Test Case THM30MD Modbus RTU
| ID | Input Mentah | Langkah Uji | Expected Hasil |
|---|---|---|---|
| THM-01 | Request 01 04 00 00 00 02 71 CB | Capture UART dengan logic analyzer | Request sesuai frame Modbus FC04 |
| THM-02 | Response valid 01 04 04 01 28 03 0D CRC CRC | Jalankan sistem normal | valid: yes, lastError: 0 |
| THM-03 | Response timeout | Lepas sensor atau putus A/B | valid: no, lastError: 1 |
| THM-04 | Header tidak sesuai | Ubah slave id sensor bukan 1 | valid: no, lastError: 2 |
| THM-05 | CRC salah | Injeksi frame CRC error | valid: no, lastError: 3 |
| THM-06 | rawTemp negatif, misal FF CC | Simulasikan suhu negatif | temperature mengikuti signed int16, contoh -5.2 C |

Contoh verifikasi hitung:
1. rawTemp 0x0128 = 296 -> 29.6 C
2. rawHum 0x030D = 781 -> 78.1 %
3. rawTemp 0xFFCC = -52 -> -5.2 C

Checklist khusus RS485:
1. Jika lastError konsisten 1, cek wiring A/B, power sensor, dan baudrate.
2. Jika lastError konsisten 2, cek slave ID dan function code response.
3. Jika lastError konsisten 3, cek kualitas kabel, noise, terminasi, dan ground reference.

## 7. Test Case Lidar
| ID | Input Mentah | Langkah Uji | Expected Hasil |
|---|---|---|---|
| LDR-01 | 5 sampel valid, contoh 397 399 398 398 400 | Jalankan pada kondisi stabil | valid: yes, validSamples: 5 |
| LDR-02 | Semua sampel gagal | Putus koneksi I2C sementara | valid: no, validSamples: 0, nilai -1 |
| LDR-03 | Distance 398 | Kondisi stabil | waterHeight 702, waveHeight 2 |
| LDR-04 | Distance lebih besar dari 1100 | Simulasi data jauh | waterHeight tidak boleh negatif, dipaksa 0 |

Contoh verifikasi hitung:
1. distance = (397+399+398+398+400)/5 = 398
2. waterHeight = 1100 - 398 = 702
3. waveHeight = 400 - 398 = 2

## 8. Test Case Pengiriman Server
| ID | Kondisi Input | Expected URL / Status |
|---|---|---|
| SRV-01 | Semua sensor valid | HTTP response > 0, server response berisi sukses |
| SRV-02 | Anemometer invalid | wind dan rain fallback ke 0, pressure fallback 1013.25 |
| SRV-03 | THM invalid | suhu 99 dan humidity 99 |
| SRV-04 | Lidar invalid | distance, waterheight, waveheight bernilai -1 |
| SRV-05 | WiFi putus | Reconnect dicoba, upload di-skip jika gagal |
| SRV-06 | HTTP gagal 3x | Counter naik sampai 3 lalu restart koneksi WiFi |

Parameter query yang harus ada:
1. windir
2. windavg
3. windmax
4. rain1h
5. rain24h
6. suhu
7. humidity
8. pressure
9. distance
10. waterheight
11. waveheight

## 9. Template Log Uji Lapangan
Gunakan format berikut saat pengambilan data lapangan:

- Tanggal:
- Lokasi:
- Operator:
- Firmware:
- Kondisi cuaca:
- Hasil uji ANM-01 s.d. ANM-06: Pass/Fail + catatan
- Hasil uji THM-01 s.d. THM-06: Pass/Fail + catatan
- Hasil uji LDR-01 s.d. LDR-04: Pass/Fail + catatan
- Hasil uji SRV-01 s.d. SRV-06: Pass/Fail + catatan
- Kesimpulan akhir:

## 10. Kriteria Lolos Sistem
Sistem dinyatakan layak operasi jika:

1. Semua test case kritikal lulus:
   - THM-02, THM-03
   - LDR-01
   - SRV-01
2. Tidak ada error kontinu lebih dari 5 menit pada kondisi normal.
3. Data masuk server dengan parameter lengkap dan format numerik benar.
