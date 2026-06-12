#include "fireant.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"

#include "esp_check.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#include "driver/uart.h"
#include "driver/gpio.h"

#define TAG "FIREANT"
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1
#define STATE_CONFIG_MODE_BIT BIT2
#define STATE_STOP_HTTP_BIT BIT3
#define MAX_WIFI_RETRY 5

#define SENSOR_INITIAL_CAPACITY 4

struct fireant_node {
    fireant_config_t config;

    char ip[FIREANT_MAX_IP_LEN];
    fireant_sensor_t *sensors;
    size_t sensor_count;
    size_t sensor_capacity;

    SemaphoreHandle_t lock;
    EventGroupHandle_t events;

    bool started;
    bool wifi_started;
    int wifi_retry_count;

    adc_oneshot_unit_handle_t adc1_handle;
    bool adc1_ready;

    int64_t last_send_ms;
    int64_t last_sync_ms;
    int http_fail_count;
};

static fireant_node_t g_node;

static int64_t now_ms(void) {
    return esp_timer_get_time() / 1000;
}

static void copy_text(char *dst, size_t dst_size, const char *src) {
    if (!dst || dst_size == 0) return;
    if (!src) src = "";
    snprintf(dst, dst_size, "%s", src);
}

void fireant_config_default(fireant_config_t *config) {
    if (!config) return;
    memset(config, 0, sizeof(*config));
    copy_text(config->server_port, sizeof(config->server_port), "8000");
    config->console_uart = UART_NUM_0;
    config->send_interval_ms = FIREANT_SEND_INTERVAL_MS;
    config->sync_interval_ms = FIREANT_SYNC_INTERVAL_MS;
    config->enable_console = true;
}

fireant_node_t *fireant_global_node(void) {
    return &g_node;
}

void fireant_global_init(const fireant_config_t *config) {
    fireant_node_init(&g_node, config);
}

void fireant_global_start(void) {
    fireant_node_start(&g_node);
}

void fireant_global_enter_config_mode(void) {
    fireant_node_enter_config_mode(&g_node);
}

void fireant_global_exit_config_mode(void) {
    fireant_node_exit_config_mode(&g_node);
}

size_t fireant_global_add_adc_sensor(const char *id, const char *type, const char *unit, adc_channel_t channel) {
    return fireant_node_add_adc_sensor(&g_node, id, type, unit, channel);
}

size_t fireant_global_add_digital_sensor(const char *id, const char *type, const char *unit, gpio_num_t pin, bool active_low) {
    return fireant_node_add_digital_sensor(&g_node, id, type, unit, pin, active_low);
}

size_t fireant_global_add_custom_sensor(const char *id, const char *type, const char *unit, fireant_sensor_reader_t read, void *ctx) {
    return fireant_node_add_custom_sensor(&g_node, id, type, unit, read, ctx);
}

static bool reserve_sensors(fireant_node_t *node, size_t capacity) {
    if (capacity <= node->sensor_capacity) return true;

    fireant_sensor_t *new_items = realloc(node->sensors, capacity * sizeof(fireant_sensor_t));
    if (!new_items) return false;

    node->sensors = new_items;
    node->sensor_capacity = capacity;
    return true;
}

static size_t add_sensor(fireant_node_t *node, const fireant_sensor_t *sensor) {
    if (!node || !sensor) return (size_t)-1;

    xSemaphoreTake(node->lock, portMAX_DELAY);

    if (node->sensor_count >= node->sensor_capacity) {
        size_t next = node->sensor_capacity == 0 ? SENSOR_INITIAL_CAPACITY : node->sensor_capacity * 2;
        if (!reserve_sensors(node, next)) {
            xSemaphoreGive(node->lock);
            return (size_t)-1;
        }
    }

    size_t index = node->sensor_count++;
    node->sensors[index] = *sensor;

    xSemaphoreGive(node->lock);
    return index;
}

static void init_adc1(fireant_node_t *node) {
    if (node->adc1_ready) return;

    adc_oneshot_unit_init_cfg_t cfg = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };

    ESP_ERROR_CHECK(adc_oneshot_new_unit(&cfg, &node->adc1_handle));
    node->adc1_ready = true;
}

void fireant_node_init(fireant_node_t *node, const fireant_config_t *config) {
    if (!node || !config) return;

    memset(node, 0, sizeof(*node));
    node->config = *config;

    if (node->config.send_interval_ms == 0) node->config.send_interval_ms = FIREANT_SEND_INTERVAL_MS;
    if (node->config.sync_interval_ms == 0) node->config.sync_interval_ms = FIREANT_SYNC_INTERVAL_MS;

    node->lock = xSemaphoreCreateMutex();
    node->events = xEventGroupCreate();

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) ESP_ERROR_CHECK(ret);
}

