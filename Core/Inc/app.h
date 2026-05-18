/* Shared sync primitives, alive bits, app entry.
 * Watchdog refreshes IWDG iff every ALIVE_* bit is seen within ~1.5 s.
 * IWDG itself is ~2 s (see MX_IWDG_Init in main.c). */
#ifndef APP_H
#define APP_H

#include <stdint.h>
#include <stddef.h>
#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"
#include "event_groups.h"

#define ALIVE_SENSOR  (1u << 0)
#define ALIVE_TM      (1u << 1)
#define ALIVE_TC      (1u << 2)
#define ALIVE_ALL     (ALIVE_SENSOR | ALIVE_TM | ALIVE_TC)

extern SemaphoreHandle_t  g_uart_tx_mtx;
extern EventGroupHandle_t g_alive_evt;
extern QueueHandle_t      g_sensor_q;

/* Task-safe (NOT ISR-safe). */
void app_uart_send(const uint8_t *data, size_t len);
void app_send_tm  (uint16_t apid, const uint8_t *payload, size_t len);

static inline void app_alive(uint32_t bit) {
    (void)xEventGroupSetBits(g_alive_evt, bit);
}

int  app_start(void);   /* call after MX_* init, before vTaskStartScheduler */

#endif
