#pragma once

#include <pw_assert/check.h>
#include <pw_thread/thread_core.h>
#include <pw_chrono/system_timer.h>
#include <pw_thread_freertos/options.h>
#include <pw_sync/mutex.h>
#include <pw_sync/thread_notification.h>
#include <pw_containers/inline_queue.h>

#include <cstdint>

namespace play::thread {

// Empty event type for ActiveObjectCore specialization
struct EmptyEvent {};

template <typename EventType, size_t kQueueLen>
class ActiveObjectCore : public pw::thread::ThreadCore {
 public:
  ActiveObjectCore() : queue_(), queue_mutex_(), notification_() {}

  bool Post(const EventType& e) {
    queue_mutex_.lock();
    
    if (!queue_.full()) {
      queue_.push(e);
      queue_mutex_.unlock();
      notification_.release();
      return true;
    } else {
      queue_mutex_.unlock();
      return false;
    }
  }

  void Run() override {
    // Post an initial event to kick things off
    PW_ASSERT(Post({EventType::Type::kInit}));

    for (;;) {
      notification_.acquire();

      EventType ev;
      while (true) {
        {
          queue_mutex_.lock();
          if (queue_.empty()){
            queue_mutex_.unlock();
            break;
          }
          ev = queue_.front();
          queue_.pop(); 
          queue_mutex_.unlock();
        }
        HandleEvent(ev);
      }
    }
  }

 protected:
  // To be implemented by derived classes
  virtual void HandleEvent(const EventType& ev) = 0;

 private:
  pw::InlineQueue<EventType, kQueueLen> queue_;
  pw::sync::Mutex queue_mutex_;
  pw::sync::ThreadNotification notification_;
};

}  // namespace play::thread