size_t fireant_node_add_adc_sensor(fireant_node_t *node, const char *id, const char *type, const char *unit, adc_channel_t channel) {
    if (!node) return (size_t)-1;

    init_adc1(node);

    adc_oneshot_chan_cfg_t chan_cfg = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(node->adc1_handle, channel, &chan_cfg));

    fireant_sensor_t sensor = {0};
    copy_text(sensor.id, sizeof(sensor.id), id);
    copy_text(sensor.type, sizeof(sensor.type), type);
    copy_text(sensor.unit, sizeof(sensor.unit), unit);
    sensor.kind = FIREANT_SENSOR_ANALOG_ADC;
    sensor.input.analog_adc.channel = channel;

    return add_sensor(node, &sensor);
}

size_t fireant_node_add_digital_sensor(fireant_node_t *node, const char *id, const char *type, const char *unit, gpio_num_t pin, bool active_low) {
    if (!node) return (size_t)-1;

    gpio_config_t io = {
        .pin_bit_mask = 1ULL << pin,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = active_low ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en = active_low ? GPIO_PULLDOWN_DISABLE : GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io));

    fireant_sensor_t sensor = {0};
    copy_text(sensor.id, sizeof(sensor.id), id);
    copy_text(sensor.type, sizeof(sensor.type), type);
    copy_text(sensor.unit, sizeof(sensor.unit), unit);
    sensor.kind = FIREANT_SENSOR_DIGITAL;
    sensor.input.digital.pin = pin;
    sensor.input.digital.active_low = active_low;

    return add_sensor(node, &sensor);
}

size_t fireant_node_add_custom_sensor(fireant_node_t *node, const char *id, const char *type, const char *unit, fireant_sensor_reader_t read, void *ctx) {
    if (!node || !read) return (size_t)-1;

    fireant_sensor_t sensor = {0};
    copy_text(sensor.id, sizeof(sensor.id), id);
    copy_text(sensor.type, sizeof(sensor.type), type);
    copy_text(sensor.unit, sizeof(sensor.unit), unit);
    sensor.kind = FIREANT_SENSOR_CUSTOM;
    sensor.input.custom.read = read;
    sensor.input.custom.ctx = ctx;

    return add_sensor(node, &sensor);
}

void fireant_node_enter_config_mode(fireant_node_t *node) {
    if (!node) return;
    xEventGroupSetBits(node->events, STATE_CONFIG_MODE_BIT | STATE_STOP_HTTP_BIT);
    ESP_LOGW(TAG, "config mode ON: HTTP paused");
}

void fireant_node_exit_config_mode(fireant_node_t *node) {
    if (!node) return;
    xEventGroupClearBits(node->events, STATE_CONFIG_MODE_BIT | STATE_STOP_HTTP_BIT);
    ESP_LOGW(TAG, "config mode OFF: HTTP resumed");
}

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data) {
    fireant_node_t *node = arg;

    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
        return;
    }

    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(node->events, WIFI_CONNECTED_BIT);

        if (xEventGroupGetBits(node->events) & STATE_CONFIG_MODE_BIT) return;

        if (node->wifi_retry_count < MAX_WIFI_RETRY) {
            node->wifi_retry_count++;
            esp_wifi_connect();
        } else {
            xEventGroupSetBits(node->events, WIFI_FAIL_BIT);
        }
        return;
    }

    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = data;
        snprintf(node->ip, sizeof(node->ip), IPSTR, IP2STR(&event->ip_info.ip));
        node->wifi_retry_count = 0;
        xEventGroupSetBits(node->events, WIFI_CONNECTED_BIT);
        ESP_LOGI(TAG, "IP: %s", node->ip);
    }
}

static void wifi_start_or_reconnect(fireant_node_t *node) {
    if (!node->wifi_started) {
        esp_netif_create_default_wifi_sta();
        wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));

        ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, node, NULL));
        ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, node, NULL));

        node->wifi_started = true;
    } else {
        esp_wifi_disconnect();
        esp_wifi_stop();
    }

    wifi_config_t wifi_cfg = {0};
    copy_text((char *)wifi_cfg.sta.ssid, sizeof(wifi_cfg.sta.ssid), node->config.wifi_ssid);
    copy_text((char *)wifi_cfg.sta.password, sizeof(wifi_cfg.sta.password), node->config.wifi_password);

    node->wifi_retry_count = 0;
    xEventGroupClearBits(node->events, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
}

