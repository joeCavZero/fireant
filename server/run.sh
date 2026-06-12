#!/usr/bin/env bash

ENV=test \
DATABASE_URL=sqlite:///./fireant.db \
JWT_SECRET_KEY=abc123 \
JWT_ALGORITHM=HS256 \
JWT_EXPIRE_MINUTES=120 \
AUTH_COOKIE_SECURE=true \
./venv/bin/uvicorn app.server:app