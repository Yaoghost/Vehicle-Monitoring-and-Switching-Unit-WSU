# Vehicle-Monitoring-and-Switching-Unit-WSU
This repository contains the **STM32 firmware** for the *Monitoring and Switching Unit for Pre-OBD II Vehicles*.  
The STM32 microcontroller acts as the bridge between the vehicle’s analog sensors and the user interface (Nextion 2.4″ display).  

---

## 📌 Project Overview
Older vehicles lack modern instrumentation and rely on outdated gauges or “dummy lights.”  
This project provides a compact module that:
- Reads signals from fuel, oil pressure, and coolant temperature sensors.
- Processes analog data through ADCs on the STM32.
- Communicates results to a Nextion display for a clean, modern UI.
- Supports future features like MPG calculation, trip logging, and accessory switching.

This repo specifically covers the **STM32 codebase**.

---

## 🛠 Hardware
- **STM32 Nucleo-F401RE** (ARM Cortex-M4, 84 MHz, 512 KB Flash, 96 KB SRAM)  
- **Nextion Display 2.4″ (Enhanced recommended)** – UART interface for UI  
- **Sensors**:
  - 680015 Temperature Sensor (coolant simulation via hotplate/fluid rig)
  - 501460 Oil Pressure Sensor (tested with hydrostatic rig)
  - Potentiometer simulating fuel sender
- **Support Components**:
  - ADC signal conditioning circuitry

---

## 📂 Repository Structure
