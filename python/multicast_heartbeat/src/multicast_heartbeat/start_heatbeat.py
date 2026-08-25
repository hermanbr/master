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


def run_subprocess(args):
    subprocess.run(args=args, check=True)


def try_run_subprocess(args):
    """Processes called here are allowed to fail. E.g. tmux and pkill processes"""
    try:
        run_subprocess(args=args)
    except subprocess.CalledProcessError:
        pass


def deploy(host):
    run_subprocess(["ssh", "-i", KEY, f"ubuntu@{host}", "rm -rf ~/multicast_heartbeat"])
    run_subprocess(
        [
            "scp",
            "-r",
            "-i",
            KEY,
            str(LOCAL_PROJECT),
            f"ubuntu@{host}:~/multicast_heartbeat",
        ]
    )
    run_subprocess(
        [
            "ssh",
            "-i",
            KEY,
            f"ubuntu@{host}",
            "rm -rf ~/multicast_heartbeat/.venv ~/multicast_heartbeat/dist",
        ]
    )
    run_subprocess(
        ["ssh", "-i", KEY, f"ubuntu@{host}", f"cd multicast_heartbeat && {UV} sync"]
    )


def main():
    deploy(UE0)
    deploy(UE1)

    run_subprocess(["ssh", "-i", KEY, f"ubuntu@{UE0}", "pkill -f heartbeat_client.py"])
    run_subprocess(["ssh", "-i", KEY, f"ubuntu@{UE1}", "pkill -f heartbeat_server.py"])

    try_run_subprocess(["tmux", "kill-session", "-t", SESSION])
    try_run_subprocess(
        [
            "tmux",
            "new-session",
            "-d",
            "-s",
            SESSION,
            ssh(
                UE0,
                f"cd multicast_heartbeat && {UV} run src/multicast_heartbeat/heartbeat_client.py",
            ),
        ]
    )
    try_run_subprocess(
        [
            "tmux",
            "split-window",
            "-h",
            "-t",
            SESSION,
            ssh(
                UE1,
                f"cd multicast_heartbeat && {UV} run src/multicast_heartbeat/heartbeat_server.py",
            ),
        ]
    )
    try_run_subprocess(
        [
            "tmux",
            "split-window",
            "-v",
            "-t",
            SESSION,
            ssh(CORE, "sudo tcpdump -i enp3s0 -n udp port 2152"),
        ]
    )
    try_run_subprocess(["tmux", "select-layout", "-t", SESSION, "tiled"])
    try_run_subprocess(["tmux", "attach-session", "-t", SESSION])


if __name__ == "__main__":
    main()
