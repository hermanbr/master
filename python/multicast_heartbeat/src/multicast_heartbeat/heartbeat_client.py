import json

from multicast import recv

port: int = 4567


receiver = recv.McastRECV()


def main():

    obj = None
    while True:
        success, data = receiver(group="224.0.0.1", port=port, ttl=1)
        if not success:
            continue
        obj = json.loads(data)

        print(f"---- MULTICAST {obj['time']} Seq: {obj['seq']}")


if __name__ == "__main__":
    main()
