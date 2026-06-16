import os
import fcntl
import ipaddress
import socket
import struct
from fastapi.templating import Jinja2Templates

from datetime import timedelta

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


def get_interface_ipv4_addresses() -> list[str]:
    addresses = []
    try:
        probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    except OSError:
        return addresses

    with probe:
        try:
            interfaces = socket.if_nameindex()
        except OSError:
            return addresses

        for _, name in interfaces:
            try:
                packed_name = struct.pack("256s", name[:15].encode())
                response = fcntl.ioctl(probe.fileno(), 0x8915, packed_name)
                addresses.append(socket.inet_ntoa(response[20:24]))
            except OSError:
                continue
    return addresses


def get_server_lan_ip() -> str:
    configured_host = os.getenv("FIREANT_SERVER_HOST")
    if configured_host:
        return configured_host

    for address in get_interface_ipv4_addresses():
        parsed = ipaddress.ip_address(address)
        if parsed.is_private and not parsed.is_loopback:
            return address

    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
            probe.connect(("8.8.8.8", 80))
            address = probe.getsockname()[0]
            if not ipaddress.ip_address(address).is_loopback:
                return address
    except OSError:
        pass

    try:
        return socket.gethostbyname(socket.gethostname())
    except OSError:
        return "localhost"


templater.env.globals["server_lan_ip"] = get_server_lan_ip

#

NODE_ACTIVE_WINDOW = timedelta(seconds=5)