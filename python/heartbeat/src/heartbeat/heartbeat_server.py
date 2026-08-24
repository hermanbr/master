import json
import socket
import time

port: int = 4567
host = "10.45.0.10"

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)


def main():
    seq = 0
    while True:
        payload = json.dumps({"seq": seq, "time": time.time()}).encode("utf-8")
        sock.sendto(payload, (host, port))
        print(f"sent seq={seq} to {host}:{port}")
        seq += 1
        time.sleep(1)


if __name__ == "__main__":
    main()
