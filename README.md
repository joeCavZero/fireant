<div align="center">
  <img src="docs/assets/fireant-logo.png" width="300" alt="Fireant Logo" />
</div>

<h1 align="center">FIREANT</h1>

Fireant is a telemetry platform for IoT projects. It connects embedded nodes to
a central server, dynamically registers their available sensors, and stores
readings for viewing through a web interface.

The repository contains:

- a C library for ESP-IDF projects;
- a C++ library for Arduino with Ethernet;
- a FastAPI server with SQLite and a web interface;
- working examples for ESP32 and Arduino Uno.

## How It Works

1. A device token is created and enabled through the server interface.
2. The node starts and registers its identity and sensors through
   `POST /api/sync`.
3. The node periodically submits readings to `POST /api/receive`.
4. The server associates readings with registered sensors and stores telemetry.
5. Authenticated users view the data through the dashboard, history, and network
   tree pages.

Device routes use their own Bearer tokens. The web interface uses a JWT session
stored in an HTTP-only cookie.

## Features

- dynamic node and sensor registration;
- separate authentication for users and devices;
- periodic collection from multiple sensors;
- historical storage in SQLite;
- dashboard with filters;
- paginated history and statistics;
- node and sensor topology view;
- libraries for ESP-IDF and Arduino;
- HTTP communication with JSON payloads.

## Project Structure

```text
.
├── arduino/       # Arduino + Ethernet library
├── espressif/     # ESP-IDF + Wi-Fi library
├── examples/      # Working library examples
├── server/        # FastAPI application, web pages, and database
└── docs/          # Detailed documentation
```

## Quick Start

To run the server locally:

```bash
cd server
python3 -m venv venv
source venv/bin/activate
pip install fastapi "uvicorn[standard]" sqlalchemy jinja2 python-multipart
python seed.py
./run.sh
```

The application will be available at `http://localhost:8000`. The seed creates
the user `admin` with password `admin123`. Change these credentials and
`JWT_SECRET_KEY` before any real deployment.

After signing in, create a token at `/node_token` and use the same value in the
device configuration.

## Documentation

- [ESP-IDF](./docs/espressif.md) - Installation, configuration, and library usage
  for Espressif devices.
- [Arduino](./docs/arduino.md) - Library usage with Arduino Uno and an Ethernet
  interface.
- [Server](./docs/server.md) - Server installation, configuration, architecture,
  and web interface.
- [API](./docs/api.md) - Authentication, endpoints, JSON payloads, and device API
  responses.

## Hardware Used by the Examples

- ESP32-WROOM-32 (xx5r69 board), with an LDR on `ADC_CHANNEL_6`;
- Arduino Uno, a compatible Ethernet shield or module, and an LDR on pin `A0`.

Other sensors can be used as long as they are registered with an identifier,
type, and unit. See the relevant library documentation for the input methods
currently implemented on each platform.
