# CoinGate ESP32 Firmware - Project Status

## Overview
This repository contains the initial implementation of the CoinGate ESP32 firmware, designed to be a clone of the JuanFi system for coin-operated Wi-Fi vending machines.

## Current Implementation
The firmware includes the following components:

1. **WiFi Manager** (`main/wifi_manager.[ch]`):
   - Handles both Station (STA) and Access Point (AP) modes
   - Loads/saves WiFi credentials to NVS
   - Automatically starts in AP mode if no credentials are found
   - Provides connection status checking

2. **Web Server** (`main/web_server.[ch]`):
   - HTTP server serving files from SPIFFS
   - Serves the configuration interface at root URI ("/")
   - Includes a test POST endpoint ("/test")
   - Handles file type detection for proper Content-Type headers

3. **Main Application** (`main/main.c`):
   - Initializes NVS
   - Starts WiFi manager
   - Starts web server
   - Main loop with delay

4. **Web Interface** (`web_interface/`):
   - Simple HTML form for WiFi configuration
   - JavaScript to handle form submission via AJAX
   - Basic styling (to be enhanced)

5. **Configuration**:
   - `partitions.csv`: Defines NVS, OTA, and SPIFFS partitions
   - `sdkconfig.defaults`: Default SDK configuration options

## Known Issues
The primary issue preventing compilation is related to the Python environment for ESP-IDF:
- The `pkg_resources` module is not available in the Python environment
- This appears to be a compatibility issue with the system Python and ESP-IDF's requirements
- Various attempts to resolve this (virtual environments, package installations) have not succeeded in this environment

## Next Steps
To continue development, the following steps are recommended:

1. **Resolve ESP-IDF Python Environment**:
   - Use a fresh Ubuntu/Debian system with Python 3.8 or 3.9 (as recommended by ESP-IDF)
   - Follow the official ESP-IDF getting started guide precisely
   - Consider using the ESP-IDF installation script with the `--enable-python-env` option

2. **Implement Missing Features**:
   - Coin acceptor interface with GPIO interrupt handling
   - Debouncing mechanism for coin pulses
   - Rate configuration and mapping system
   - MikroTik API client for voucher generation
   - LCD display support (I2C 16x2)
   - Anti-abuse system for coin acceptor
   - Complete web admin interface with all configuration pages
   - OTA update functionality
   - System monitoring and reporting features

3. **Hardware Integration**:
   - Connect universal coin acceptor to ESP32 GPIO
   - Optional: Connect 16x2 LCD via I2C
   - Power supply considerations (12V to 5V regulation)

4. **Testing and Validation**:
   - Test WiFi connection and web interface
   - Validate coin acceptor pulse counting
   - Test MikroTik API communication
   - Verify LCD display output
   - Test complete voucher generation flow

## Building the Project
Once the Python environment is resolved, the project can be built with:

```bash
# Set up ESP-IDF environment
. $IDF_PATH/export.sh

# Set target
idf.py set-target esp32

# Build, flash, and monitor
idf.py build flash monitor
```

## License
This project is open source and available under the MIT License.