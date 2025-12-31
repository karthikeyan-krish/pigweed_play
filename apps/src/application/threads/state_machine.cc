#include "state_machine.h"

#include <pw_log/log.h>

namespace play::thread {

StateMachineContext::StateMachineContext(StateChangeCb&& state_change_cb)
    : prev_state_(nullptr),
      curr_state_(nullptr),
      state_change_cb_(std::move(state_change_cb)),
      button_pressed_(false) {}

int StateMachineContext::Start() {
  SetState(&StateIdle::instance());
  return 0;
}

void StateMachineContext::HandleButtonPress() {
  if (curr_state_ != nullptr) {
    curr_state_->HandleButtonPress(*this);
  }
}

void StateMachineContext::HandleButtonRelease() {
  if (curr_state_ != nullptr) {
    curr_state_->HandleButtonRelease(*this);
  }
}

void StateMachineContext::SetState(State* new_state) {
  if (curr_state_ != nullptr) {
    curr_state_->Exit(*this);
  }
  prev_state_ = curr_state_;
  curr_state_ = new_state;
  if (curr_state_ != nullptr) {
    curr_state_->Entry(*this);
  }
  state_change_cb_(prev_state_, curr_state_);
}

// Abstract base state class
}  // namespace play::thread
