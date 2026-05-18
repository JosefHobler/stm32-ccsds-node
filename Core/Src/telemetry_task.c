/* HK downlink on APID 0x064. Period is set at runtime via CMD_SET_TM_RATE. */
#include "telemetry_task.h"
#include "sensor_task.h"
#include "ccsds.h"
#include "app.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include <string.h>

#define TM_PERIOD_MIN_MS     50u
#define TM_PERIOD_DEFAULT_MS 5000u
/* Must be < the 1.5 s watchdog window or ALIVE_TM gets starved on long periods. */
#define TM_HEARTBEAT_MS      500u

static volatile uint32_t s_period_ms = TM_PERIOD_DEFAULT_MS;
static uint16_t          s_seq;

void tm_set_period_ms(uint32_t ms)
{
    if (ms < TM_PERIOD_MIN_MS) ms = TM_PERIOD_MIN_MS;
    s_period_ms = ms;
}

uint32_t tm_get_period_ms(void) { return s_period_ms; }

static void emit_hk(void)
{
    sensor_sample_t s = {0};
    if (xQueuePeek(g_sensor_q, &s, pdMS_TO_TICKS(50)) != pdTRUE) return;

    uint8_t payload[12];
    sensor_pack_be(&s, payload);

    uint8_t pkt[CCSDS_PRIMARY_HDR_LEN + sizeof(payload) + CCSDS_CRC_LEN];
    size_t  n = ccsds_encode(pkt, sizeof(pkt),
                             APID_HK_TM, CCSDS_TYPE_TM,
                             s_seq++, payload, sizeof(payload));
    if (n) app_uart_send(pkt, n);
}

void telemetry_task(void *arg)
{
    (void)arg;
    TickType_t last_tx = xTaskGetTickCount();

    for (;;) {
        TickType_t now = xTaskGetTickCount();
        if ((TickType_t)(now - last_tx) >= pdMS_TO_TICKS(s_period_ms)) {
            emit_hk();
            last_tx = now;
        }
        app_alive(ALIVE_TM);
        vTaskDelay(pdMS_TO_TICKS(TM_HEARTBEAT_MS));
    }
}
