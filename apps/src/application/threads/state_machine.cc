#include "state_machine.h"

#include <pw_log/log.h>

namespace play::thread {

StateMachineContext::StateMachineContext(StateChangeCb&& state_change_cb)
    : prev_state_(nullptr),
      curr_state_(nullptr),
      state_change_cb_(std::move(state_change_cb)),
      button_pressed_(false) {}

}  // namespace play::thread
