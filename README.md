# pigweed_play 🎛️

[![Build with Bazel](https://img.shields.io/badge/build-Bazel-green)](https://bazel.build)
![FreeRTOS](https://img.shields.io/badge/RTOS-FreeRTOS-blue)
![Pigweed](https://img.shields.io/badge/framework-Pigweed-purple)
![C++17](https://img.shields.io/badge/language-C++17-orange)
![License](https://img.shields.io/badge/license-MIT-lightgrey)

A demo embedded project to showcase modern C++ design, FreeRTOS, and Pigweed integration.

---

## 🚀 Overview
`pigweed_play` is a small embedded firmware project designed to demonstrate:
- Event-driven architecture using **Active Objects (AO)**
- Threading with **FreeRTOS (via Pigweed)**
- Structured logging with **Pigweed log**
- Hardware control through a simple **BSP (Board Support Package)**
- Unit testing with **Google Test**
- Finite State Machines (FSMs) for robust control logic
- GitHub Actions CI workflow for Bazel
- **Dual-slot bootloader with rollback support**

---

## 📂 Project Structure
```
📂 pigweed_play/
├── .github/workflows/  
|    └── ci.yml
├── apps/                       # Application code (Active Objects, state machines)
│   ├── include/
│   │   └── main.h
│   ├── src/
│   │   ├── application/
│   │   │   ├── main.cc
│   │   │   ├── stm32l4xx_it.c
│   │   │   ├── stm32l4xx_it.h
│   │   │   ├── bootloader_confirm/
│   │   │   |   └── bld_confirm.h
│   │   │   |   └── bld_confirm.c
│   │   │   └── threads/
│   │   │       └── active_object.h
|   |   |       └── state_machine.cc
|   |   |       └── state_machine.h
|   |   |       └── test/
|   |   |           └── active_object_test.cc
|   |   |           └── state_machine_test.cc
│   │   ├─── bsp/
│   │   |   ├── gpio.c
│   │   |   └── gpio.h
│   │   └── bootloader/
│   │       ├── include/
│   │       |   └──bld_boot.h
│   │       |   └──bld_config.h
│   │       |   └──bld_crc32.h
│   │       |   └──bld_engine.h
│   │       |   └──bld_meta.h
│   │       |   └──bld_protocol.h
│   │       |   └──bld_storage_flash.h
│   │       |   └──bld_storage.h
│   │       |   └──bld_transport_uart_dma.h
│   │       |   └──bld_transport.h
│   │       |   └──stm32l4xx_it.h
│   │       ├── src/
│   │       |   └──bld_boot.c
│   │       |   └──bld_crc32.c
│   │       |   └──bld_engine.c
│   │       |   └──bld_meta.c
│   │       |   └──bld_storage_flash.c
│   │       |   └──bld_transport_uart_dma.c
│   │       |   └──main.cc
│   │       |   └──stm32_hal_msp.c
│   │       |   └──stm32l4xx_it.c
│   │       └── test/
│   │           └──bld_crc32_test.cc
│   │           └──bld_enginetest.cc
│   │           └──bld_meta_test.cc
│   │           └──bld_storage_flashtest.cc
│   │           └──bld_transport_uart_dma_test.cc
│   │           └──test_stubs.cc
│   │           └──test_stubs.h
│   └── startup/
│       └── startup_stm32l475xx.s
├── targets/                    # Platform-specific configuration
│   └── stm32l4xx/
│       ├── config/
│       |   ├── FreeRTOSConfig.h
│       |   └── stm32l4xx_hal_conf.h
│       └── ldscripts/
│           ├── stm32l475vgtx_flash_app.ld
│           └── stm32l475vgtx_flash_bld.ld
|           └── stm32l475vgtx_flash.ld
├── third_party/                # External dependencies (ignored in git)
├── tools/                      # Scripts to flash the program
│   └── flash.py
├── MODULE                      # Bazel MODULE file
├── BUILD.bazel                 # Bazel build rules
└── README.md
```


---

## ✨ Features Implemented
- ✅ BSP for on-board **LEDs** and **button**
- ✅ Bazel build system
- ✅ **FreeRTOS integration** via Pigweed `pw_thread_freertos`
- ✅ **ActiveObject pattern** using Pigweed’s thread and sync primitives
- ✅ Structured **logging** with Pigweed log
- ✅ Integrate **Google Test** for unit testing AO
- ✅ Add **state machine framework** for AO event handling
- ✅ Integrate **GitHub Actions CI** workflow for Bazel
- ✅ Implemented **Bootloader with A/B slot + rollback**

---

## 📖 Design Highlights

### 🟢 Active Objects (AO)
- Each AO runs in its own thread.
- Events are queued and processed sequentially.
- Current AOs:  
  - `LedActiveObject` → controls on-board LEDs  
  - `ButtonActiveObject` → handles push-button input  
- A **WorkQueue thread** is implemented to:
  - Receive and buffer messages coming from **ISRs**  
  - Dispatch those messages to the appropriate Active Object  
  - Safely decouple interrupt context from application logic

### 🟡 BSP (Board Support Package)
- Abstracts GPIO access to LEDs and button.
- Keeps hardware-specific details separate from application logic.

### 🔵 Pigweed Modules
- **pw_log** → structured logging
- **pw_thread_freertos** → FreeRTOS-backed threads
- **pw_sync** → Mutex, ThreadNotification for event delivery
- **pw_containers** → InlineQueue for AO event queues
- **pw_chrono** → Timer for periodic tasks

### 🟠 Bootloader (A/B + Rollback)
- Dual-slot firmware update mechanism (**Slot A / Slot B**)
- Updates written to inactive slot to ensure safety
- Metadata-driven boot control:
  - `active`, `confirmed`, `pending` slots
  - image version, size, CRC32
  - boot attempts tracking
- Automatic rollback:
  - Invalid image or failed boots → slot marked **BAD**
  - Fallback to last **confirmed** slot
- Clean separation of layers:
  - protocol, engine (state machine), storage, transport

### 🧪 Testing Strategy
- Unit tests using **Google Test**
- Hardware-independent testing via **fake storage and transport**
- Coverage includes:
  - bootloader engine (state machine + protocol flow)
  - metadata handling and rollback logic
  - transport layer parsing
  - CRC validation
- Focus on:
  - edge cases
  - failure paths (CRC errors, invalid states)
  - deterministic behavior

---

## ⚡ Build & Run
```bash
# Clone project
git clone https://github.com/karthikeyan-krish/pigweed_play.git
cd pigweed_play

# Build with Bazel
bazel build //apps:application.elf --platforms=//targets/stm32l4xx:platform

# Flash to board
bazel run //tools:flash_application --platforms=//targets/stm32l4xx:platform

# Clang format for C
clang-format -i -style=file:apps/src/bsp/.clang-format ../../*.c ../../*.h

# Clang format for C++
clang-format -i ../../*.cc ../../*.h