# VesuviOven MagmaFlow V1 Firmware

This firmware provides precision reflow capabilities for the Ninja Foodi DT200UK, targeting the STM32G0 microcontroller. Developed by MungerWare, it focuses on high-fidelity thermal telemetry and robust power delivery.

## ⚡ Hardware & Power Architecture

The MagmaFlow V1 platform is designed for both high-power operation and safe, low-power development.

* **Dual-Mode USB-PD:** Features a fully negotiated USB Power Delivery stack. 
    * **Mains Mode:** Drives high-current AC loads via phase-angle or ON/OFF control.
    * **Debug Mode:** Provides up to 20V @ 15W via USB-PD, allowing for full logic and peripheral testing/flashing from a PC without AC mains present.
* **Thermal Interface:**
    * Dual Type-K Thermocouples via MAX31856 universal digitizers.
    * External Cold-Junction Compensation (CJC) via board-level NTCs to eliminate internal sensor self-heating errors.
* **Closed-Loop Cooling:** Dual tacho-monitored fan channels (Convection & Board Cooling) for active thermal management and safety interlocks.
* **Storage:** 64MB Serial Flash for persistent storage of reflow profiles, calibration constants, and detailed telemetry logging.

## 🚀 Software Design

The firmware uses an event-driven architecture to ensure safety and responsiveness:

* **RTOS:** FreeRTOS-based task management.
* **Concurrency:** Uses Direct-to-Task notifications for low-latency handling of system events.
* **Communication Protocol:** * **RESTful USB Interface:** The USB CDC stack implements a REST-based protocol, allowing for seamless integration with web-based dashboards via Web Serial.
    * **CLI Mode:** A comprehensive Command Line Interface is provided for local terminal debugging and manual hardware control.
* **FS & Config:** Uses littlefs for robust flash wear-levelling and cJSON for profile parsing.

## 📂 Project Structure

* Core/: Application logic, thermal PID loops, and task handlers.
* USB_Device/: Custom USB-PD and CDC class implementation logic.
* Middlewares/Third_Party/: Localised, vendor-independent versions of cJSON and littlefs.
* cmake/stm32cubemx/: STM32 HAL/LL peripheral initialisation.

## 🔨 Build Instructions

### Prerequisites
* ARM GNU Toolchain (arm-none-eabi-gcc)
* CMake 3.22+
* Ninja or Make build generator

### Compilation
mkdir build && cd build
cmake .. -GNinja
ninja

## 🔗 Project Resources
* Hardware Design: https://oshwlab.com/trhosking/reflow-oven

---

Copyright © 2026 Tim Hosking

Licensed under the MIT License.

SAFETY WARNING: This device controls mains-voltage elements. A physical hardware-level thermal cutoff (thermal fuse) must be installed to prevent fire in the event of software or SSR failure.