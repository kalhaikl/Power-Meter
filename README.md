<div align="center">

# ⚡ HaikalPower

**Monitor daya listrik AC berbasis ESP32 dengan dashboard web, MQTT, dan update firmware lewat WiFi (OTA).**

![Platform](https://img.shields.io/badge/platform-ESP32-blue)
![Framework](https://img.shields.io/badge/framework-Arduino-00979D)
![MQTT](https://img.shields.io/badge/protocol-MQTT-660066)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

[Fitur](#-fitur) · [Perangkat Keras](#-perangkat-keras) · [Instalasi](#-instalasi) · [Konfigurasi](#-konfigurasi-pertama-kali) · [MQTT](#-mqtt) · [Kalibrasi](#-kalibrasi)

</div>

---

## 📖 Tentang

**HaikalPower** mengukur **tegangan**, **arus**, dan **daya** listrik AC secara real-time, lalu menampilkannya di dashboard web yang bisa dibuka dari HP atau laptop dan mengirimkannya ke broker MQTT (misalnya untuk Home Assistant, Node-RED, atau Grafana).

Tegangan dibaca dengan sensor **ZMPT101B**, arus dengan sensor **SCT013** melalui ADC eksternal **ADS1115** (16-bit) agar pembacaan lebih akurat daripada ADC bawaan ESP32. Pengaturan WiFi dan MQTT dilakukan lewat halaman web, jadi tidak perlu mengubah kode dan upload ulang setiap kali pindah jaringan.

## ✨ Fitur

- 📊 **Dashboard web** responsif (mobile-first) dengan grafik sparkline daya 1 menit terakhir, tampilan tegangan, arus, daya, dan status MQTT
- 🌐 **Berjalan offline** — tanpa CDN atau font eksternal, semua CSS, JS, dan ikon tertanam di firmware
- 📡 **MQTT** dengan *Last Will and Testament* (status Online/Offline), data *retained*, dan dukungan username/password
- 🛠️ **Pengaturan lewat web** — WiFi dan MQTT disimpan di memori flash (NVS) dan tetap ada setelah restart
- 📶 **Mode Access Point otomatis** jika WiFi belum diatur atau gagal terhubung
- 🔄 **OTA update** lewat halaman `/update` (ElegantOTA)
- 🔁 **Auto reconnect** untuk WiFi dan MQTT
- 🎯 **Pembacaan stabil** — tegangan memakai median dari beberapa jendela sampling untuk membuang lonjakan noise

## 🔧 Perangkat Keras

| Komponen | Fungsi |
| --- | --- |
| ESP32 (DevKit) | Mikrokontroler utama |
| ADS1115 | ADC 16-bit (I²C) untuk sensor arus |
| SCT013 (tipe keluaran tegangan) | Sensor arus *non-invasive* |
| ZMPT101B | Modul sensor tegangan AC |

> Nilai kalibrasi bawaan (`30.12`) cocok untuk SCT013 dengan keluaran tegangan sekitar 1 V pada 30 A. Jika sensor Anda berbeda, sesuaikan nilainya (lihat [Kalibrasi](#-kalibrasi)).

### Wiring

| Dari | Ke ESP32 / Komponen |
| --- | --- |
| ADS1115 `SDA` | GPIO 21 |
| ADS1115 `SCL` | GPIO 22 |
| ADS1115 `VDD` / `GND` | 3V3 / GND |
| ADS1115 `ADDR` | GND (alamat `0x48`) |
| SCT013 | ADS1115 `A0` – `A1` (pembacaan diferensial) |
| ZMPT101B `OUT` | GPIO 33 |
| ZMPT101B `VCC` / `GND` | Sesuai spesifikasi modul / GND |

Ubah pin pada bagian atas kode jika perlu:

```cpp
#define SDA_PIN 21
#define SCL_PIN 22
#define ZMPT_PIN 33
```

### ⚠️ Peringatan Keselamatan

Proyek ini berhubungan dengan **tegangan listrik AC yang berbahaya**. Pastikan instalasi dilakukan dalam keadaan listrik mati, gunakan casing yang terisolasi, dan jangan sentuh bagian bertegangan. Jika ragu, mintalah bantuan orang yang berpengalaman. Anda bertanggung jawab penuh atas keselamatan saat membangun dan menggunakan perangkat ini.

## 📦 Instalasi

### Prasyarat

- [Arduino IDE](https://www.arduino.cc/en/software) atau PlatformIO
- Dukungan board ESP32 (Arduino-ESP32 core)

### Library yang dibutuhkan

| Library | Keterangan |
| --- | --- |
| `Adafruit ADS1X15` | Driver ADS1115 |
| `PubSubClient` | Klien MQTT |
| `ElegantOTA` | OTA update lewat web |
| `WiFi`, `WebServer`, `Preferences`, `Wire` | Sudah termasuk di core ESP32 |

Contoh `platformio.ini`:

```ini
[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino
monitor_speed = 115200
lib_deps =
    adafruit/Adafruit ADS1X15
    knolleary/PubSubClient
    ayushsharma82/ElegantOTA
```

### Upload

```bash
# Clone repositori
git clone https://github.com/USERNAME/REPO.git
cd REPO

# PlatformIO
pio run -t upload
pio device monitor
```

Atau buka file `.ino` di Arduino IDE, pilih board **ESP32 Dev Module**, lalu klik **Upload**. Upload pertama harus lewat kabel USB; update berikutnya bisa lewat OTA.

## ⚙️ Konfigurasi Pertama Kali

1. Nyalakan perangkat. Karena WiFi belum diatur, ESP32 membuat Access Point:
   - **SSID:** `HaikalPower-Setup`
   - **Password:** `12345678`
2. Sambungkan HP/laptop ke AP tersebut, lalu buka alamat IP yang tampil di Serial Monitor (biasanya `http://192.168.4.1`).
3. Buka tab **WiFi**, isi SSID dan password jaringan rumah, lalu **Save and restart**.
4. Setelah terhubung, cek alamat IP baru di Serial Monitor, router, atau topik MQTT `IP`, lalu buka dashboard.
5. Buka tab **MQTT** dan isi host broker, port, serta login (jika ada).

### Halaman web

| Alamat | Fungsi |
| --- | --- |
| `/` | Dashboard (tegangan, arus, daya, status MQTT) |
| `/data` | Data sensor dalam format JSON |
| `/wifi` | Pengaturan WiFi |
| `/mqtt` | Pengaturan MQTT |
| `/update` | OTA update firmware (ElegantOTA) |

Contoh respons `/data`:

```json
{ "voltage": 221.35, "current": 0.842, "power": 186.40, "mqtt": true }
```

## 📡 MQTT

Topik utama (*root*) bisa diubah lewat web, nilai bawaannya `HaikalPower`.

| Topik | Isi | Retained |
| --- | --- | --- |
| `HaikalPower/Data` | JSON `{"voltage":..,"current":..,"power":..}` | ✅ |
| `HaikalPower/IP` | Alamat IP perangkat | ✅ |
| `HaikalPower/LWT` | `Online` / `Offline` | ✅ |

- Data dipublikasikan setiap **5 detik**
- Sensor dibaca setiap **3 detik**
- Client ID: `HaikalPower-<IP perangkat>`
- Saat perangkat terputus tak wajar, broker otomatis mengirim `Offline` ke topik LWT

Coba dengan Mosquitto:

```bash
mosquitto_sub -h 192.168.1.10 -t "HaikalPower/#" -v
```

### Contoh Home Assistant

```yaml
mqtt:
  sensor:
    - name: "Tegangan"
      state_topic: "HaikalPower/Data"
      value_template: "{{ value_json.voltage }}"
      unit_of_measurement: "V"
      device_class: voltage
    - name: "Arus"
      state_topic: "HaikalPower/Data"
      value_template: "{{ value_json.current }}"
      unit_of_measurement: "A"
      device_class: current
    - name: "Daya"
      state_topic: "HaikalPower/Data"
      value_template: "{{ value_json.power }}"
      unit_of_measurement: "W"
      device_class: power
      availability_topic: "HaikalPower/LWT"
      payload_available: "Online"
      payload_not_available: "Offline"
```

## 🎯 Kalibrasi

Nilai kalibrasi ada di bagian atas kode:

```cpp
#define SCT_CALIBRATION 30.12      // faktor arus (A per Volt RMS)
float zmptCalibration = 674.0f;    // faktor tegangan (V per Volt RMS di pin)
#define ZMPT_NOISE_THRESHOLD 20    // di bawah ini dianggap 0 V
```

**Tegangan (ZMPT101B)**

1. Ukur tegangan sebenarnya dengan multimeter yang andal.
2. Lihat nilai `Voltage` di Serial Monitor.
3. Hitung ulang: `zmptCalibration_baru = zmptCalibration_lama × (tegangan_multimeter / tegangan_terbaca)`
4. Putar trimmer pada modul ZMPT101B jika sinyal terlalu kecil atau terpotong, lalu upload ulang.

**Arus (SCT013)**

1. Pasang beban yang arusnya diketahui (atau ukur dengan clamp meter).
2. Hitung ulang: `SCT_CALIBRATION_baru = SCT_CALIBRATION_lama × (arus_aktual / arus_terbaca)`

Arus di bawah **0,02 A** otomatis dibaca sebagai 0 untuk menghindari noise saat tanpa beban.

## 🔬 Cara Kerja Singkat

```
ZMPT101B ──► ADC ESP32 (GPIO33) ──► RMS dari 1000 sampel × 5 jendela ──► median ──► Tegangan (V)
SCT013   ──► ADS1115 (diferensial) ──► RMS dari 870 sampel ───────────────────────► Arus (A)

Daya (W) = Tegangan × Arus
```

> 📝 **Catatan:** Daya dihitung sebagai `V × I` tanpa memperhitungkan faktor daya, sehingga nilainya sebenarnya adalah **daya semu (VA)**. Untuk beban resistif (lampu pijar, pemanas) nilainya mendekati daya nyata, tetapi untuk beban induktif atau kapasitif (motor, adaptor, LED driver) akan lebih tinggi dari daya nyata.

## 🔐 Catatan Keamanan

- Dashboard web **tidak memiliki autentikasi**; gunakan hanya di jaringan lokal yang tepercaya
- Password WiFi dan MQTT disimpan sebagai teks biasa di memori flash ESP32
- Password AP bawaan (`12345678`) sebaiknya diganti sebelum dipakai permanen:

```cpp
WiFi.softAP("HaikalPower-Setup", "12345678");
```

- Halaman `/update` dapat dipakai siapa saja yang bisa mengakses perangkat; pertimbangkan menambahkan username/password OTA lewat `ElegantOTA.begin(&server, "user", "pass")`

## 🗺️ Ide Pengembangan

- [ ] Perhitungan faktor daya dan daya nyata (W)
- [ ] Penghitung energi (kWh) dan penyimpanan riwayat
- [ ] Autentikasi untuk dashboard dan OTA
- [ ] Mode captive portal untuk setup WiFi
- [ ] MQTT Discovery untuk Home Assistant

## 🤝 Kontribusi

Kontribusi, saran, dan laporan bug sangat diterima. Silakan buat *issue* atau kirim *pull request*.

1. Fork repositori ini
2. Buat branch fitur (`git checkout -b fitur/nama-fitur`)
3. Commit perubahan (`git commit -m 'Tambah fitur'`)
4. Push ke branch (`git push origin fitur/nama-fitur`)
5. Buka Pull Request

## 📄 Lisensi

Didistribusikan di bawah lisensi MIT. Lihat [LICENSE](LICENSE) untuk detail.

---

<div align="center">

Dibuat dengan ❤️ oleh **Haikal**

Jika proyek ini bermanfaat, jangan lupa beri ⭐

</div>
