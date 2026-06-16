# Fireant Server

The server is a FastAPI application that authenticates devices, registers nodes
and sensors, stores telemetry, and provides a web interface for administration
and data viewing.

## Technology

- Python 3.12;
- FastAPI and Uvicorn;
- SQLAlchemy;
- SQLite by default;
- Jinja2 for HTML pages;
- HS256 JWTs for user sessions;
- PBKDF2-SHA256 for passwords.

## Installation

Run these commands from `server/`:

```bash
cd server
python3 -m venv venv
source venv/bin/activate
pip install fastapi "uvicorn[standard]" sqlalchemy jinja2 python-multipart
```

The project does not currently include a pinned dependency file. The packages
above cover the external imports used by the application.

## Configuration

The application reads these environment variables:

| Variable | Default | Purpose |
| --- | --- | --- |
| `ENV` | `prod` | `test` enables OpenAPI, Swagger, and ReDoc |
| `DATABASE_URL` | `sqlite:///./fireant.db` | SQLAlchemy database URL |
| `JWT_SECRET_KEY` | insecure development value | JWT signing secret |
| `JWT_ALGORITHM` | `HS256` | Algorithm supported by the implementation |
| `JWT_EXPIRE_MINUTES` | `60` | Session lifetime |
| `AUTH_COOKIE_SECURE` | `false` | Sends the cookie only over HTTPS when `true` |
| `FIREANT_SERVER_HOST` | auto-detected | Host/IP shown by templates when available |

For production, define at least a strong secret:

```bash
export ENV=prod
export DATABASE_URL=sqlite:///./fireant.db
export JWT_SECRET_KEY='replace-with-a-long-random-secret'
export JWT_ALGORITHM=HS256
export JWT_EXPIRE_MINUTES=60
export AUTH_COOKIE_SECURE=true
```

`AUTH_COOKIE_SECURE=true` requires HTTPS access.

## Database and Initial User

Tables are created automatically when `app.server` is imported. Create the
initial administrator with:

```bash
python seed.py
```

The seed is idempotent and creates:

```text
username: admin
password: admin123
```

Change the password at `/me` before exposing the server. To change the initial
seed credentials, edit `server/seed.py` before the first run.

## Running the Server

The provided development script enables automatic documentation and reload:

```bash
./run.sh
```

It starts Uvicorn at `0.0.0.0:8000` with `ENV=test`. It also defines a simple
`JWT_SECRET_KEY` suitable only for development.

An equivalent manual command is:

```bash
ENV=test \
DATABASE_URL=sqlite:///./fireant.db \
JWT_SECRET_KEY='local-secret' \
AUTH_COOKIE_SECURE=false \
uvicorn app.server:app --host 0.0.0.0 --port 8000 --reload
```

With `ENV=test`, these URLs are available:

- Swagger UI: `http://localhost:8000/docs`;
- ReDoc: `http://localhost:8000/redoc`;
- OpenAPI schema: `http://localhost:8000/openapi.json`.

These routes are disabled when `ENV=prod`.

Requests to undefined routes are redirected to `/`. Explicit application and
device API `404` responses keep their original JSON error instead. HTML
requests that fail user authentication with `401` are redirected to `/`.

## Web Interface

| Route | Access | Function |
| --- | --- | --- |
| `GET /` | public | Home page |
| `GET /login` | public | Login form |
| `POST /login` | public | Authenticates and creates the JWT cookie |
| `POST /logout` | authenticated | Removes the cookie |
| `GET /me` | authenticated | User profile |
| `PATCH /me` | authenticated | Changes username and/or password |
| `GET /dashboard` | authenticated | Filtered readings and telemetry charts |
| `GET /telemetry` | authenticated | History, pagination, and statistics |
| `GET /tree` | authenticated | Server, node, and sensor topology |
| `GET /node_token` | authenticated | Lists device tokens |
| `GET /node_token/new` | authenticated | Token creation form |
| `POST /node_token` | authenticated | Creates a token |
| `GET /node_token/{id}/edit` | authenticated | Token edit form |
| `POST /node_token/{id}` | authenticated | Updates token and state |
| `POST /node_token/{id}/delete` | authenticated | Deletes a token |

Login stores the JWT in the HTTP-only `fireant_access_token` cookie with
`SameSite=Lax`. Protected routes also accept a user JWT through an
`Authorization: Bearer` header.

## Creating a Device Token

1. Open `/login`.
2. Sign in with a valid user.
3. Open `/node_token`.
4. Create a token and leave it enabled.
5. Set the same value as `server_token` in the firmware.

Inactive or unknown tokens receive `401` from the API. Changing a token value
through the interface also updates nodes already associated with it. Deleting a
token blocks new authenticated requests but does not automatically remove
existing nodes or telemetry.

## Data Model

### `users`

Stores a unique username, password hash, and creation timestamp.

### `node_tokens`

Credentials shared with devices. Each token is unique, can be enabled or
disabled, and records its last successful use.

### `nodes`

Each `node_id` is globally unique. The record stores its token, IP address, port,
and creation and update timestamps.

### `node_sensors`

Sensors belong to a node. The combination of node and `sensor_id` is unique and
stores the sensor type and unit.

### `telemetry`

Each row stores a `float` value, a sensor reference, and a server-generated
timestamp.

## Data Flow

1. `/api/sync` validates the token, creates or updates the node, and creates or
   updates its sensors.
2. `/api/receive` confirms that the node belongs to the token and that every
   sensor has already been synchronized.
3. A telemetry row is created for each received reading.
4. The dashboard, history, and network tree query these relationships.

The reading timestamp does not come from the device. `telemetry.created_at` is
generated by the database when the reading is received.

The dashboard charts use the exact same node, sensor, type, date, and result
limit filters as the readings table. Time-series charts are separated by unit
to avoid placing incompatible measurements on the same value scale.

The network tree marks a node as connected when it has recent telemetry, a
recent node update, or a successful short TCP connection to the node IP and
stored port. The recent-activity window is currently 5 seconds.

## Application Structure

```text
server/
├── app/
│   ├── models/       # SQLAlchemy models
│   ├── routers/      # main_router.py and api_router.py
│   ├── schemas/      # Pydantic validation
│   ├── services/     # Authentication, JWTs, and passwords
│   ├── config.py     # Environment variables
│   ├── database.py   # Engine and sessions
│   └── server.py     # FastAPI application setup
├── static/           # CSS, images, and JavaScript
├── templates/        # Jinja2 templates
├── seed.py           # Initial user
└── run.sh            # Development command
```

Template, static file, and SQLite paths are relative to the current directory.
Run Uvicorn from inside `server/`.

## Production Considerations

- replace the default JWT secret;
- change the password created by the seed;
- use HTTPS and `AUTH_COOKIE_SECURE=true`;
- run without `--reload`;
- keep Swagger and OpenAPI disabled with `ENV=prod`;
- back up `fireant.db`;
- place a reverse proxy in front of Uvicorn;
- the device API currently uses HTTP and static tokens, so protect the network
  or add TLS before using it in an untrusted environment.
