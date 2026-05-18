# VesuviOven MagmaFlow V1 Firmware

> **Work in progress.** Hardware and firmware are under active development and are not yet complete.

Precision reflow soldering controller for the Ninja Foodi DT200UK, built by MungerWare.
Targets the STM32G0B1 and runs on FreeRTOS.

## What it does

**Thermal control**

Drives three independently switchable heating zones (top quartz, bottom quartz, rear
convection) with configurable ramp rate, target temperature, and hold tolerance.
Two Type-K thermocouples and an oven cavity thermistor are averaged for the control
input; any faulted sensor is automatically excluded. A hard over-temperature limit
de-energises all elements and latches a fault regardless of the active profile.

**Reflow profiles**

Profiles define a sequence of temperature stages (preheat, soak, reflow, cool) with
per-stage ramp rates and hold times. Profiles are stored on the onboard flash and
can be loaded, edited, and triggered remotely.

**Connectivity**

A USB CDC interface exposes a REST API for profile management, live telemetry, and
manual control. A CLI is available on the same port for terminal-based debugging and
direct hardware access.

**Power**

Mains operation is fed by a RAC20 PSU at 24 V / 20 W. When mains is absent, the
board operates as a USB Power Delivery sink and accepts whatever voltage the host
negotiates — enough to run the full logic stack, sensors, fans, and communications
for development and flashing without AC present. As a USB-PD source the board offers
5 V, 9 V, 12 V, and 20 V, all capped at 15 W.

**Storage**

64 MB of NOR flash is managed through a layered filesystem: a partition table in
block 0 defines up to eight named partitions, each independently mountable via
LittleFS. Files carry owner UIDs and Unix-style permission modes; UID 0 (System)
bypasses all checks, while partitions, directories, and files can be marked
read-only or hidden to protect system data from accidental damage via the
API. Reads and writes have both synchronous and asynchronous variants; the
async path accepts any FreeRTOS synchronisation primitive as a completion
signal.

**Cooling**

Dual fan channels: an AC convection fan inside the oven cavity, and a DC board
cooling fan with closed-loop speed control. Both are tachometer-monitored; a stall
or out-of-range speed raises a fault.

## Safety

This device controls mains-voltage heating elements. Over-temperature protection is
enforced in firmware, but the last-resort cutoff is the fuse in the UK mains plug.
Do not defeat it.

The board carries a header for a hardware emergency-stop button — ideally a
very large, extremely red, normally-closed mushroom-head type. The signal is held
low while the button is armed; pressing it breaks the circuit and immediately cuts
power to all AC loads (heaters, fan, and light) at the hardware level, with no
possibility of MCU override. This wiring convention also means a disconnected
or broken cable is treated as a stop rather than a silent bypass. If no
button is fitted, the header must be shorted with a jumper to allow normal
operation.

## Build

**Prerequisites**

- ARM GNU Toolchain (`arm-none-eabi-gcc`)
- CMake 3.22+
- Ninja
- Whisky, large

**Compile**

    mkdir build && cd build
    cmake .. -GNinja
    ninja

## Resources

- Hardware design: https://oshwlab.com/trhosking/reflow-oven
- API documentation: https://munger.github.io/Reflow-Oven/

---

Copyright &copy; 2026 Tim Hosking — MIT Licence
