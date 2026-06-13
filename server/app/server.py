from fastapi import FastAPI, Request
from fastapi.exception_handlers import http_exception_handler
from fastapi.responses import RedirectResponse
from fastapi.staticfiles import StaticFiles
from starlette.exceptions import HTTPException as StarletteHTTPException

import app.config
import app.routers as routers
from app.database import Base, engine
import app.models  # noqa: F401

Base.metadata.create_all(bind=engine)

app = FastAPI(
    title="FIREANT",
    docs_url=app.config.ENV_DOCS_URL,
    redoc_url=app.config.ENV_REDOC_URL,
    openapi_url=app.config.ENV_OPENAPI_URL,
)


@app.exception_handler(StarletteHTTPException)
async def handle_http_exception(
    request: Request,
    exc: StarletteHTTPException,
):
    if exc.status_code == 404 and exc.detail == "Not Found":
        return RedirectResponse(url="/", status_code=303)
    return await http_exception_handler(request, exc)


app.mount("/static", StaticFiles(directory="static"), name="static")

app.include_router(routers.main_router)
app.include_router(routers.api_router)
