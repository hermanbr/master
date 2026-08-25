import json
import socket

port: int = 4567

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind(("", port))


def main():

    obj = None
    while True:
        data, addr = sock.recvfrom(4096)
        obj = json.loads(data)

        print(f"{obj['time']} Seq: {obj['seq']} from: {addr}")


if __name__ == "__main__":
    main()
