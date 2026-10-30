#!/usr/bin/env python3
"""ZMQ IQ broker: one srsRAN/OCUDU-style gNB <-> N UEs (e.g. OAI nrUE oai_zmqdevif).

Both sides use the srsRAN ZMQ convention: the TX side binds a REP socket and
replies with raw complex64 samples, the RX side connects a REQ socket and sends
a 1-byte dummy request.

  DL: gNB tx (REP)  <- REQ [broker] REP ->  UE_i rx (REQ)    same block to every UE
  UL: UE_i tx (REP) <- REQ [broker] REP ->  gNB rx (REQ)     samples summed over UEs
"""

import argparse
import logging
import threading
import time

import numpy as np
import zmq

log = logging.getLogger("zmq-broker")
DUMMY = b"\x00"


class Counters:
    def __init__(self):
        self.lock = threading.Lock()
        self.dl_blocks = self.dl_samples = 0
        self.ul_blocks = self.ul_samples = 0

    def add(self, direction, nsamples):
        with self.lock:
            if direction == "dl":
                self.dl_blocks += 1
                self.dl_samples += nsamples
            else:
                self.ul_blocks += 1
                self.ul_samples += nsamples


def downlink(ctx, gnb_tx, ue_dl_binds, counters):
    """Fan out every gNB TX block to all UEs, keeping them in lockstep."""
    gnb = ctx.socket(zmq.REQ)
    gnb.connect(gnb_tx)
    ues = []
    for addr in ue_dl_binds:
        s = ctx.socket(zmq.REP)
        s.bind(addr)
        ues.append(s)
    log.info("DL: gNB %s -> UEs %s", gnb_tx, ", ".join(ue_dl_binds))

    first = True
    while True:
        # Every UE must ask before the gNB is advanced, so no UE falls behind.
        for s in ues:
            s.recv()
        if first:
            log.info("DL: all %d UEs connected", len(ues))
            first = False
        gnb.send(DUMMY)
        block = gnb.recv(copy=False)
        for s in ues:
            s.send(block, copy=False)
        counters.add("dl", len(block) // 8)


def uplink(ctx, gnb_rx_bind, ue_tx, counters):
    """Answer gNB RX requests with the sum of all UE TX samples."""
    gnb = ctx.socket(zmq.REP)
    gnb.bind(gnb_rx_bind)
    ues = []
    for addr in ue_tx:
        s = ctx.socket(zmq.REQ)
        s.connect(addr)
        ues.append(s)
    log.info("UL: UEs %s -> gNB %s", ", ".join(ue_tx), gnb_rx_bind)

    # UEs may reply with different block lengths; buffer per UE and only emit
    # the overlap so every UE's sample timeline stays aligned.
    pending = [np.zeros(0, dtype=np.complex64) for _ in ues]
    while True:
        gnb.recv()
        for i, s in enumerate(ues):
            if len(pending[i]) == 0:
                s.send(DUMMY)
                pending[i] = np.frombuffer(s.recv(), dtype=np.complex64)
        n = min(len(p) for p in pending)
        out = np.zeros(n, dtype=np.complex64)
        for i, p in enumerate(pending):
            out += p[:n]
            pending[i] = p[n:]
        gnb.send(out.tobytes())
        counters.add("ul", n)


def report(counters, interval):
    while True:
        time.sleep(interval)
        with counters.lock:
            log.info(
                "DL %d blocks / %.2f Msamples, UL %d blocks / %.2f Msamples",
                counters.dl_blocks, counters.dl_samples / 1e6,
                counters.ul_blocks, counters.ul_samples / 1e6,
            )


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--gnb-tx", required=True, help="gNB TX REP endpoint to connect to, e.g. tcp://10.33.33.16:2000")
    p.add_argument("--gnb-rx-bind", required=True, help="REP endpoint the gNB RX connects to, e.g. tcp://*:2001")
    p.add_argument("--ue-dl-bind", action="append", required=True,
                   help="REP endpoint per UE RX (repeat per UE), e.g. tcp://*:2100")
    p.add_argument("--ue-tx", action="append", required=True,
                   help="UE TX REP endpoint to connect to (repeat per UE), e.g. tcp://10.33.33.31:2001")
    p.add_argument("--report-interval", type=float, default=5.0)
    args = p.parse_args()

    if len(args.ue_dl_bind) != len(args.ue_tx):
        p.error("--ue-dl-bind and --ue-tx must be given once per UE")

    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(name)s %(message)s")
    ctx = zmq.Context()
    counters = Counters()
    threads = [
        threading.Thread(target=downlink, args=(ctx, args.gnb_tx, args.ue_dl_bind, counters), daemon=True),
        threading.Thread(target=uplink, args=(ctx, args.gnb_rx_bind, args.ue_tx, counters), daemon=True),
        threading.Thread(target=report, args=(counters, args.report_interval), daemon=True),
    ]
    for t in threads:
        t.start()
    threads[0].join()


if __name__ == "__main__":
    main()
