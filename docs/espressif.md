# Fireant Library for ESP-IDF

The implementation in [`espressif/`](../espressif/) connects Espressif devices
to the Fireant server over Wi-Fi. The library initializes networking, registers
sensors, reads their values, and performs HTTP requests in FreeRTOS tasks.

The reference example is
[`examples/esp32-xx5r69.c`](../examples/esp32-xx5r69.c).

## Implemented Features

- Wi-Fi station mode;
- initial configuration over serial;
- ADC sensors using the `adc_oneshot` driver;
- mapped ADC values with optional inversion;
- digital sensors with an `active_low` option;
- custom sensors through callbacks;
- periodic metadata synchronization;
- periodic telemetry submission;
- serial console for status and runtime configuration;
- Wi-Fi retries and a 15-second backoff after three HTTP failures.

## Adding It to a Project

You can keep the files in `espressif/` and reference them from the `main`
component. The test project uses this configuration:

```cmake
idf_component_register(
    SRCS
        "main.c"
        "../../espressif/fireant.c"
    INCLUDE_DIRS
        "."
        "../../espressif"
)
```

If you copy `fireant.c` and `fireant.h` into `main`, replace the paths with
`"fireant.c"` and `"."`. The repository test project uses ESP-IDF 6.0.1.

## Basic Flow

The global API is used in four steps:

1. load the default values;
2. fill in or read the configuration;
3. initialize the library and add sensors;
4. start the internal tasks.

```c
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal/adc_types.h"
#include "driver/uart.h"
#include "fireant.h"

void app_main(void) {
    fireant_config_t config;
    fireant_config_default(&config);

    config.console_uart = UART_NUM_0;
    config.enable_console = true;

    fireant_config_read_serial(&config);
    fireant_global_init(&config);

    fireant_global_add_adc_sensor(
        "ldr_1",
        "light",
        "raw",
        ADC_CHANNEL_6
    );

    fireant_global_start();

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
```

`fireant_global_start()` creates the network and console tasks. The application
does not need to call a library loop function.

## Configuration

`fireant_config_default()` clears the structure and sets:

| Field | Default | Description |
| --- | --- | --- |
| `server_port` | `"8000"` | Server HTTP port |
| `console_uart` | `UART_NUM_0` | UART used by the console |
| `send_interval_ms` | `5000` | Interval between telemetry requests |
| `sync_interval_ms` | `5000` | Interval between synchronization requests |
| `enable_console` | `true` | Enables the console task |

The `id`, `server_ip`, `server_token`, `wifi_ssid`, and `wifi_password` fields
must be filled before initialization. They can be assigned in code:

```c
fireant_config_t config;
fireant_config_default(&config);

snprintf(config.id, sizeof(config.id), "esp32_1");
snprintf(config.server_ip, sizeof(config.server_ip), "192.168.0.100");
snprintf(config.server_port, sizeof(config.server_port), "8000");
snprintf(config.server_token, sizeof(config.server_token), "DEVICE_TOKEN");
snprintf(config.wifi_ssid, sizeof(config.wifi_ssid), "MyNetwork");
snprintf(config.wifi_password, sizeof(config.wifi_password), "MyPassword");
```

Or read interactively:

```c
fireant_config_read_serial(&config);
```

This function blocks while requesting `id`, `server_ip`, `server_port`,
`server_token`, `wifi_ssid`, and `wifi_password`, in that order. Values are not
automatically persisted to NVS.

## Registering Sensors

Each sensor has:

- `id`: an identifier unique within the node;
- `type`: a category used by filters, such as `light` or `temperature`;
- `unit`: the stored unit, such as `raw`, `lx`, or `celsius`.

### ADC

```c
size_t index = fireant_global_add_adc_sensor(
    "ldr_1",
    "light",
    "raw",
    ADC_CHANNEL_6
);
```

The library uses `ADC_UNIT_1`, the default bit width, and 12 dB attenuation. The
raw ADC reading is converted to `float` before submission.

### Mapped ADC

```c
fireant_global_add_adc_sensor_mapped(
    "ldr_percent",
    "light",
    "%",
    ADC_CHANNEL_6,
    0.0f,
    4095.0f,
    0.0f,
    100.0f,
    false
);
```

The raw reading is clamped to `[raw_min, raw_max]`, mapped to
`[out_min, out_max]`, and optionally inverted within the output range. The
repository example uses this form for the LDR.

### Digital

```c
fireant_global_add_digital_sensor(
    "door_1",
    "door",
    "boolean",
    GPIO_NUM_27,
    true
);
```

With `active_low = true`, the input uses a pull-up and the logic level is
inverted before submission. With `false`, the input uses a pull-down.

### Custom Callback

```c
static float read_temperature(void *ctx) {
    // Read the hardware referenced by ctx.
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

The network task invokes the callback before building each telemetry payload.
The callback should return quickly and avoid long blocking operations.

Sensor registration functions return the sensor index. On failure, they return
`(size_t)-1`.

## Runtime Console

When `enable_console` is enabled, the UART runs at 115200 baud and accepts:

| Command | Effect |
| --- | --- |
| `status` | Shows IP, Wi-Fi state, configuration mode, and sensor count |
| `config` or `cfg` | Pauses HTTP requests |
| `exit` or `run` | Resumes HTTP requests |
| `server_ip=...` | Updates the server address |
| `server_port=...` | Updates the server port |
| `server_token=...` | Updates the device token |
| `wifi_ssid=...` | Updates the SSID and reconnects |
| `wifi_password=...` | Updates the password and reconnects |

Changes remain in memory only until the device restarts. After changing Wi-Fi
settings through the console, use `exit` to resume HTTP traffic.

## Instance API

In addition to the `fireant_global_*` functions, the library exposes functions
that receive a `fireant_node_t *`:

```c
fireant_node_t *node = fireant_global_node();

fireant_node_add_digital_sensor(
    node,
    "button_1",
    "button",
    "boolean",
    GPIO_NUM_25,
    true
);
```

The type is opaque, so the global instance returned by
`fireant_global_node()` is the available way to access it outside
`fireant.c`.

## Server Communication

Synchronization submits metadata to `/api/sync`; telemetry is submitted to
`/api/receive`. Both use HTTP, JSON, and this header:

```http
Authorization: Bearer DEVICE_TOKEN
```

The HTTP URL uses `config.server_port`. The `port` field stored for the node
during synchronization is currently fixed to `"8000"` by
`FIREANT_DEFAULT_NODE_PORT`, so changing `config.server_port` changes where the
firmware sends requests but does not change the advertised node port in the sync
payload. Complete payloads and validation rules are documented in
[API](./api.md).

## Limits

- identifier: up to 31 characters;
- type: up to 31 characters;
- unit: up to 15 characters;
- server host: up to 63 characters;
- token: up to 127 characters;
- SSID: up to 31 characters;
- Wi-Fi password: up to 63 characters;
- HTTP payload: fixed 4096-byte buffer;
- current transport: HTTP without TLS.

The sensor array grows dynamically. Its practical limits are available memory
and the JSON buffer size.
