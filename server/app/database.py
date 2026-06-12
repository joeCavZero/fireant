from sqlalchemy import create_engine
from sqlalchemy.orm import declarative_base, sessionmaker

import app.config

connect_args = (
    {"check_same_thread": False}
    if app.config.DATABASE_URL.startswith("sqlite")
    else {}
)

engine = create_engine(app.config.DATABASE_URL, connect_args=connect_args)

SessionLocal = sessionmaker(autocommit=False, autoflush=False, bind=engine)

Base = declarative_base()


async def get_db():
    db = SessionLocal()

    try:
        yield db
    finally:
        db.close()
