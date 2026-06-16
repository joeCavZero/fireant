#include "fireant.h"

#include <stdlib.h>
#include <string.h>

static fireant_config_t g_config;
static fireant_sensor_t g_sensors[FIREANT_MAX_SENSORS];
static uint8_t g_sensor_count = 0;

static bool g_started = false;

static unsigned long g_last_send = 0;
static unsigned long g_last_sync = 0;

static void fireant_log(const char *message) {
    if (g_config.enable_console) {
        Serial.println(message);
    }
}

static void copy_text(char *dst, size_t dst_size, const char *src) {
    if (dst == NULL || dst_size == 0) return;
    if (src == NULL) src = "";

    strncpy(dst, src, dst_size - 1);
    dst[dst_size - 1] = '\0';
}

static int fireant_server_port(void) {
    int port = atoi(g_config.server_port);
    if (port <= 0) port = 8000;
    return port;
}

static float fireant_map_float(
    float x,
    float in_min,
    float in_max,
    float out_min,
    float out_max
) {
    if (in_max == in_min) return out_min;

    if (x < in_min) x = in_min;
    if (x > in_max) x = in_max;

    return (x - in_min) * (out_max - out_min) /
           (in_max - in_min) + out_min;
}

void fireant_config_default(fireant_config_t *config) {
    if (config == NULL) return;

    memset(config, 0, sizeof(fireant_config_t));

    copy_text(config->id, sizeof(config->id), "arduino_uno");
    copy_text(config->server_ip, sizeof(config->server_ip), "192.168.0.100");
    copy_text(config->server_port, sizeof(config->server_port), FIREANT_DEFAULT_NODE_PORT);
    copy_text(config->server_token, sizeof(config->server_token), "");

    config->enable_console = true;
    config->send_interval_ms = FIREANT_DEFAULT_SEND_INTERVAL_MS;
    config->sync_interval_ms = FIREANT_DEFAULT_SYNC_INTERVAL_MS;

    config->mac[0] = 0xDE;
    config->mac[1] = 0xAD;
    config->mac[2] = 0xBE;
    config->mac[3] = 0xEF;
    config->mac[4] = 0xFE;
    config->mac[5] = 0xED;

    config->use_static_ip = false;
    config->static_ip = IPAddress(192, 168, 0, 177);
    config->dns = IPAddress(8, 8, 8, 8);
    config->gateway = IPAddress(192, 168, 0, 1);
    config->subnet = IPAddress(255, 255, 255, 0);
}

static bool fireant_read_line(char *buffer, size_t size) {
    if (buffer == NULL || size == 0) return false;

    size_t len = 0;
    buffer[0] = '\0';

    while (true) {
        while (!Serial.available()) {
            delay(10);
        }

        char c = (char)Serial.read();

        if (c == '\r') continue;

        if (c == '\n') {
            if (len == 0) continue;

            buffer[len] = '\0';
            Serial.println();
            return true;
        }

        if (c == 8 || c == 127) {
            if (len > 0) {
                len--;
                Serial.print("\b \b");
            }
            continue;
        }

        if (len < size - 1) {
            buffer[len++] = c;
            Serial.print(c);
        }
    }
}

bool fireant_config_read_serial(fireant_config_t *config) {
    if (config == NULL) return false;

    Serial.println();
    Serial.println("==== FIREANT CONFIG ====");
    Serial.println("Digite os valores.");
    Serial.println();

    Serial.print("id: ");
    fireant_read_line(config->id, sizeof(config->id));

    Serial.print("server_ip: ");
    fireant_read_line(config->server_ip, sizeof(config->server_ip));

    Serial.print("server_port: ");
    fireant_read_line(config->server_port, sizeof(config->server_port));

    Serial.print("server_token: ");
    fireant_read_line(config->server_token, sizeof(config->server_token));

    Serial.println("CONFIG OK");
    return true;
}

bool fireant_global_init(const fireant_config_t *config) {
    if (config == NULL) return false;

    memcpy(&g_config, config, sizeof(fireant_config_t));

    if (g_config.send_interval_ms == 0) {
        g_config.send_interval_ms = FIREANT_DEFAULT_SEND_INTERVAL_MS;
    }

    if (g_config.sync_interval_ms == 0) {
        g_config.sync_interval_ms = FIREANT_DEFAULT_SYNC_INTERVAL_MS;
    }

    g_sensor_count = 0;
    g_started = false;
    g_last_send = 0;
    g_last_sync = 0;

    fireant_log("[FIREANT] init");

    if (g_config.use_static_ip) {
        Ethernet.begin(
            g_config.mac,
            g_config.static_ip,
            g_config.dns,
            g_config.gateway,
            g_config.subnet
        );
    } else {
        if (Ethernet.begin(g_config.mac) == 0) {
            fireant_log("[FIREANT] DHCP failed");

            Ethernet.begin(
                g_config.mac,
                g_config.static_ip,
                g_config.dns,
                g_config.gateway,
                g_config.subnet
            );
        }
    }

    delay(1000);

    if (g_config.enable_console) {
        Serial.print("[FIREANT] IP: ");
        Serial.println(Ethernet.localIP());
    }

    return true;
}

