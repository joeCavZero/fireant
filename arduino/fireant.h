#ifndef FIREANT_H
#define FIREANT_H

#include <Arduino.h>
#include <Ethernet.h>

#define FIREANT_MAX_SENSORS 8
#define FIREANT_STR_SIZE 32
#define FIREANT_TOKEN_SIZE 96
#define FIREANT_PORT_SIZE 8

typedef struct {
    char id[FIREANT_STR_SIZE];
    char server_ip[FIREANT_STR_SIZE];
    char server_port[FIREANT_PORT_SIZE];
    char server_token[FIREANT_TOKEN_SIZE];

    bool enable_console;

    unsigned long send_interval_ms;
    unsigned long sync_interval_ms;
} fireant_config_t;

typedef struct {
    char id[FIREANT_STR_SIZE];
    char type[FIREANT_STR_SIZE];
    char unit[FIREANT_STR_SIZE];

    uint8_t pin;
} fireant_adc_sensor_t;

void fireant_config_default(fireant_config_t *config);

bool fireant_config_read_serial(fireant_config_t *config);

bool fireant_global_init(const fireant_config_t *config);

bool fireant_global_add_adc_sensor(
    const char *id,
    const char *type,
    const char *unit,
    uint8_t pin
);

void fireant_global_start(void);

void fireant_global_loop(void);

bool fireant_global_sync(void);

bool fireant_global_send(void);

#endif