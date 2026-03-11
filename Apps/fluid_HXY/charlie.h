#ifndef __CHARLIE_H
#define __CHARLIE_H

#include "debug.h"

/* Global typedef */
#define Period 300U
#define Compensation 0 // this value compensates multiple row. if led is too bright when only few is lit in a row, reduce this number
#define Brightness 5 //this controls how bright the led is. less value = brighter.
/* Global define */

#define LEDON 1
#define LEDOFF 0
#define PinCount 16
/* Global Variable */

void LED_SetPixel(uint16_t num, uint8_t color);
void LED_InitPeri(void);
void LED_Show(void);

#endif
