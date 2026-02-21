#ifndef __AHT20_H_
#define __AHT20_H_

#define SDA_PIN GPIO_Pin_3
#define SCL_PIN GPIO_Pin_1

#include "debug.h"



#define ACK 1
#define NACK 0
#define SDA_LOW  SDA_OUTLOW()
#define SDA_HIGH SDA_IPU()
#define SCL_LOW  GPIO_ResetBits(GPIOA,SCL_PIN) 
#define SCL_HIGH GPIO_SetBits(GPIOA,SCL_PIN)
#define READ_SDA() (GPIO_ReadInputDataBit(GPIOA,SDA_PIN))

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
