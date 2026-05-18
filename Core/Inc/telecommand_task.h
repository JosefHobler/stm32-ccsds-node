/* TC RX, parse, dispatch. Acks land on APID 0x065. */
#ifndef TELECOMMAND_TASK_H
#define TELECOMMAND_TASK_H

#include <stdint.h>

void telecommand_task(void *arg);
void telecommand_uart_rx_isr(uint8_t byte);  /* called from HAL UART RX cb */

#endif
