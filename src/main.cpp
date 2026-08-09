// ============================================================================
//  RainPurify — kendali penyaringan air hujan bertingkat
//
//  Perintah serial (115200):
//     START            mulai satu siklus pengolahan
//     STOP             hentikan, kembali ke IDLE
//     STATUS           keadaan dan pembacaan terakhir
//     CALPH <volt>     kalibrasi pH: tegangan terukur pada buffer 7,00
//     BATCH            hasil muatan terakhir
//
//  PERINGATAN yang ikut dicetak setiap kali muatan lulus, dan disengaja:
//  penyaringan bukan disinfeksi. Air keluaran memenuhi ambang kekeruhan
//  dan pH; ia tidak diuji terhadap mikroba dan tidak boleh disebut layak
//  minum.
// ============================================================================

#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <math.h>

#include "config.h"
#include "treatment.h"
#include "water_quality.h"

namespace {
WaterQuality quality;
Treatment    treatment;

WaterSample last{};
bool sd_ok = false;
TreatState last_state = TreatState::IDLE;

uint32_t last_sample_ms = 0;
uint32_t last_log_ms = 0;

const char* CSV_HEADER =
    "uptime_ms,state,batch_id,pass,turbidity_ntu,ph,tds_ppm,water_temp_c,"
    "raw_level_cm,verdict,rejection";

String fnum(float v, unsigned int digits) {
  return isnan(v) ? String() : String(v, digits);
}

void logRow(uint32_t now) {
  const Batch b = treatment.batch();

  String row = String(now);
  row += "," + String(treatStateName(treatment.state()));
  row += "," + String(b.id);
  row += "," + String(b.pass);
  row += "," + fnum(last.turbidity_ntu, 2);
  row += "," + fnum(last.ph, 2);
  row += "," + fnum(last.tds_ppm, 0);
  row += "," + fnum(last.temperature_c, 1);
  row += "," + fnum(last.raw_level_cm, 1);
  row += "," + String(verdictName(b.verdict));
  row += ",";
  row += (b.rejection ? b.rejection : "");

  Serial.println(row);
  if (!sd_ok) return;

  File f = SD.open(LOG_PATH, FILE_APPEND);
  if (!f) {
    sd_ok = false;
    Serial.println("# microSD hilang");
    return;
  }
  f.println(row);
  f.close();
}

void announceBatch() {
  const Batch b = treatment.batch();
  Serial.printf("# muatan %lu, %u lintasan: %.2f -> %.2f NTU | %s\n",
                (unsigned long)b.id, b.pass, b.turbidity_in_ntu,
                b.turbidity_out_ntu, verdictName(b.verdict));

  if (b.verdict == Verdict::MEETS_THRESHOLDS) {
    Serial.println("# LULUS AMBANG KEKERUHAN DAN pH.");
    Serial.println("# Penyaringan bukan disinfeksi. Air ini tidak diuji");
    Serial.println("# terhadap mikroba. Untuk mencuci, menyiram, dan air baku");
    Serial.println("# yang masih akan dimasak — bukan untuk langsung diminum.");
  } else if (b.rejection) {
    Serial.printf("# ditolak: %s\n", b.rejection);
    if (String(b.rejection) == "no_improvement_media_likely_spent") {
      Serial.println("# kekeruhan berhenti turun antar lintasan — periksa atau");
      Serial.println("# ganti media sedimen dan karbon aktif sebelum siklus lain.");
    }
  }
}

void printStatus() {
  Serial.printf("# %s | muatan %lu lintasan %u\n",
                treatStateName(treatment.state()),
                (unsigned long)treatment.batch().id, treatment.batch().pass);
  Serial.printf("# keruh %.2f NTU%s | pH %.2f%s | TDS %.0f ppm | %.1f degC\n",
                last.turbidity_ntu, last.turbidity_valid ? "" : " (TIDAK SAHIH)",
                last.ph, last.ph_valid ? "" : " (TIDAK SAHIH)", last.tds_ppm,
                last.temperature_c);
  Serial.printf("# tandon mentah %.1f cm | terkirim %lu | ditolak %lu\n",
                last.raw_level_cm, (unsigned long)treatment.delivered(),
                (unsigned long)treatment.rejected());
}

void handleCommand(String line) {
  line.trim();
  if (line.length() == 0) return;

  const int sp = line.indexOf(' ');
  const String verb = (sp < 0) ? line : line.substring(0, sp);
  const String arg = (sp < 0) ? String("") : line.substring(sp + 1);

  if (verb.equalsIgnoreCase("START")) {
    treatment.requestStart();
    Serial.println("# siklus diminta");
  } else if (verb.equalsIgnoreCase("STOP")) {
    treatment.begin();
    Serial.println("# dihentikan, kembali ke IDLE");
  } else if (verb.equalsIgnoreCase("STATUS")) {
    printStatus();
  } else if (verb.equalsIgnoreCase("BATCH")) {
    announceBatch();
  } else if (verb.equalsIgnoreCase("CALPH")) {
    quality.calibratePh(arg.toFloat());
    Serial.printf("# pH: tegangan pada buffer 7,00 = %.3f V\n", arg.toFloat());
  } else {
    Serial.println("# START | STOP | STATUS | BATCH | CALPH <volt>");
  }
}
}  // namespace

void setup() {
  Serial.begin(115200);
  delay(300);

  quality.begin();
  treatment.begin();

  sd_ok = SD.begin();
  if (sd_ok && !SD.exists(LOG_PATH)) {
    File f = SD.open(LOG_PATH, FILE_WRITE);
    if (f) {
      f.println(CSV_HEADER);
      f.close();
    }
  }

  Serial.println();
  Serial.println("== RainPurify ==");
  Serial.printf("# ambang lulus %.1f NTU | pH %.1f-%.1f | maksimum %u lintasan\n",
                TURBIDITY_PASS_NTU, PH_MIN_ACCEPT, PH_MAX_ACCEPT,
                MAX_RECIRCULATION_PASSES);
  Serial.println("# keluaran NON-KONSUMSI: penyaringan bukan disinfeksi");
  Serial.println(CSV_HEADER);
}

void loop() {
  const uint32_t now = millis();

  if (Serial.available()) handleCommand(Serial.readStringUntil('\n'));

  if (now - last_sample_ms >= SAMPLE_PERIOD_MS) {
    last_sample_ms = now;
    last = quality.sample();
    treatment.update(now, last);

    // Peralihan keadaan dicatat saat itu juga, bukan menunggu jadwal log,
    // supaya urutan keputusan satu muatan dapat ditelusuri dari berkas.
    if (treatment.state() != last_state) {
      last_state = treatment.state();
      logRow(now);
      if (last_state == TreatState::DELIVERING ||
          last_state == TreatState::REJECTED) {
        announceBatch();
      }
    }
  }

  if (now - last_log_ms >= LOG_PERIOD_MS) {
    last_log_ms = now;
    logRow(now);
  }

  delay(10);
}
