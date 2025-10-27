#include "pw_unit_test/framework.h"
#include "pw_async2/dispatcher.h"
#include "pw_async2/simulated_time_provider.h"
#include "pw_chrono/system_clock.h"
#include "pw_allocator/testing.h"
#include "apps/src/application/threads/active_object.h"

#include <deque>
#include <mutex>

namespace play::thread {
namespace {

using AllocatorForTest = ::pw::allocator::test::AllocatorForTest<512>;
using ::pw::async2::Dispatcher;
using ::pw::async2::SimulatedTimeProvider;
using ::pw::chrono::SystemClock;

// Test event definition
struct TestEvent {
  enum class Type { kInit, kPing } type;
};

// Simple concrete task that flips a flag when run
class FlagTask : public pw::async2::Task {
 public:
  explicit FlagTask(bool& flag) : flag_(flag) {}

 private:
  pw::async2::Poll<> DoPend(pw::async2::Context&) override {
    flag_ = true;
    return pw::async2::Ready();
  }

  bool& flag_;
};

// Test ActiveObjectCore with simulated environment
class TestActiveObject : public ActiveObjectCore<TestEvent, 4> {
 public:
  TestActiveObject(SimulatedTimeProvider<SystemClock>& time,
                   Dispatcher& dispatcher)
      : time_(time),
        dispatcher_(dispatcher),
        callback_task_(callback_triggered_) {}

  size_t handled_count() const { return handled_count_; }
  const std::deque<TestEvent::Type>& history() const { return history_; }
  bool callback_triggered() const { return callback_triggered_; }

  void EnqueueForTest(const TestEvent& ev) {
    std::lock_guard<pw::sync::Mutex> lock(queue_mutex_for_test_);
    queue_for_test_.push_back(ev);
  }

  void ProcessNextEventForTest() {
    TestEvent ev;
    {
      std::lock_guard<pw::sync::Mutex> lock(queue_mutex_for_test_);
      if (queue_for_test_.empty()) return;
      ev = queue_for_test_.front();
      queue_for_test_.pop_front();
    }
    HandleEvent(ev);  // safe (we’re in subclass)
  }

 protected:
  void HandleEvent(const TestEvent& ev) override {
    handled_count_++;
    history_.push_back(ev.type);

    // Access simulated time to demonstrate integration
    (void)time_.now();

    // Post async callback task
    dispatcher_.Post(callback_task_);
  }

 private:
  SimulatedTimeProvider<SystemClock>& time_;
  Dispatcher& dispatcher_;
  FlagTask callback_task_;

  size_t handled_count_ = 0;
  std::deque<TestEvent::Type> history_;
  bool callback_triggered_ = false;

  std::deque<TestEvent> queue_for_test_;
  pw::sync::Mutex queue_mutex_for_test_;
};

// Unit test using simulated dispatcher + time provider
TEST(ActiveObjectSimulatedTest, ProcessesInitEventUsingDispatcher) {
  AllocatorForTest allocator;
  Dispatcher dispatcher;
  SimulatedTimeProvider<SystemClock> time;

  TestActiveObject ao(time, dispatcher);

  ao.EnqueueForTest({TestEvent::Type::kInit});
  ao.ProcessNextEventForTest();

  // Run dispatcher to process callback
  dispatcher.RunUntilStalled().IgnorePoll();
  time.AdvanceTime(std::chrono::milliseconds(10));

  // Verify event was handled and callback ran
  EXPECT_GE(ao.handled_count(), 1u);
  EXPECT_EQ(ao.history().front(), TestEvent::Type::kInit);
  EXPECT_TRUE(ao.callback_triggered());
}

}  // namespace
}  // namespace play::thread