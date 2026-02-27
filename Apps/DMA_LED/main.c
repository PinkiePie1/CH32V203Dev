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

    //LED_SetPixel(15,LEDON);
    Delay_Ms(5000);

    TIM_ITConfig(TIM1,TIM_IT_CC1 | TIM_IT_Update,ENABLE);
	NVIC_EnableIRQ(TIM1_CC_IRQn);
    NVIC_EnableIRQ(TIM1_UP_IRQn);



    uint32_t lednum = 0;
    u8 color = LEDON;
    u32 iters = 0;

    while(1)
    {   
        if(iters++>6000)
        {
            iters = 0;
            LED_SetPixel(lednum, color);
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
