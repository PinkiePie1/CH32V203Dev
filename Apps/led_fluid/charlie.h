#ifndef __CHARLIE_H
#define __CHARLIE_H

#include "debug.h"

/* Global typedef */
#define onTime 2
#define offTime (100U-onTime)
/* Global define */

#define LEDON 1
#define LEDOFF 0
#define PinCount 9
/* Global Variable */

void LED_SetPixel(uint16_t num, uint8_t color);
void LED_InitPeri(void);
void LED_Show(void);

#endif
