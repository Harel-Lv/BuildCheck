FROM python:3.10-slim

WORKDIR /app

COPY BuildCheck/Engine/requirements.txt ./requirements.txt
RUN python -m pip install --no-cache-dir -r requirements.txt

COPY BuildCheck/Engine /app

EXPOSE 9090

CMD ["python", "-m", "uvicorn", "engine_service:app", "--host", "0.0.0.0", "--port", "9090"]
