#ifndef __CHARLIE_H
#define __CHARLIE_H

#include "debug.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CHARLIE_LED_SIZE 16U

void LED_InitPeri(void);
void LED_SetBuffer(uint8_t *buffer[]);
void LED_SetPixel(uint8_t x, uint8_t y, uint8_t color);
void LED_Show(void);
void LED_TurnOff(void);

#ifdef __cplusplus
}
#endif

#endif
