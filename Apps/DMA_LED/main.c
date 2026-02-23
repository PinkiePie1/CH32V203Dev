/********************************** (C) COPYRIGHT *******************************
 * File Name          : main.c
 * Author             : WCH
 * Version            : V1.0.0
 * Date               : 2021/06/06
 * Description        : Main program body.
 *********************************************************************************
 * Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
 * Attention: This software (modified or not) and binary are used for
 * microcontroller manufactured by Nanjing Qinheng Microelectronics.
 *******************************************************************************/

#include "debug.h"
#include "charlie.h"

int main(void)
{
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_1);
    SystemCoreClockUpdate();
    Delay_Init();
    USART_Printf_Init(115200);

    PRINT("SystemClk:%d\r\n", SystemCoreClock);
    PRINT("ChipID:%08x\r\n", DBGMCU_GetCHIPID());
    PRINT("DMA Charlie LED demo\r\n");

    LED_InitPeri();

    for(uint8_t i = 0; i < CHARLIE_LED_SIZE; i++)
    {
        LED_SetPixel(i, (uint8_t)((i + 1U) % CHARLIE_LED_SIZE), 1);
    }

    LED_Show();

    while(1)
    {
    }
}
