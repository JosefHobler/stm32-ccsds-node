/* Internal temp + VDDA sampler. */
#ifndef SENSOR_TASK_H
#define SENSOR_TASK_H

#include <stdint.h>

/* HK payload, big-endian on the wire. Layout is part of the protocol. */
typedef struct {
    uint32_t timestamp_ms;
    int16_t  mcu_temp_c100;   /* °C * 100, after factory cal */
    uint16_t vrefint_mv;
    uint16_t vdda_mv;
    uint16_t counter;
} sensor_sample_t;

void sensor_task(void *arg);
void sensor_pack_be(const sensor_sample_t *s, uint8_t out[12]);

#endif
