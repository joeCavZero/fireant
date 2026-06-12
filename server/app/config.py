import os
from fastapi.templating import Jinja2Templates

# ENV

ENV = os.getenv("ENV", "prod")

ENV_DOCS_URL = None
ENV_REDOC_URL = None
ENV_OPENAPI_URL = None

match ENV:
    case "prod":
        ENV_DOCS_URL = None
        ENV_REDOC_URL = None
        ENV_OPENAPI_URL = None
    case "test":
        ENV_DOCS_URL = "/docs"
        ENV_REDOC_URL = "/redoc"
        ENV_OPENAPI_URL = "/openapi.json"

# DATABASE AND STORAGE

DATABASE_URL = os.getenv("DATABASE_URL", "sqlite:///./fireant.db")

# JWT

JWT_SECRET_KEY = os.getenv(
    "JWT_SECRET_KEY",
    "change-this-secret-before-running-in-production",
)

JWT_ALGORITHM = os.getenv("JWT_ALGORITHM", "HS256")

JWT_EXPIRE_MINUTES = int(os.getenv("JWT_EXPIRE_MINUTES", "60"))

AUTH_COOKIE_NAME = "fireant_access_token"

AUTH_COOKIE_SECURE = os.getenv("AUTH_COOKIE_SECURE", "false").lower() == "true"

# TEMPLATER

templater = Jinja2Templates(directory="templates")
