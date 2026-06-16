# Fireant Library for Arduino

The implementation in [`arduino/`](../arduino/) connects an Arduino to the
Fireant server using the Arduino `Ethernet` library. The current code is written
for Arduino-compatible boards with an Ethernet shield or module and supports
ADC, mapped ADC, digital, and custom callback sensors.

The reference example is
[`examples/arduino-uno.cpp`](../examples/arduino-uno.cpp).

Note: the Arduino Uno path has not been fully validated end to end on physical
hardware in this revision because the available Uno did not have a network
module attached. The library and example reflect the intended Ethernet-based
integration.

## Implemented Features

- Ethernet networking with DHCP;
- fallback IPv4 configuration if DHCP fails;
- interactive serial configuration;
- up to 8 sensors;
- periodic metadata synchronization;
- readings through `analogRead()`, `digitalRead()`, and callback functions;
- mapped ADC values with optional inversion;
- configurable MAC address and static IPv4 fallback;
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
upload_speed = 115200

lib_deps =
    arduino-libraries/Ethernet
```

Copy `arduino/fireant.h` and `arduino/fireant.cpp` to `lib/fireant/` in the
project, or add the repository folder to your include and source paths.

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

The repository example uses `fireant_global_add_adc_sensor_mapped()` to submit
the LDR reading as a percentage:

```cpp
fireant_global_add_adc_sensor_mapped(
    "ldr",
    "light",
    "%",
    A0,
    0.0f,
    1023.0f,
    0.0f,
    100.0f,
    false
);
```

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
| `mac` | `DE:AD:BE:EF:FE:ED` | MAC address passed to Ethernet |
| `use_static_ip` | `false` | Forces static network configuration when true |
| `static_ip` | `192.168.0.177` | Static IP and DHCP fallback address |
| `dns` | `8.8.8.8` | DNS server for static/fallback setup |
| `gateway` | `192.168.0.1` | Gateway for static/fallback setup |
| `subnet` | `255.255.255.0` | Subnet mask for static/fallback setup |

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

config.use_static_ip = true;
config.static_ip = IPAddress(192, 168, 0, 177);
config.gateway = IPAddress(192, 168, 0, 1);
config.subnet = IPAddress(255, 255, 255, 0);
```

Or read from serial:

```cpp
fireant_config_read_serial(&config);
```

Serial configuration requests `id`, `server_ip`, `server_port`, and
`server_token`. It blocks `setup()` until all lines are received and does not
persist values to EEPROM.

## Network Initialization

`fireant_global_init()` copies the configuration and starts Ethernet. If
`use_static_ip` is `true`, it immediately uses the configured static network
values. Otherwise it attempts DHCP first and falls back to the configured static
values if DHCP fails.

The defaults are:

| Parameter | Value |
| --- | --- |
| IP | `192.168.0.177` |
| DNS | `8.8.8.8` |
| Gateway | `192.168.0.1` |
| Subnet mask | `255.255.255.0` |

On a different network, set the fields in `fireant_config_t` before calling
`fireant_global_init()`.

## Registering Sensors

Each sensor has:

- `id`: sensor identifier unique within the node;
- `type`: category used by server filters;
- `unit`: reading unit.

### ADC

```cpp
bool added = fireant_global_add_adc_sensor(
    "ldr_1",
    "light",
    "raw",
    A0
);
```

Parameters:

- `pin`: pin passed to `analogRead()`.

The function returns `false` when all 8 sensor slots are occupied. The ADC value
is submitted as a JSON number and converted to `float` by the server.

### Mapped ADC

```cpp
fireant_global_add_adc_sensor_mapped(
    "ldr_percent",
    "light",
    "%",
    A0,
    0.0f,
    1023.0f,
    0.0f,
    100.0f,
    false
);
```

The raw reading is clamped to `[raw_min, raw_max]`, mapped to
`[out_min, out_max]`, and optionally inverted within the output range. On ESP32
or ESP8266 Arduino builds, the default unmapped range used by
`fireant_global_add_adc_sensor()` is `0..4095`; on AVR boards such as Uno it is
`0..1023`.

### Digital

```cpp
fireant_global_add_digital_sensor(
    "button_1",
    "button",
    "boolean",
    2,
    true
);
```

With `active_low = true`, the pin is configured with `INPUT_PULLUP` and the
read value is inverted before submission. With `false`, the pin uses `INPUT`.
Digital readings are submitted as `0.000` or `1.000`.

### Custom Callback

```cpp
static float read_temperature(void *ctx) {
    return 24.5f;
}

fireant_global_add_custom_sensor(
    "temp_1",
    "temperature",
    "celsius",
    read_temperature,
    NULL
);
```

The callback is invoked immediately before each telemetry payload is built. It
should return quickly because all network and sensor work runs synchronously
from `fireant_global_loop()`.

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

The return value indicates whether the server IP was valid and the TCP
connection was opened. The current Arduino implementation prints the HTTP
response when `enable_console` is true, but it does not parse the status code
when determining success.

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
- `id`, sensor identifiers, and types use 32-byte buffers;
- `server_ip` uses a 64-byte buffer;
- sensor units use a 16-byte buffer;
- the port uses an 8-byte buffer;
- the token uses a 128-byte buffer;
- only literal IPv4 addresses are accepted by `IPAddress.fromString()`;
- ADC, mapped ADC, digital, and custom callback sensors are implemented;
- the MAC address and fallback/static network values are configurable through
  `fireant_config_t`;
- HTTP transport without TLS;
- JSON is assembled with `String`, which should be considered on boards with
  limited SRAM.
