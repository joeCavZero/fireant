#ifndef FIREANT_H
#define FIREANT_H

#include <Arduino.h>
#include <Ethernet.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FIREANT_MAX_SENSORS 8

#define FIREANT_MAX_ID_LEN 32
#define FIREANT_MAX_TYPE_LEN 32
#define FIREANT_MAX_UNIT_LEN 16
#define FIREANT_MAX_HOST_LEN 64
#define FIREANT_MAX_PORT_LEN 8
#define FIREANT_MAX_TOKEN_LEN 128
#define FIREANT_MAX_IP_LEN 16

#define FIREANT_STR_SIZE FIREANT_MAX_ID_LEN
#define FIREANT_TOKEN_SIZE FIREANT_MAX_TOKEN_LEN
#define FIREANT_PORT_SIZE FIREANT_MAX_PORT_LEN

#define FIREANT_DEFAULT_NODE_PORT "8000"
#define FIREANT_DEFAULT_SYNC_INTERVAL_MS 5000UL
#define FIREANT_DEFAULT_SEND_INTERVAL_MS 5000UL
#define FIREANT_HTTP_TIMEOUT_MS 3000UL

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
            uint8_t pin;

            float raw_min;
            float raw_max;

            float out_min;
            float out_max;

            bool invert;
            bool mapped;
        } analog_adc;

        struct {
            uint8_t pin;
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

    bool enable_console;

    unsigned long send_interval_ms;
    unsigned long sync_interval_ms;

    byte mac[6];

    bool use_static_ip;
    IPAddress static_ip;
    IPAddress dns;
    IPAddress gateway;
    IPAddress subnet;
} fireant_config_t;

void fireant_config_default(fireant_config_t *config);

bool fireant_config_read_serial(fireant_config_t *config);

bool fireant_global_init(const fireant_config_t *config);

bool fireant_global_add_adc_sensor(
    const char *id,
    const char *type,
    const char *unit,
    uint8_t pin
);

bool fireant_global_add_adc_sensor_mapped(
    const char *id,
    const char *type,
    const char *unit,
    uint8_t pin,
    float raw_min,
    float raw_max,
    float out_min,
    float out_max,
    bool invert
);

bool fireant_global_add_digital_sensor(
    const char *id,
    const char *type,
    const char *unit,
    uint8_t pin,
    bool active_low
);

bool fireant_global_add_custom_sensor(
    const char *id,
    const char *type,
    const char *unit,
    fireant_sensor_reader_t read,
    void *ctx
);

void fireant_global_start(void);

void fireant_global_loop(void);

bool fireant_global_sync(void);

bool fireant_global_send(void);

#endif