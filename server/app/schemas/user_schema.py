from datetime import datetime
from pydantic import BaseModel, ConfigDict, Field, field_validator


class UserCreate(BaseModel):
    username: str = Field(..., min_length=3, max_length=64)
    password: str = Field(..., min_length=8, max_length=128)

    @field_validator("username", mode="before")
    @classmethod
    def validate_username(cls, value: str) -> str:
        if not isinstance(value, str):
            raise ValueError("Username must be a string")
        value = value.strip().lower()
        if not value.replace("_", "").replace("-", "").isalnum():
            raise ValueError(
                "Username may contain only letters, numbers, hyphens and underscores"
            )
        return value


class UserLogin(BaseModel):
    username: str = Field(..., min_length=1, max_length=64)
    password: str = Field(..., min_length=1, max_length=128)


class UserUpdate(BaseModel):
    username: str | None = Field(default=None, min_length=3, max_length=64)
    password: str | None = Field(default=None, min_length=8, max_length=128)

    @field_validator("username", mode="before")
    @classmethod
    def validate_username(cls, value: str | None) -> str | None:
        if value is None:
            return value
        if not isinstance(value, str):
            raise ValueError("Username must be a string")
        value = value.strip().lower()
        if not value.replace("_", "").replace("-", "").isalnum():
            raise ValueError(
                "Username may contain only letters, numbers, hyphens and underscores"
            )
        return value


class UserResponse(BaseModel):
    model_config = ConfigDict(from_attributes=True)

    id: int
    username: str
    created_at: datetime
