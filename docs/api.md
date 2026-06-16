# Device API

The HTTP API connects embedded libraries to the Fireant server. It provides two
endpoints:

- `POST /api/sync`: registers or updates a node and its sensors;
- `POST /api/receive`: receives sensor readings.

The JSON examples below match the server's current schemas and routers.

## Base URL

During development:

```text
http://SERVER_IP:8000
```

The current libraries use HTTP without TLS. The server can be published behind
an HTTPS proxy, but the firmware must be adapted to use HTTPS.

## Authentication

First create and enable a token on the `/node_token` page. Every API request
requires:

```http
Authorization: Bearer DEVICE_TOKEN
Content-Type: application/json
```

This token is separate from the user JWT used by the web interface.

Common authentication errors:

| Status | `detail` | Cause |
| --- | --- | --- |
| `401` | `Device bearer token required` | Missing header or non-Bearer scheme |
| `401` | `Invalid device token` | Unknown or inactive token |
| `401` | `Body token does not match bearer token` | Sync body token differs from header |

## Synchronize a Node and Its Sensors

```http
POST /api/sync
```

### Request Body

```json
{
  "id": "esp32_1",
  "token": "DEVICE_TOKEN",
  "ip": "192.168.0.42",
  "port": "8000",
  "sensors": [
    {
      "id": "ldr_1",
      "type": "light",
      "unit": "raw"
    },
    {
      "id": "temp_1",
      "type": "temperature",
      "unit": "celsius"
    }
  ]
}
```

### Fields

| Field | Type | Rules |
| --- | --- | --- |
| `id` | string | Required, 1 to 255 characters; globally identifies the node |
| `token` | string | Required, 1 to 255; must match the Bearer token |
| `ip` | string | Required, 1 to 64; stored as metadata |
| `port` | string | Required, 1 to 16; stored as metadata |
| `sensors` | array | Optional; defaults to an empty list |
| `sensors[].id` | string | Required, 1 to 255; unique within the payload |
| `sensors[].type` | string | Required, 1 to 255 |
| `sensors[].unit` | string | Required, 1 to 16 |

The server creates the node if its `id` does not exist. Otherwise, it updates
the token, IP address, and port. New sensors are created, while existing sensor
types and units are updated.

Synchronization does not remove old sensors that no longer appear in the array.
The token's `last_used_at` field is updated after a successful synchronization.

### Success Response

```json
{
  "ok": true,
  "node_id": "esp32_1",
  "sensors_synced": 2
}
```

### Duplicate Sensor Error

If two items have the same `id`:

```json
{
  "detail": "Duplicate sensor id: ldr_1"
}
```

Status: `422 Unprocessable Entity`.

### curl Example

```bash
curl -X POST http://localhost:8000/api/sync \
  -H 'Authorization: Bearer DEVICE_TOKEN' \
  -H 'Content-Type: application/json' \
  -d '{
    "id": "esp32_1",
    "token": "DEVICE_TOKEN",
    "ip": "192.168.0.42",
    "port": "8000",
    "sensors": [
      {"id": "ldr_1", "type": "light", "unit": "raw"}
    ]
  }'
```

## Submit Telemetry

```http
POST /api/receive
```

The node and every submitted sensor must have been synchronized first.

### Request Body

```json
{
  "id": "esp32_1",
  "sensors": [
    {
      "id": "ldr_1",
      "value": 1834.0
    },
    {
      "id": "temp_1",
      "value": 24.5
    }
  ]
}
```

### Fields

| Field | Type | Rules |
| --- | --- | --- |
| `id` | string | Required, 1 to 255; previously synchronized node |
| `sensors` | array | Required, at least one reading |
| `sensors[].id` | string | Required, 1 to 255; unique and already synchronized |
| `sensors[].value` | number | Required; converted and stored as `float` |

The server sets the timestamp when the row is inserted. The API does not accept
a device-generated timestamp. The token's `last_used_at` field is updated after
a successful telemetry submission.

### Success Response

```json
{
  "ok": true,
  "node_id": "esp32_1",
  "readings_received": 2
}
```

### Node Not Associated with the Token

```json
{
  "detail": "Node is not synced with this token"
}
```

Status: `404 Not Found`.

This happens when the node has not been synchronized or was synchronized with a
different token.

### Duplicate IDs

```json
{
  "detail": "Sensor ids must be unique in a telemetry payload"
}
```

Status: `422 Unprocessable Entity`.

### Sensor Not Yet Synchronized

```json
{
  "detail": {
    "message": "Sensors must be synced before sending telemetry",
    "sensor_ids": [
      "temp_1"
    ]
  }
}
```

Status: `422 Unprocessable Entity`.

### curl Example

```bash
curl -X POST http://localhost:8000/api/receive \
  -H 'Authorization: Bearer DEVICE_TOKEN' \
  -H 'Content-Type: application/json' \
  -d '{
    "id": "esp32_1",
    "sensors": [
      {"id": "ldr_1", "value": 1834.0}
    ]
  }'
```

## Validation Errors

When a field is missing, empty, too long, or has an invalid type, FastAPI
returns `422` with an error list. Abbreviated example:

```json
{
  "detail": [
    {
      "type": "missing",
      "loc": ["body", "id"],
      "msg": "Field required"
    }
  ]
}
```

## Recommended Sequence

1. Create and enable a token on the server.
2. Start the device with stable `id` and token values.
3. Call `/api/sync` after connecting to the network.
4. Submit to `/api/receive` only after a successful synchronization.
5. Synchronize again when sensors, type, unit, IP address, or port changes.
6. Keep sensor IDs unique within each node.
