#include "ccsds.h"
#include <string.h>

/* CRC-16-CCITT (XMODEM): poly 0x1021, init 0xFFFF, no reflect, no xorout. */
uint16_t ccsds_crc16(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int b = 0; b < 8; b++)
            crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x1021u)
                                  : (uint16_t)(crc << 1);
    }
    return crc;
}

size_t ccsds_encode(uint8_t *out, size_t out_cap,
                    uint16_t apid, uint8_t type, uint16_t seq_count,
                    const uint8_t *payload, size_t payload_len)
{
    const size_t total = CCSDS_PRIMARY_HDR_LEN + payload_len + CCSDS_CRC_LEN;
    if (!out || total > out_cap)                  return 0;
    if (payload_len + CCSDS_CRC_LEN > 0xFFFFu)    return 0;

    uint16_t pkt_id  = (uint16_t)((CCSDS_VERSION & 0x7u) << 13)
                     | (uint16_t)((type & 0x1u)   << 12)
                     /* sec hdr flag = 0 */
                     | (uint16_t)(apid & 0x7FFu);
    uint16_t pkt_seq = (uint16_t)((CCSDS_SEQ_UNSEGMENTED & 0x3u) << 14)
                     | (uint16_t)(seq_count & 0x3FFFu);
    uint16_t pkt_len = (uint16_t)(payload_len + CCSDS_CRC_LEN - 1u);

    out[0] = (uint8_t)(pkt_id  >> 8);   out[1] = (uint8_t)pkt_id;
    out[2] = (uint8_t)(pkt_seq >> 8);   out[3] = (uint8_t)pkt_seq;
    out[4] = (uint8_t)(pkt_len >> 8);   out[5] = (uint8_t)pkt_len;

    if (payload_len && payload)
        memcpy(&out[CCSDS_PRIMARY_HDR_LEN], payload, payload_len);

    uint16_t crc = ccsds_crc16(out, CCSDS_PRIMARY_HDR_LEN + payload_len);
    out[CCSDS_PRIMARY_HDR_LEN + payload_len    ] = (uint8_t)(crc >> 8);
    out[CCSDS_PRIMARY_HDR_LEN + payload_len + 1] = (uint8_t)crc;
    return total;
}

bool ccsds_decode(const uint8_t *in, size_t in_len, ccsds_packet_t *out)
{
    if (!in || !out)                                       return false;
    if (in_len < CCSDS_PRIMARY_HDR_LEN + CCSDS_CRC_LEN)    return false;

    uint16_t pkt_id  = ((uint16_t)in[0] << 8) | in[1];
    uint16_t pkt_seq = ((uint16_t)in[2] << 8) | in[3];
    uint16_t pkt_len = ((uint16_t)in[4] << 8) | in[5];

    if (((pkt_id >> 13) & 0x7u) != CCSDS_VERSION) return false;

    size_t data_field = (size_t)pkt_len + 1u;          /* includes CRC */
    if (data_field < CCSDS_CRC_LEN)                       return false;
    if (CCSDS_PRIMARY_HDR_LEN + data_field > in_len)      return false;

    size_t   crc_pos = CCSDS_PRIMARY_HDR_LEN + (data_field - CCSDS_CRC_LEN);
    uint16_t want    = ((uint16_t)in[crc_pos] << 8) | in[crc_pos + 1];
    uint16_t got     = ccsds_crc16(in, crc_pos);
    if (want != got) return false;

    out->apid         = (uint16_t)(pkt_id & 0x7FFu);
    out->type         = (uint8_t) ((pkt_id >> 12) & 0x1u);
    out->sec_hdr_flag = (uint8_t) ((pkt_id >> 11) & 0x1u);
    out->seq_count    = (uint16_t)(pkt_seq & 0x3FFFu);
    out->data         = &in[CCSDS_PRIMARY_HDR_LEN];
    out->data_len     = (uint16_t)(data_field - CCSDS_CRC_LEN);
    return true;
}
