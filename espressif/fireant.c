#include "fireant.h"

static fireant_node_t _fireant_node = {
    .connected = false,
    .last_sync = 0,
};


#define FIREANT_WIFI_CONNECTED_BIT BIT0
#define FIREANT_WIFI_FAIL_BIT      BIT1
#define FIREANT_WIFI_MAX_RETRY     5

void fireant_init()
{
    _fireant_node.config_mutex = xSemaphoreCreateMutex();

    _fireant_init_uart_driver(_fireant_node.config.uart_port);
    fireant_connect_to_wifi();
}

void _fireant_init_uart_driver(uart_port_t uart_port) {

    esp_err_t err = uart_driver_install(
        uart_port,
        1024,   // RX buffer
        1024,   // TX buffer
        0,
        NULL,
        0
    );

    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        printf("[FIREANT] [ERROR] UART init error: %s\n", esp_err_to_name(err));
        return;
    }

    printf("[FIREANT] initialized on UART %d\n", uart_port);
}

void _fireant_read_line(char *buffer, size_t size) {
    size_t pos = 0;
    uint8_t c;

    while (pos < size - 1) {
        int len = uart_read_bytes(
            _fireant_node.config.uart_port,
            &c,
            1,
            portMAX_DELAY
        );

        if (len <= 0) {
            continue;
        }

        if (c == '\r' || c == '\n') {
            break;
        }

        // Backspace ou DEL
        if (c == '\b' || c == 127) {
            if (pos > 0) {
                pos--;

                // volta, apaga na tela, volta de novo
                uart_write_bytes(_fireant_node.config.uart_port, "\b \b", 3);
            }

            continue;
        }

        // ignora caracteres de controle
        if (c < 32 || c > 126) {
            continue;
        }

        buffer[pos++] = (char)c;
        uart_write_bytes(_fireant_node.config.uart_port, (const char *)&c, 1);
    }

    buffer[pos] = '\0';
    uart_write_bytes(_fireant_node.config.uart_port, "\r\n", 2);
}

void fireant_read_config() {
    static char id[64];
    static char server_ip[64];
    static char server_port[16];
    static char server_token[128];
    static char wifi_ssid[64];
    static char wifi_password[64];

    printf("\n==== FIREANT CONFIG ====\n");

    printf("Node ID: ");
    fflush(stdout);
    _fireant_read_line(id, sizeof(id));

    printf("Server IP: ");
    fflush(stdout);
    _fireant_read_line(server_ip, sizeof(server_ip));

    printf("Server port: ");
    fflush(stdout);
    _fireant_read_line(server_port, sizeof(server_port));

    printf("Server token: ");
    fflush(stdout);
    _fireant_read_line(server_token, sizeof(server_token));

    printf("WiFi SSID: ");
    fflush(stdout);
    _fireant_read_line(wifi_ssid, sizeof(wifi_ssid));

    printf("WiFi password: ");
    fflush(stdout);
    _fireant_read_line(wifi_password, sizeof(wifi_password));

    _fireant_node.config.id = id;
    _fireant_node.config.server_ip = server_ip;
    _fireant_node.config.server_port = server_port;
    _fireant_node.config.server_token = server_token;
    _fireant_node.config.wifi_ssid = wifi_ssid;
    _fireant_node.config.wifi_password = wifi_password;

    printf("\n==== FIREANT CONFIG UPDATED ====\n");
    printf("ID: %s\n", _fireant_node.config.id);
    printf("Server: %s:%s\n", _fireant_node.config.server_ip, _fireant_node.config.server_port);
    printf("Token: %s\n", _fireant_node.config.server_token);
    printf("WiFi SSID: %s\n", _fireant_node.config.wifi_ssid);
    printf("WiFi password: %s\n", _fireant_node.config.wifi_password);

}

#define FIREANT_INITIAL_SENSOR_CAPACITY 4

