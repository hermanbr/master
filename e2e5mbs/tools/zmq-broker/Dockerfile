FROM python:3.12-slim
RUN pip install --no-cache-dir pyzmq==26.2.0 numpy==2.1.3
COPY zmq_broker.py /usr/local/bin/zmq_broker.py
ENTRYPOINT ["python3", "-u", "/usr/local/bin/zmq_broker.py"]
