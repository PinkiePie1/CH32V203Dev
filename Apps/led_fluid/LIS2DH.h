#ifndef _LIS2DH_H_
#define _LIS2DH_H_
#include "debug.h"

void LIS2DH_Init(void);
void LIS2DH_Get(int16_t * x, int16_t * y, int16_t * z);

#endif