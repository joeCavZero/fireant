#include <Arduino.h>
#include <fireant.h>

void setup() {
    Serial.begin(115200);
    delay(2000);

    fireant_config_t config;
    fireant_config_default(&config);

    config.enable_console = true;
    config.send_interval_ms = 5000;
    config.sync_interval_ms = 5000;

    fireant_config_read_serial(&config);

    fireant_global_init(&config);

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

    fireant_global_start();
}

void loop() {
    fireant_global_loop();
}