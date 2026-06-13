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