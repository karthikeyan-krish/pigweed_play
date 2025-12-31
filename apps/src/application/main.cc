#include "main.h"

#include <FreeRTOS.h>
#include <pw_assert/check.h>
#include <pw_chrono/system_clock.h>
#include <pw_chrono/system_timer.h>
#include <pw_log/log.h>
#include <pw_sys_io_stm32cube/init.h>
#include <pw_system/work_queue.h>
#include <pw_thread/detached_thread.h>
#include <pw_thread/sleep.h>
#include <task.h>

#include "gpio.h"
#include "threads/active_object.h"
#include "threads/state_machine.h"

namespace {
using namespace std::chrono_literals;

static constexpr auto kMorseAPeriod = 100ms;
static constexpr auto kMorseBPeriod = 200ms;
static constexpr auto kLedBlinkPeriod = 100ms;
static constexpr auto kButtonWatchdogPeriod = 50ms;
static constexpr auto kMorseAData = 0xA8EEE2A0U;
static constexpr auto kMorseBData = 0xE22A3800U;

enum class ThreadPriority : UBaseType_t {
  kLEDPriority = tskIDLE_PRIORITY + 1,
  kWorkQueue = tskIDLE_PRIORITY + 2,
  kNumPriorities,
};

static_assert(static_cast<UBaseType_t>(ThreadPriority::kNumPriorities) <=
              configMAX_PRIORITIES);

constexpr size_t kLEDStackSizeWords = 512;
constexpr size_t kWorkQueueThreadWords = 512;

struct AoEventLED {
  enum class Type : uint8_t {
    kInit,
    kMorseA,
    kMorseB,
  } type;
};

pw::sync::Mutex fsm_mutex_;

}  // namespace

// Functions needed when configGENERATE_RUN_TIME_STATS is on.
extern "C" void configureTimerForRunTimeStats(void) {}
extern "C" unsigned long getRunTimeCounterValue(void) { return uwTick; }

// System Clock Configuration: 80MHz
extern "C" void SystemClock_Config(void) {
  RCC_OscInitTypeDef RCC_OscInitStruct = {};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {};

  /** Configure the main internal regulator output voltage
   */
  if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK) {
    Error_Handler();
  }

  /** Initializes the RCC Oscillators according to the specified parameters
   * in the RCC_OscInitTypeDef structure.
   */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_MSI;
  RCC_OscInitStruct.MSIState = RCC_MSI_ON;
  RCC_OscInitStruct.MSICalibrationValue = 0;
  RCC_OscInitStruct.MSIClockRange = RCC_MSIRANGE_6;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_MSI;
  RCC_OscInitStruct.PLL.PLLM = 1;
  RCC_OscInitStruct.PLL.PLLN = 40;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV7;
  RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
  RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
   */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                                RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK) {
    Error_Handler();
  }
}

namespace {
class LEDActiveObject : public play::thread::ActiveObjectCore<AoEventLED, 8> {
 public:
  LEDActiveObject()
      : morse_a_timer_(
            [this](pw::chrono::SystemClock::time_point expired_deadline) {
              if (this->Post({AoEventLED::Type::kMorseA}) != true) {
                PW_LOG_ERROR(
                    "LEDActiveObject queue full, dropping Morse A event");
              }
              morse_a_timer_.InvokeAt(expired_deadline + morse_a_period_);
            }),
        morse_b_timer_(
            [this](pw::chrono::SystemClock::time_point expired_deadline) {
              if (this->Post({AoEventLED::Type::kMorseB}) != true) {
                PW_LOG_ERROR(
                    "LEDActiveObject queue full, dropping Morse B event");
              }
              morse_b_timer_.InvokeAt(expired_deadline + morse_b_period_);
            }),
        morse_a_period_(kMorseAPeriod),
        morse_b_period_(kMorseBPeriod) {}

 protected:
  void HandleEvent(const AoEventLED& ev) override {
    switch (ev.type) {
      case AoEventLED::Type::kInit:
        bsp_led_green_off();
        morse_a_timer_.InvokeAfter(morse_a_period_);
        morse_b_timer_.InvokeAfter(morse_b_period_);
        break;
      case AoEventLED::Type::kMorseA:
        bsp_send_morse_code(kMorseAData);
        break;
      case AoEventLED::Type::kMorseB:
        bsp_send_morse_code(kMorseBData);
        break;
      default:
        PW_LOG_ERROR("LEDActiveObject received unknown event");
        PW_ASSERT(false);
        break;
    }
  }

