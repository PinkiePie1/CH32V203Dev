#ifndef __AHT20_H_
#define __AHT20_H_

#include "debug.h"



#define ACK 1
#define NACK 0

#define I2CDelayUs(x) 

void SoftI2CInit(void);
void I2CStart(void);
void I2CStop(void);
uint8_t I2C_Write(uint8_t dat);
uint8_t I2C_Read(uint8_t ack);
void OLED_Init(void);
void OLED_GDDRAM(uint8_t * data);
void OLED_TurnOn(void);
void OLED_TurnOff(void);
void OLED_16(uint8_t * data);

#endif
