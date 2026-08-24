import subprocess
from pathlib import Path

KEY = "~/.ssh/id_ed25519"
UE0 = "158.39.48.105"
UE1 = "158.39.48.51"
CORE = "158.39.48.158"

SESSION = "5g-heartbeat"

LOCAL_PROJECT = Path(__file__).resolve().parents[2]
UV = "~/.local/bin/uv"


def ssh(host, cmd):
    return f"ssh -t -i {KEY} ubuntu@{host} '{cmd}'"


def deploy(host):
    subprocess.run(["ssh", "-i", KEY, f"ubuntu@{host}", "rm -rf ~/multicast_heartbeat"])
    subprocess.run(["scp", "-r", "-i", KEY, str(LOCAL_PROJECT), f"ubuntu@{host}:~/multicast_heartbeat"])
    # .venv/dist came along for the ride from the local (macOS) copy and are
    # the wrong platform - drop them so uv sync builds a fresh Linux one.
    subprocess.run(["ssh", "-i", KEY, f"ubuntu@{host}", "rm -rf ~/multicast_heartbeat/.venv ~/multicast_heartbeat/dist"])
    subprocess.run(["ssh", "-i", KEY, f"ubuntu@{host}", f"cd multicast_heartbeat && {UV} sync"])


def main():
    deploy(UE0)
    deploy(UE1)

    subprocess.run(["ssh", "-i", KEY, f"ubuntu@{UE0}", "pkill -f heartbeat_client.py"])
    subprocess.run(["ssh", "-i", KEY, f"ubuntu@{UE1}", "pkill -f heartbeat_server.py"])

    subprocess.run(["tmux", "kill-session", "-t", SESSION], stderr=subprocess.DEVNULL)
    subprocess.run(["tmux", "new-session", "-d", "-s", SESSION,
                     ssh(UE0, f"cd multicast_heartbeat && {UV} run src/multicast_heartbeat/heartbeat_client.py")])
    subprocess.run(["tmux", "split-window", "-h", "-t", SESSION,
                     ssh(UE1, f"cd multicast_heartbeat && {UV} run src/multicast_heartbeat/heartbeat_server.py")])
    # UE<->UE traffic never touches ogstun - Open5GS relays it directly at
    # the GTP-U layer inside upfd. Capture the N3 link to the gNB instead,
    # since every packet for both UEs crosses it either way.
    subprocess.run(["tmux", "split-window", "-v", "-t", SESSION, ssh(CORE, "sudo tcpdump -i enp3s0 -n udp port 2152")])
    subprocess.run(["tmux", "select-layout", "-t", SESSION, "tiled"])
    subprocess.run(["tmux", "attach-session", "-t", SESSION])


if __name__ == "__main__":
    main()
