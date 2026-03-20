#ifndef _LIS2DH_H_
#define _LIS2DH_H_
#include "debug.h"

uint8_t LIS2DWHXY_Init(void);
uint8_t LIS2DWHXY_INTERRUT(void);
void LIS2DWHXY_Deinit(void);
void LIS2DWHXY_Get(int16_t * x, int16_t * y, int16_t * z);

#endif