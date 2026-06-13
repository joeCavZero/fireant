#include "fireant.h"

static fireant_config_t g_config;
static fireant_adc_sensor_t g_sensors[FIREANT_MAX_SENSORS];
static uint8_t g_sensor_count = 0;

static bool g_started = false;

static unsigned long g_last_send = 0;
static unsigned long g_last_sync = 0;

static byte g_mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };

static void fireant_log(const char *message) {
    if (g_config.enable_console) {
        Serial.println(message);
    }
}

static int fireant_server_port(void) {
    return atoi(g_config.server_port);
}

void fireant_config_default(fireant_config_t *config) {
    if (config == NULL) {
        return;
    }

    memset(config, 0, sizeof(fireant_config_t));

    strcpy(config->id, "arduino_uno");
    strcpy(config->server_ip, "192.168.0.100");
    strcpy(config->server_port, "8000");
    strcpy(config->server_token, "");

    config->enable_console = true;
    config->send_interval_ms = 5000;
    config->sync_interval_ms = 5000;
}

static bool fireant_read_line(char *buffer, size_t size) {
    if (buffer == NULL || size == 0) {
        return false;
    }

    size_t len = 0;
    buffer[0] = '\0';

    while (true) {
        while (!Serial.available()) {
            delay(10);
        }

        char c = Serial.read();

        if (c == '\r') {
            continue;
        }

        if (c == '\n') {
            if (len == 0) {
                continue;
            }

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
    if (config == NULL) {
        return false;
    }

    Serial.println();
    Serial.println("==== FIREANT CONFIG ====");
    Serial.println("Digite os valores.");

    Serial.print("id: ");
    fireant_read_line(config->id, FIREANT_STR_SIZE);

    Serial.print("server_ip: ");
    fireant_read_line(config->server_ip, FIREANT_STR_SIZE);

    Serial.print("server_port: ");
    fireant_read_line(config->server_port, FIREANT_PORT_SIZE);

    Serial.print("server_token: ");
    fireant_read_line(config->server_token, FIREANT_TOKEN_SIZE);

    return true;
}

bool fireant_global_init(const fireant_config_t *config) {
    if (config == NULL) {
        return false;
    }

    memcpy(&g_config, config, sizeof(fireant_config_t));

    g_sensor_count = 0;
    g_started = false;
    g_last_send = 0;
    g_last_sync = 0;

    fireant_log("[FIREANT] init");

    if (Ethernet.begin(g_mac) == 0) {
        fireant_log("[FIREANT] DHCP failed");

        IPAddress ip(192, 168, 0, 177);
        IPAddress dns(8, 8, 8, 8);
        IPAddress gateway(192, 168, 0, 1);
        IPAddress subnet(255, 255, 255, 0);

        Ethernet.begin(g_mac, ip, dns, gateway, subnet);

        delay(1000);

        Serial.print("[FIREANT] IP: ");
        Serial.println(Ethernet.localIP());
    }

    delay(1000);

    if (g_config.enable_console) {
        Serial.print("[FIREANT] IP: ");
        Serial.println(Ethernet.localIP());
    }

    return true;
}

bool fireant_global_add_adc_sensor(
    const char *id,
    const char *type,
    const char *unit,
    uint8_t pin
) {
    if (g_sensor_count >= FIREANT_MAX_SENSORS) {
        fireant_log("[FIREANT] max sensors reached");
        return false;
    }

    fireant_adc_sensor_t *sensor = &g_sensors[g_sensor_count];

    memset(sensor, 0, sizeof(fireant_adc_sensor_t));

    strncpy(sensor->id, id, FIREANT_STR_SIZE - 1);
    strncpy(sensor->type, type, FIREANT_STR_SIZE - 1);
    strncpy(sensor->unit, unit, FIREANT_STR_SIZE - 1);

    sensor->pin = pin;

    g_sensor_count++;

    fireant_log("[FIREANT] sensor added");

    return true;
}

void fireant_global_start(void) {
    g_started = true;

    g_last_send = millis();
    g_last_sync = millis();

    fireant_log("[FIREANT] started");

    fireant_global_sync();
}

void fireant_global_loop(void) {
    if (!g_started) {
        return;
    }

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

bool fireant_global_sync(void) {
    EthernetClient client;

    IPAddress server;
    if (!server.fromString(g_config.server_ip)) {
        fireant_log("[FIREANT] invalid server IP");
        return false;
    }

    int port = fireant_server_port();

    if (!client.connect(server, port)) {
        fireant_log("[FIREANT] sync connection failed");
        return false;
    }

    String body = "{";
    body += "\"id\":\"";
    body += g_config.id;
    body += "\",";
    body += "\"token\":\"";
    body += g_config.server_token;
    body += "\",";
    body += "\"ip\":\"";
    
    IPAddress ip = Ethernet.localIP();

    body += ip[0];
    body += ".";
    body += ip[1];
    body += ".";
    body += ip[2];
    body += ".";
    body += ip[3];

    body += "\",";
    body += "\"port\":\"";
    body += g_config.server_port;
    body += "\",";
    body += "\"sensors\":[";

    for (uint8_t i = 0; i < g_sensor_count; i++) {
        if (i > 0) {
            body += ",";
        }

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

    client.println("POST /api/sync HTTP/1.1");
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

    unsigned long timeout = millis();

    while (client.connected() && millis() - timeout < 3000) {
        while (client.available()) {
            char c = client.read();

            if (g_config.enable_console) {
                Serial.write(c);
            }

            timeout = millis();
        }
    }

    client.stop();

    fireant_log("[FIREANT] sync sent");

    return true;
}

bool fireant_global_send(void) {
    EthernetClient client;

    IPAddress server;
    if (!server.fromString(g_config.server_ip)) {
        fireant_log("[FIREANT] invalid server IP");
        return false;
    }

    int port = fireant_server_port();

    if (!client.connect(server, port)) {
        fireant_log("[FIREANT] send connection failed");
        return false;
    }

    String body = "{";
    body += "\"id\":\"";
    body += g_config.id;
    body += "\",";
    body += "\"sensors\":[";

    for (uint8_t i = 0; i < g_sensor_count; i++) {
        if (i > 0) {
            body += ",";
        }

        int value = analogRead(g_sensors[i].pin);

        body += "{";
        body += "\"id\":\"";
        body += g_sensors[i].id;
        body += "\",";
        body += "\"value\":";
        body += value;
        body += "}";
    }

    body += "]";
    body += "}";

    client.println("POST /api/receive HTTP/1.1");
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

    unsigned long timeout = millis();

    while (client.connected() && millis() - timeout < 3000) {
        while (client.available()) {
            char c = client.read();

            if (g_config.enable_console) {
                Serial.write(c);
            }

            timeout = millis();
        }
    }

    client.stop();

    fireant_log("[FIREANT] data sent");

    return true;
}