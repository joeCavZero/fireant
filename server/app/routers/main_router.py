from urllib.parse import quote

from fastapi import APIRouter, Depends, Form, HTTPException, Request, status
from fastapi.responses import RedirectResponse
from sqlalchemy.orm import Session

from app.database import get_db
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
            f"/login?error={quote('Usuário ou senha inválidos.')}",
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
