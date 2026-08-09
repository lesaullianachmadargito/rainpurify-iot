#include "water_quality.h"

#include <DallasTemperature.h>
#include <OneWire.h>
#include <math.h>

namespace {
OneWire onewire(PIN_TEMP_1WIRE);
DallasTemperature thermometer(&onewire);

float readAveragedVolts(uint8_t pin, uint8_t samples = 32) {
  uint32_t acc = 0;
  for (uint8_t i = 0; i < samples; ++i) {
    acc += analogRead(pin);
    delayMicroseconds(300);
  }
  return (acc / (float)samples) / 4095.0f * 3.3f;
}
}  // namespace

const char* verdictName(Verdict v) {
  switch (v) {
    case Verdict::MEETS_THRESHOLDS:  return "MEETS_THRESHOLDS";
    case Verdict::TOO_TURBID:        return "TOO_TURBID";
    case Verdict::PH_OUT_OF_RANGE:   return "PH_OUT_OF_RANGE";
    case Verdict::SENSOR_UNRELIABLE: return "SENSOR_UNRELIABLE";
  }
  return "?";
}

void WaterQuality::begin() {
  analogReadResolution(12);
  analogSetPinAttenuation(PIN_TURBIDITY, ADC_11db);
  analogSetPinAttenuation(PIN_PH, ADC_11db);
  analogSetPinAttenuation(PIN_TDS, ADC_11db);

  pinMode(PIN_LEVEL_TRIG, OUTPUT);
  pinMode(PIN_LEVEL_ECHO, INPUT);

  thermometer.begin();
  thermometer.setResolution(11);
}

float WaterQuality::readTurbidityNtu(float temperature_c) {
  const float v = readAveragedVolts(PIN_TURBIDITY);

  // Rel bawah atau atas berarti sensor lepas atau terendam lumpur pekat.
  if (v < 0.05f || v > 3.25f) return NAN;

  float ntu = TURB_C2 * v * v + TURB_C1 * v + TURB_C0;

  // Kompensasi suhu. Hamburan cahaya bergeser terhadap suhu air, dan pada
  // sensor murah pergeserannya cukup untuk memindahkan satu muatan dari
  // "lulus" ke "gagal" sepanjang hari.
  if (!isnan(temperature_c)) {
    ntu *= 1.0f + TURB_TEMP_COEF * (temperature_c - TURB_TEMP_REF);
  }

  if (ntu < 0.0f) ntu = 0.0f;
  if (ntu > TURBIDITY_SENSOR_MAX) return NAN;  // di luar kurva kalibrasi
  return ntu;
}

float WaterQuality::readPh() {
  const float v = readAveragedVolts(PIN_PH);
  if (v < 0.05f || v > 3.25f) return NAN;
  return 7.0f + (v - ph_v_at_7_) / PH_SLOPE_V_PER_PH;
}

float WaterQuality::readTds(float temperature_c) {
  const float v = readAveragedVolts(PIN_TDS);
  if (v < 0.01f) return NAN;

  const float t = isnan(temperature_c) ? 25.0f : temperature_c;
  const float compensated = v / (1.0f + 0.02f * (t - 25.0f));
  return (133.42f * compensated * compensated * compensated -
          255.86f * compensated * compensated + 857.39f * compensated) *
         0.5f;
}

float WaterQuality::readLevelCm() {
  digitalWrite(PIN_LEVEL_TRIG, LOW);
  delayMicroseconds(4);
  digitalWrite(PIN_LEVEL_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(PIN_LEVEL_TRIG, LOW);

  const uint32_t duration = pulseIn(PIN_LEVEL_ECHO, HIGH, 30000);
  if (duration == 0) return NAN;  // tidak ada gema

  const float distance_cm = duration * 0.0343f / 2.0f;
  if (distance_cm < SENSOR_DEAD_ZONE_CM) return NAN;  // zona buta

  const float level = TANK_HEIGHT_CM - distance_cm;
  return (level < 0.0f) ? 0.0f : level;
}

WaterSample WaterQuality::sample() {
  WaterSample s{};

  thermometer.requestTemperatures();
  const float t = thermometer.getTempCByIndex(0);
  s.temperature_c = (t > -50.0f && t < 90.0f) ? t : NAN;

  s.turbidity_ntu = readTurbidityNtu(s.temperature_c);
  s.ph = readPh();
  s.tds_ppm = readTds(s.temperature_c);
  s.raw_level_cm = readLevelCm();

  s.turbidity_valid = !isnan(s.turbidity_ntu);
  s.ph_valid = !isnan(s.ph) && s.ph > 0.0f && s.ph < 14.0f;

  return s;
}

Verdict WaterQuality::judge(const WaterSample& s) {
  // Sensor yang tidak menjawab tidak pernah menghasilkan kelulusan. Muatan
  // air yang mutunya tidak diketahui diperlakukan sama dengan yang gagal.
  if (!s.turbidity_valid || !s.ph_valid) return Verdict::SENSOR_UNRELIABLE;

  if (s.turbidity_ntu > TURBIDITY_PASS_NTU) return Verdict::TOO_TURBID;
  if (s.ph < PH_MIN_ACCEPT || s.ph > PH_MAX_ACCEPT) {
    return Verdict::PH_OUT_OF_RANGE;
  }
  return Verdict::MEETS_THRESHOLDS;
}

void WaterQuality::calibratePh(float measured_v_at_7) {
  ph_v_at_7_ = measured_v_at_7;
}
