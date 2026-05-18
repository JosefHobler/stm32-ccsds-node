# stm32-ccsds-node

CCSDS Space Packet TM/TC over UART. STM32F411RE + FreeRTOS on one end,
Python REPL on the other.

End-to-end CCSDS framing on a real MCU, with a ground
station. Not a simulation.

## Demo

<video src="./assets/demo.mp4" controls="controls" width="100%"></video>

## Architecture

```
 +-----------+   USART2 @ 115200 8N1   +--------+
 | Nucleo    | <---------------------> | gs.py  |
 | F411RE    |     CCSDS Space Packet  | host   |
 +-----------+        + CRC16          +--------+
```

## Wire format

```
Packet ID (2)  | Packet Sequence Control(2) | Packet Data Length(2) | payload | CRC16(2)


  Packet ID = vvv t s aaaaaaaaaaa     PDL = (data field octets) - 1
              ^   ^ ^ ^                   = (payload + 2 crc) - 1
              |   | | APID (11)
              |   | sec-hdr flag (0)
              |   type: 0=TM 1=TC
              version (0)
```

CRC is CCITT/XMODEM over header + payload.
Framing on a raw stream uses the length field, with 1-byte slip on
CRC failure to resync. False positives during slip are rare (roughly 1 in 65,536) with CCITT.

## APIDs

| APID  | dir | name  | payload                                                  |
|-------|-----|-------|----------------------------------------------------------|
| 0x064 | TM  | HK    | `>IhHHH` (ts_ms, T·100, VREFINT_mV, VDDA_mV, cnt)        |
| 0x065 | TM  | EVENT | `>BBH` (cmd, status, orig_seq) + optional tail           |
| 0x0C8 | TC  | CMD   | `>B` cmd + args                                          |

## Commands

| op          | val  | args            | notes                              |
|-------------|------|-----------------|------------------------------------|
| PING        | 0x01 | -               | ack with status=0                  |
| REBOOT      | 0x02 | -               | `NVIC_SystemReset` after ack       |
| SET_TM_RATE | 0x03 | `>I` ms (≥ 50)  | HK period; clamped at 50           |
| GET_VERSION | 0x04 | -               | tail = ASCII version string        |
| LED_CTRL    | 0x05 | `>B` 0\|1       | LD2                                |

## Tasks

```
sensor_task   10 Hz  -- xQueueOverwrite -->  g_sensor_q (cap=1)
                                                  |
                                                  v
telemetry_task --xQueuePeek--> encode HK -- app_uart_send --> USART2
                                                     ^
telecommand_task <-- stream_buffer <-- HAL_UART_RxCpltCallback
        |
        +---- dispatch ---- ack/version ---> app_uart_send --> USART2
                                                  ^
                                          g_uart_tx_mtx serializes TX

watchdog_task: waits on (ALIVE_SENSOR|ALIVE_TM|ALIVE_TC) every 1500 ms,
               refreshes IWDG only if all three checked in.
               IWDG hw timeout ≈ 2 s → ~500 ms slack.
```

The TM task runs a 500 ms heartbeat loop and only emits packets when
`s_period_ms` has elapsed.

## Layout

```
Core/Inc/                firmware headers
Core/Src/                firmware sources  (app, ccsds, sensor_task, tm, tc)
ground/                  client-side ground station (gs.py, ccsds_codec.py)
ccsds_node.ioc           CubeMX project
```

## How to run it

**Firmware:** Open `ccsds_node.ioc` in CubeIDE, build, flash. `app_start()`
is already wired into `main.c`'s USER CODE region. HK starts streaming on USART2.

**Ground station.**
```
cd ground
pip install -r requirements.txt
python gs.py --port COM7

Commands:
> ping
> rate n (where n is a natural number)
> led on
> version
```

## Challenges & Solutions

**IWDG vs. TM period:** Earlier version had `vTaskDelayUntil(period_ms)`
inside the TM task. With period = 10 s and watchdog window = 1.5 s, the
TM alive bit never showed up in time -> IWDG reset every ~2 s -> every HK
packet looked like the initial one. Fixed by
heart-beating at 500 ms regardless of period.

**TX is polling:** `app_uart_send` is blocking-polling under a mutex.
Works with 10 s HK + occasional acks. But if you set `rate` past a few Hz
with bigger payloads, switch to `HAL_UART_Transmit_DMA` + a TxCplt
semaphore.

**Encoder/Decoder is host-testable:** `ccsds.c` has zero HAL dependencies. C and Python implementations are byte-identical.

## As soon as I finish my exams, I will add new features to this project