 private:
  pw::chrono::SystemTimer morse_a_timer_;
  pw::chrono::SystemTimer morse_b_timer_;
  pw::chrono::SystemClock::duration morse_a_period_;
  pw::chrono::SystemClock::duration morse_b_period_;
};

class ButtonObject {
 public:
  using ReleaseHandler = pw::Function<void()>;

  explicit ButtonObject(ReleaseHandler on_release)
      : on_release_(std::move(on_release)),
        button_watchdog_timer_(
            [this](pw::chrono::SystemClock::time_point expired_deadline) {
              if (!watchdog_enabled_) {
                return;
              }
              bool pressed = bsp_button_status();
              if (pressed && !last_state_) {
                last_state_ = true;
                blink_timer_.InvokeAfter(blink_period_);
              } else if (!pressed && last_state_) {
                last_state_ = false;
                if (on_release_)
                  on_release_();
              }
              if (watchdog_enabled_) {
                button_watchdog_timer_.InvokeAt(expired_deadline +
                                                button_watchdog_period_);
              }
            }),
        blink_timer_([this](pw::chrono::SystemClock::time_point) {
          bsp_led_blue_toggle();
          blink_timer_.InvokeAfter(blink_period_);
        }),
        button_watchdog_period_(kButtonWatchdogPeriod),
        blink_period_(kLedBlinkPeriod),
        last_state_(false) {}

  void StartWatchdog() {
    watchdog_enabled_ = true;
    button_watchdog_timer_.InvokeAfter(button_watchdog_period_);
  }

  void StopWatchdog() {
    watchdog_enabled_ = false;
    button_watchdog_timer_.Cancel();
    blink_timer_.Cancel();
    bsp_led_blue_off();
  }

 private:
  ReleaseHandler on_release_;
  pw::chrono::SystemTimer button_watchdog_timer_;
  pw::chrono::SystemTimer blink_timer_;
  pw::chrono::SystemClock::duration button_watchdog_period_;
  pw::chrono::SystemClock::duration blink_period_;
  bool last_state_;
  bool watchdog_enabled_ = false;
};

play::thread::StateMachineContext* fsm = nullptr;
ButtonObject* button = nullptr;

static LEDActiveObject led_ao;
static void StartLEDThread() {
  pw::thread::DetachedThread(
      pw::thread::freertos::Options()
          .set_name("LEDThread")
          .set_priority(static_cast<UBaseType_t>(ThreadPriority::kLEDPriority))
          .set_stack_size(kLEDStackSizeWords),
      led_ao);
}

auto wq = pw::system::GetWorkQueue;
static void StartWorkQueueThread() {
  pw::thread::DetachedThread(
      pw::thread::freertos::Options()
          .set_name("WorkQueueThread")
          .set_priority(static_cast<UBaseType_t>(ThreadPriority::kWorkQueue))
          .set_stack_size(kWorkQueueThreadWords),
      wq());
}

static void ButtonPressed() {
  fsm_mutex_.lock();
  if (fsm != nullptr) {
    fsm->HandleButtonPress();
  }
  fsm_mutex_.unlock();
}

}  // namespace

extern "C" int main(void) {
  HAL_Init();

  SystemClock_Config();
  pw_sys_io_Init();

  bsp_init();

  StartWorkQueueThread();
  pw::system::GetWorkQueue().CheckPushWork(StartLEDThread);

  static play::thread::StateMachineContext fsm_instance{
      [](const play::thread::State* prev_state,
         const play::thread::State* state) {
        if (prev_state == &play::thread::StateButtonPressed::instance()) {
          button->StopWatchdog();
        }
        if (state == &play::thread::StateButtonPressed::instance()) {
          button->StartWatchdog();
        }
      }};
  fsm = &fsm_instance;

  vTaskStartScheduler();

  while (1) {
  }
}

extern "C" void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
  if (GPIO_Pin == BUTTON_EXTI13_Pin) {
    pw::system::GetWorkQueue().CheckPushWork(ButtonPressed);
  }
}
extern "C" PW_NO_RETURN void Error_Handler(void) { PW_CRASH("Error"); }