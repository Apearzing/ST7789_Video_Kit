#ifndef __VIDEO_PLAYER_H
#define __VIDEO_PLAYER_H

#include "stm32f4xx_hal.h"
#include <stdbool.h>

/* Max frame size accepted from the PC (120x120 RGB565 = 28800 bytes).
 * Displayed 2x on the 240x240 screen. */
#define VIDEO_MAX_W  120
#define VIDEO_MAX_H  120

/* Wire protocol (PC -> MCU, little endian):
 *   0x55 0xAA | W:u16 | H:u16 | LEN:u32 | DATA[LEN] | CHECKSUM:u16
 * DATA is RGB565, high byte first, LEN must equal W*H*2.
 * CHECKSUM = sum of all DATA bytes mod 65536.
 * MCU replies per frame: 0x06 = ACK (received OK, display follows),
 * 0x15 = NACK (resend). Reception uses UART DMA double buffers so the
 * next frame arrives while the current one is displayed. */

/* Block up to timeoutMs waiting for a stream. True once the 0x55 0xAA
 * magic is seen (magic bytes are consumed). */
bool Video_WaitForStream(uint32_t timeoutMs);

/* Play frames forever. PC silence keeps the last frame on screen. */
void Video_Loop(void);

#endif