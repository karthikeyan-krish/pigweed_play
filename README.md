# play-fm 🎛️

[![Build with Bazel](https://img.shields.io/badge/build-Bazel-green)](https://bazel.build)
![FreeRTOS](https://img.shields.io/badge/RTOS-FreeRTOS-blue)
![Pigweed](https://img.shields.io/badge/framework-Pigweed-purple)
![C++17](https://img.shields.io/badge/language-C++17-orange)
![License](https://img.shields.io/badge/license-MIT-lightgrey)

A demo embedded project to showcase modern C++ design, FreeRTOS, and Pigweed integration.

---

## 🚀 Overview
`play-fm` is a small embedded firmware project designed to demonstrate:
- Event-driven architecture using **Active Objects (AO)**
- Threading with **FreeRTOS (via Pigweed)**
- Structured logging with **Pigweed log**
- Hardware control through a simple **BSP (Board Support Package)**

Planned extensions:
- Finite State Machines (FSMs) for robust control logic
- Sensor driver libraries (accelerometer/temperature)
- Remote communication using **gRPC**
- Unit testing with **Google Test**
- Embedded Linux integration using **Yocto**
- GitHub Actions CI workflow for Bazel

---

## 📂 Project Structure

play-fm/
├── apps/ # Application code (Active Objects, state machines)
│   ├── include/
│   |   └── main.h
│   ├── src/
|   |   └── application/
|   |   |   ├── main.cc
|   |   |   ├── stm32l4xx_it.c
|   |   |   ├── stm32l4xx_it.h
|   |   |   └── threads/
|   |   |       └── active_object.h
|   |   └── bsp/
|   |       ├── gpio.c
|   |       └── gpio.h
│   └── startup/
|       └── startup_stm32l475xx.s
├── targets/ # Platform-specific configuration
|   └── stm32l4xx
|       └── config/
|           ├── FreeRTOSConfig.h
|           └── stm32l4xx_hal_conf.h
├── third_party/ # External deps (ignored in git)
├── tools/ # Script to flash the program
|   └── flash.py
├── MODULE # Bazel MODULE file
├── BUILD.bazel # Bazel build rules
└── README.md


---

## ✨ Features Implemented
- ✅ BSP for on-board **LEDs** and **button**
- ✅ Bazel build system
- ✅ **FreeRTOS integration** via Pigweed `pw_thread_freertos`
- ✅ **ActiveObject pattern** using Pigweed’s thread and sync primitives
- ✅ Structured **logging** with Pigweed log

---

## 🛠️ Planned Work
- [ ] Integrate **Google Test** for unit testing AO and BSP layers
- [ ] Add **state machine framework** for AO event handling
- [ ] Add **gRPC service** for remote communication (host ↔ device)
- [ ] Explore **Yocto recipes** to integrate with embedded Linux
- [ ] Integrate **GitHub Actions CI** workflow for Bazel
- [ ] Write a **sensor library** (e.g., I²C or SPI driver demo)

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

---

## ⚡ Build & Run
```bash
# Clone project
git clone https://github.com/karthikeyan-krish/play-fm.git
cd play-fm

# Build with Bazel
bazel build //apps:application.elf --platforms=//targets/stm32l4xx:platform

# Flash to board
bazel run //tools:flash_application --platforms=//targets/stm32l4xx:platform

