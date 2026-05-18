#include "app.h"
#include "ccsds.h"
#include "sensor_task.h"
#include "telemetry_task.h"
#include "telecommand_task.h"
#include "main.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "event_groups.h"

static uint8_t s_isr_byte;

SemaphoreHandle_t  g_uart_tx_mtx;
EventGroupHandle_t g_alive_evt;
QueueHandle_t      g_sensor_q;

extern UART_HandleTypeDef huart2;
extern IWDG_HandleTypeDef hiwdg;

void app_uart_send(const uint8_t *data, size_t len)
{
    if (!data || !len) return;
    if (xSemaphoreTake(g_uart_tx_mtx, pdMS_TO_TICKS(200)) == pdTRUE) {
        (void)HAL_UART_Transmit(&huart2, (uint8_t *)data, (uint16_t)len, 200);
        xSemaphoreGive(g_uart_tx_mtx);
    }
}

void app_send_tm(uint16_t apid, const uint8_t *payload, size_t len)
{
    static uint16_t seq;
    uint8_t pkt[CCSDS_MAX_PACKET_LEN];
    size_t  n = ccsds_encode(pkt, sizeof(pkt), apid, CCSDS_TYPE_TM,
                             seq++, payload, len);
    if (n) app_uart_send(pkt, n);
}

/* IWDG ~2 s, window 1.5 s -> ~500 ms of slack before HW reset if any task
 * misses the check-in. */
static void watchdog_task(void *arg)
{
    (void)arg;
    for (;;) {
        EventBits_t b = xEventGroupWaitBits(
            g_alive_evt, ALIVE_ALL,
            pdTRUE,    /* clear on exit */
            pdTRUE,    /* wait for ALL  */
            pdMS_TO_TICKS(1500));

        if ((b & ALIVE_ALL) == ALIVE_ALL)
            HAL_IWDG_Refresh(&hiwdg);
        /* else: at least one task is stuck; IWDG will reset us in ~500 ms. */
    }
}

int app_start(void)
{
    g_uart_tx_mtx = xSemaphoreCreateMutex();
    g_alive_evt   = xEventGroupCreate();
    g_sensor_q    = xQueueCreate(1, sizeof(sensor_sample_t));
    configASSERT(g_uart_tx_mtx && g_alive_evt && g_sensor_q);

    HAL_UART_Receive_IT(&huart2, &s_isr_byte, 1);

    BaseType_t ok = pdPASS;
    ok &= xTaskCreate(sensor_task,      "sensor", 512, NULL, 2, NULL);
    ok &= xTaskCreate(telemetry_task,   "tm",     384, NULL, 2, NULL);
    ok &= xTaskCreate(telecommand_task, "tc",     384, NULL, 3, NULL);
    ok &= xTaskCreate(watchdog_task,    "wdg",    256, NULL, 4, NULL);
    configASSERT(ok == pdPASS);

    return 0;
}

/* HAL hooks live here (could also go in stm32f4xx_it.c). The RX IRQ feeds the
 * TC stream buffer and re-arms itself for the next byte. */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance != USART2) return;
    telecommand_uart_rx_isr(s_isr_byte);
    HAL_UART_Receive_IT(huart, &s_isr_byte, 1);
}

/* FreeRTOS hooks. Spin so the IWDG reboots us */
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask; (void)pcTaskName;
    for (;;) { __asm volatile("nop"); }
}

void vApplicationMallocFailedHook(void)
{
    for (;;) { __asm volatile("nop"); }
}
