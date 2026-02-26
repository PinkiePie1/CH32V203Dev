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

/*
 *@Note
 *USART Print debugging routine:
 *USART1_Tx(PA9).
 *This example demonstrates using USART1(PA9) as a print debug port output.
 *
 */

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
    PRINT("This is DMA charlie example\r\n");

    LED_InitPeri();
    LED_Show();

    LED_SetPixel(15,LEDON);
    Delay_Ms(1000);
    PRINT("RCC is:%x\r\n",RCC->AHBPCENR);
    PRINT("PWR is:%x\r\n",PWR->CTLR);
    PWR->CTLR |= 1<<20;

    uint32_t lednum = 0;
    u8 color = LEDON;

    while(1)
    {
        LED_SetPixel(lednum, color);

        lednum += 1;
        if(lednum > 71)
        {
            lednum = 0;
            color = (color == LEDON) ? LEDOFF : LEDON;
        }

        Delay_Ms(100);
    }
}
