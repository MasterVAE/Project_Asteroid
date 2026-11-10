FROM python:3.12-slim
WORKDIR /opt/asteroid
COPY asteroid.py runner.py ./
USER 65534:65534
ENTRYPOINT ["python3", "-I", "-u", "/opt/asteroid/runner.py"]
