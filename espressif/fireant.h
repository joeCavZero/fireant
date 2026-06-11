#ifndef FIREANT_H
#define FIREANT_H

    #include <stddef.h>
    #include <stdint.h>
    #include <stdbool.h>
    #include <stdio.h>

    #include <string.h>

    #include <esp_timer.h>
    #include "esp_http_client.h"
    #include "esp_wifi.h"
    #include "esp_event.h"
    #include "esp_netif.h"
    #include "nvs_flash.h"
    #include "freertos/event_groups.h"

    #include <driver/uart.h>
    
    // ==== SENSORS ====

    typedef struct {
        char *id;
        char *type;
        char *unit;  
    } fireant_sensor_t;

    typedef struct {
        fireant_sensor_t *sensors;
        size_t count;
        size_t capacity;
    } _fireant_sensor_vector_t;

    // ==== NODE ====
    
    typedef struct {
        char *id;
        char *server_ip;
        char *server_port;
        char *server_token;

        char *wifi_ssid;
        char *wifi_password;

        uart_port_t uart_port;
    } fireant_config_t;
    
    void fireant_read_config();

    typedef struct {
        fireant_config_t config;
        _fireant_sensor_vector_t sensors;

        bool connected;
        int64_t last_sync;

        EventGroupHandle_t wifi_event_group;
        int wifi_retry_count;

        SemaphoreHandle_t config_mutex;

        bool wifi_initialized;
        bool wifi_reconnect_required;
    } fireant_node_t;

    // ==== MISC ====

    /// Initializes the node and drivers
    void fireant_init();

    /// Sets the configuration to the global node
    void fireant_config(fireant_config_t config);

    /// Adds a sensor to the sensor vector, then return it's index
    size_t fireant_add_sensor(fireant_sensor_t sensor);

    /// Removes a sensor from the sensor vector by it's index
    bool fireant_remove_sensor(size_t sensor_index);

    void fireant_start();
    void _fireant_send_task(void *pvParameters);
    void _fireant_config_task(void *pvParameters);

    void fireant_send_data();

    void fireant_connect_to_wifi();

    void _fireant_init_uart_driver(uart_port_t uart_port);

    void _fireant_read_line(char *buffer, size_t size);

#endif