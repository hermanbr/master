import subprocess

KEY = "~/.ssh/id_ed25519"
UE0 = "158.39.48.105"
UE1 = "158.39.48.51"
CORE = "158.39.48.158"

SESSION = "5g-heartbeat"


def ssh(host, cmd):
    return f"ssh -t -i {KEY} ubuntu@{host} '{cmd}'"


def main():
    # standalone calls, not chained with the launch command below - pkill -f
    # matches its own invoking shell's command line too, and if that line
    # also contains "heartbeat_client.py" (because it's chained with the
    # next command), pkill kills its own parent shell and the ssh session
    # dies before the launch command ever runs.
    subprocess.run(["ssh", "-i", KEY, f"ubuntu@{UE0}", "pkill -f heartbeat_client.py"])
    subprocess.run(["ssh", "-i", KEY, f"ubuntu@{UE1}", "pkill -f heartbeat_server.py"])

    subprocess.run(["tmux", "kill-session", "-t", SESSION], stderr=subprocess.DEVNULL)
    subprocess.run(["tmux", "new-session", "-d", "-s", SESSION, ssh(UE0, "python3 -u heartbeat_client.py")])
    subprocess.run(["tmux", "split-window", "-h", "-t", SESSION, ssh(UE1, "python3 -u heartbeat_server.py")])
    # UE<->UE traffic never touches ogstun - Open5GS relays it directly at
    # the GTP-U layer inside upfd. Capture the N3 link to the gNB instead,
    # since every packet for both UEs crosses it either way.
    subprocess.run(["tmux", "split-window", "-v", "-t", SESSION, ssh(CORE, "sudo tcpdump -i enp3s0 -n udp port 2152")])
    subprocess.run(["tmux", "select-layout", "-t", SESSION, "tiled"])
    subprocess.run(["tmux", "attach-session", "-t", SESSION])


if __name__ == "__main__":
    main()
