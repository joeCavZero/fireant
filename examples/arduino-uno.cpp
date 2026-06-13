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