# CoinGate ESP32 Firmware

An open-source ESP32 firmware for coin-operated Wi-Fi vending machines, inspired by JuanFi. Integrates with MikroTik hotspot routers to create pay-per-use Wi-Fi access systems.

## Features

- **Coin Acceptor Interface**: GPIO interrupt-driven pulse counting with debouncing
- **Anti-Abuse System**: Rate limiting, cooldown periods, suspicious activity detection
- **Rate Configuration**: Flexible coin-to-time mapping with multiple rate tiers
- **MikroTik API Client**: Full RouterOS API integration for hotspot voucher generation
- **Web Admin Interface**: Complete configuration portal with status monitoring
- **WiFi Manager**: AP/STA mode with automatic failover for easy setup
- **NVS Storage**: Persistent configuration and statistics

## Hardware Requirements

| Component | Specification |
|-----------|--------------|
| MCU | ESP32 (ESP32-WROOM-32 or similar) |
| Coin Acceptor | Universal 12V pulse-type coin acceptor |
| Power Supply | 12V 2A with LM2596 buck to 5V for ESP32 |
| Optional | 16x2 I2C LCD, W5500 Ethernet module |

## Quick Start

```bash
# Set up ESP-IDF environment
. $IDF_PATH/export.sh

# Configure and build
idf.py set-target esp32
idf.py build

# Flash and monitor
idf.py flash monitor
```

## Project Structure

```
coingate/
├── main/
│   ├── main.c              # Application entry point
│   ├── wifi_manager.c/h    # WiFi AP/STA management
│   ├── web_server.c/h      # HTTP server with REST API
│   ├── coin_acceptor.c/h   # GPIO coin pulse detection
│   ├── anti_abuse.c/h      # Rate limiting & security
│   ├── rate_config.c/h     # Coin-to-time rate mapping
│   └── mikrotik_api.c/h    # RouterOS API client
├── web_interface/          # Admin portal (HTML/CSS/JS)
├── partitions.csv           # Flash partition layout
└── sdkconfig.defaults      # Build configuration
```

## Configuration

### Coin Acceptor
- Default GPIO: 4 (configurable)
- Debounce: 50ms
- Pulse width validation: 30-200ms
- Max pulses/minute: 30

### Default Rates
| Coins | Duration | Name |
|-------|----------|------|
| 5 | 1 Hour | 1 Hour |
| 10 | 2 Hours | 2 Hours |
| 20 | 4 Hours | 4 Hours |
| 50 | 12 Hours | 12 Hours |

### MikroTik Setup Required
1. Enable hotspot on RouterOS
2. Create user profiles (e.g., "1h", "2h", "4h") with time limits
3. Enable API access: `/ip service enable api`
4. Configure API user permissions

## API Endpoints

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/status` | GET | System status and statistics |
| `/api/rates` | GET | List configured rates |
| `/api/voucher` | POST | Generate voucher from coins |
| `/api/coin` | GET | Current coin count and duration |
| `/api/config` | GET | System configuration |
| `/api/reset` | POST | Reset session counters |

## Web Interface

Access at `http://<esp32-ip>` or connect to AP mode (`CoinGate_Setup`).

**Default AP Credentials:**
- **SSID:** CoinGate_Setup
- **Password:** CoinGate123

Tabs:
- **Status**: Real-time system info
- **WiFi**: Network configuration
- **MikroTik**: Router connection settings
- **Rates**: Rate plan management
- **Coin**: Acceptor calibration

## Security Features

- Anti-abuse rate limiting (max 30 pulses/minute)
- Pulse spacing validation
- Cooldown periods after suspicious activity
- NVS-encrypted credential storage

### Default Credentials
| Service | Username | Password |
|---------|----------|----------|
| Admin Panel | admin | admin |
| WiFi AP Mode | - | CoinGate123 |

## Development

Built with ESP-IDF v4.4 using:
- FreeRTOS for task scheduling
- ESP32 dual-core support
- SPIFFS for web interface storage
- NVS for persistent configuration

## License

MIT License

## Credits

Inspired by the JuanFi open-source project for coin-operated Wi-Fi systems.
