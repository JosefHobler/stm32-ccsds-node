/* CCSDS 133.0-B-2 Space Packet codec.
 * Wire format: [id(2) | seq(2) | len(2) | payload | crc16(2)]
 * len = data_field_octets - 1. CRC covers everything before the CRC field. */
#ifndef CCSDS_H
#define CCSDS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define CCSDS_VERSION          0
#define CCSDS_TYPE_TM          0
#define CCSDS_TYPE_TC          1
#define CCSDS_SEQ_UNSEGMENTED  0x3

#define APID_HK_TM     0x064
#define APID_EVENT_TM  0x065
#define APID_CMD_TC    0x0C8
#define APID_IDLE      0x7FF

#define CMD_PING         0x01
#define CMD_REBOOT       0x02
#define CMD_SET_TM_RATE  0x03   /* u32 BE ms */
#define CMD_GET_VERSION  0x04
#define CMD_LED_CTRL     0x05   /* u8: 0=off 1=on */

#define CCSDS_PRIMARY_HDR_LEN  6u
#define CCSDS_CRC_LEN          2u
#define CCSDS_MAX_PACKET_LEN   256u

typedef struct {
    uint16_t       apid;
    uint8_t        type;          /* 0=TM, 1=TC */
    uint8_t        sec_hdr_flag;
    uint16_t       seq_count;     /* 14-bit */
    const uint8_t *data;          /* points into the source buffer */
    uint16_t       data_len;
} ccsds_packet_t;

size_t   ccsds_encode(uint8_t *out, size_t out_cap,
                      uint16_t apid, uint8_t type, uint16_t seq_count,
                      const uint8_t *payload, size_t payload_len);
bool     ccsds_decode(const uint8_t *in, size_t in_len, ccsds_packet_t *out);
uint16_t ccsds_crc16 (const uint8_t *data, size_t len);

#endif
