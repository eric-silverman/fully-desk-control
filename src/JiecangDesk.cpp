#include "JiecangDesk.h"

namespace jiecang {

// Scoped mutex. Every public entry point and tick() takes it, so the web
// server task, the HomeKit task and the realtime desk task can all poke the
// driver without interleaving half-written frames on the UART.
struct Guard {
  SemaphoreHandle_t h;
  explicit Guard(SemaphoreHandle_t s) : h(s) { xSemaphoreTakeRecursive(h, portMAX_DELAY); }
  ~Guard() { xSemaphoreGiveRecursive(h); }
};

void Desk::begin(HardwareSerial &serial, int rxPin, int txPin) {
  lock_ = xSemaphoreCreateRecursiveMutex();
  serial_ = &serial;
  serial_->begin(9600, SERIAL_8N1, rxPin, txPin);
  probe_next_ms_ = millis() + 500;
}

// ---------------------------------------------------------------------------
// Transmit
// ---------------------------------------------------------------------------

void Desk::sendFrame(uint8_t cmd, const uint8_t *params, uint8_t len) {
  uint8_t buf[16];
  uint8_t n = 0;
  buf[n++] = 0xF1;
  buf[n++] = 0xF1;
  buf[n++] = cmd;
  buf[n++] = len;
  uint8_t sum = cmd + len;
  for (uint8_t i = 0; i < len; i++) {
    buf[n++] = params[i];
    sum += params[i];
  }
  buf[n++] = sum;
  buf[n++] = 0x7E;
  serial_->write(buf, n);
}

void Desk::wakeIfNeeded() {
  // The box powers its handset link down when idle and ignores the first
  // command after that. A bare STOP is the safe wake-up knock.
  if (last_rx_ms_ == 0 || (millis() - last_rx_ms_) > WAKE_IDLE_MS) {
    sendFrame(CMD_STOP);
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

void Desk::sendSimple(uint8_t cmd) {
  wakeIfNeeded();
  sendFrame(cmd);
}

// ---------------------------------------------------------------------------
// Receive
// ---------------------------------------------------------------------------

void Desk::feed(uint8_t b) {
  switch (ps_) {
    case P_A1:
      if (b == 0xF2) ps_ = P_A2;
      break;

    case P_A2:
      // Second address byte must repeat. If it does not, this byte might
      // itself be the start of a real frame, so re-test it rather than
      // dropping it.
      ps_ = (b == 0xF2) ? P_CMD : P_A1;
      break;

    case P_CMD:
      cmd_ = b;
      sum_ = b;
      ps_ = P_LEN;
      break;

    case P_LEN:
      len_ = b;
      sum_ += b;
      idx_ = 0;
      if (len_ > sizeof(par_)) {  // implausible, resync
        frames_bad_++;
        ps_ = P_A1;
      } else {
        ps_ = len_ ? P_PAR : P_SUM;
      }
      break;

    case P_PAR:
      par_[idx_++] = b;
      sum_ += b;
      if (idx_ >= len_) ps_ = P_SUM;
      break;

    case P_SUM:
      sum_ok_ = (b == sum_);
      ps_ = P_EOM;
      break;

    case P_EOM:
      // Length-driven parsing, so unlike a naive 0x7E-terminated scanner
      // this does not corrupt on payloads that happen to contain 0x7E.
      if (b == 0x7E && sum_ok_) {
        frames_good_++;
        handleFrame();
      } else {
        frames_bad_++;
      }
      ps_ = P_A1;
      break;
  }
}

static inline uint16_t w16(const uint8_t *p) {
  return (uint16_t)((p[0] << 8) | p[1]);
}

void Desk::handleFrame() {
  last_rx_ms_ = millis();

  switch (cmd_) {
    case 0x01: {  // height report
      if (len_ < 2) break;
      uint16_t h = w16(par_);
      if (h < HEIGHT_RAW_MIN || h > HEIGHT_RAW_MAX) {
        frames_bad_++;
        break;
      }
      if (h != height_raw_) {
        motion_ = (height_raw_ == 0) ? Motion::Idle
                  : (h > height_raw_) ? Motion::Up
                                      : Motion::Down;
        height_raw_ = h;
        last_height_change_ms_ = millis();
      }
      break;
    }

    case 0x07:  // physical range of travel: {max}{min}
      if (len_ >= 4) {
        phys_max_ = w16(par_);
        phys_min_ = w16(par_ + 2);
      }
      break;

    case 0x0E:  // units
      if (len_ >= 1) units_ = par_[0];
      break;

    case 0x1B:  // controller echoing the target it accepted
      break;

    case 0x20:  // which soft limits are set (low nibble max, high nibble min)
      if (len_ >= 1) {
        if ((par_[0] & 0x0F) == 0 && phys_max_) limit_max_ = phys_max_;
        if ((par_[0] >> 4) == 0 && phys_min_) limit_min_ = phys_min_;
      }
      break;

    case 0x21:  // soft max
      if (len_ >= 2) limit_max_ = w16(par_);
      break;

    case 0x22:  // soft min
      if (len_ >= 2) limit_min_ = w16(par_);
      break;

    case 0x23:  // travel limit hit
      if (goto_active_) abortMove("limit");
      break;

    case 0x25:
    case 0x26:
    case 0x27:
    case 0x28:  // preset heights 1..4
      if (len_ >= 2) preset_[cmd_ == 0x25 ? 0 : cmd_ == 0x26 ? 1 : cmd_ == 0x27 ? 2 : 3] = w16(par_);
      break;

    default:
      break;
  }
}

// ---------------------------------------------------------------------------
// Commands
// ---------------------------------------------------------------------------

void Desk::stop() {
  Guard g(lock_);
  nudge_dir_ = 0;
  nudge_deadline_ = 0;
  goto_active_ = false;
  sendFrame(CMD_STOP);
}

void Desk::nudgeUp() {
  Guard g(lock_);
  if (goto_active_) {  // a physical press always wins over a queued move
    goto_active_ = false;
    sendFrame(CMD_STOP);
  }
  fault_ = nullptr;
  nudge_dir_ = 1;
  nudge_deadline_ = millis() + NUDGE_DEADMAN_MS;
}

void Desk::nudgeDown() {
  Guard g(lock_);
  if (goto_active_) {
    goto_active_ = false;
    sendFrame(CMD_STOP);
  }
  fault_ = nullptr;
  nudge_dir_ = -1;
  nudge_deadline_ = millis() + NUDGE_DEADMAN_MS;
}

bool Desk::gotoRaw(uint16_t target) {
  Guard g(lock_);
  if (!limitsKnown() || !heightKnown()) return false;

  if (target < limit_min_) target = limit_min_;
  if (target > limit_max_) target = limit_max_;

  // Already there. Report success without touching the motor.
  if (abs((int)target - (int)height_raw_) <= GOTO_TOLERANCE_RAW) {
    goto_active_ = false;
    return true;
  }

  nudge_dir_ = 0;
  nudge_deadline_ = 0;
  fault_ = nullptr;

  wakeIfNeeded();
  uint8_t p[2] = {(uint8_t)(target >> 8), (uint8_t)(target & 0xFF)};
  sendFrame(CMD_GOTO, p, 2);

  goto_active_ = true;
  goto_target_ = target;
  goto_start_ms_ = millis();
  goto_last_height_ = height_raw_;
  goto_progress_ms_ = millis();
  return true;
}

bool Desk::gotoPercent(float pct) {
  Guard g(lock_);
  if (!limitsKnown()) return false;
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  float span = (float)limit_max_ - (float)limit_min_;
  return gotoRaw((uint16_t)lroundf(limit_min_ + span * pct / 100.0f));
}

void Desk::gotoPreset(uint8_t n) {
  Guard g(lock_);
  switch (n) {
    case 1: sendSimple(CMD_MOVE_1); break;
    case 2: sendSimple(CMD_MOVE_2); break;
    case 3: sendSimple(CMD_MOVE_3); break;
    case 4: sendSimple(CMD_MOVE_4); break;
    default: return;
  }
  // Preset moves are driven entirely by the box. Watchdog them the same way
  // as a goto, but with no target to compare against.
  goto_active_ = true;
  goto_target_ = 0;
  goto_start_ms_ = millis();
  goto_last_height_ = height_raw_;
  goto_progress_ms_ = millis();
}

void Desk::savePreset(uint8_t n) {
  Guard g(lock_);
  switch (n) {
    case 1: sendSimple(CMD_PROGMEM_1); break;
    case 2: sendSimple(CMD_PROGMEM_2); break;
    case 3: sendSimple(CMD_PROGMEM_3); break;
    case 4: sendSimple(CMD_PROGMEM_4); break;
    default: break;
  }
}

uint16_t Desk::preset(uint8_t n) const {
  return (n >= 1 && n <= 4) ? preset_[n - 1] : 0;
}

float Desk::percent() const {
  if (!limitsKnown() || !heightKnown()) return NAN;
  float span = (float)limit_max_ - (float)limit_min_;
  float p = ((float)height_raw_ - (float)limit_min_) * 100.0f / span;
  if (p < 0) p = 0;
  if (p > 100) p = 100;
  return p;
}

void Desk::abortMove(const char *why) {
  goto_active_ = false;
  nudge_dir_ = 0;
  nudge_deadline_ = 0;
  fault_ = why;
  sendFrame(CMD_STOP);
}

// ---------------------------------------------------------------------------
// Discovery
// ---------------------------------------------------------------------------

void Desk::probeTick() {
  if (limitsKnown() && (units_ != 0xFF || !FORCE_UNITS_INCHES)) return;

  // Never probe mid-move. The sweep opens with a STOP to wake the box, and
  // if limits never arrive the sweep repeats forever — which would cancel
  // the user's move every few seconds.
  if (goto_active_ || nudge_dir_ != 0) return;

  uint32_t now = millis();
  if (now < probe_next_ms_) return;
  probe_next_ms_ = now + 800;

  switch (probe_step_) {
    case 0:
      sendFrame(CMD_STOP);  // wake
      break;
    case 1:
#if FORCE_UNITS_INCHES
      if (!units_sent_) {
        uint8_t p = 0x01;  // 0x00 = cm, 0x01 = inches
        sendFrame(CMD_UNITS, &p, 1);
        units_sent_ = true;
      }
#endif
      break;
    case 2:
      sendFrame(CMD_PHYS_LIMITS);  // 0x0C -> responds 0x07
      break;
    case 3:
      sendFrame(CMD_LIMITS);  // 0x20 -> responds 0x20 [+0x21 +0x22]
      break;
    case 4:
      sendFrame(CMD_SETTINGS);  // 0x07 -> presets, units, limits
      break;
    default:
      probe_step_ = 0;              // wrap and retry until limits land
      probe_next_ms_ = now + 3000;  // ...but back off between sweeps
      return;
  }
  probe_step_++;
}

// ---------------------------------------------------------------------------
// Realtime tick
// ---------------------------------------------------------------------------

void Desk::tick() {
  Guard g(lock_);
  uint32_t now = millis();

  while (serial_->available()) feed((uint8_t)serial_->read());

  // The box stops reporting height changes shortly after motion ends.
  if (motion_ != Motion::Idle && (now - last_height_change_ms_) > 400) {
    motion_ = Motion::Idle;
  }

  probeTick();

  // --- deadman nudging -------------------------------------------------
  if (nudge_dir_ != 0) {
    if ((int32_t)(now - nudge_deadline_) >= 0) {
      // Caller went away (button released, browser closed, WiFi died).
      nudge_dir_ = 0;
      sendFrame(CMD_STOP);
    } else if ((now - last_nudge_tx_) >= NUDGE_PERIOD_MS) {
      wakeIfNeeded();
      sendFrame(nudge_dir_ > 0 ? CMD_UP : CMD_DOWN);
      last_nudge_tx_ = now;
    }
  }

  // --- absolute move watchdog ------------------------------------------
  if (goto_active_) {
    if (height_raw_ != goto_last_height_) {
      goto_last_height_ = height_raw_;
      goto_progress_ms_ = now;
    }

    if (goto_target_ && abs((int)height_raw_ - (int)goto_target_) <= GOTO_TOLERANCE_RAW) {
      goto_active_ = false;  // arrived; the box has already stopped itself
    } else if ((now - goto_start_ms_) > MAX_MOVE_MS) {
      abortMove("timeout");
    } else if ((now - goto_progress_ms_) > STALL_TIMEOUT_MS) {
      // Not moving despite being told to: obstruction, soft limit, or the
      // box never accepted the command.
      abortMove("stall");
    }
  }
}

}  // namespace jiecang