size_t fireant_add_sensor(fireant_sensor_t sensor)
{
    _fireant_sensor_vector_t *vector = &_fireant_node.sensors;

    if (vector->count >= vector->capacity) {
        size_t new_capacity =
            (vector->capacity == 0)
                ? FIREANT_INITIAL_SENSOR_CAPACITY
                : vector->capacity * 2;

        fireant_sensor_t *new_sensors = realloc(
            vector->sensors,
            new_capacity * sizeof(fireant_sensor_t)
        );

        if (new_sensors == NULL) {
            printf("[FIREANT] [ERROR] Failed to allocate sensor vector\n");
            return SIZE_MAX;
        }

        vector->sensors = new_sensors;
        vector->capacity = new_capacity;
    }

    size_t sensor_index = vector->count;

    vector->sensors[sensor_index] = sensor;
    vector->count++;

    printf(
        "[FIREANT] Sensor added [%u]: %s\n",
        (unsigned int)sensor_index,
        sensor.id
    );

    return sensor_index;
}

bool fireant_remove_sensor(size_t sensor_index)
{
    _fireant_sensor_vector_t *vector = &_fireant_node.sensors;

    if (sensor_index >= vector->count) {
        printf(
            "[FIREANT] [ERROR] Invalid sensor index: %u\n",
            (unsigned int)sensor_index
        );

        return false;
    }

    for (
        size_t i = sensor_index;
        i < vector->count - 1;
        i++
    ) {
        vector->sensors[i] = vector->sensors[i + 1];
    }

    vector->count--;

    printf(
        "[FIREANT] Sensor removed [%u]\n",
        (unsigned int)sensor_index
    );

    return true;
}

void fireant_config(fireant_config_t config) {
    _fireant_node.config = config;
}

void fireant_start() {
    xTaskCreate(
        _fireant_send_task,
        "fireant_send_task",
        8192,
        NULL,
        5,
        NULL
    );

    xTaskCreate(
        _fireant_config_task,
        "fireant_config_task",
        4096,
        NULL,
        4,
        NULL
    );
}

void _fireant_send_task(void *pvParameters) {
    while (true) {
        fireant_send_data();

        vTaskDelay(pdMS_TO_TICKS(5000)); // envia a cada 5 segundos
    }
}

void _fireant_config_task(void *pvParameters) {
    while (true) {

        char command[32];

        _fireant_read_line(command, sizeof(command));

        if (strcmp(command, "config") == 0) {

            char old_ssid[64];
            char old_password[64];

            snprintf(
                old_ssid,
                sizeof(old_ssid),
                "%s",
                _fireant_node.config.wifi_ssid
            );

            snprintf(
                old_password,
                sizeof(old_password),
                "%s",
                _fireant_node.config.wifi_password
            );
            
            fireant_read_config();

            bool wifi_changed =
                strcmp(old_ssid, _fireant_node.config.wifi_ssid) != 0 ||
                strcmp(old_password, _fireant_node.config.wifi_password) != 0;

            if (wifi_changed) {

                printf("[FIREANT] WiFi config changed\n");

                esp_wifi_disconnect();

                fireant_connect_to_wifi();
            }
        }
    }
}

esp_err_t _fireant_http_event_handler(esp_http_client_event_handle_t evt) {
    switch(evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            if (!esp_http_client_is_chunked_response(evt->client)) {
                printf(
                    "[FIREANT] %.*s", 
                    evt->data_len, 
                    (char*)evt->data
                );
            }
            break;
        default:
            break;
    }
    return ESP_OK;
}


