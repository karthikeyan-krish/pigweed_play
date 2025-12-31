#pragma once

#include <pw_function/function.h>

namespace play::thread {

class State;

class StateMachineContext {
 public:
  using StateChangeCb = pw::Function<void(const State*, const State*)>;

  StateMachineContext(StateChangeCb&& state_change_cb);
  ~StateMachineContext() = default;

  int Start();

  void HandleButtonPress();
  void HandleButtonRelease();

  void SetState(State* newState);
  const State* GetCurrentState() { return curr_state_; }
  const State* GetPreviousState() { return prev_state_; }
  bool GetButtonPressed() const { return button_pressed_; }
  void SetButtonPressed(bool pressed) { button_pressed_ = pressed; }

 private:
  State *prev_state_, *curr_state_;
  StateChangeCb state_change_cb_;
  bool button_pressed_;
};

}  // namespace play::thread
