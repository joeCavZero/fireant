from fastapi import FastAPI
from fastapi.staticfiles import StaticFiles

import app.config
import app.routers as routers
from app.database import Base, engine
from app.models import user_model  # noqa: F401

Base.metadata.create_all(bind=engine)

app = FastAPI(
    title="FIREANT",
    docs_url=app.config.ENV_DOCS_URL,
    redoc_url=app.config.ENV_REDOC_URL,
    openapi_url=app.config.ENV_OPENAPI_URL,
)

app.mount("/static", StaticFiles(directory="static"), name="static")

app.include_router(routers.main_router)
