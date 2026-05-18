#!/usr/bin/env python3
"""Minimal CCSDS ground station for the Nucleo-F411RE node.

    python gs.py --port /dev/ttyACM0
    python gs.py --port COM5 --baud 115200

REPL: ping | led on|off | rate <ms> | version | reboot | quit
"""
from __future__ import annotations

import argparse
import struct
import sys
import threading
import time

import serial   # pyserial

from ccsds_codec import (
    Packet, encode, decode, decode_hk, decode_ack,
    APID_HK_TM, APID_EVENT_TM, APID_CMD_TC,
    TYPE_TC,
    CMD_PING, CMD_REBOOT, CMD_SET_TM_RATE, CMD_GET_VERSION, CMD_LED_CTRL,
    PRIMARY_HDR_LEN,
)

MAX_PKT = 256


def reader_loop(ser: serial.Serial, stop: threading.Event) -> None:
    """Stream-frame TM with byte-slip resync."""
    buf = bytearray()
    while not stop.is_set():
        chunk = ser.read(64)
        if chunk:
            buf.extend(chunk)

        while True:
            if len(buf) < PRIMARY_HDR_LEN:
                break

            total = PRIMARY_HDR_LEN + ((buf[4] << 8) | buf[5]) + 1
            if total < PRIMARY_HDR_LEN + 2 or total > MAX_PKT:
                del buf[0]            # implausible len, slip
                continue
            if len(buf) < total:
                break                 # need more data

            p = decode(bytes(buf[:total]))
            if p is None:
                del buf[0]            # bad header / CRC, slip
                continue

            handle(p)
            del buf[:total]


def handle(p: Packet) -> None:
    ts = time.strftime("%H:%M:%S")

    if p.apid == APID_HK_TM:
        d = decode_hk(p.payload)
        if d is None:
            print(f"[{ts}] HK malformed: {p.payload.hex()}")
            return
        print(
            f"[{ts}] HK   seq={p.seq:5d}  t_ms={d['timestamp_ms']:>10d}  "
            f"T={d['mcu_temp_c']:6.2f} C  Vref={d['vrefint_mv']:4d}mV  "
            f"Vdda={d['vdda_mv']:4d}mV  cnt={d['counter']}"
        )
        return

    if p.apid == APID_EVENT_TM:
        a = decode_ack(p.payload)
        if a is None:
            print(f"[{ts}] EVT malformed: {p.payload.hex()}")
            return
        tail = ""
        if a["info"]:
            try:
                tail = "  info=" + a["info"].decode("ascii", errors="replace")
            except Exception:
                tail = "  info=" + a["info"].hex()
        print(f"[{ts}] EVT  cmd=0x{a['cmd']:02X}  status={a['status']}  "
              f"orig_seq={a['orig_seq']}{tail}")
        return

    print(f"[{ts}] APID=0x{p.apid:03X} type={p.pkt_type} seq={p.seq} "
          f"payload={p.payload.hex()}")


class TcSender:
    def __init__(self, ser: serial.Serial):
        self.ser  = ser
        self.seq  = 0
        self.lock = threading.Lock()

    def send(self, cmd: int, args: bytes = b"") -> None:
        with self.lock:
            pkt = encode(APID_CMD_TC, TYPE_TC, self.seq, bytes([cmd]) + args)
            self.ser.write(pkt)
            self.seq = (self.seq + 1) & 0x3FFF


def repl(ser: serial.Serial) -> None:
    tc = TcSender(ser)
    print("Commands: ping | led on|off | rate <ms> | version | reboot | quit")
    while True:
        try:
            line = input("> ").strip().split()
        except (EOFError, KeyboardInterrupt):
            print()
            return
        if not line:
            continue

        op = line[0].lower()
        try:
            if   op == "ping":                            tc.send(CMD_PING)
            elif op == "led"     and len(line) >= 2:      tc.send(CMD_LED_CTRL, bytes([1 if line[1] == "on" else 0]))
            elif op == "rate"    and len(line) >= 2:      tc.send(CMD_SET_TM_RATE, struct.pack(">I", int(line[1])))
            elif op == "version":                         tc.send(CMD_GET_VERSION)
            elif op == "reboot":                          tc.send(CMD_REBOOT)
            elif op in ("quit", "exit"):                  return
            else:                                         print("?")
        except Exception as e:
            print(f"err: {e}")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", required=True, help="e.g. /dev/ttyACM0 or COM5")
    ap.add_argument("--baud", type=int, default=115200)
    args = ap.parse_args()

    ser = serial.Serial(args.port, args.baud, timeout=0.05)
    print(f"opened {args.port} @ {args.baud}")

    stop = threading.Event()
    threading.Thread(target=reader_loop, args=(ser, stop), daemon=True).start()

    try:
        repl(ser)
    finally:
        stop.set()
        time.sleep(0.1)
        ser.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
