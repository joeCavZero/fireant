from datetime import datetime
import math
import secrets
from urllib.parse import quote

from fastapi import (
    APIRouter,
    Depends,
    Form,
    HTTPException,
    Query,
    Request,
    status,
)
from fastapi.responses import RedirectResponse
from sqlalchemy import func
from sqlalchemy.exc import IntegrityError
from sqlalchemy.orm import Session
from sqlalchemy.orm import joinedload

from app.database import get_db
from app.models.node_model import Node
from app.models.node_sensor_model import NodeSensor
from app.models.node_token_model import NodeToken
from app.models.telemetry_model import Telemetry
from app.models.user_model import User
from app.schemas.user_schema import UserResponse, UserUpdate
from app.services.auth_service import authenticate_user
from app.services.jwt_service import get_current_user, get_current_user_optional
from app.services.security_service import create_access_token, hash_password

from app.config import (
    AUTH_COOKIE_NAME,
    AUTH_COOKIE_SECURE,
    JWT_EXPIRE_MINUTES,
    templater,
)


main_router = APIRouter()


def parse_datetime_filter(value: str | None) -> datetime | None:
    if not value:
        return None
    return datetime.fromisoformat(value)


def get_inventory(db: Session) -> tuple[list[Node], list[NodeSensor]]:
    nodes = (
        db.query(Node)
        .options(joinedload(Node.sensors))
        .order_by(Node.node_id)
        .all()
    )
    sensors = (
        db.query(NodeSensor)
        .options(joinedload(NodeSensor.node))
        .join(NodeSensor.node)
        .order_by(Node.node_id, NodeSensor.sensor_id)
        .all()
    )
    return nodes, sensors


def render_node_token_form(
    request: Request,
    current_user: User,
    *,
    node_token: NodeToken | None = None,
    error: str | None = None,
    token_value: str = "",
    active: bool = True,
):
    return templater.TemplateResponse(
        request=request,
        name="node_token_form.html",
        context={
            "current_user": current_user,
            "node_token": node_token,
            "error": error,
            "token_value": token_value,
            "active": active,
        },
        status_code=status.HTTP_400_BAD_REQUEST if error else status.HTTP_200_OK,
    )


@main_router.get("/")
async def index_page(
    request: Request,
    current_user: User | None = Depends(get_current_user_optional),
):
    return templater.TemplateResponse(
        request=request,
        name="index.html",
        context={"current_user": current_user},
    )


@main_router.get("/login")
async def login_page(
    request: Request,
    current_user: User | None = Depends(get_current_user_optional),
):
    if current_user:
        return RedirectResponse("/me", status_code=status.HTTP_303_SEE_OTHER)

    return templater.TemplateResponse(
        request=request,
        name="login.html",
        context={"error": request.query_params.get("error")},
    )


@main_router.post("/login")
async def login_submit(
    username: str = Form(),
    password: str = Form(),
    db: Session = Depends(get_db),
):
    user = authenticate_user(db, username, password)

    if user is None:
        return RedirectResponse(
            f"/login?error={quote('Invalid username or password.')}",
            status_code=status.HTTP_303_SEE_OTHER,
        )

    token = create_access_token(
        {
            "sub": str(user.id),
            "username": user.username,
        }
    )

    response = RedirectResponse("/me", status_code=status.HTTP_303_SEE_OTHER)

    response.set_cookie(
        key=AUTH_COOKIE_NAME,
        value=token,
        max_age=JWT_EXPIRE_MINUTES * 60,
        httponly=True,
        secure=AUTH_COOKIE_SECURE,
        samesite="lax",
    )

    return response

@main_router.get("/me")
async def me_page(
    request: Request,
    current_user: User | None = Depends(get_current_user_optional),
):
    if current_user is None:
        return RedirectResponse("/login", status_code=status.HTTP_303_SEE_OTHER)

    return templater.TemplateResponse(
        request=request,
        name="me.html",
        context={"current_user": current_user},
    )


@main_router.post("/logout")
async def logout():
    response = RedirectResponse("/", status_code=status.HTTP_303_SEE_OTHER)

    response.delete_cookie(
        AUTH_COOKIE_NAME,
        httponly=True,
        secure=AUTH_COOKIE_SECURE,
        samesite="lax",
    )

    return response