static bool add_sensor(const fireant_sensor_t *sensor) {
    if (sensor == NULL) return false;

    if (g_sensor_count >= FIREANT_MAX_SENSORS) {
        fireant_log("[FIREANT] max sensors reached");
        return false;
    }

    g_sensors[g_sensor_count] = *sensor;
    g_sensor_count++;

    fireant_log("[FIREANT] sensor added");
    return true;
}

bool fireant_global_add_adc_sensor(
    const char *id,
    const char *type,
    const char *unit,
    uint8_t pin
) {
    return fireant_global_add_adc_sensor_mapped(
        id,
        type,
        unit,
        pin,
        0.0f,
#if defined(ESP32) || defined(ESP8266)
        4095.0f,
#else
        1023.0f,
#endif
        0.0f,
#if defined(ESP32) || defined(ESP8266)
        4095.0f,
#else
        1023.0f,
#endif
        false
    );
}

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
) {
    fireant_sensor_t sensor;
    memset(&sensor, 0, sizeof(sensor));

    copy_text(sensor.id, sizeof(sensor.id), id);
    copy_text(sensor.type, sizeof(sensor.type), type);
    copy_text(sensor.unit, sizeof(sensor.unit), unit);

    sensor.kind = FIREANT_SENSOR_ANALOG_ADC;
    sensor.input.analog_adc.pin = pin;
    sensor.input.analog_adc.raw_min = raw_min;
    sensor.input.analog_adc.raw_max = raw_max;
    sensor.input.analog_adc.out_min = out_min;
    sensor.input.analog_adc.out_max = out_max;
    sensor.input.analog_adc.invert = invert;
    sensor.input.analog_adc.mapped = true;
    sensor.value = 0.0f;

    return add_sensor(&sensor);
}

bool fireant_global_add_digital_sensor(
    const char *id,
    const char *type,
    const char *unit,
    uint8_t pin,
    bool active_low
) {
    pinMode(pin, active_low ? INPUT_PULLUP : INPUT);

    fireant_sensor_t sensor;
    memset(&sensor, 0, sizeof(sensor));

    copy_text(sensor.id, sizeof(sensor.id), id);
    copy_text(sensor.type, sizeof(sensor.type), type);
    copy_text(sensor.unit, sizeof(sensor.unit), unit);

    sensor.kind = FIREANT_SENSOR_DIGITAL;
    sensor.input.digital.pin = pin;
    sensor.input.digital.active_low = active_low;
    sensor.value = 0.0f;

    return add_sensor(&sensor);
}

bool fireant_global_add_custom_sensor(
    const char *id,
    const char *type,
    const char *unit,
    fireant_sensor_reader_t read,
    void *ctx
) {
    if (read == NULL) return false;

    fireant_sensor_t sensor;
    memset(&sensor, 0, sizeof(sensor));

    copy_text(sensor.id, sizeof(sensor.id), id);
    copy_text(sensor.type, sizeof(sensor.type), type);
    copy_text(sensor.unit, sizeof(sensor.unit), unit);

    sensor.kind = FIREANT_SENSOR_CUSTOM;
    sensor.input.custom.read = read;
    sensor.input.custom.ctx = ctx;
    sensor.value = 0.0f;

    return add_sensor(&sensor);
}

void fireant_global_start(void) {
    g_started = true;

    g_last_send = millis();
    g_last_sync = millis();

    fireant_log("[FIREANT] started");

    fireant_global_sync();
}

void fireant_global_loop(void) {
    if (!g_started) return;

    unsigned long now = millis();

    if (now - g_last_sync >= g_config.sync_interval_ms) {
        g_last_sync = now;
        fireant_global_sync();
    }

    if (now - g_last_send >= g_config.send_interval_ms) {
        g_last_send = now;
        fireant_global_send();
    }
}

