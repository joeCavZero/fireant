from datetime import datetime

from sqlalchemy import DateTime, ForeignKey, String, UniqueConstraint
from sqlalchemy.orm import Mapped, mapped_column, relationship
from sqlalchemy.sql import func

from app.database import Base


class NodeSensor(Base):
    __tablename__ = "node_sensors"
    __table_args__ = (
        UniqueConstraint("node_id", "sensor_id", name="uq_node_sensor"),
    )

    id: Mapped[int] = mapped_column(primary_key=True)

    sensor_id: Mapped[str] = mapped_column(
        String(255),
        nullable=False,
    )

    type: Mapped[str] = mapped_column(
        String(255),
        nullable=False,
    )

    unit: Mapped[str] = mapped_column(
        String(16),
        nullable=False,
    )

    node_id: Mapped[int] = mapped_column(
        ForeignKey("nodes.id"),
        nullable=False,
    )

    node = relationship(
        "Node",
        back_populates="sensors",
    )

    telemetries = relationship(
        "Telemetry",
        back_populates="sensor",
        cascade="all, delete-orphan",
    )

    created_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True),
        server_default=func.now(),
        nullable=False,
    )

    updated_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True),
        server_default=func.now(),
        onupdate=func.now(),
        nullable=False,
    )