@main_router.patch("/me", response_model=UserResponse)
async def update_me(
    payload: UserUpdate,
    current_user: User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    changes = payload.model_dump(exclude_unset=True)

    if "username" in changes:
        existing_user = db.query(User).filter(
            User.username == changes["username"],
            User.id != current_user.id,
        ).first()
        if existing_user:
            raise HTTPException(
                status_code=status.HTTP_409_CONFLICT,
                detail="Username already exists",
            )

    if "password" in changes:
        changes["password_hash"] = hash_password(changes.pop("password"))

    for field, value in changes.items():
        setattr(current_user, field, value)

    db.commit()
    db.refresh(current_user)
    return current_user


@main_router.get("/dashboard")
async def dashboard(
    request: Request,
    node_id: str | None = Query(default=None),
    sensor_id: str | None = Query(default=None),
    sensor_type: str | None = Query(default=None),
    start: str | None = Query(default=None),
    end: str | None = Query(default=None),
    limit: str | None = Query(default="100"),
    current_user: User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    filter_error = None

    try:
        start_datetime = parse_datetime_filter(start)
        end_datetime = parse_datetime_filter(end)
    except ValueError:
        start_datetime = None
        end_datetime = None
        filter_error = "The supplied dates are invalid."

    if (
        start_datetime
        and end_datetime
        and start_datetime > end_datetime
    ):
        filter_error = "The start date must be earlier than the end date."
        start_datetime = None
        end_datetime = None

    try:
        parsed_limit = int(limit or "100")
    except ValueError:
        parsed_limit = 100
        filter_error = "The limit must be a number between 1 and 500."

    if not 1 <= parsed_limit <= 500:
        parsed_limit = 100
        filter_error = "The limit must be a number between 1 and 500."

    nodes, sensors = get_inventory(db)

    telemetry_query = (
        db.query(Telemetry)
        .options(
            joinedload(Telemetry.sensor).joinedload(NodeSensor.node)
        )
        .join(Telemetry.sensor)
        .join(NodeSensor.node)
    )

    if node_id:
        telemetry_query = telemetry_query.filter(Node.node_id == node_id)
    if sensor_id:
        telemetry_query = telemetry_query.filter(
            NodeSensor.sensor_id == sensor_id
        )
    if sensor_type:
        telemetry_query = telemetry_query.filter(
            NodeSensor.type == sensor_type
        )
    if start_datetime:
        telemetry_query = telemetry_query.filter(
            Telemetry.created_at >= start_datetime
        )
    if end_datetime:
        telemetry_query = telemetry_query.filter(
            Telemetry.created_at <= end_datetime
        )

    telemetries = (
        telemetry_query
        .order_by(Telemetry.created_at.desc(), Telemetry.id.desc())
        .limit(parsed_limit)
        .all()
    )
    sensor_types = sorted({sensor.type for sensor in sensors})
    chart_readings = [
        {
            "timestamp": telemetry.created_at.isoformat(),
            "value": telemetry.value,
            "node": telemetry.sensor.node.node_id,
            "sensor": telemetry.sensor.sensor_id,
            "type": telemetry.sensor.type,
            "unit": telemetry.sensor.unit,
        }
        for telemetry in reversed(telemetries)
    ]

    return templater.TemplateResponse(
        request=request,
        name="dashboard.html",
        context={
            "current_user": current_user,
            "nodes": nodes,
            "sensors": sensors,
            "sensor_types": sensor_types,
            "telemetries": telemetries,
            "chart_readings": chart_readings,
            "filter_error": filter_error,
            "filters": {
                "node_id": node_id or "",
                "sensor_id": sensor_id or "",
                "sensor_type": sensor_type or "",
                "start": start_datetime.strftime("%Y-%m-%dT%H:%M")
                if start_datetime
                else "",
                "end": end_datetime.strftime("%Y-%m-%dT%H:%M")
                if end_datetime
                else "",
                "limit": parsed_limit,
            },
        },
    )


@main_router.get("/telemetry")
async def telemetry_history(
    request: Request,
    node_id: str | None = Query(default=None),
    sensor_id: str | None = Query(default=None),
    sensor_type: str | None = Query(default=None),
    unit: str | None = Query(default=None),
    start: str | None = Query(default=None),
    end: str | None = Query(default=None),
    min_value: str | None = Query(default=None),
    max_value: str | None = Query(default=None),
    page: str | None = Query(default="1"),
    per_page: str | None = Query(default="50"),
    current_user: User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    filter_error = None

    try:
        start_datetime = parse_datetime_filter(start)
        end_datetime = parse_datetime_filter(end)
        parsed_min = float(min_value) if min_value else None
        parsed_max = float(max_value) if max_value else None
        parsed_page = max(1, int(page or "1"))
        parsed_per_page = int(per_page or "50")
        if parsed_per_page not in {25, 50, 100, 250}:
            raise ValueError
    except ValueError:
        start_datetime = None
        end_datetime = None
        parsed_min = None
        parsed_max = None
        parsed_page = 1
        parsed_per_page = 50
        filter_error = "One or more filters are invalid."

    if start_datetime and end_datetime and start_datetime > end_datetime:
        filter_error = "The start date must be earlier than the end date."
        start_datetime = None
        end_datetime = None
    if parsed_min is not None and parsed_max is not None and parsed_min > parsed_max:
        filter_error = "The minimum value must not exceed the maximum value."
        parsed_min = None
        parsed_max = None

    nodes, sensors = get_inventory(db)
    query = (
        db.query(Telemetry)
        .options(joinedload(Telemetry.sensor).joinedload(NodeSensor.node))
        .join(Telemetry.sensor)
        .join(NodeSensor.node)
    )

    if node_id:
        query = query.filter(Node.node_id == node_id)
    if sensor_id:
        query = query.filter(NodeSensor.sensor_id == sensor_id)
    if sensor_type:
        query = query.filter(NodeSensor.type == sensor_type)
    if unit:
        query = query.filter(NodeSensor.unit == unit)
    if start_datetime:
        query = query.filter(Telemetry.created_at >= start_datetime)
    if end_datetime:
        query = query.filter(Telemetry.created_at <= end_datetime)
    if parsed_min is not None:
        query = query.filter(Telemetry.value >= parsed_min)
    if parsed_max is not None:
        query = query.filter(Telemetry.value <= parsed_max)

    aggregate = query.with_entities(
        func.count(Telemetry.id),
        func.min(Telemetry.value),
        func.avg(Telemetry.value),
        func.max(Telemetry.value),
    ).one()
    total = aggregate[0]
    total_pages = max(1, math.ceil(total / parsed_per_page))
    parsed_page = min(parsed_page, total_pages)

    telemetries = (
        query.order_by(Telemetry.created_at.desc(), Telemetry.id.desc())
        .offset((parsed_page - 1) * parsed_per_page)
        .limit(parsed_per_page)
        .all()
    )

    query_params = {
        "node_id": node_id or "",
        "sensor_id": sensor_id or "",
        "sensor_type": sensor_type or "",
        "unit": unit or "",
        "start": start or "",
        "end": end or "",
        "min_value": min_value or "",
        "max_value": max_value or "",
        "per_page": parsed_per_page,
    }
    pagination_query = "&".join(
        f"{key}={quote(str(value))}"
        for key, value in query_params.items()
        if value != ""
    )

    return templater.TemplateResponse(
        request=request,
        name="telemetry.html",
        context={
            "current_user": current_user,
            "nodes": nodes,
            "sensors": sensors,
            "sensor_types": sorted({sensor.type for sensor in sensors}),
            "units": sorted({sensor.unit for sensor in sensors}),
            "telemetries": telemetries,
            "filter_error": filter_error,
            "filters": query_params,
            "summary": {
                "total": total,
                "minimum": aggregate[1],
                "average": aggregate[2],
                "maximum": aggregate[3],
            },
            "pagination": {
                "page": parsed_page,
                "per_page": parsed_per_page,
                "total_pages": total_pages,
                "has_previous": parsed_page > 1,
                "has_next": parsed_page < total_pages,
                "query": pagination_query,
            },
        },
    )


@main_router.get("/tree")
async def network_tree(
    request: Request,
    current_user: User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    nodes, sensors = get_inventory(db)
    latest_ids = (
        db.query(func.max(Telemetry.id))
        .group_by(Telemetry.sensor_id)
    )
    latest_rows = db.query(Telemetry).filter(
        Telemetry.id.in_(latest_ids)
    ).all()
    latest_by_sensor: dict[int, Telemetry] = {}
    for telemetry in latest_rows:
        latest_by_sensor.setdefault(telemetry.sensor_id, telemetry)

    graph_nodes = [
        {
            "id": "server",
            "label": "FIREANT Server",
            "kind": "server",
            "subtitle": f"{len(nodes)} nodes · {len(sensors)} sensors",
        }
    ]
    graph_links = []

    for node in nodes:
        graph_id = f"node-{node.id}"
        graph_nodes.append(
            {
                "id": graph_id,
                "label": node.node_id,
                "kind": "node",
                "subtitle": f"{node.ip}:{node.port}",
                "details": f"{len(node.sensors)} sensors",
            }
        )
        graph_links.append({"source": "server", "target": graph_id})

        for sensor in node.sensors:
            latest = latest_by_sensor.get(sensor.id)
            sensor_graph_id = f"sensor-{sensor.id}"
            graph_nodes.append(
                {
                    "id": sensor_graph_id,
                    "label": sensor.sensor_id,
                    "kind": "sensor",
                    "subtitle": f"{sensor.type} · {sensor.unit}",
                    "details": (
                        f"Latest: {latest.value:g} {sensor.unit}"
                        if latest
                        else "No telemetry yet"
                    ),
                }
            )
            graph_links.append(
                {"source": graph_id, "target": sensor_graph_id}
            )

    return templater.TemplateResponse(
        request=request,
        name="tree.html",
        context={
            "current_user": current_user,
            "graph": {"nodes": graph_nodes, "links": graph_links},
            "node_count": len(nodes),
            "sensor_count": len(sensors),
        },
    )


@main_router.get("/node_token")
async def node_token_list(
    request: Request,
    current_user: User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    node_tokens = (
        db.query(NodeToken)
        .order_by(NodeToken.created_at.desc(), NodeToken.id.desc())
        .all()
    )
    node_counts = dict(
        db.query(Node.token, func.count(Node.id))
        .group_by(Node.token)
        .all()
    )

    return templater.TemplateResponse(
        request=request,
        name="node_token.html",
        context={
            "current_user": current_user,
            "node_tokens": node_tokens,
            "node_counts": node_counts,
        },
    )


@main_router.get("/node_token/new")
async def node_token_create_page(
    request: Request,
    current_user: User = Depends(get_current_user),
):
    return render_node_token_form(
        request,
        current_user,
        token_value=secrets.token_urlsafe(32),
    )


@main_router.post("/node_token")
async def node_token_create(
    request: Request,
    token: str = Form(),
    active: bool = Form(default=False),
    current_user: User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    token = token.strip()
    if not token or len(token) > 255:
        return render_node_token_form(
            request,
            current_user,
            error="The token must contain between 1 and 255 characters.",
            token_value=token,
            active=active,
        )

    db.add(NodeToken(token=token, active=active))
    try:
        db.commit()
    except IntegrityError:
        db.rollback()
        return render_node_token_form(
            request,
            current_user,
            error="This token already exists.",
            token_value=token,
            active=active,
        )

    return RedirectResponse(
        "/node_token",
        status_code=status.HTTP_303_SEE_OTHER,
    )


@main_router.get("/node_token/{node_token_id}/edit")
async def node_token_edit_page(
    node_token_id: int,
    request: Request,
    current_user: User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    node_token = db.get(NodeToken, node_token_id)
    if node_token is None:
        raise HTTPException(
            status_code=status.HTTP_404_NOT_FOUND,
            detail="Node token not found",
        )

    return render_node_token_form(
        request,
        current_user,
        node_token=node_token,
        token_value=node_token.token,
        active=node_token.active,
    )


@main_router.post("/node_token/{node_token_id}")
async def node_token_update(
    node_token_id: int,
    request: Request,
    token: str = Form(),
    active: bool = Form(default=False),
    current_user: User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    node_token = db.get(NodeToken, node_token_id)
    if node_token is None:
        raise HTTPException(
            status_code=status.HTTP_404_NOT_FOUND,
            detail="Node token not found",
        )

    token = token.strip()
    if not token or len(token) > 255:
        return render_node_token_form(
            request,
            current_user,
            node_token=node_token,
            error="The token must contain between 1 and 255 characters.",
            token_value=token,
            active=active,
        )

    old_token = node_token.token
    node_token.token = token
    node_token.active = active

    if token != old_token:
        db.query(Node).filter(Node.token == old_token).update(
            {Node.token: token},
            synchronize_session=False,
        )

    try:
        db.commit()
    except IntegrityError:
        db.rollback()
        return render_node_token_form(
            request,
            current_user,
            node_token=node_token,
            error="This token already exists.",
            token_value=token,
            active=active,
        )

    return RedirectResponse(
        "/node_token",
        status_code=status.HTTP_303_SEE_OTHER,
    )


@main_router.post("/node_token/{node_token_id}/delete")
async def node_token_delete(
    node_token_id: int,
    current_user: User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    node_token = db.get(NodeToken, node_token_id)
    if node_token is None:
        raise HTTPException(
            status_code=status.HTTP_404_NOT_FOUND,
            detail="Node token not found",
        )

    db.delete(node_token)
    db.commit()

    return RedirectResponse(
        "/node_token",
        status_code=status.HTTP_303_SEE_OTHER,
    )