static float read_sensor(fireant_node_t *node, fireant_sensor_t *sensor) {
    switch (sensor->kind) {
        case FIREANT_SENSOR_ANALOG_ADC: {
            int raw = 0;
            if (adc_oneshot_read(node->adc1_handle, sensor->input.analog_adc.channel, &raw) == ESP_OK) {
                return (float)raw;
            }
            return sensor->value;
        }
        case FIREANT_SENSOR_DIGITAL: {
            int level = gpio_get_level(sensor->input.digital.pin);
            if (sensor->input.digital.active_low) level = !level;
            return (float)level;
        }
        case FIREANT_SENSOR_CUSTOM:
            return sensor->input.custom.read(sensor->input.custom.ctx);
        default:
            return sensor->value;
    }
}

static void update_sensors(fireant_node_t *node) {
    xSemaphoreTake(node->lock, portMAX_DELAY);
    for (size_t i = 0; i < node->sensor_count; i++) {
        node->sensors[i].value = read_sensor(node, &node->sensors[i]);
    }
    xSemaphoreGive(node->lock);
}

static bool http_post_json(fireant_node_t *node, const char *path, const char *json) {
    char url[256];
    snprintf(url, sizeof(url), "http://%s:%s/%s",
        node->config.server_ip,
        node->config.server_port,
        path
    );

    ESP_LOGI(TAG, "POST %s", url);
    ESP_LOGI(TAG, "JSON: %s", json);

    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 5000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        ESP_LOGE(TAG, "esp_http_client_init failed");
        return false;
    }

    char bearer[160];
    snprintf(bearer, sizeof(bearer), "Bearer %s", node->config.server_token);

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "Authorization", bearer);
    esp_http_client_set_post_field(client, json, strlen(json));

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    int content_length = esp_http_client_get_content_length(client);

    ESP_LOGI(TAG, "HTTP result: err=%s status=%d content_length=%d",
        esp_err_to_name(err),
        status,
        content_length
    );

    esp_http_client_cleanup(client);

    return err == ESP_OK && status >= 200 && status < 300;
}

static bool sync_node(fireant_node_t *node) {
    char json[FIREANT_HTTP_BUFFER_SIZE];
    size_t off = 0;

    xSemaphoreTake(node->lock, portMAX_DELAY);

    off += snprintf(json + off, sizeof(json) - off,
        "{\"id\":\"%s\",\"token\":\"%s\",\"ip\":\"%s\",\"port\":\"%s\",\"sensors\":[",
        node->config.id, node->config.server_token, node->ip, FIREANT_DEFAULT_NODE_PORT);

    for (size_t i = 0; i < node->sensor_count; i++) {
        fireant_sensor_t *s = &node->sensors[i];
        off += snprintf(json + off, sizeof(json) - off,
            "%s{\"id\":\"%s\",\"type\":\"%s\",\"unit\":\"%s\"}",
            i == 0 ? "" : ",", s->id, s->type, s->unit);
    }

    snprintf(json + off, sizeof(json) - off, "]}");
    xSemaphoreGive(node->lock);

    return http_post_json(node, "api/sync", json);
}

static bool send_values(fireant_node_t *node) {
    char json[FIREANT_HTTP_BUFFER_SIZE];
    size_t off = 0;

    update_sensors(node);

    xSemaphoreTake(node->lock, portMAX_DELAY);

    off += snprintf(json + off, sizeof(json) - off,
        "{\"id\":\"%s\",\"sensors\":[", node->config.id);

    for (size_t i = 0; i < node->sensor_count; i++) {
        fireant_sensor_t *s = &node->sensors[i];
        off += snprintf(json + off, sizeof(json) - off,
            "%s{\"id\":\"%s\",\"value\":%.3f}",
            i == 0 ? "" : ",", s->id, s->value);
    }

    snprintf(json + off, sizeof(json) - off, "]}");
    xSemaphoreGive(node->lock);

    return http_post_json(node, "api/receive", json);
}

