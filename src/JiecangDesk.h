#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "config.h"

// ===========================================================================
// Jiecang / Fully Jarvis control box driver.
//
// Wire format, both directions:
//
//     <addr> <addr> <cmd> <len> <params...> <cksum> 0x7E
//
//   addr   0xF1 twice from the handset, 0xF2 twice from the controller
//   cksum  (cmd + len + sum(params)) & 0xFF
//   line   9600 baud, 8N1
//
// Movement policy: this driver never enters the controller's continuous
// movement mode. It offers exactly two primitives, both of which stop on
// their own:
//
//   nudgeUp/nudgeDown  repeated 0x01/0x02 step packets. The box moves only
//                      while packets keep arriving, so releasing a button
//                      (or losing the network, or crashing the caller) stops
//                      the desk within NUDGE_DEADMAN_MS.
//   gotoRaw            0x1B with an absolute target. The box drives to the
//                      target and halts by itself; we watchdog it anyway.
// ===========================================================================

namespace jiecang {

enum class Motion : uint8_t { Idle, Up, Down };

class Desk {
 public:
  void begin(HardwareSerial &serial, int rxPin, int txPin);

  // Drive the driver. Call from a dedicated high-priority task every ~5 ms.
  // Everything time-sensitive (deadman, stall detection, move ceiling,
  // step repetition) happens here, so this must not be starved by WiFi.
  void tick();

  // --- deadman movement -------------------------------------------------
  // Call repeatedly (faster than NUDGE_DEADMAN_MS) for as long as the user
  // is holding the control. Stop calling and the desk stops.
  void nudgeUp();
  void nudgeDown();

  // --- absolute movement ------------------------------------------------
  // Return false if limits are not known yet or the target is unusable.
  bool gotoRaw(uint16_t rawTarget);
  bool gotoPercent(float pct);

  // Cancels everything: deadman nudging, an in-flight goto, and tells the
  // box to halt. Safe to call from any task at any time.
  void stop();

  // --- presets (as programmed on the original handset) -------------------
  void gotoPreset(uint8_t n);  // 1..4
  void savePreset(uint8_t n);  // 1..4, stores current height

  // --- state (all safe to read from other tasks) ------------------------
  bool heightKnown() const { return height_raw_ != 0; }
  uint16_t heightRaw() const { return height_raw_; }
  float heightInches() const { return height_raw_ / 10.0f; }

  bool limitsKnown() const { return limit_max_ > limit_min_ && limit_min_ > 0; }
  uint16_t limitMin() const { return limit_min_; }
  uint16_t limitMax() const { return limit_max_; }

  // 0 = at lower limit, 100 = at upper limit. NAN until limits are known.
  float percent() const;

  Motion motion() const { return motion_; }
  bool moving() const { return motion_ != Motion::Idle; }
  bool gotoActive() const { return goto_active_; }
  uint16_t gotoTargetRaw() const { return goto_target_; }

  uint16_t preset(uint8_t n) const;  // 1..4, 0 if unknown

  // True once the controller has ever answered us.
  bool linkUp() const { return last_rx_ms_ != 0 && (millis() - last_rx_ms_) < 30000; }
  uint32_t msSinceRx() const { return last_rx_ms_ ? millis() - last_rx_ms_ : 0xFFFFFFFF; }

  // Last abort reason ("stall", "timeout", "limit"), or nullptr. Cleared on
  // the next successful move.
  const char *fault() const { return fault_; }
  void clearFault() { fault_ = nullptr; }

  // Counters, for the diagnostics endpoint.
  uint32_t framesGood() const { return frames_good_; }
  uint32_t framesBad() const { return frames_bad_; }

 private:
  // --- protocol ---------------------------------------------------------
  static constexpr uint8_t CMD_UP = 0x01;
  static constexpr uint8_t CMD_DOWN = 0x02;
  static constexpr uint8_t CMD_PROGMEM_1 = 0x03;
  static constexpr uint8_t CMD_PROGMEM_2 = 0x04;
  static constexpr uint8_t CMD_MOVE_1 = 0x05;
  static constexpr uint8_t CMD_MOVE_2 = 0x06;
  static constexpr uint8_t CMD_SETTINGS = 0x07;
  static constexpr uint8_t CMD_PHYS_LIMITS = 0x0C;
  static constexpr uint8_t CMD_UNITS = 0x0E;
  static constexpr uint8_t CMD_GOTO = 0x1B;
  static constexpr uint8_t CMD_LIMITS = 0x20;
  static constexpr uint8_t CMD_PROGMEM_3 = 0x25;
  static constexpr uint8_t CMD_PROGMEM_4 = 0x26;
  static constexpr uint8_t CMD_MOVE_3 = 0x27;
  static constexpr uint8_t CMD_MOVE_4 = 0x28;
  static constexpr uint8_t CMD_STOP = 0x2B;

  void sendFrame(uint8_t cmd, const uint8_t *params = nullptr, uint8_t len = 0);
  void sendSimple(uint8_t cmd);  // wakes the box first if it has gone quiet
  void wakeIfNeeded();

  void feed(uint8_t b);
  void handleFrame();

  void probeTick();      // one-shot limit/settings discovery, with retries
  void abortMove(const char *why);

  // --- receive state machine -------------------------------------------
  enum PState : uint8_t { P_A1, P_A2, P_CMD, P_LEN, P_PAR, P_SUM, P_EOM };
  PState ps_ = P_A1;
  uint8_t cmd_ = 0, len_ = 0, idx_ = 0, sum_ = 0;
  uint8_t par_[8] = {0};
  bool sum_ok_ = false;

  HardwareSerial *serial_ = nullptr;
  SemaphoreHandle_t lock_ = nullptr;

  // --- desk state -------------------------------------------------------
  uint16_t height_raw_ = 0;
  uint16_t limit_min_ = 0, limit_max_ = 0;
  uint16_t phys_min_ = 0, phys_max_ = 0;
  uint16_t preset_[4] = {0, 0, 0, 0};
  uint8_t units_ = 0xFF;  // 0 = cm, 1 = in, 0xFF = unknown

  Motion motion_ = Motion::Idle;
  uint32_t last_height_change_ms_ = 0;
  uint32_t last_rx_ms_ = 0;

  // --- movement bookkeeping --------------------------------------------
  int8_t nudge_dir_ = 0;  // -1 down, 0 idle, +1 up
  uint32_t nudge_deadline_ = 0;
  uint32_t last_nudge_tx_ = 0;

  bool goto_active_ = false;
  uint16_t goto_target_ = 0;
  uint32_t goto_start_ms_ = 0;
  uint16_t goto_last_height_ = 0;
  uint32_t goto_progress_ms_ = 0;

  const char *fault_ = nullptr;

  // --- discovery --------------------------------------------------------
  uint8_t probe_step_ = 0;
  uint32_t probe_next_ms_ = 0;
  bool units_sent_ = false;

  uint32_t frames_good_ = 0, frames_bad_ = 0;
};

}  // namespace jiecang
