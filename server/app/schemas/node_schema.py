from pydantic import BaseModel, Field


class NodeSensorSync(BaseModel):
    id: str = Field(min_length=1, max_length=255)
    type: str = Field(min_length=1, max_length=255)
    unit: str = Field(min_length=1, max_length=16)


class NodeSync(BaseModel):
    id: str = Field(min_length=1, max_length=255)
    token: str = Field(min_length=1, max_length=255)
    ip: str = Field(min_length=1, max_length=64)
    port: str = Field(min_length=1, max_length=16)

    sensors: list[NodeSensorSync] = Field(default_factory=list)


class SensorReading(BaseModel):
    id: str = Field(min_length=1, max_length=255)
    value: float


class TelemetryReceive(BaseModel):
    id: str = Field(min_length=1, max_length=255)
    sensors: list[SensorReading] = Field(min_length=1)
