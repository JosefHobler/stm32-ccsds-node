/* CCSDS TC parser. Stream-frames using pkt_len, slip-resyncs on bad header
 * or CRC. CCITT false-positives during slip are astronomically rare. */
#include "telecommand_task.h"
#include "telemetry_task.h"
#include "ccsds.h"
#include "app.h"
#include "main.h"
#include "FreeRTOS.h"
#include "task.h"
#include "stream_buffer.h"
#include <string.h>

#ifndef FIRMWARE_VERSION_STR
#define FIRMWARE_VERSION_STR "ccsds-node 0.1"
#endif

#define VERSION_MAX 32

extern UART_HandleTypeDef huart2;

static StreamBufferHandle_t s_rx_stream;
static uint16_t             s_ack_seq;

void telecommand_uart_rx_isr(uint8_t byte)
{
    BaseType_t hpw = pdFALSE;
    if (s_rx_stream)
        (void)xStreamBufferSendFromISR(s_rx_stream, &byte, 1, &hpw);
    portYIELD_FROM_ISR(hpw);
}

static void send_evt(const uint8_t *payload, size_t len)
{
    uint8_t pkt[CCSDS_PRIMARY_HDR_LEN + VERSION_MAX + 8 + CCSDS_CRC_LEN];
    size_t  n = ccsds_encode(pkt, sizeof(pkt),
                             APID_EVENT_TM, CCSDS_TYPE_TM,
                             s_ack_seq++, payload, len);
    if (n) app_uart_send(pkt, n);
}

static void send_ack(uint8_t cmd, uint8_t status, uint16_t orig_seq)
{
    uint8_t p[4] = { cmd, status, (uint8_t)(orig_seq >> 8), (uint8_t)orig_seq };
    send_evt(p, sizeof(p));
}

static void send_version(uint16_t orig_seq)
{
    const char *v    = FIRMWARE_VERSION_STR;
    size_t      vlen = strlen(v);
    if (vlen > VERSION_MAX) vlen = VERSION_MAX;

    uint8_t p[4 + VERSION_MAX];
    p[0] = CMD_GET_VERSION;
    p[1] = 0;
    p[2] = (uint8_t)(orig_seq >> 8);
    p[3] = (uint8_t) orig_seq;
    memcpy(&p[4], v, vlen);
    send_evt(p, 4 + vlen);
}

static void dispatch(const ccsds_packet_t *p)
{
    if (p->type != CCSDS_TYPE_TC || p->apid != APID_CMD_TC) return;
    if (p->data_len < 1) return;

    uint8_t cmd    = p->data[0];
    uint8_t status = 0;

    switch (cmd) {
    case CMD_PING:
        break;

    case CMD_LED_CTRL:
        if (p->data_len >= 2)
            HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin,
                              p->data[1] ? GPIO_PIN_SET : GPIO_PIN_RESET);
        else
            status = 1;
        break;

    case CMD_SET_TM_RATE:
        if (p->data_len >= 5) {
            uint32_t ms = ((uint32_t)p->data[1] << 24)
                        | ((uint32_t)p->data[2] << 16)
                        | ((uint32_t)p->data[3] <<  8)
                        |  (uint32_t)p->data[4];
            tm_set_period_ms(ms);
        } else status = 1;
        break;

    case CMD_GET_VERSION:
        send_version(p->seq_count);
        return;

    case CMD_REBOOT:
        send_ack(cmd, 0, p->seq_count);
        vTaskDelay(pdMS_TO_TICKS(20));   /* let the UART drain */
        NVIC_SystemReset();
        return;

    default:
        status = 0xFF;
        break;
    }

    send_ack(cmd, status, p->seq_count);
}

/* Pull pkt_len out of the buffer head; returns total frame size including CRC. */
static inline size_t framed_total(const uint8_t *buf)
{
    return CCSDS_PRIMARY_HDR_LEN + ((size_t)buf[4] << 8 | buf[5]) + 1u;
}

void telecommand_task(void *arg)
{
    (void)arg;
    s_rx_stream = xStreamBufferCreate(256, 1);
    configASSERT(s_rx_stream);

    /* RX ISR is re-armed by HAL_UART_RxCpltCallback (app.c). */
    static uint8_t s_rx_byte;
    HAL_UART_Receive_IT(&huart2, &s_rx_byte, 1);

    uint8_t buf[CCSDS_MAX_PACKET_LEN];
    size_t  fill = 0;

    for (;;) {
        size_t want;
        if (fill < CCSDS_PRIMARY_HDR_LEN) {
            want = CCSDS_PRIMARY_HDR_LEN - fill;
        } else {
            size_t total = framed_total(buf);
            if (total > sizeof(buf) || total < CCSDS_PRIMARY_HDR_LEN + CCSDS_CRC_LEN) {
                memmove(buf, buf + 1, fill - 1);
                fill--;
                app_alive(ALIVE_TC);
                continue;
            }
            want = total - fill;
        }

        size_t got = xStreamBufferReceive(s_rx_stream, buf + fill, want,
                                          pdMS_TO_TICKS(200));
        fill += got;
        app_alive(ALIVE_TC);

        if (fill < CCSDS_PRIMARY_HDR_LEN) continue;

        size_t total = framed_total(buf);
        if (total > sizeof(buf) || total < CCSDS_PRIMARY_HDR_LEN + CCSDS_CRC_LEN) {
            memmove(buf, buf + 1, fill - 1);
            fill--;
            continue;
        }
        if (fill < total) continue;

        ccsds_packet_t p;
        if (ccsds_decode(buf, total, &p)) {
            dispatch(&p);
            size_t rem = fill - total;
            if (rem) memmove(buf, buf + total, rem);
            fill = rem;
        } else {
            /* bad CRC/version -> slip and retry */
            memmove(buf, buf + 1, fill - 1);
            fill--;
        }
    }
}
