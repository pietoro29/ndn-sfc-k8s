FROM python:3.10-slim
RUN pip install --no-cache-dir pyyaml

WORKDIR /app

COPY generate_values.py .
CMD ["python", "generate_values.py"]
