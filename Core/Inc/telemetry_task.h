/* Periodic HK downlink (APID 0x064). */
#ifndef TELEMETRY_TASK_H
#define TELEMETRY_TASK_H

#include <stdint.h>

void     telemetry_task(void *arg);
void     tm_set_period_ms(uint32_t ms);   /* TC parser pokes this */
uint32_t tm_get_period_ms(void);

#endif
