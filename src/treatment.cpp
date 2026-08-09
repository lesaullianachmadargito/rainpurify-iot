#include "treatment.h"

#include <math.h>

#include "config.h"

const char* treatStateName(TreatState s) {
  switch (s) {
    case TreatState::IDLE:          return "IDLE";
    case TreatState::FILTERING:     return "FILTERING";
    case TreatState::SETTLING:      return "SETTLING";
    case TreatState::TESTING:       return "TESTING";
    case TreatState::RECIRCULATING: return "RECIRCULATING";
    case TreatState::DELIVERING:    return "DELIVERING";
    case TreatState::REJECTED:      return "REJECTED";
    case TreatState::FAULT_DRY:     return "FAULT_DRY";
  }
  return "?";
}

void Treatment::begin() {
  pinMode(PIN_PUMP, OUTPUT);
  pinMode(PIN_VALVE_CLEAN, OUTPUT);
  pinMode(PIN_VALVE_RECIRC, OUTPUT);
  pinMode(PIN_VALVE_WASTE, OUTPUT);
  pinMode(PIN_LED_OK, OUTPUT);
  pinMode(PIN_LED_FAULT, OUTPUT);

  digitalWrite(PIN_PUMP, LOW);
  setValves(false, false, false);

  state_ = TreatState::IDLE;
  state_since_ = millis();
}

void Treatment::setValves(bool clean, bool recirc, bool waste) {
  digitalWrite(PIN_VALVE_CLEAN, clean ? HIGH : LOW);
  digitalWrite(PIN_VALVE_RECIRC, recirc ? HIGH : LOW);
  digitalWrite(PIN_VALVE_WASTE, waste ? HIGH : LOW);
}

void Treatment::requestStart() { start_requested_ = true; }

void Treatment::enter(TreatState s, uint32_t now) {
  state_ = s;
  state_since_ = now;

  switch (s) {
    case TreatState::IDLE:
    case TreatState::FAULT_DRY:
      digitalWrite(PIN_PUMP, LOW);
      setValves(false, false, false);
      break;

    case TreatState::FILTERING:
    case TreatState::RECIRCULATING:
      // Selama menyaring, keluaran dikembalikan ke tandon mentah. Air tidak
      // pernah masuk tandon bersih sebelum diuji — sekali air buruk masuk ke
      // sana, seluruh isinya ikut tercemar dan harus dibuang.
      setValves(false, true, false);
      digitalWrite(PIN_PUMP, HIGH);
      break;

    case TreatState::SETTLING:
      digitalWrite(PIN_PUMP, LOW);
      setValves(false, false, false);
      break;

    case TreatState::TESTING:
      digitalWrite(PIN_PUMP, LOW);
      break;

    case TreatState::DELIVERING:
      setValves(true, false, false);
      digitalWrite(PIN_PUMP, HIGH);
      break;

    case TreatState::REJECTED:
      digitalWrite(PIN_PUMP, HIGH);
      setValves(false, false, true);
      break;
  }
}

void Treatment::update(uint32_t now, const WaterSample& s) {
  const uint32_t in_state = now - state_since_;

  // Perlindungan pompa berlaku di setiap keadaan yang memompa dari tandon
  // mentah. Pompa diafragma yang berjalan kering rusak dalam hitungan menit.
  const bool pumping_from_raw =
      state_ == TreatState::FILTERING || state_ == TreatState::RECIRCULATING;
  if (pumping_from_raw && !isnan(s.raw_level_cm) &&
      s.raw_level_cm < RAW_MIN_LEVEL_CM) {
    enter(TreatState::FAULT_DRY, now);
  }

  digitalWrite(PIN_LED_FAULT, state_ == TreatState::FAULT_DRY ||
                                  state_ == TreatState::REJECTED);
  digitalWrite(PIN_LED_OK, state_ == TreatState::DELIVERING);

  switch (state_) {
    case TreatState::IDLE:
      if (start_requested_ && !isnan(s.raw_level_cm) &&
          s.raw_level_cm >= RAW_MIN_LEVEL_CM) {
        start_requested_ = false;
        batch_ = Batch{next_id_++, 1, s.turbidity_ntu, NAN, NAN,
                       Verdict::SENSOR_UNRELIABLE, nullptr};
        enter(TreatState::FILTERING, now);
      }
      break;

    case TreatState::FILTERING:
    case TreatState::RECIRCULATING:
      if (in_state >= PASS_DURATION_MS) enter(TreatState::SETTLING, now);
      break;

    case TreatState::SETTLING:
      // Mengukur kekeruhan saat pompa baru mati memberi angka yang dipenuhi
      // gelembung. Aliran dibiarkan tenang lebih dulu.
      if (in_state >= SETTLE_BEFORE_TEST_MS) enter(TreatState::TESTING, now);
      break;

    case TreatState::TESTING: {
      const Verdict verdict = WaterQuality::judge(s);
      batch_.verdict = verdict;
      batch_.turbidity_out_ntu = s.turbidity_ntu;

      if (verdict == Verdict::MEETS_THRESHOLDS) {
        enter(TreatState::DELIVERING, now);
        break;
      }

      // pH tidak diperbaiki oleh media saring mana pun di sistem ini.
      // Melewatkannya berulang kali hanya membuang listrik.
      if (verdict == Verdict::PH_OUT_OF_RANGE) {
        batch_.rejection = "ph_not_correctable_by_filtration";
        enter(TreatState::REJECTED, now);
        break;
      }

      if (verdict == Verdict::SENSOR_UNRELIABLE) {
        batch_.rejection = "sensor_unreliable";
        enter(TreatState::REJECTED, now);
        break;
      }

      // Masih keruh. Boleh diulang, dengan dua syarat.
      if (batch_.pass >= MAX_RECIRCULATION_PASSES) {
        batch_.rejection = "max_passes_reached";
        enter(TreatState::REJECTED, now);
        break;
      }

      const bool improving =
          isnan(batch_.turbidity_last) ||
          (batch_.turbidity_last - s.turbidity_ntu) >= MIN_IMPROVEMENT_NTU;
      if (!improving) {
        // Dua lintasan berturut tanpa perbaikan berarti media jenuh atau
        // tersumbat. Lintasan berikutnya akan memberi angka yang sama.
        batch_.rejection = "no_improvement_media_likely_spent";
        enter(TreatState::REJECTED, now);
        break;
      }

      batch_.turbidity_last = s.turbidity_ntu;
      ++batch_.pass;
      enter(TreatState::RECIRCULATING, now);
      break;
    }

    case TreatState::DELIVERING:
      if (in_state >= PASS_DURATION_MS) {
        ++delivered_;
        enter(TreatState::IDLE, now);
      }
      break;

    case TreatState::REJECTED:
      if (in_state >= PASS_DURATION_MS / 3) {
        ++rejected_;
        enter(TreatState::IDLE, now);
      }
      break;

    case TreatState::FAULT_DRY:
      if (!isnan(s.raw_level_cm) && s.raw_level_cm >= RAW_MIN_LEVEL_CM * 1.5f) {
        enter(TreatState::IDLE, now);
      }
      break;
  }
}
