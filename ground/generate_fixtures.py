#!/usr/bin/env python3
"""Generate randomised conformance fixtures for the C++ codec.

Drives ccsds_codec.encode (the proven reference) over a wide range of
valid (apid, type, seq, payload) tuples and emits the resulting wire
bytes as JSON. The C++ side decodes each frame, re-encodes it, and
must produce the exact same bytes — that's the differential check.

Usage:
    python ground/generate_fixtures.py [--seed N] [--count N] [-o path]
"""
from __future__ import annotations

import argparse
import json
import random
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from ccsds_codec import (  # noqa: E402  (path tweak above)
    APID_CMD_TC,
    APID_EVENT_TM,
    APID_HK_TM,
    TYPE_TC,
    TYPE_TM,
    encode,
)

# Match cpp/include/ccsds/codec.hpp:kMaxFrameLen. data_field = payload + 2 (CRC),
# total = 6 (header) + data_field, so payload <= kMaxFrameLen - 8.
MAX_PAYLOAD = 256 - 8


def gen(count: int, rng: random.Random) -> list[dict]:
    """Return `count` randomly-shaped fixtures spanning the field ranges."""
    # APIDs the firmware actually uses plus one random APID to flex the field.
    apid_pool = [APID_HK_TM, APID_EVENT_TM, APID_CMD_TC]
    type_pool = [TYPE_TM, TYPE_TC]
    out: list[dict] = []
    for i in range(count):
        # Half real APIDs, half random — covers both the firmware-realistic
        # decode path and the edge cases of the 11-bit field.
        if i % 2 == 0:
            apid = rng.choice(apid_pool)
        else:
            apid = rng.randrange(0, 0x800)  # 11-bit
        pkt_type = rng.choice(type_pool)
        # Sequence: cover small, mid, max, and one random.
        seq = rng.choice([
            0,
            1,
            rng.randrange(0, 0x4000),
            0x3FFF,
        ])
        # Payload: cover empty, single byte, mid, max.
        size = rng.choice([
            0,
            1,
            rng.randrange(0, MAX_PAYLOAD + 1),
            MAX_PAYLOAD,
        ])
        payload = bytes(rng.randrange(0, 256) for _ in range(size))
        frame = encode(apid, pkt_type, seq, payload)
        out.append({
            "apid":     apid,
            "type":     pkt_type,
            "seq":      seq,
            "payload":  payload.hex(),
            "frame":    frame.hex(),
        })
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--seed",  type=int, default=0xCC5D5,
                    help="RNG seed (default: stable per-run identity)")
    ap.add_argument("--count", type=int, default=200,
                    help="number of fixtures to generate")
    ap.add_argument("-o", "--output", type=Path,
                    default=Path(__file__).resolve().parents[1]
                    / "cpp" / "tests" / "generated_fixtures.json",
                    help="output JSON path")
    args = ap.parse_args()

    rng = random.Random(args.seed)
    data = {
        "seed":     args.seed,
        "count":    args.count,
        "fixtures": gen(args.count, rng),
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(data, indent=2) + "\n")
    print(f"wrote {len(data['fixtures'])} fixtures to {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
