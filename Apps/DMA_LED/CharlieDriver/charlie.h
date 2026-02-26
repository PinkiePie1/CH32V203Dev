#ifndef __CHARLIE_H
#define __CHARLIE_H

#include "debug.h"

/* Global typedef */
#define onTime 10
#define offTime 190
/* Global define */

#define LEDON 1
#define LEDOFF 0
#define PinCount 9
/* Global Variable */

void LED_SetPixel(uint16_t num, uint8_t color);
void LED_InitPeri(void);
void LED_Show(void);

#endif
