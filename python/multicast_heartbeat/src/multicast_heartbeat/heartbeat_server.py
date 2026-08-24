import json
import time

from multicast import send

port: int = 4567

sender = send.McastSAY()

def main():
    seq = 0
    while True:
        payload = json.dumps({"seq": seq, "time": time.time()}).encode("utf-8")

        sender(group='224.0.0.1', port=port, ttl=1, data=payload)

        print(f"sent seq={seq} to multicast:{port}")
        seq += 1
        time.sleep(1)


if __name__ == "__main__":
    main()
