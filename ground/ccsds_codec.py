"""CCSDS Space Packet codec. Mirrors firmware/Core/Src/ccsds.c

    [ id(2) | seq(2) | len(2) | payload | crc16(2) ]

CRC: CCITT/XMODEM (poly 0x1021, init 0xFFFF, no reflect, no xorout).
"""
from __future__ import annotations

import struct
from dataclasses import dataclass
from typing import Optional

CCSDS_VERSION   = 0
TYPE_TM         = 0
TYPE_TC         = 1
SEQ_UNSEGMENTED = 0b11

APID_HK_TM    = 0x064
APID_EVENT_TM = 0x065
APID_CMD_TC   = 0x0C8

CMD_PING        = 0x01
CMD_REBOOT      = 0x02
CMD_SET_TM_RATE = 0x03
CMD_GET_VERSION = 0x04
CMD_LED_CTRL    = 0x05

PRIMARY_HDR_LEN = 6
CRC_LEN         = 2


def crc16_ccitt(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


@dataclass
class Packet:
    apid: int
    pkt_type: int          # 0=TM, 1=TC
    sec_hdr_flag: int
    seq: int               # 14-bit
    payload: bytes


def encode(apid: int, pkt_type: int, seq: int, payload: bytes) -> bytes:
    if not 0 <= apid <= 0x7FF:           raise ValueError("apid out of range")
    if not 0 <= seq  <= 0x3FFF:          raise ValueError("seq out of range")
    if len(payload) + CRC_LEN > 0xFFFF:  raise ValueError("payload too large")

    pkt_id  = (CCSDS_VERSION << 13) | ((pkt_type & 1) << 12) | (apid & 0x7FF)
    pkt_seq = (SEQ_UNSEGMENTED << 14) | (seq & 0x3FFF)
    pkt_len = len(payload) + CRC_LEN - 1
    body    = struct.pack(">HHH", pkt_id, pkt_seq, pkt_len) + payload
    return body + struct.pack(">H", crc16_ccitt(body))


def decode(buf: bytes) -> Optional[Packet]:
    """Strict decode. None on any error (length, version, CRC, ...)."""
    if len(buf) < PRIMARY_HDR_LEN + CRC_LEN:
        return None

    pkt_id, pkt_seq, pkt_len = struct.unpack(">HHH", buf[:PRIMARY_HDR_LEN])
    if (pkt_id >> 13) & 0x7 != CCSDS_VERSION:
        return None

    data_field = pkt_len + 1
    total      = PRIMARY_HDR_LEN + data_field
    if total > len(buf) or data_field < CRC_LEN:
        return None

    crc_pos = total - CRC_LEN
    want    = struct.unpack(">H", buf[crc_pos:crc_pos + CRC_LEN])[0]
    if want != crc16_ccitt(buf[:crc_pos]):
        return None

    return Packet(
        apid         = pkt_id & 0x7FF,
        pkt_type     = (pkt_id >> 12) & 0x1,
        sec_hdr_flag = (pkt_id >> 11) & 0x1,
        seq          = pkt_seq & 0x3FFF,
        payload      = buf[PRIMARY_HDR_LEN:crc_pos],
    )


def decode_hk(payload: bytes) -> Optional[dict]:
    if len(payload) != 12:
        return None
    ts, temp_c100, vref_mv, vdda_mv, cnt = struct.unpack(">IhHHH", payload)
    return {
        "timestamp_ms": ts,
        "mcu_temp_c":   temp_c100 / 100.0,
        "vrefint_mv":   vref_mv,
        "vdda_mv":      vdda_mv,
        "counter":      cnt,
    }


def decode_ack(payload: bytes) -> Optional[dict]:
    """APID 0x065 ack. At least 4 bytes; anything after is optional info (e.g. version)."""
    if len(payload) < 4:
        return None
    cmd, status, seq_hi, seq_lo = payload[:4]
    return {
        "cmd":      cmd,
        "status":   status,
        "orig_seq": (seq_hi << 8) | seq_lo,
        "info":     payload[4:],
    }
