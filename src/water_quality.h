#pragma once

#include <Arduino.h>

#include "config.h"

struct WaterSample {
  float turbidity_ntu;   // NAN bila di luar jangkauan sensor
  float ph;
  float tds_ppm;
  float temperature_c;
  float raw_level_cm;
  bool  turbidity_valid;
  bool  ph_valid;
};

// Hasil penilaian satu muatan air setelah disaring.
enum class Verdict : uint8_t {
  MEETS_THRESHOLDS,   // lulus kekeruhan dan pH — BUKAN berarti aman diminum
  TOO_TURBID,
  PH_OUT_OF_RANGE,
  SENSOR_UNRELIABLE,
};

const char* verdictName(Verdict v);

class WaterQuality {
 public:
  void begin();

  WaterSample sample();

  // Penilaian tidak menerima pembacaan yang tidak sahih sebagai lulus.
  // Tidak tahu bukan berarti bersih.
  static Verdict judge(const WaterSample& s);

  // Kalibrasi pH dua titik, dijalankan dari perintah serial.
  void calibratePh(float measured_v_at_7);

 private:
  float readTurbidityNtu(float temperature_c);
  float readPh();
  float readTds(float temperature_c);
  float readLevelCm();

  float ph_v_at_7_ = PH_V_AT_7;
};
