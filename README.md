# Remote Input Dongle

A Bluetooth Low Energy (BLE) USB HID dongle that allows remote control of keyboard and mouse input through the [Remote Input App](https://github.com/mradugin/remote-input-app). Built with ESP32-S3 and PlatformIO.

## Table of Contents

- [Overview](#overview)
- [Hardware Requirements](#hardware-requirements)
- [Installation](#installation)
- [Usage](#usage)
- [Technical Details](#technical-details)

## Overview

The Remote Input Dongle acts as a bridge between your mobile device and computer, allowing you to control keyboard and mouse input remotely through a secure BLE connection.

### Key Components

- **BLE Service**: Receives keyboard and mouse events from the host device over BLE
- **USB HID Device**: Acts as a USB keyboard and mouse to send events to the target device
- **Secure Pairing**: BLE PIN-based authentication with physical confirmation
- **Status Indicator**: Visual feedback for connection and pairing states

## Hardware Requirements

### Supported Boards

- **Waveshare ESP32-S3-Zero** (primary target)
- **MHet ESP32 DevKit** (debugging only - no native USB support)

> **Note**: Firmware can be easily adapted to support other ESP32-S3 boards.

### Required Components

- ESP32-S3 microcontroller
- USB connection for HID functionality
- BLE controller
- WS2812 RGB LED (connected to GPIO21)
- Boot button (GPIO0)

## Installation

### Prerequisites

- [PlatformIO Core](https://platformio.org/install/cli) or [PlatformIO IDE](https://platformio.org/install/ide)
- [VS Code](https://code.visualstudio.com/) (recommended)
- [PlatformIO Extension](https://marketplace.visualstudio.com/items?itemName=platformio.platformio-ide)

### Dependencies

- NimBLE-Arduino (BLE stack)
- FastLED (WS2812 LED control)
- Bounce2 (Button debouncing)

### Building and Flashing

1. **Clone the Repository**
   ```bash
   git clone https://github.com/mradugin/remote-input-dongle.git
   cd remote-input-dongle
   ```

2. **Build and Flash**

   **Using CLI:**
   ```bash
   # Build
   pio run -e waveshare_esp32_s3_zero
   
   # Flash
   pio run -e waveshare_esp32_s3_zero -t upload
   ```

   **Using VS Code:**
   1. Open the project in VS Code
   2. Select the target environment from the PlatformIO toolbar
   3. Click the "Upload" button or press `Ctrl+Option+U`

## Usage

### Initial Setup

1. Connect the dongle to USB port of target device
2. The LED will blink blue, indicating it's advertising for connections
3. Use the [Remote Input App](https://github.com/mradugin/remote-input-app) to connect
4. The dongle will appear as "Remote Input XXXX" (where XXXX is the unique serial number)

### Pairing Process

1. When a connection is attempted, the LED will blink yellow
2. **Press the boot button within 30 seconds** to confirm pairing
3. If no button press is detected, pairing will be rejected
4. Once paired, the LED will turn solid blue, indicating readiness

### Sending Input

Follow the instructions in the Remote Input App to capture and forward keyboard and mouse events to the dongle.

## Technical Details

### BLE Services and Characteristics

#### Remote Input Service (UUID: `aa8713fe-6f22-4820-9edd-e8462b0762ea`)

| Characteristic | UUID | Type | Description |
|----------------|------|------|-------------|
| **Keyboard** | `9eeba577-04b8-4dc6-aeef-a9ac12eddb68` | Write-only | Sends individual key events to USB |
| **Mouse** | `9eeba577-04b8-4dc6-aeef-a9ac12eddb69` | Write-only | Sends mouse movement and button events to USB |
| **Status** | `9eeba577-04b8-4dc6-aeef-a9ac12eddb6a` | Read-only | Returns device status (1 = ready) |

> **Note**: All characteristics are authenticated and encrypted for security.
