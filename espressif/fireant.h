#ifndef FIREANT_H
#define FIREANT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"

#define FIREANT_MAX_ID_LEN 32
#define FIREANT_MAX_TYPE_LEN 32
#define FIREANT_MAX_UNIT_LEN 16
#define FIREANT_MAX_HOST_LEN 64
#define FIREANT_MAX_PORT_LEN 8
#define FIREANT_MAX_TOKEN_LEN 128
#define FIREANT_MAX_WIFI_SSID_LEN 32
#define FIREANT_MAX_WIFI_PASS_LEN 64
#define FIREANT_MAX_IP_LEN 16

#define FIREANT_DEFAULT_NODE_PORT "8000"
#define FIREANT_UART_LINE_SIZE 256
#define FIREANT_HTTP_BUFFER_SIZE 4096
#define FIREANT_SYNC_INTERVAL_MS 30000
#define FIREANT_SEND_INTERVAL_MS 5000
#define FIREANT_HTTP_FAIL_BACKOFF_MS 15000

typedef enum {
    FIREANT_SENSOR_ANALOG_ADC,
    FIREANT_SENSOR_DIGITAL,
    FIREANT_SENSOR_CUSTOM
} fireant_sensor_kind_t;

typedef float (*fireant_sensor_reader_t)(void *ctx);

typedef struct {
    char id[FIREANT_MAX_ID_LEN];
    char type[FIREANT_MAX_TYPE_LEN];
    char unit[FIREANT_MAX_UNIT_LEN];

    fireant_sensor_kind_t kind;

    union {
        struct {
            adc_channel_t channel;
        } analog_adc;

        struct {
            gpio_num_t pin;
            bool active_low;
        } digital;

        struct {
            fireant_sensor_reader_t read;
            void *ctx;
        } custom;
    } input;

    float value;
} fireant_sensor_t;

typedef struct {
    char id[FIREANT_MAX_ID_LEN];

    char server_ip[FIREANT_MAX_HOST_LEN];
    char server_port[FIREANT_MAX_PORT_LEN];
    char server_token[FIREANT_MAX_TOKEN_LEN];

    char wifi_ssid[FIREANT_MAX_WIFI_SSID_LEN];
    char wifi_password[FIREANT_MAX_WIFI_PASS_LEN];

    uart_port_t console_uart;

    uint32_t send_interval_ms;
    uint32_t sync_interval_ms;
    bool enable_console;
} fireant_config_t;

typedef struct fireant_node fireant_node_t;

void fireant_config_default(fireant_config_t *config);

void fireant_global_init(const fireant_config_t *config);
void fireant_global_start(void);

size_t fireant_global_add_adc_sensor(
    const char *id,
    const char *type,
    const char *unit,
    adc_channel_t channel
);

size_t fireant_global_add_digital_sensor(
    const char *id,
    const char *type,
    const char *unit,
    gpio_num_t pin,
    bool active_low
);

size_t fireant_global_add_custom_sensor(
    const char *id,
    const char *type,
    const char *unit,
    fireant_sensor_reader_t read,
    void *ctx
);

void fireant_global_enter_config_mode(void);
void fireant_global_exit_config_mode(void);

fireant_node_t *fireant_global_node(void);

void fireant_node_init(fireant_node_t *node, const fireant_config_t *config);
void fireant_node_start(fireant_node_t *node);

size_t fireant_node_add_adc_sensor(
    fireant_node_t *node,
    const char *id,
    const char *type,
    const char *unit,
    adc_channel_t channel
);

size_t fireant_node_add_digital_sensor(
    fireant_node_t *node,
    const char *id,
    const char *type,
    const char *unit,
    gpio_num_t pin,
    bool active_low
);

size_t fireant_node_add_custom_sensor(
    fireant_node_t *node,
    const char *id,
    const char *type,
    const char *unit,
    fireant_sensor_reader_t read,
    void *ctx
);

void fireant_node_enter_config_mode(fireant_node_t *node);
void fireant_node_exit_config_mode(fireant_node_t *node);

#endif