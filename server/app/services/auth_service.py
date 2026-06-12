from sqlalchemy.orm import Session

from app.models.user_model import User
from app.services.security_service import create_access_token, verify_password


def login_user(db, username, password):
    user = authenticate_user(db, username, password)

    if user is None:
        return None

    token = create_access_token({
        "sub": str(user.id),
        "username": user.username,
    })

    return user, token


def authenticate_user(db: Session, login: str, password: str) -> User | None:
    normalized_login = login.strip().lower()
    user = db.query(User).filter(User.username == normalized_login).first()

    if user is None or not verify_password(password, user.password_hash):
        return None
    return user
