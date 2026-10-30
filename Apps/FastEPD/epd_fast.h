#ifndef EPD_FAST_H
#define EPD_FAST_H

#include <stdint.h>

/* 1.54 inch 152x152 SSD1680, black and white only.
 *
 * Frame format: 19 bytes per row, 2888 bytes, MSB is the leftmost pixel,
 * bit 1 = WHITE and bit 0 = BLACK.
 *
 *   移植到 CH32V203C8T6 后使用 SPI1：
 *   BUSY PA0/EXTI0, DC PA3, CS PA4, SCK PA5,
 *   RST PA6, MOSI PA7。
 *
 * 具体引脚宏在 epd_fast_net.c 顶部的「移植区」里，换板子改那里即可。
 */
#define EPD_W 152
#define EPD_H 152
#define EPD_STRIDE (EPD_W / 8)
#define EPD_FRAME_BYTES (EPD_STRIDE * EPD_H)

/* ------------------------------------------------------------- commands */

void EPD_Cmd(uint8_t cmd);
void EPD_Dat(uint8_t dat);


/* ------------------------------------------------------------------ 延时 */

/* 纯软件延时。 */
void EPD_DelayUs(uint32_t us);
void EPD_DelayMs(uint32_t ms);

/* ---------------------------------------------------------------- setup */

void EPD_Hal_Init(void);
void EPD_Init(void);

/* --------------------------------------------------------- full refresh */

void EPD_SendDisplay(const uint8_t *image);

/* ---------------------------------------------------------- fast refresh */

// set up the panel (LUT, RAM, boost converter, stuff like that) for fast refresh
void EPD_FastPrepare(void);

// 
void EPD_FastPrepareFrames(uint8_t frames);

// maximum budget for balck or white pixels
void EPD_SetPulseBudget(uint8_t black, uint8_t white);

/* One fast scan: drive the pixels in black toward black and the pixels in
 * white toward white. Pixels in neither mask keep their current colour,
   Overlapped pixels are also not driven*/
void EPD_FastScan(const uint8_t *black, const uint8_t *white);

// Initialize the net drive state f
void EPD_FastBegin(const uint8_t *shown);

/* 推进一帧。激活后直接返回。这段时间CPU 可以继续算下一帧；
 * 下一次调用会自动等上一帧刷完再激活 */
int EPD_FastFrame(const uint8_t *next);

/* 等面板把已激活的那一帧刷完（阻塞）。 */
void EPD_FastSync(void);

// Fast scans since the last full refresh
uint32_t EPD_FastScanCount(void);

// 0: normal. 1: timeout, try reset the panel.
uint8_t EPD_TakeTimeoutFlag(void);

// puts the EPD back to sleep

void EPD_Sleep(void);

#endif
