#ifndef __CHARLIE_H
#define __CHARLIE_H

#include "debug.h"

/* Global typedef */
#define Period 600U
#define Compensation 0 // this value compensates multiple row. if led is too bright when only few is lit in a row, reduce this number
#define Brightness 8 //this controls how bright the led is. less value = brighter.
/* Global define */

#define LEDON 1
#define LEDOFF 0
#define PinCount 16
/* Global Variable */

/* TIM1更新中断打拍计数，主循环用它做帧周期睡眠 */
extern volatile uint32_t tim1Tick;

void LED_SetPixel(uint16_t num, uint8_t color);
void LED_SetPixelFast(uint16_t num, uint8_t color); //只写像素不重算亮度，批量刷新后用LED_CommitBrightness统一重算
void LED_CommitBrightness(void);
void LED_InitPeri(void);
void LED_Show(void);
void LED_TurnOff(void);
void LED_Brightness(int32_t brightness);

#endif