static float fireant_read_sensor(fireant_sensor_t *sensor) {
    if (sensor == NULL) return 0.0f;

    switch (sensor->kind) {
        case FIREANT_SENSOR_ANALOG_ADC: {
            int raw = analogRead(sensor->input.analog_adc.pin);

            if (!sensor->input.analog_adc.mapped) {
                return (float)raw;
            }

            float value = fireant_map_float(
                (float)raw,
                sensor->input.analog_adc.raw_min,
                sensor->input.analog_adc.raw_max,
                sensor->input.analog_adc.out_min,
                sensor->input.analog_adc.out_max
            );

            if (sensor->input.analog_adc.invert) {
                value = sensor->input.analog_adc.out_max -
                        value +
                        sensor->input.analog_adc.out_min;
            }

            return value;
        }

        case FIREANT_SENSOR_DIGITAL: {
            int level = digitalRead(sensor->input.digital.pin);
            if (sensor->input.digital.active_low) level = !level;
            return (float)level;
        }

        case FIREANT_SENSOR_CUSTOM:
            if (sensor->input.custom.read != NULL) {
                return sensor->input.custom.read(sensor->input.custom.ctx);
            }
            return sensor->value;

        default:
            return sensor->value;
    }
}

static String fireant_local_ip_string(void) {
    IPAddress ip = Ethernet.localIP();

    String text = "";
    text += ip[0];
    text += ".";
    text += ip[1];
    text += ".";
    text += ip[2];
    text += ".";
    text += ip[3];

    return text;
}

static bool fireant_read_http_response(EthernetClient *client) {
    if (client == NULL) return false;

    unsigned long timeout = millis();
    bool got_anything = false;

    while (client->connected() && millis() - timeout < FIREANT_HTTP_TIMEOUT_MS) {
        while (client->available()) {
            char c = (char)client->read();
            got_anything = true;

            if (g_config.enable_console) {
                Serial.write(c);
            }

            timeout = millis();
        }
    }

    while (client->available()) {
        char c = (char)client->read();
        got_anything = true;

        if (g_config.enable_console) {
            Serial.write(c);
        }
    }

    return got_anything;
}

static bool fireant_post_json(const char *path, const String &body) {
    EthernetClient client;

    IPAddress server;
    if (!server.fromString(g_config.server_ip)) {
        fireant_log("[FIREANT] invalid server IP");
        return false;
    }

    int port = fireant_server_port();

    if (!client.connect(server, port)) {
        fireant_log("[FIREANT] connection failed");
        return false;
    }

    if (g_config.enable_console) {
        Serial.print("[FIREANT] POST ");
        Serial.println(path);
        Serial.print("[FIREANT] JSON: ");
        Serial.println(body);
    }

    client.print("POST ");
    client.print(path);
    client.println(" HTTP/1.1");
    client.print("Host: ");
    client.println(g_config.server_ip);
    client.print("Authorization: Bearer ");
    client.println(g_config.server_token);
    client.println("Content-Type: application/json");
    client.print("Content-Length: ");
    client.println(body.length());
    client.println("Connection: close");
    client.println();
    client.print(body);

    fireant_read_http_response(&client);
    client.stop();

    return true;
}

bool fireant_global_sync(void) {
    String body = "{";
    body += "\"id\":\"";
    body += g_config.id;
    body += "\",";
    body += "\"token\":\"";
    body += g_config.server_token;
    body += "\",";
    body += "\"ip\":\"";
    body += fireant_local_ip_string();
    body += "\",";
    body += "\"port\":\"";
    body += g_config.server_port;
    body += "\",";
    body += "\"sensors\":[";

    for (uint8_t i = 0; i < g_sensor_count; i++) {
        if (i > 0) body += ",";

        body += "{";
        body += "\"id\":\"";
        body += g_sensors[i].id;
        body += "\",";
        body += "\"type\":\"";
        body += g_sensors[i].type;
        body += "\",";
        body += "\"unit\":\"";
        body += g_sensors[i].unit;
        body += "\"";
        body += "}";
    }

    body += "]";
    body += "}";

    bool ok = fireant_post_json("/api/sync", body);

    if (ok) fireant_log("[FIREANT] sync sent");
    return ok;
}

bool fireant_global_send(void) {
    String body = "{";
    body += "\"id\":\"";
    body += g_config.id;
    body += "\",";
    body += "\"sensors\":[";

    for (uint8_t i = 0; i < g_sensor_count; i++) {
        if (i > 0) body += ",";

        g_sensors[i].value = fireant_read_sensor(&g_sensors[i]);

        body += "{";
        body += "\"id\":\"";
        body += g_sensors[i].id;
        body += "\",";
        body += "\"value\":";
        body += String(g_sensors[i].value, 3);
        body += "}";
    }

    body += "]";
    body += "}";

    bool ok = fireant_post_json("/api/receive", body);

    if (ok) fireant_log("[FIREANT] data sent");
    return ok;
}