#pragma once

#include <Arduino.h>

#include "water_quality.h"

// ============================================================================
//  Mesin keadaan pengolahan.
//
//   IDLE ──► FILTERING ──► SETTLING ──► TESTING ──┬──► DELIVERING ──► IDLE
//              ▲                                   │
//              └──────── RECIRCULATING ◄───────────┤
//                                                  │
//                            REJECTED ◄────────────┘
//
//  RECIRCULATING kembali ke FILTERING hanya selama dua syarat terpenuhi:
//  jumlah lintasan belum habis, DAN kekeruhan benar-benar membaik. Syarat
//  kedua itu yang membedakan alat ini dari perulangan buta — media yang jenuh
//  akan memberi hasil yang sama berapa kali pun air dilewatkan.
// ============================================================================

enum class TreatState : uint8_t {
  IDLE,
  FILTERING,
  SETTLING,
  TESTING,
  RECIRCULATING,
  DELIVERING,
  REJECTED,       // muatan ini tidak dapat diolah — dibuang
  FAULT_DRY,      // tandon mentah kosong, pompa dilindungi
};

const char* treatStateName(TreatState s);

struct Batch {
  uint32_t id;
  uint8_t  pass;              // lintasan ke berapa
  float    turbidity_in_ntu;  // sebelum lintasan pertama
  float    turbidity_last;    // hasil pengujian sebelumnya
  float    turbidity_out_ntu; // hasil akhir
  Verdict  verdict;
  const char* rejection;      // nullptr bila tidak ditolak
};

class Treatment {
 public:
  void begin();
  void update(uint32_t now, const WaterSample& sample);

  TreatState state() const { return state_; }
  Batch      batch() const { return batch_; }

  uint32_t delivered() const { return delivered_; }
  uint32_t rejected() const { return rejected_; }

  // Meminta satu siklus dimulai. Tidak menjamin dimulai — tandon mentah bisa
  // kosong.
  void requestStart();

 private:
  void enter(TreatState s, uint32_t now);
  void setValves(bool clean, bool recirc, bool waste);

  TreatState state_ = TreatState::IDLE;
  Batch      batch_{};
  uint32_t   state_since_ = 0;
  uint32_t   next_id_ = 1;
  bool       start_requested_ = false;

  uint32_t delivered_ = 0;
  uint32_t rejected_ = 0;
};
