# Genericgame
# 🕹️ ESP32 2D Game Console

![C++](https://img.shields.io/badge/Language-C++-blue)
![Platform](https://img.shields.io/badge/Platform-ESP32-orange)
![Framework](https://img.shields.io/badge/Framework-Arduino%20%2F%20FreeRTOS-lightgrey)

A custom-built, portable 2D game console engineered entirely from scratch using an ESP32-WROOM-32 microcontroller. Rather than relying on a heavy operating system, this project features a bare-metal custom game engine, real-time dual-core multithreading, and a hardware-level I2S audio synthesizer.

This repository contains the complete software stack (`main-ver2`), including the "OS" boot menu and two fully playable games.

---

## ✨ System Features
* **Dual-Core Architecture (FreeRTOS):** The main game engine, physics, and SPI rendering run at a stable 30 FPS on Core 1, while a custom 8-bit audio synthesizer runs as a pinned background task on Core 0 utilizing Direct Memory Access (DMA).
* **"Ghost Erasure" Rendering:** A highly optimized rendering pipeline that bypasses the SPI bottleneck by only redrawing specific modified bounding boxes instead of full-screen frame buffers.
* **Deterministic Memory Management:** Utilizes strict Object Pooling for dynamic entities (enemies, projectiles) to completely eliminate dynamic heap allocation (`malloc`/`new`), preventing fragmentation and crashes in the ESP32's limited 520KB SRAM.
* **Master State Machine:** A unified OS-level boot menu allowing seamless switching between multiple loaded games without memory overlap.

---

## 🎮 The Games

### 1. Action RPG (Arena Survival)
A fast-paced, multi-stage arena combat game. 
* **Mechanics:** 4-way movement, double-tap to dash, melee combo system, and ranged shooting.
* **Enemies:** Features 3 distinct Finite State Machine (FSM) AI types: Swarmers (Bats), ranged kiters (Shooters), and heavy AoE dashers (Brutes). 
* **Progression:** Wave-based spawning system using pre-configured `Stages.h` data.

### 2. T-Rex Runner (Dino Clone)
A highly optimized, infinite runner clone designed to test hardware inputs, gravity physics, and high-speed parallax background scrolling.

---

## 🛠️ Hardware Requirements (BOM)
* **Microcontroller:** ESP32-WROOM-32 Development Board
* **Display:** 1.8-inch TFT LCD (ST7735 Driver, 128x160 resolution)
* **Audio:** MAX98357A I2S Class D Amplifier
* **Speaker:** 3W 8Ω Mini Full-Range Speaker
* **Inputs:** 6x Tactile Push Buttons (D-Pad, A, B)
* **Power:** 5V USB / Breadboard Power Supply

### Pin Wiring Mapping
| Peripheral | ESP32 Pin | Note |
| :--- | :--- | :--- |
| **TFT Display** | Standard SPI | Set `MOSI`, `MISO`, `SCK`, `CS`, `DC`, `RST` in `User_Setup.h` |
| **Audio I2S (DOUT)** | `GPIO 25` | Digital Audio Data |
| **Audio I2S (BCLK)** | `GPIO 26` | Bit Clock |
| **Audio I2S (LRC)** | `GPIO 27` | Left/Right Word Clock |
| **D-Pad UP** | `GPIO 13` | Input Pullup |
| **D-Pad DOWN** | `GPIO 14` | Input Pullup |
| **D-Pad LEFT** | `GPIO 33` | Input Pullup |
| **D-Pad RIGHT** | `GPIO 32` | Input Pullup |
| **Button A** | `GPIO 16` | Jump / Melee / Select |
| **Button B** | `GPIO 17` | Shoot / Back / Exit |

*Note: Keep I2S audio wires (25, 26, 27) under 10cm to prevent electromagnetic crosstalk with the SPI clock.*

---

## 🚀 Installation & Setup

1. **Environment:** Install [Visual Studio Code](https://code.visualstudio.com/) and the [PlatformIO](https://platformio.org/) extension.
2. **Clone the Repo:**
   ```bash
   git clone [https://github.com/your-username/your-repo-name.git](https://github.com/your-username/your-repo-name.git)