static void network_task(void *arg) {
    fireant_node_t *node = arg;

    wifi_start_or_reconnect(node);

    while (true) {
        EventBits_t bits = xEventGroupGetBits(node->events);

        if (bits & STATE_STOP_HTTP_BIT) {
            vTaskDelay(pdMS_TO_TICKS(300));
            continue;
        }

        if (!(bits & WIFI_CONNECTED_BIT)) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        int64_t now = now_ms();

        if (node->http_fail_count >= 3) {
            vTaskDelay(pdMS_TO_TICKS(FIREANT_HTTP_FAIL_BACKOFF_MS));
            node->http_fail_count = 0;
            continue;
        }

        if (now - node->last_send_ms >= node->config.send_interval_ms) {
            bool ok = send_values(node);
            node->last_send_ms = now;
            node->http_fail_count = ok ? 0 : node->http_fail_count + 1;
        }

        if (now - node->last_sync_ms >= node->config.sync_interval_ms) {
            bool ok = sync_node(node);
            node->last_sync_ms = now;
            node->http_fail_count = ok ? 0 : node->http_fail_count + 1;
        }

        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

static void console_uart_init(uart_port_t uart) {
    uart_config_t cfg = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    uart_driver_delete(uart);
    ESP_ERROR_CHECK(uart_param_config(uart, &cfg));
    ESP_ERROR_CHECK(uart_driver_install(uart, FIREANT_UART_LINE_SIZE * 4, 0, 0, NULL, 0));
}

static bool uart_read_command_line(uart_port_t uart, char *out, size_t out_size) {
    static char line[FIREANT_UART_LINE_SIZE];
    static size_t len = 0;

    uint8_t c = 0;
    int n = uart_read_bytes(uart, &c, 1, pdMS_TO_TICKS(50));
    if (n <= 0) return false;

    if (c == '\r' || c == '\n') {
        if (len == 0) return false;
        line[len] = '\0';
        copy_text(out, out_size, line);
        len = 0;
        return true;
    }

    if (c == 0x08 || c == 0x7f) {
        if (len > 0) len--;
        return false;
    }

    if (len < sizeof(line) - 1) {
        line[len++] = (char)c;
    }

    return false;
}

static void apply_command(fireant_node_t *node, const char *line) {
    if (strcmp(line, "config") == 0 || strcmp(line, "cfg") == 0) {
        fireant_node_enter_config_mode(node);
        return;
    }

    if (strcmp(line, "exit") == 0 || strcmp(line, "run") == 0) {
        fireant_node_exit_config_mode(node);
        return;
    }

    if (strcmp(line, "status") == 0) {
        EventBits_t bits = xEventGroupGetBits(node->events);
        ESP_LOGI(TAG, "ip=%s wifi=%s config=%s sensors=%u",
            node->ip[0] ? node->ip : "none",
            (bits & WIFI_CONNECTED_BIT) ? "connected" : "disconnected",
            (bits & STATE_CONFIG_MODE_BIT) ? "on" : "off",
            (unsigned)node->sensor_count);
        return;
    }

    const char *eq = strchr(line, '=');
    if (!eq) {
        ESP_LOGW(TAG, "unknown command: %s", line);
        return;
    }

    char key[48];
    size_t key_len = eq - line;
    if (key_len >= sizeof(key)) key_len = sizeof(key) - 1;
    memcpy(key, line, key_len);
    key[key_len] = '\0';

    const char *value = eq + 1;
    bool reconnect_wifi = false;

    xSemaphoreTake(node->lock, portMAX_DELAY);

    if (strcmp(key, "server_ip") == 0) copy_text(node->config.server_ip, sizeof(node->config.server_ip), value);
    else if (strcmp(key, "server_port") == 0) copy_text(node->config.server_port, sizeof(node->config.server_port), value);
    else if (strcmp(key, "server_token") == 0) copy_text(node->config.server_token, sizeof(node->config.server_token), value);
    else if (strcmp(key, "wifi_ssid") == 0) { copy_text(node->config.wifi_ssid, sizeof(node->config.wifi_ssid), value); reconnect_wifi = true; }
    else if (strcmp(key, "wifi_password") == 0) { copy_text(node->config.wifi_password, sizeof(node->config.wifi_password), value); reconnect_wifi = true; }
    else ESP_LOGW(TAG, "unknown key: %s", key);

    xSemaphoreGive(node->lock);

    ESP_LOGI(TAG, "updated: %s", key);

    if (reconnect_wifi) {
        fireant_node_enter_config_mode(node);
        wifi_start_or_reconnect(node);
    }
}

static void console_task(void *arg) {
    fireant_node_t *node = arg;

    console_uart_init(node->config.console_uart);

    ESP_LOGI(TAG, "console ready");
    ESP_LOGI(TAG, "commands: config | status | server_ip=... | server_port=... | wifi_ssid=... | wifi_password=... | exit");

    char line[FIREANT_UART_LINE_SIZE];

    while (true) {
        if (uart_read_command_line(node->config.console_uart, line, sizeof(line))) {
            ESP_LOGI(TAG, "> %s", line);
            apply_command(node, line);
        }
    }
}

void fireant_node_start(fireant_node_t *node) {
    if (!node || node->started) return;
    node->started = true;

    esp_log_level_set("esp-tls", ESP_LOG_WARN);
    esp_log_level_set("transport_base", ESP_LOG_WARN);
    esp_log_level_set("HTTP_CLIENT", ESP_LOG_WARN);

    xTaskCreate(network_task, "fireant_net", 8192, node, 5, NULL);

    if (node->config.enable_console) {
        xTaskCreate(console_task, "fireant_console", 4096, node, 6, NULL);
    }
}