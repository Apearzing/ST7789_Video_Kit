#include "st7789_hspi.h"

/* ST7789 hardware-SPI driver (240x240).
 * Init sequence and window logic are kept identical to the working
 * software-SPI version (st7789_softspi.c); only the byte transfer
 * is replaced by HAL_SPI_Transmit on SPI1.
 *
 * Timing notes:
 * - DC is set BEFORE CS goes low, so the DC level is already stable
 *   when the transfer starts.
 * - SPI1 runs in Mode 3 (CPOL=HIGH, CPHA=2EDGE, see spi.c), which
 *   reproduces the software bit-bang waveform: SCLK idles HIGH,
 *   one bit is latched on every rising edge.
 */

/* One full LCD line (240 pixels x 2 bytes) used by ST7789_Clear. */
static uint8_t s_lineBuf[ST7789_W * 2];

static inline void ST7789_DC_Cmd(void)
{
    HAL_GPIO_WritePin(ST7789_DC_PORT, ST7789_DC_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(ST7789_CS_PORT, ST7789_CS_PIN, GPIO_PIN_RESET);
}

static inline void ST7789_DC_Data(void)
{
    HAL_GPIO_WritePin(ST7789_DC_PORT, ST7789_DC_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(ST7789_CS_PORT, ST7789_CS_PIN, GPIO_PIN_RESET);
}

static inline void ST7789_CS_High(void)
{
    HAL_GPIO_WritePin(ST7789_CS_PORT, ST7789_CS_PIN, GPIO_PIN_SET);
}

/* Send 1 byte, MSB first */
void ST7789_SPI_SendByte(uint8_t dat)
{
    HAL_SPI_Transmit(&ST7789_HSPI_HANDLE, &dat, 1, HAL_MAX_DELAY);
}

/* Write command, DC = 0 */
void ST7789_WriteCmd(uint8_t cmd)
{
    ST7789_DC_Cmd();
    HAL_SPI_Transmit(&ST7789_HSPI_HANDLE, &cmd, 1, HAL_MAX_DELAY);
    ST7789_CS_High();
}

/* Write 8-bit data, DC = 1 */
void ST7789_WriteData(uint8_t data)
{
    ST7789_DC_Data();
    HAL_SPI_Transmit(&ST7789_HSPI_HANDLE, &data, 1, HAL_MAX_DELAY);
    ST7789_CS_High();
}

/* Write 16-bit color data, RGB565, high byte first */
void ST7789_WriteData16(uint16_t data)
{
    uint8_t buf[2];
    buf[0] = (uint8_t)((data >> 8U) & 0xFFU);
    buf[1] = (uint8_t)(data & 0xFFU);
    ST7789_DC_Data();
    HAL_SPI_Transmit(&ST7789_HSPI_HANDLE, buf, 2, HAL_MAX_DELAY);
    ST7789_CS_High();
}

/* Hardware reset, same delays as software version */
void ST7789_Reset(void)
{
    HAL_GPIO_WritePin(ST7789_RST_PORT, ST7789_RST_PIN, GPIO_PIN_RESET);
    HAL_Delay(20);
    HAL_GPIO_WritePin(ST7789_RST_PORT, ST7789_RST_PIN, GPIO_PIN_SET);
    HAL_Delay(120);
}

/* ST7789V init: 4-line SPI mode, RGB565 (16-bit).
 * Command list is identical to the working software version. */
void ST7789_Init(void)
{
    ST7789_Reset();

    ST7789_WriteCmd(0x01); // SWRESET software reset
    HAL_Delay(120);

    ST7789_WriteCmd(0x11); // SLPOUT exit sleep mode
    HAL_Delay(120);

    ST7789_WriteCmd(0x3A); // COLMOD pixel format
    ST7789_WriteData(0x55); // 0x55 = RGB565

    ST7789_WriteCmd(0x36); // MADCTL scan direction
    ST7789_WriteData(0x00);

    ST7789_WriteCmd(0x21); // INVON display inversion on
    ST7789_WriteCmd(0x29); // DISPON display on

    // 240*240 panel window inside the 240*320 controller RAM (see ST7789_Y_OFFSET)
    ST7789_SetWindow(ST7789_X_OFFSET, ST7789_Y_OFFSET, 239 + ST7789_X_OFFSET, 239 + ST7789_Y_OFFSET);
    ST7789_Clear(BLACK);
}

/* Set read/write window: CASET column address + RASET row address */
void ST7789_SetWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    ST7789_WriteCmd(0x2A); // CASET
    ST7789_WriteData((uint8_t)((x0 >> 8U) & 0xFFU));
    ST7789_WriteData((uint8_t)(x0 & 0xFFU));
    ST7789_WriteData((uint8_t)((x1 >> 8U) & 0xFFU));
    ST7789_WriteData((uint8_t)(x1 & 0xFFU));

    ST7789_WriteCmd(0x2B); // RASET
    ST7789_WriteData((uint8_t)((y0 >> 8U) & 0xFFU));
    ST7789_WriteData((uint8_t)(y0 & 0xFFU));
    ST7789_WriteData((uint8_t)((y1 >> 8U) & 0xFFU));
    ST7789_WriteData((uint8_t)(y1 & 0xFFU));

    ST7789_WriteCmd(0x2C); // RAMWR memory write
}

/* Fill full screen. CS stays low for one whole line (480 bytes),
 * so a 240x240 clear needs only 240 HAL_SPI_Transmit calls. */
void ST7789_Clear(uint16_t color)
{
    uint8_t hi = (uint8_t)((color >> 8U) & 0xFFU);
    uint8_t lo = (uint8_t)(color & 0xFFU);
    for (uint16_t i = 0; i < ST7789_W; i++)
    {
        s_lineBuf[2 * i]     = hi;
        s_lineBuf[2 * i + 1] = lo;
    }

    ST7789_SetWindow(0, 0, 239, 239);
    HAL_GPIO_WritePin(ST7789_DC_PORT, ST7789_DC_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(ST7789_CS_PORT, ST7789_CS_PIN, GPIO_PIN_RESET);
    for (uint16_t row = 0; row < ST7789_H; row++)
    {
        HAL_SPI_Transmit(&ST7789_HSPI_HANDLE, s_lineBuf, sizeof(s_lineBuf), HAL_MAX_DELAY);
    }
    ST7789_CS_High();
}

/* Draw one point (ST7789_Y_OFFSET applied for 240x240 cut panel) */
void ST7789_DrawPoint(uint16_t x, uint16_t y, uint16_t color)
{
    y += ST7789_Y_OFFSET;
    ST7789_SetWindow(x, y, x, y);
    ST7789_WriteData16(color);
}

void ST7789_DrawRect(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color)
{
    // Clip to 240*240
    if (x0 > 239) x0 = 239;
    if (y0 > 239) y0 = 239;
    if (x1 > 239) x1 = 239;
    if (y1 > 239) y1 = 239;
    if (x0 > x1) { uint16_t t = x0; x0 = x1; x1 = t; }
    if (y0 > y1) { uint16_t t = y0; y0 = y1; y1 = t; }

    uint16_t i;
    for (i = x0; i <= x1; i++) ST7789_DrawPoint(i, y0, color); // top
    for (i = x0; i <= x1; i++) ST7789_DrawPoint(i, y1, color); // bottom
    for (i = y0 + 1; i <= y1 - 1; i++) ST7789_DrawPoint(x0, i, color); // left
    for (i = y0 + 1; i <= y1 - 1; i++) ST7789_DrawPoint(x1, i, color); // right
}

/* Fill rectangle. One CS-low burst per row. */
void ST7789_DrawFillRect(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color)
{
    if (x0 > 239) x0 = 239;
    if (y0 > 239) y0 = 239;
    if (x1 > 239) x1 = 239;
    if (y1 > 239) y1 = 239;
    if (x0 > x1) { uint16_t t = x0; x0 = x1; x1 = t; }
    if (y0 > y1) { uint16_t t = y0; y0 = y1; y1 = t; }

    uint8_t buf[2];
    buf[0] = (uint8_t)((color >> 8U) & 0xFFU);
    buf[1] = (uint8_t)(color & 0xFFU);

    for (uint16_t y = y0; y <= y1; y++)
    {
        uint16_t real_y = y + ST7789_Y_OFFSET; // same offset as DrawPoint
        ST7789_SetWindow(x0, real_y, x1, real_y);
        HAL_GPIO_WritePin(ST7789_DC_PORT, ST7789_DC_PIN, GPIO_PIN_SET);
        HAL_GPIO_WritePin(ST7789_CS_PORT, ST7789_CS_PIN, GPIO_PIN_RESET);
        uint16_t len = (uint16_t)(x1 - x0 + 1);
        for (uint16_t x = 0; x < len; x++)
        {
            HAL_SPI_Transmit(&ST7789_HSPI_HANDLE, buf, 2, HAL_MAX_DELAY);
        }
        ST7789_CS_High();
    }
}
/* ---- Filled circle (for ball animation) ---- */

/* Max circle radius supported by the row buffer below. */
#define ST7789_CIRCLE_R_MAX  40

/* One row of the circle: 2 bytes per pixel. */
static uint8_t s_circleRow[(ST7789_CIRCLE_R_MAX * 2 + 1) * 2];

/* Integer square root (no math lib dependency). */
static uint16_t ST7789_ISqrt(uint32_t n)
{
    uint32_t res = 0;
    uint32_t bit = 1UL << 30;
    while (bit > n) bit >>= 2;
    while (bit != 0)
    {
        if (n >= res + bit)
        {
            n   -= res + bit;
            res += bit << 1;
        }
        res >>= 1;
        bit >>= 2;
    }
    return (uint16_t)res;
}

/* Draw a filled circle. Coordinates are LCD pixels (0..239),
 * ST7789_Y_OFFSET for the 240x240 cut panel is applied inside.
 * Each row is sent in one CS-low burst: fast enough for animation. */
void ST7789_DrawFillCircle(int16_t x0, int16_t y0, int16_t r, uint16_t color)
{
    if (r <= 0) return;
    if (r > ST7789_CIRCLE_R_MAX) r = ST7789_CIRCLE_R_MAX;

    uint8_t hi = (uint8_t)((color >> 8U) & 0xFFU);
    uint8_t lo = (uint8_t)(color & 0xFFU);
    int32_t r2 = (int32_t)r * r;

    for (int16_t dy = -r; dy <= r; dy++)
    {
        int16_t y = y0 + dy;
        if (y < 0 || y > 239) continue;

        uint16_t dx = ST7789_ISqrt((uint32_t)(r2 - (int32_t)dy * dy));
        int16_t xa = x0 - (int16_t)dx;
        int16_t xb = x0 + (int16_t)dx;
        if (xa < 0) xa = 0;
        if (xb > 239) xb = 239;
        if (xa > xb) continue;

        /* Fill row buffer */
        uint16_t w = (uint16_t)(xb - xa + 1);
        for (uint16_t i = 0; i < w; i++)
        {
            s_circleRow[2 * i]     = hi;
            s_circleRow[2 * i + 1] = lo;
        }

        /* Same Y offset as DrawPoint */
        uint16_t real_y = (uint16_t)((int16_t)y + ST7789_Y_OFFSET);
        ST7789_SetWindow((uint16_t)xa, real_y, (uint16_t)xb, real_y);
        HAL_GPIO_WritePin(ST7789_DC_PORT, ST7789_DC_PIN, GPIO_PIN_SET);
        HAL_GPIO_WritePin(ST7789_CS_PORT, ST7789_CS_PIN, GPIO_PIN_RESET);
        HAL_SPI_Transmit(&ST7789_HSPI_HANDLE, s_circleRow, w * 2, HAL_MAX_DELAY);
        ST7789_CS_High();
    }
}
/* Move a filled circle in ONE window update (no flicker).
 * The union bounding box of old+new positions is refreshed in a single
 * CS-low session: pixels inside the NEW circle get fgColor, everything
 * else gets bgColor. There is no intermediate "erased" frame on screen,
 * unlike erase-then-draw which flashes. Coordinates are LCD pixels. */
void ST7789_MoveFillCircle(int16_t xOld, int16_t yOld, int16_t xNew, int16_t yNew,
                           int16_t r, uint16_t fgColor, uint16_t bgColor)
{
    if (r <= 0) return;
    if (r > ST7789_CIRCLE_R_MAX) r = ST7789_CIRCLE_R_MAX;

    int16_t xa = (xOld < xNew ? xOld : xNew) - r;
    int16_t xb = (xOld > xNew ? xOld : xNew) + r;
    int16_t ya = (yOld < yNew ? yOld : yNew) - r;
    int16_t yb = (yOld > yNew ? yOld : yNew) + r;
    if (xa < 0) xa = 0;
    if (xb > 239) xb = 239;
    if (ya < 0) ya = 0;
    if (yb > 239) yb = 239;
    if (xa > xb || ya > yb) return;

    uint8_t fgHi = (uint8_t)((fgColor >> 8U) & 0xFFU);
    uint8_t fgLo = (uint8_t)(fgColor & 0xFFU);
    uint8_t bgHi = (uint8_t)((bgColor >> 8U) & 0xFFU);
    uint8_t bgLo = (uint8_t)(bgColor & 0xFFU);
    int32_t r2 = (int32_t)r * r;

    /* Reuse the 480-byte line buffer (one full LCD row max). */
    uint16_t w = (uint16_t)(xb - xa + 1);

    ST7789_SetWindow((uint16_t)xa, (uint16_t)(ya + ST7789_Y_OFFSET),
                     (uint16_t)xb, (uint16_t)(yb + ST7789_Y_OFFSET));
    HAL_GPIO_WritePin(ST7789_DC_PORT, ST7789_DC_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(ST7789_CS_PORT, ST7789_CS_PIN, GPIO_PIN_RESET);
    for (int16_t y = ya; y <= yb; y++)
    {
        int32_t dyy = (int32_t)(y - yNew);
        int32_t dyy2 = dyy * dyy;
        for (int16_t x = xa; x <= xb; x++)
        {
            int32_t dxx = (int32_t)(x - xNew);
            uint16_t i = (uint16_t)((x - xa) * 2);
            if (dxx * dxx + dyy2 <= r2)
            {
                s_lineBuf[i]     = fgHi;
                s_lineBuf[i + 1] = fgLo;
            }
            else
            {
                s_lineBuf[i]     = bgHi;
                s_lineBuf[i + 1] = bgLo;
            }
        }
        HAL_SPI_Transmit(&ST7789_HSPI_HANDLE, s_lineBuf, w * 2, HAL_MAX_DELAY);
    }
    ST7789_CS_High();
}
/* Show a w*h RGB565 image (byte stream, high byte first) scaled 2x,
 * centered on the 240x240 screen. The PC sender transmits 120x120,
 * which fills the whole screen. Reuses the 480-byte line buffer
 * (max supported w is 120). */
void ST7789_ShowImage2x(const uint8_t *src, uint16_t w, uint16_t h)
{
    if (src == 0 || w == 0 || h == 0 || w > 120 || h > 120) return;

    uint16_t dw = w * 2;   // dest width
    uint16_t dh = h * 2;   // dest height
    uint16_t x0 = (ST7789_W - dw) / 2;
    uint16_t y0 = (ST7789_H - dh) / 2;

    ST7789_SetWindow(x0, (uint16_t)(y0 + ST7789_Y_OFFSET),
                     (uint16_t)(x0 + dw - 1), (uint16_t)(y0 + dh - 1 + ST7789_Y_OFFSET));
    HAL_GPIO_WritePin(ST7789_DC_PORT, ST7789_DC_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(ST7789_CS_PORT, ST7789_CS_PIN, GPIO_PIN_RESET);
    for (uint16_t sy = 0; sy < h; sy++)
    {
        const uint8_t *row = src + (uint32_t)sy * w * 2;
        /* Nearest-neighbor scale: duplicate each pixel horizontally */
        for (uint16_t sx = 0; sx < w; sx++)
        {
            uint16_t i = sx * 4;
            s_lineBuf[i]     = row[sx * 2];
            s_lineBuf[i + 1] = row[sx * 2 + 1];
            s_lineBuf[i + 2] = row[sx * 2];
            s_lineBuf[i + 3] = row[sx * 2 + 1];
        }
        /* Write the dest row twice (vertical duplication) */
        HAL_SPI_Transmit(&ST7789_HSPI_HANDLE, s_lineBuf, dw * 2, HAL_MAX_DELAY);
        HAL_SPI_Transmit(&ST7789_HSPI_HANDLE, s_lineBuf, dw * 2, HAL_MAX_DELAY);
    }
    ST7789_CS_High();
}
/* Show a w*h RGB565 image scaled by integer `scale`, centered.
 * Reuses the 480-byte line buffer (w*scale must be <= 240). */
void ST7789_ShowImageScale(const uint8_t *src, uint16_t w, uint16_t h, uint8_t scale, uint8_t bpp)
{
    if (src == 0 || w == 0 || h == 0 || w > 120 || h > 120)
        return;
    if (scale < 1) scale = 1;
    if (scale > 8) scale = 8;
    uint16_t dw = (uint16_t)(w * scale);
    uint16_t dh = (uint16_t)(h * scale);
    if (dw == 0 || dh == 0 || dw > 240 || dh > 240)
        return;

    uint16_t x0 = (uint16_t)((ST7789_W - dw) / 2);
    uint16_t y0 = (uint16_t)((ST7789_H - dh) / 2);

    ST7789_SetWindow(x0, (uint16_t)(y0 + ST7789_Y_OFFSET),
                     (uint16_t)(x0 + dw - 1), (uint16_t)(y0 + dh - 1 + ST7789_Y_OFFSET));
    HAL_GPIO_WritePin(ST7789_DC_PORT, ST7789_DC_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(ST7789_CS_PORT, ST7789_CS_PIN, GPIO_PIN_RESET);
    uint16_t stride = (uint16_t)(w * bpp);
    for (uint16_t sy = 0; sy < h; sy++)
    {
        const uint8_t *row = src + (uint32_t)sy * stride;
        /* Nearest-neighbor: duplicate each pixel `scale` times */
        for (uint16_t sx = 0; sx < w; sx++)
        {
            uint8_t hi, lo;
            if (bpp == 1)
            {
                /* RGB332 -> RGB565, bit-replicate expansion */
                uint8_t v = row[sx];
                uint8_t r3 = (uint8_t)((v >> 5) & 7);
                uint8_t g3 = (uint8_t)((v >> 2) & 7);
                uint8_t b2 = (uint8_t)(v & 3);
                uint8_t r5 = (uint8_t)((r3 << 2) | (r3 >> 1));
                uint8_t g6 = (uint8_t)((g3 << 3) | g3);
                uint8_t b5 = (uint8_t)((b2 << 3) | (b2 << 1) | (b2 >> 1));
                hi = (uint8_t)((r5 << 3) | (g6 >> 3));
                lo = (uint8_t)(((g6 & 7) << 5) | b5);
            }
            else
            {
                hi = row[sx * 2];
                lo = row[sx * 2 + 1];
            }
            for (uint8_t k = 0; k < scale; k++)
            {
                uint16_t i = (uint16_t)((sx * scale + k) * 2);
                s_lineBuf[i]     = hi;
                s_lineBuf[i + 1] = lo;
            }
        }
        /* Write the dest row `scale` times (vertical duplication) */
        for (uint8_t k = 0; k < scale; k++)
            HAL_SPI_Transmit(&ST7789_HSPI_HANDLE, s_lineBuf, (uint16_t)(dw * 2), HAL_MAX_DELAY);
    }
    ST7789_CS_High();
}

/* Auto scale: largest integer factor that still fills the screen.
 * 120->2x, 90->2x, 80->3x fullscreen, 60->4x fullscreen. */
void ST7789_ShowImageAuto(const uint8_t *src, uint16_t w, uint16_t h, uint8_t bpp)
{
    if (src == 0 || w == 0 || h == 0)
        return;
    uint8_t s = (uint8_t)(240 / w);
    uint8_t t = (uint8_t)(240 / h);
    if (t < s) s = t;
    if (s < 1) s = 1;
    ST7789_ShowImageScale(src, w, h, s, bpp);
}