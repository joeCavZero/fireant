# Fireant Library for Arduino

The implementation in [`arduino/`](../arduino/) connects an Arduino to the
Fireant server using the `Ethernet` library. The current version targets Arduino
Uno, a compatible Ethernet shield or module, and analog sensors.

The working example is
[`examples/arduino-uno.cpp`](../examples/arduino-uno.cpp).

## Implemented Features

- Ethernet networking with DHCP;
- fallback IPv4 configuration if DHCP fails;
- interactive serial configuration;
- up to 8 ADC sensors;
- periodic metadata synchronization;
- readings through `analogRead()` and periodic telemetry submission;
- manual synchronization and submission functions.

## Dependencies and Hardware

- Arduino framework;
- `Ethernet` library;
- Arduino Uno or a compatible board;
- Ethernet shield or module;
- SPI configured for the selected hardware.

On the standard Uno Ethernet shield, chip select is normally pin 10, which is
the `Ethernet` library default. For another module or CS pin, initialize it
before Fireant:

```cpp
Ethernet.init(10);
```

With PlatformIO, declare the dependency as follows:

```ini
[env:uno]
platform = atmelavr
board = uno
framework = arduino

monitor_speed = 115200

lib_deps =
    arduino-libraries/Ethernet
```

Copy `arduino/fireant.h` and `arduino/fireant.cpp` to `lib/fireant/` in the
project.

## Complete Example

```cpp
#include <Arduino.h>
#include <fireant.h>

void setup() {
    Serial.begin(115200);
    delay(2000);

    fireant_config_t config;
    fireant_config_default(&config);

    fireant_config_read_serial(&config);
    fireant_global_init(&config);

    fireant_global_add_adc_sensor(
        "ldr_1",
        "light",
        "raw",
        A0
    );

    fireant_global_start();
}

void loop() {
    fireant_global_loop();
}
```

`fireant_global_loop()` must run continuously. It checks intervals with
`millis()` and triggers synchronization and telemetry submissions.

## Configuration

`fireant_config_default()` sets:

| Field | Default | Description |
| --- | --- | --- |
| `id` | `"arduino_uno"` | Node identifier |
| `server_ip` | `"192.168.0.100"` | Server IPv4 address |
| `server_port` | `"8000"` | Server HTTP port |
| `server_token` | empty | Token created on the server |
| `enable_console` | `true` | Prints logs and HTTP responses |
| `send_interval_ms` | `5000` | Telemetry interval |
| `sync_interval_ms` | `5000` | Synchronization interval |

Fields can be overridden in code:

```cpp
fireant_config_t config;
fireant_config_default(&config);

strncpy(config.id, "arduino_room", sizeof(config.id) - 1);
strncpy(config.server_ip, "192.168.0.50", sizeof(config.server_ip) - 1);
strncpy(config.server_token, "DEVICE_TOKEN",
        sizeof(config.server_token) - 1);

config.send_interval_ms = 10000;
config.sync_interval_ms = 60000;
```

Or read from serial:

```cpp
fireant_config_read_serial(&config);
```

Serial configuration requests `id`, `server_ip`, `server_port`, and
`server_token`. It blocks `setup()` until all lines are received and does not
persist values to EEPROM.

## Network Initialization

`fireant_global_init()` attempts to obtain an address through DHCP using the
fixed MAC address `DE:AD:BE:EF:FE:ED`.

If DHCP fails, the current implementation uses:

| Parameter | Value |
| --- | --- |
| IP | `192.168.0.177` |
| DNS | `8.8.8.8` |
| Gateway | `192.168.0.1` |
| Subnet mask | `255.255.255.0` |

These values and the MAC address are defined in `arduino/fireant.cpp`. On a
different network, update the library or ensure DHCP is available.

## Registering ADC Sensors

```cpp
bool added = fireant_global_add_adc_sensor(
    "ldr_1",
    "light",
    "raw",
    A0
);
```

Parameters:

- `id`: sensor identifier unique within the node;
- `type`: category used by the server;
- `unit`: reading unit;
- `pin`: pin passed to `analogRead()`.

The function returns `false` when all 8 sensor slots are occupied. The ADC value
is submitted as a JSON number and converted to `float` by the server.

## Starting and Running

`fireant_global_start()` enables processing, records the initial time, and
performs an immediate synchronization.

Keep this call in `loop()`:

```cpp
void loop() {
    fireant_global_loop();
}
```

Avoid blocking longer than the configured intervals because all networking runs
synchronously in the same flow. Each request waits for server data for up to
approximately 3 seconds.

Operations can also be triggered manually:

```cpp
bool sync_ok = fireant_global_sync();
bool send_ok = fireant_global_send();
```

The return value indicates whether the IP was valid and the TCP connection was
opened. The current implementation does not inspect the HTTP status code when
determining success.

## Server Communication

The library submits:

- metadata to `POST /api/sync`;
- readings to `POST /api/receive`.

Both requests use:

```http
Authorization: Bearer DEVICE_TOKEN
Content-Type: application/json
```

The same token is also included in the synchronization body. See
[API](./api.md) for the actual JSON payloads and responses.

## Current Limits and Behavior

- maximum of 8 sensors;
- `id`, `server_ip`, sensor identifiers, types, and units use 32-byte buffers;
- the port uses an 8-byte buffer;
- the token uses a 96-byte buffer;
- only literal IPv4 addresses are accepted by `IPAddress.fromString()`;
- only ADC sensors are implemented;
- the MAC address and fallback network are fixed in the source;
- HTTP transport without TLS;
- JSON is assembled with `String`, which should be considered on boards with
  limited SRAM.
