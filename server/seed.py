from app.database import Base, SessionLocal, engine
from app.models.user_model import User
from app.services.security_service import hash_password


USERS = (
    {
        "username": "admin",
        "password": "admin123",
    },
)


def seed_users() -> tuple[int, int]:
    created = 0
    skipped = 0

    Base.metadata.create_all(bind=engine)

    with SessionLocal() as db:
        try:
            for user_data in USERS:
                existing_user = (
                    db.query(User)
                    .filter(User.username == user_data["username"])
                    .first()
                )

                if existing_user:
                    skipped += 1
                    continue

                db.add(
                    User(
                        username=user_data["username"],
                        password_hash=hash_password(user_data["password"]),
                    )
                )

                created += 1

            db.commit()
            return created, skipped

        except Exception:
            db.rollback()
            raise


def main() -> None:
    created, skipped = seed_users()
    print(f"users: {created} created, {skipped} skipped")


if __name__ == "__main__":
    main()
