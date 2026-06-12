from datetime import datetime, timezone

from fastapi import APIRouter, Depends, HTTPException, status
from fastapi.security import HTTPAuthorizationCredentials, HTTPBearer
from sqlalchemy.orm import Session

from app.database import get_db
from app.models.node_model import Node
from app.models.node_sensor_model import NodeSensor
from app.models.node_token_model import NodeToken
from app.models.telemetry_model import Telemetry
from app.schemas.node_schema import NodeSync, TelemetryReceive

api_router = APIRouter()
device_bearer_scheme = HTTPBearer(auto_error=False)


def get_device_token(
    credentials: HTTPAuthorizationCredentials | None = Depends(
        device_bearer_scheme
    ),
    db: Session = Depends(get_db),
) -> NodeToken:
    if credentials is None or credentials.scheme.lower() != "bearer":
        raise HTTPException(
            status_code=status.HTTP_401_UNAUTHORIZED,
            detail="Device bearer token required",
            headers={"WWW-Authenticate": "Bearer"},
        )

    node_token = (
        db.query(NodeToken)
        .filter(
            NodeToken.token == credentials.credentials,
            NodeToken.active.is_(True),
        )
        .first()
    )
    if node_token is None:
        raise HTTPException(
            status_code=status.HTTP_401_UNAUTHORIZED,
            detail="Invalid device token",
            headers={"WWW-Authenticate": "Bearer"},
        )
    return node_token


@api_router.post("/api/sync")
async def sync_node(
    data: NodeSync,
    node_token: NodeToken = Depends(get_device_token),
    db: Session = Depends(get_db),
):
    if node_token.token != data.token:
        raise HTTPException(
            status_code=status.HTTP_401_UNAUTHORIZED,
            detail="Body token does not match bearer token",
            headers={"WWW-Authenticate": "Bearer"},
        )

    node = (
        db.query(Node)
        .filter(Node.node_id == data.id)
        .first()
    )

    if node is None:
        node = Node(
            node_id=data.id,
            token=data.token,
            ip=data.ip,
            port=data.port,
        )

        db.add(node)
        db.flush()
    else:
        node.token = data.token
        node.ip = data.ip
        node.port = data.port

    synced_count = 0
    sensor_ids = set()

    for sensor_data in data.sensors:
        if sensor_data.id in sensor_ids:
            raise HTTPException(
                status_code=status.HTTP_422_UNPROCESSABLE_ENTITY,
                detail=f"Duplicate sensor id: {sensor_data.id}",
            )
        sensor_ids.add(sensor_data.id)

        sensor = (
            db.query(NodeSensor)
            .filter(
                NodeSensor.node_id == node.id,
                NodeSensor.sensor_id == sensor_data.id,
            )
            .first()
        )

        if sensor is None:
            sensor = NodeSensor(
                sensor_id=sensor_data.id,
                node_id=node.id,
                type=sensor_data.type,
                unit=sensor_data.unit,
            )

            db.add(sensor)

        else:
            sensor.type = sensor_data.type
            sensor.unit = sensor_data.unit

        synced_count += 1

    node_token.last_used_at = datetime.now(timezone.utc)

    try:
        db.commit()
    except Exception:
        db.rollback()
        raise

    return {
        "ok": True,
        "node_id": data.id,
        "sensors_synced": synced_count,
    }

@api_router.post("/api/receive")
async def receive(
    data: TelemetryReceive,
    node_token: NodeToken = Depends(get_device_token),
    db: Session = Depends(get_db),
):
    node = (
        db.query(Node)
        .filter(
            Node.node_id == data.id,
            Node.token == node_token.token,
        )
        .first()
    )
    if node is None:
        raise HTTPException(
            status_code=status.HTTP_404_NOT_FOUND,
            detail="Node is not synced with this token",
        )

    reading_ids = [reading.id for reading in data.sensors]
    if len(reading_ids) != len(set(reading_ids)):
        raise HTTPException(
            status_code=status.HTTP_422_UNPROCESSABLE_ENTITY,
            detail="Sensor ids must be unique in a telemetry payload",
        )

    sensors = (
        db.query(NodeSensor)
        .filter(
            NodeSensor.node_id == node.id,
            NodeSensor.sensor_id.in_(reading_ids),
        )
        .all()
    )
    sensors_by_id = {sensor.sensor_id: sensor for sensor in sensors}
    missing_ids = sorted(set(reading_ids) - sensors_by_id.keys())
    if missing_ids:
        raise HTTPException(
            status_code=status.HTTP_422_UNPROCESSABLE_ENTITY,
            detail={
                "message": "Sensors must be synced before sending telemetry",
                "sensor_ids": missing_ids,
            },
        )

    for reading in data.sensors:
        db.add(
            Telemetry(
                sensor_id=sensors_by_id[reading.id].id,
                value=reading.value,
            )
        )

    node_token.last_used_at = datetime.now(timezone.utc)

    try:
        db.commit()
    except Exception:
        db.rollback()
        raise

    return {
        "ok": True,
        "node_id": data.id,
        "readings_received": len(data.sensors),
    }
