#pragma once

#include <Arduino.h>

// ============================================================================
//  RainPurify — kendali penyaringan air hujan bertingkat
//  Hult Prize 2026 Indonesia (National Finalist) · GEMASTIK Smart City ·
//  UNITY UNY · Roots SDGs 2025 (Juara 1)
//
//  Alur: hujan -> pra-saring -> sedimen -> karbon aktif -> membran halus
//        -> sensor -> keputusan -> tandon bersih atau resirkulasi
//
//  ---------------------------------------------------------------------
//  BATAS YANG TIDAK BOLEH DIHILANGKAN DARI KODE MANA PUN DI REPOSITORI INI
//
//  Penyaringan bukan disinfeksi. Sistem ini menurunkan kekeruhan dan
//  menstabilkan pH; ia TIDAK membunuh bakteri, virus, atau protozoa, dan
//  tidak ada sensor di sini yang dapat mendeteksinya. Air keluarannya
//  ditujukan untuk penggunaan non-konsumsi: mencuci, menyiram, air baku
//  yang masih akan dimasak.
//
//  Karena itu keluaran alat ini tidak pernah ditandai "aman diminum",
//  melainkan "memenuhi ambang kekeruhan dan pH". Perbedaan itu bukan
//  soal kehati-hatian bahasa — itu perbedaan antara alat yang membantu
//  dan alat yang membuat orang sakit.
//  ---------------------------------------------------------------------
// ============================================================================

// ---------------------------------------------------------------------------
//  Pin (ESP32 DevKit V1)
// ---------------------------------------------------------------------------
constexpr uint8_t PIN_TURBIDITY   = 34;  // ADC1, sensor keruh analog
constexpr uint8_t PIN_PH          = 35;  // ADC1, modul pH
constexpr uint8_t PIN_TDS         = 32;  // ADC1
constexpr uint8_t PIN_TEMP_1WIRE  = 15;  // DS18B20, kompensasi suhu
constexpr uint8_t PIN_LEVEL_TRIG  = 5;   // ultrasonik tandon mentah
constexpr uint8_t PIN_LEVEL_ECHO  = 18;
constexpr uint8_t PIN_PUMP        = 25;
constexpr uint8_t PIN_VALVE_CLEAN = 26;  // katup ke tandon bersih
constexpr uint8_t PIN_VALVE_RECIRC= 27;  // katup balik ke tandon mentah
constexpr uint8_t PIN_VALVE_WASTE = 14;  // pembuangan
constexpr uint8_t PIN_LED_OK      = 2;
constexpr uint8_t PIN_LED_FAULT   = 4;

// ---------------------------------------------------------------------------
//  Ambang mutu air
//
//  Angka acuan berasal dari kondisi lapangan purwarupa: air atap mentah
//  terukur 11,09 NTU dengan pH 9,67, dan keluaran mencapai 1,44 NTU.
//  Ambang di bawah ini ditetapkan dari situ, bukan dari baku mutu air minum.
// ---------------------------------------------------------------------------
constexpr float TURBIDITY_PASS_NTU   = 5.0f;   // batas lulus keluaran
constexpr float TURBIDITY_GOOD_NTU   = 2.0f;   // setara capaian lapangan
constexpr float TURBIDITY_SENSOR_MAX = 300.0f; // di atas ini sensor tak berarti

// pH tidak diperbaiki oleh penyaringan. Karbon aktif hampir tidak
// menggesernya, dan pH tinggi pada air atap biasanya berasal dari semen atau
// seng talang. Maka pH di luar rentang MEMBLOKIR pengaliran ke tandon bersih,
// bukan sekadar dicatat — tidak ada gunanya menyimpan air yang tetap tidak
// layak setelah disaring.
constexpr float PH_MIN_ACCEPT = 6.5f;
constexpr float PH_MAX_ACCEPT = 8.5f;

constexpr float TDS_WARN_PPM = 500.0f;

// ---------------------------------------------------------------------------
//  Resirkulasi
//
//  Air yang belum jernih dikembalikan untuk disaring ulang. Tetapi ada batas:
//  air yang tidak jernih setelah beberapa lintasan tidak akan jernih pada
//  lintasan kesepuluh — yang ada hanya pompa aus dan listrik terbuang. Setelah
//  batas tercapai, muatan itu ditandai tidak dapat diolah dan dibuang.
// ---------------------------------------------------------------------------
constexpr uint8_t  MAX_RECIRCULATION_PASSES = 4;
constexpr uint32_t PASS_DURATION_MS         = 180000;  // 3 menit per lintasan
constexpr uint32_t SETTLE_BEFORE_TEST_MS    = 20000;   // biarkan aliran tenang

// Perbaikan minimal yang harus terlihat antar lintasan. Kalau kekeruhan
// tidak turun sedikit pun, media saring jenuh atau tersumbat — dan lintasan
// berikutnya hanya akan mengulang hasil yang sama.
constexpr float MIN_IMPROVEMENT_NTU = 0.5f;

// ---------------------------------------------------------------------------
//  Tandon
// ---------------------------------------------------------------------------
constexpr float TANK_HEIGHT_CM      = 100.0f;
constexpr float RAW_MIN_LEVEL_CM    = 15.0f;  // di bawah ini pompa kering
constexpr float SENSOR_DEAD_ZONE_CM = 4.0f;   // ultrasonik buta di dekat muka

// ---------------------------------------------------------------------------
//  Kalibrasi sensor keruh
//
//  Sensor keruh analog murah bersifat non-linear dan **peka suhu**. Kurva di
//  bawah adalah pendekatan kuadratik terhadap larutan formazin standar; wajib
//  dikalibrasi ulang per unit. Tanpa kalibrasi, keluarannya adalah tegangan,
//  bukan NTU, dan menyebutnya NTU adalah kekeliruan yang paling sering
//  ditemukan pada proyek sejenis.
// ---------------------------------------------------------------------------
constexpr float TURB_C2 = -1120.4f;
constexpr float TURB_C1 = 5742.3f;
constexpr float TURB_C0 = -4352.9f;
constexpr float TURB_TEMP_COEF = 0.02f;  // per degC dari 25 degC acuan
constexpr float TURB_TEMP_REF  = 25.0f;

// pH: dua titik kalibrasi buffer 4,00 dan 7,00
constexpr float PH_V_AT_7 = 2.50f;
constexpr float PH_SLOPE_V_PER_PH = -0.1667f;

// ---------------------------------------------------------------------------
//  Waktu
// ---------------------------------------------------------------------------
constexpr uint32_t SAMPLE_PERIOD_MS = 2000;
constexpr uint32_t LOG_PERIOD_MS    = 60000;
constexpr char     LOG_PATH[]       = "/rainpurify.csv";
