#pragma once

#include "HomeSpan.h"
#include "JiecangDesk.h"

// HomeKit Window Covering.
//
// Window Covering is the right profile for a desk: the Home app gives you a
// percentage slider plus open/close, and Siri understands "set my desk to
// 50 percent", "raise my desk", "lower my desk". 0% is the desk's lower
// travel limit, 100% is the upper limit, both read live from the control box.
//
// Garage Door was the alternative and is worse: it is binary, it has no
// position characteristic, and iOS treats it as a security accessory
// (confirmation prompts, no unattended Siri).

struct DeskCover : Service::WindowCovering {
  Characteristic::CurrentPosition cur{0};
  Characteristic::TargetPosition tgt{0};
  Characteristic::PositionState state{2};  // 0 down, 1 up, 2 stopped
  Characteristic::HoldPosition hold;
  Characteristic::ObstructionDetected obstruction{false};

  jiecang::Desk *desk;
  uint32_t next_publish_ = 0;
  int last_cur_ = -1;
  int last_state_ = -1;
  bool last_obs_ = false;

  explicit DeskCover(jiecang::Desk *d) : desk(d) {}

  boolean update() override {
    // HoldPosition is HomeKit's "stop where you are" control.
    if (hold.updated() && hold.getNewVal<bool>()) {
      desk->stop();
      float p = desk->percent();
      if (!isnan(p)) tgt.setVal((int)lroundf(p));
      return true;
    }

    if (tgt.updated()) {
      if (!desk->gotoPercent((float)tgt.getNewVal())) {
        // Limits not read yet. Reject so the Home app does not show a
        // position the desk never moved to.
        return false;
      }
      desk->clearFault();
    }
    return true;
  }

  void loop() override {
    if (millis() < next_publish_) return;
    next_publish_ = millis() + 400;

    float p = desk->percent();
    if (isnan(p)) return;
    int pos = (int)lroundf(p);

    if (pos != last_cur_) {
      cur.setVal(pos);
      last_cur_ = pos;
    }

    int st = desk->motion() == jiecang::Motion::Up     ? 1
             : desk->motion() == jiecang::Motion::Down ? 0
                                                       : 2;
    if (st != last_state_) {
      state.setVal(st);
      last_state_ = st;
    }

    // A stalled move means something is in the way (or a soft limit was
    // hit). Surface it rather than silently doing nothing.
    bool obs = desk->fault() != nullptr && strcmp(desk->fault(), "stall") == 0;
    if (obs != last_obs_) {
      obstruction.setVal(obs);
      last_obs_ = obs;
    }

    // When the desk is parked and nothing is driving it, keep the target in
    // sync with reality so the Home app slider matches after someone used
    // the physical buttons.
    if (st == 2 && !desk->gotoActive() && tgt.getVal() != pos) {
      tgt.setVal(pos);
    }
  }
};
