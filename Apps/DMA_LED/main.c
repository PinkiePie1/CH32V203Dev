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

const uint8_t LUT[] = {
8 ,0 ,1 ,2 ,3 ,4 ,5 ,6 ,7,
16,17,9 ,10,11,12,13,14,15,
24,25,26,18,19,20,21,22,23,
32,33,34,35,27,28,29,30,31,
40,41,42,43,44,36,37,38,39,
48,49,50,51,52,53,45,46,47,
56,57,58,59,60,61,62,54,55,
64,65,66,67,68,69,70,71,63
};


int main(void)
{
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_1);
    SystemCoreClockUpdate();
    Delay_Init();

    GPIO_InitTypeDef gpioInit = {0};
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB | RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOC | RCC_APB2Periph_GPIOD, ENABLE);
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);

    gpioInit.GPIO_Pin = GPIO_Pin_All;
    gpioInit.GPIO_Mode = GPIO_Mode_IPU;
    gpioInit.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &gpioInit);
    GPIO_Init(GPIOB, &gpioInit);
    GPIO_Init(GPIOC, &gpioInit);
    GPIO_Init(GPIOD, &gpioInit);

    USART_Printf_Init(115200);

    PRINT("SystemClk:%d\r\n", SystemCoreClock);
    PRINT("ChipID:%08x\r\n", DBGMCU_GetCHIPID());
    PRINT("This is DMA charlie example\r\n");

    LED_InitPeri();
    LED_Show();


    uint32_t lednum = 0;
    u8 color = LEDON;
    u32 iters = 0;

    while(1)
    {   
        if(iters++>3000)
        {
            iters = 0;
            LED_SetPixel(LUT[lednum], color);
            lednum += 1;
            if(lednum > 71)
            {
                lednum = 0;
                color = (color == LEDON) ? LEDOFF : LEDON;
            }
        }

        __WFI();
    }
}
