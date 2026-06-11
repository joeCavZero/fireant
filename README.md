<div align="center">
  <img src="docs/assets/fireant-logo.png" width="300" alt="Fireant Logo" />
</div>

<h1 align="center">FIREANT</h1>

Fireant is a flexible IoT platform and C library designed to collect, process and visualize data from multiple sensor nodes connected over a network.

The platform allows heterogeneous devices to dynamically register themselves and report sensor data without requiring modifications to the central server.

---

## Features

- Dynamic node registration
- Token-based node authentication
- Support for heterogeneous sensors
- Real-time data collection
- Historical measurements storage
- Online/offline node monitoring
- Sensor metadata discovery
- Web dashboard for visualization and filtering
- HTTP + JSON communication

---

## Supported Sensors

Examples:
* Luminosity

Any sensor can be integrated as long as the node reports its metadata and readings using the Fireant protocol.

---

## Tested Boards

* ESP32 DevKit V1
* NodeMCU ESP32
* ESP32-WROOM-32

---

## Example Node Registration

```json
{
  "node_id": "node-01",
  "token": "abc123",
  "name": "Laboratory Node",
  "sensors": [
    {
      "sensor_id": "temp-01",
      "type": "temperature",
      "category": "environmental",
      "unit": "celsius"
    }
  ]
}
```

---

## Example Sensor Reading

```json
{
  "node_id": "node-01",
  "timestamp": "2026-06-10T18:00:00",
  "readings": [
    {
      "sensor_id": "temp-01",
      "value": 28.5
    }
  ]
}
```

---

## Technology Stack

### Firmware

* ESP-IDF
* C

### Backend

* FastAPI

### Database

* SQLite

### Frontend

* HTML
* CSS
* JavaScript

### Communication

* HTTP
* JSON

---

## Project Goals

Fireant aims to provide a reusable infrastructure for IoT deployments where new nodes and sensors can be added dynamically without requiring changes to the central monitoring system.