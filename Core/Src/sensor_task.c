/* 10 Hz internal temp + VREFINT sampler. Publishes the latest sample to a
 * length-1 overwrite queue so the TM task always sees the freshest reading. */
#include "sensor_task.h"
#include "app.h"
#include "main.h"               /* CubeMX: provides hadc1 */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include <string.h>

extern ADC_HandleTypeDef hadc1;

/* F411 factory cal in system memory (RM0383 / DS9716). */
#define TS_CAL1     (*(volatile uint16_t *)0x1FFF7A2CU)  /* TS @ 30 °C, 3.3 V */
#define TS_CAL2     (*(volatile uint16_t *)0x1FFF7A2EU)  /* TS @ 110 °C, 3.3 V */
#define VREFINT_CAL (*(volatile uint16_t *)0x1FFF7A2AU)  /* VREFINT @ 3.3 V */

static uint16_t adc_read_channel(uint32_t channel)
{
    ADC_ChannelConfTypeDef cfg = {0};
    cfg.Channel      = channel;
    cfg.Rank         = 1;
    cfg.SamplingTime = ADC_SAMPLETIME_480CYCLES;

    if (HAL_ADC_ConfigChannel(&hadc1, &cfg) != HAL_OK) return 0;
    if (HAL_ADC_Start(&hadc1) != HAL_OK)               return 0;
    if (HAL_ADC_PollForConversion(&hadc1, 10) != HAL_OK) {
        HAL_ADC_Stop(&hadc1);
        return 0;
    }
    uint16_t v = (uint16_t)HAL_ADC_GetValue(&hadc1);
    HAL_ADC_Stop(&hadc1);
    return v;
}

void sensor_pack_be(const sensor_sample_t *s, uint8_t out[12])
{
    out[0]  = (uint8_t)(s->timestamp_ms >> 24);
    out[1]  = (uint8_t)(s->timestamp_ms >> 16);
    out[2]  = (uint8_t)(s->timestamp_ms >>  8);
    out[3]  = (uint8_t) s->timestamp_ms;

    uint16_t t = (uint16_t)s->mcu_temp_c100;
    out[4]  = (uint8_t)(t >> 8);
    out[5]  = (uint8_t) t;

    out[6]  = (uint8_t)(s->vrefint_mv >> 8);
    out[7]  = (uint8_t) s->vrefint_mv;
    out[8]  = (uint8_t)(s->vdda_mv    >> 8);
    out[9]  = (uint8_t) s->vdda_mv;
    out[10] = (uint8_t)(s->counter    >> 8);
    out[11] = (uint8_t) s->counter;
}

void sensor_task(void *arg)
{
    (void)arg;
    TickType_t last = xTaskGetTickCount();
    static uint16_t cnt = 0;

    for (;;) {
        uint16_t raw_vref = adc_read_channel(ADC_CHANNEL_VREFINT);
        uint16_t raw_ts   = adc_read_channel(ADC_CHANNEL_TEMPSENSOR);

        /* VDDA from VREFINT (factory cal at 3.3 V). */
        uint32_t vdda_mv = raw_vref ? (3300u * (uint32_t)VREFINT_CAL) / raw_vref
                                    : 3300u;

        /* Normalize TS to the 3.3 V cal point, then linear-interp the cal pair. */
        int32_t ts_n  = ((int32_t)raw_ts * (int32_t)vdda_mv) / 3300;
        int32_t span  = (int32_t)TS_CAL2 - (int32_t)TS_CAL1;
        int32_t t100  = span ? (((ts_n - (int32_t)TS_CAL1) * (110 - 30) * 100) / span) + 3000
                             : 0;

        sensor_sample_t s = {
            .timestamp_ms  = (uint32_t)cnt * 100u,
            .mcu_temp_c100 = (int16_t)t100,
            .vrefint_mv    = (uint16_t)((3300u * (uint32_t)raw_vref) / 4095u),
            .vdda_mv       = (uint16_t)vdda_mv,
            .counter       = cnt,
        };
        cnt++;

        (void)xQueueOverwrite(g_sensor_q, &s);  /* keep only latest */
        app_alive(ALIVE_SENSOR);

        vTaskDelayUntil(&last, pdMS_TO_TICKS(100));   /* 10 Hz */
    }
}
