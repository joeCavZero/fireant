from datetime import datetime

from sqlalchemy import DateTime, Float, ForeignKey
from sqlalchemy.orm import Mapped, mapped_column, relationship
from sqlalchemy.sql import func

from app.database import Base


class Telemetry(Base):
    __tablename__ = "telemetry"

    id: Mapped[int] = mapped_column(primary_key=True)
    value: Mapped[float] = mapped_column(
        Float,
        nullable=False,
    )

    sensor_id: Mapped[int] = mapped_column(
        ForeignKey("node_sensors.id"),
        nullable=False,
    )

    sensor = relationship(
        "NodeSensor",
        back_populates="telemetries",
    )

    created_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True),
        server_default=func.now(),
        nullable=False,
    )
