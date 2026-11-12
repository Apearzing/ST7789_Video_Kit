#include "video_player.h"
#include "usart.h"
#include "st7789_hspi.h"

#define V_MAGIC0  0x55
#define V_MAGIC1  0xAA
#define V_ACK     0x06
#define V_NACK    0x15

/* Double buffers: while one frame is displayed over SPI (blocking CPU),
 * the next frame is received in the background via UART DMA.
 * Throughput becomes max(display, receive) instead of their sum. */
#define VBUF_BYTES  (VIDEO_MAX_W * VIDEO_MAX_H * 2)
static uint8_t s_bufA[VBUF_BYTES];
static uint8_t s_bufB[VBUF_BYTES];

static void V_TxByte(uint8_t b)
{
    HAL_UART_Transmit(&huart1, &b, 1, 100);
}

/* Start a DMA RX, false if UART busy or length insane. */
static bool V_DmaRxStart(uint8_t *p, uint32_t len)
{
    if (len == 0 || len > VBUF_BYTES)
        return false;
    return HAL_UART_Receive_DMA(&huart1, p, (uint16_t)len) == HAL_OK;
}

/* Wait for an ongoing DMA RX. False on timeout (DMA aborted). */
static bool V_DmaRxWait(uint32_t timeoutMs)
{
    uint32_t t0 = HAL_GetTick();
    while (HAL_UART_GetState(&huart1) != HAL_UART_STATE_READY)
    {
        if ((HAL_GetTick() - t0) > timeoutMs)
        {
            HAL_UART_Abort(&huart1);
            return false;
        }
    }
    return true;
}

/* Scan the stream for the 2-byte magic (polling, 1 byte at a time). */
static bool V_WaitMagic(uint32_t budgetMs)
{
    uint32_t start = HAL_GetTick();
    uint8_t b, prev = 0;
    bool havePrev = false;
    while ((HAL_GetTick() - start) < budgetMs)
    {
        __HAL_UART_CLEAR_OREFLAG(&huart1);
        if (HAL_UART_Receive(&huart1, &b, 1, 100) != HAL_OK)
            continue;
        if (havePrev && prev == V_MAGIC0 && b == V_MAGIC1)
            return true;
        prev = b;
        havePrev = true;
    }
    return false;
}

bool Video_WaitForStream(uint32_t timeoutMs)
{
    return V_WaitMagic(timeoutMs);
}

static uint16_t V_Checksum(uint8_t *p, uint32_t len)
{
    uint32_t sum = 0;
    for (uint32_t i = 0; i < len; i++)
        sum += p[i];
    return (uint16_t)(sum & 0xFFFF);
}

void Video_Loop(void)
{
    uint8_t *front = s_bufA, *back = s_bufB, *tmp;
    uint16_t fw = 0, fh = 0;
    uint8_t fbpp = 2;
    bool haveFront = false, frontShown = true, first = true;
    uint8_t hdr[8];

    for (;;)
    {
        /* 1. frame header (first frame: magic already consumed). */
        if (first)
        {
            if (!V_DmaRxStart(hdr, 8) || !V_DmaRxWait(3000))
                continue;
            first = false;
        }
        else
        {
            /* Stream paused: flush the last pending frame to the screen. */
            if (!V_WaitMagic(1500))
            {
                if (haveFront && !frontShown)
                {
                    ST7789_ShowImageAuto(front, fw, fh, fbpp);
                    frontShown = true;
                }
                continue;
            }
            if (!V_DmaRxStart(hdr, 8) || !V_DmaRxWait(500))
                continue;
        }

        uint16_t w = (uint16_t)(hdr[0] | ((uint16_t)hdr[1] << 8));
        uint16_t h = (uint16_t)(hdr[2] | ((uint16_t)hdr[3] << 8));
        uint32_t len = (uint32_t)hdr[4] | ((uint32_t)hdr[5] << 8) |
                       ((uint32_t)hdr[6] << 16) | ((uint32_t)hdr[7] << 24);
        uint32_t px = (uint32_t)w * h;
        if (w == 0 || h == 0 || w > VIDEO_MAX_W || h > VIDEO_MAX_H ||
            (len != px * 2 && len != px))
        {
            V_TxByte(V_NACK);
            continue;
        }

        /* 2. receive data into the back buffer (DMA, background). */
        if (!V_DmaRxStart(back, len))
            continue;

        /* 3. overlap: display the previous frame while new data arrives. */
        if (haveFront && !frontShown)
        {
            ST7789_ShowImageAuto(front, fw, fh, fbpp);
            frontShown = true;
        }

        /* 4. wait for data, then verify. */
        if (!V_DmaRxWait(2500))
            continue;
        uint8_t chk[2];
        if (!V_DmaRxStart(chk, 2) || !V_DmaRxWait(500))
            continue;
        uint16_t rxChk = (uint16_t)(chk[0] | ((uint16_t)chk[1] << 8));
        if (V_Checksum(back, len) != rxChk)
        {
            V_TxByte(V_NACK);
            continue;
        }

        /* 5. swap + ACK at once: the PC sends the next frame while
         *    this one is displayed at the top of the next round. */
        tmp = front; front = back; back = tmp;
        fw = w; fh = h; fbpp = (len == px) ? 1 : 2;
        haveFront = true;
        frontShown = false;
        V_TxByte(V_ACK);
    }
}