void fireant_send_data() {
    char server_ip[64];
    char server_port[16];
    char server_token[128];

    xSemaphoreTake(_fireant_node.config_mutex, portMAX_DELAY);

    snprintf(server_ip, sizeof(server_ip), "%s", _fireant_node.config.server_ip);
    snprintf(server_port, sizeof(server_port), "%s", _fireant_node.config.server_port);
    snprintf(server_token, sizeof(server_token), "%s", _fireant_node.config.server_token);

    xSemaphoreGive(_fireant_node.config_mutex);

    char url[256];

    snprintf(
        url,
        sizeof(url),
        "http://%s:%s/api/v1/send",
        server_ip,
        server_port
    );

    char data_to_send[256];

    snprintf(
        data_to_send,
        sizeof(data_to_send),
        "{\"node_id\":\"%s\",\"message\":\"hi\"}",
        _fireant_node.config.id
    );

    esp_http_client_config_t config = {
        .url = url,
        .event_handler = _fireant_http_event_handler,
        .method = HTTP_METHOD_POST,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "Authorization", server_token);
    esp_http_client_set_post_field(client, data_to_send, strlen(data_to_send));

    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK) {
        printf(
            "[FIREANT] HTTP POST Status = %d, Length = %lld\n",
            esp_http_client_get_status_code(client),
            esp_http_client_get_content_length(client)
        );
    } else {
        printf(
            "[FIREANT] HTTP POST failed: %s\n",
            esp_err_to_name(err)
        );
    }

    esp_http_client_cleanup(client);
}

static void _fireant_wifi_event_handler(
    void *arg,
    esp_event_base_t event_base,
    int32_t event_id,
    void *event_data
) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    }

    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        _fireant_node.connected = false;

        if (_fireant_node.wifi_retry_count < FIREANT_WIFI_MAX_RETRY) {
            esp_wifi_connect();
            _fireant_node.wifi_retry_count++;

            printf(
                "[FIREANT] WiFi retry %d/%d\n",
                _fireant_node.wifi_retry_count,
                FIREANT_WIFI_MAX_RETRY
            );
        } else {
            xEventGroupSetBits(
                _fireant_node.wifi_event_group,
                FIREANT_WIFI_FAIL_BIT
            );

            printf("[FIREANT] WiFi connection failed\n");
        }
    }

    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;

        _fireant_node.wifi_retry_count = 0;
        _fireant_node.connected = true;

        printf(
            "[FIREANT] WiFi connected. IP: " IPSTR "\n",
            IP2STR(&event->ip_info.ip)
        );

        xEventGroupSetBits(
            _fireant_node.wifi_event_group,
            FIREANT_WIFI_CONNECTED_BIT
        );
    }
}

void fireant_connect_to_wifi()
{
    if (
        _fireant_node.config.wifi_ssid == NULL ||
        _fireant_node.config.wifi_password == NULL
    ) {
        printf("[FIREANT] [ERROR] WiFi SSID/password not configured\n");
        return;
    }

    esp_err_t err = nvs_flash_init();

    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    } else {
        ESP_ERROR_CHECK(err);
    }

    _fireant_node.wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    err = esp_event_loop_create_default();

    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_ERROR_CHECK(err);
    }

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT();

    ESP_ERROR_CHECK(esp_wifi_init(&wifi_init_config));

    ESP_ERROR_CHECK(esp_event_handler_register(
        WIFI_EVENT,
        ESP_EVENT_ANY_ID,
        &_fireant_wifi_event_handler,
        NULL
    ));

    ESP_ERROR_CHECK(esp_event_handler_register(
        IP_EVENT,
        IP_EVENT_STA_GOT_IP,
        &_fireant_wifi_event_handler,
        NULL
    ));

    wifi_config_t wifi_config = {0};

    strncpy(
        (char *)wifi_config.sta.ssid,
        _fireant_node.config.wifi_ssid,
        sizeof(wifi_config.sta.ssid)
    );

    strncpy(
        (char *)wifi_config.sta.password,
        _fireant_node.config.wifi_password,
        sizeof(wifi_config.sta.password)
    );

    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    printf("[FIREANT] Connecting to WiFi: %s\n", _fireant_node.config.wifi_ssid);

    EventBits_t bits = xEventGroupWaitBits(
        _fireant_node.wifi_event_group,
        FIREANT_WIFI_CONNECTED_BIT | FIREANT_WIFI_FAIL_BIT,
        pdFALSE,
        pdFALSE,
        portMAX_DELAY
    );

    if (bits & FIREANT_WIFI_CONNECTED_BIT) {
        printf("[FIREANT] Connected to WiFi\n");
    } else if (bits & FIREANT_WIFI_FAIL_BIT) {
        printf("[FIREANT] Failed to connect to WiFi\n");
    }
}