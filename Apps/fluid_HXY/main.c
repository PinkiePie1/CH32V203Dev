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
#include "SandSim.h"
#include "LIS2DWHXY.h"
#include "charlie.h"

/* Global typedef */

/* Global define */

#define PUSH_ITER 1
#define GRID_ITER 12
uint8_t ticks=0;
/* Global Variable */




void GPIOallPU(void){

RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB | RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOC| RCC_APB2Periph_GPIOD |RCC_APB2Periph_AFIO, ENABLE);
GPIOA->OUTDR = 0xFFFFFFFF;
GPIOB->OUTDR = 0xFFFFFFFF;
GPIOC->OUTDR = 0xFFFFFFFF;
GPIOD->OUTDR = 0xFFFFFFFF;

GPIOA->CFGLR=0x88888888;
GPIOA->CFGHR=0x88888888;
GPIOB->CFGLR=0x88888888;
GPIOB->CFGHR=0x88888888;
GPIOC->CFGLR=0x88888888;
GPIOC->CFGHR=0x88888888;
GPIOD->CFGLR=0x88888888;
GPIOD->CFGHR=0x88888888;

}

void shutdown(void){

    GPIO_PinRemapConfig(GPIO_Remap_SWJ_Disable,ENABLE);
    LIS2DWHXY_Deinit();
    GPIOallPU();
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR,ENABLE);
    PWR_EnterSTOPMode(PWR_Regulator_LowPower,PWR_STOPEntry_WFI);
    NVIC_SystemReset();
}

void GetAcce(uint32_t i, _iq * accex, _iq * accey)
{
    int16_t x,y,z;
    LIS2DWHXY_Get(&x,&y,&z);
    float xp = (float) ((y-x) * 0.19f);
    float yp = (float) ((-x-y) * 0.19f);

    *accex = _IQ(yp);
    *accey = _IQ(xp);

}


void Show(void)
{
    
    screen_update();
    
}


/*********************************************************************
 * @fn      main
 *
 * @brief   Main program.
 *
 * @return  none
 */
int main(void)
{
    uint32_t timer = 0;
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_1);
    SystemCoreClockUpdate();
    Delay_Init();
    Delay_Ms(5);
    USART_Printf_Init(115200);

    PRINT("SystemClk:%d\r\n", SystemCoreClock);
    PRINT( "ChipID:%08x\r\n", DBGMCU_GetCHIPID() );
    PRINT("This is ACCE example\r\n");

    InitParticles();
    ParticleIntegrate(0, _IQ(9.8f));
    PushParticlesApart(PUSH_ITER);
    particles_to_grid();
    density_update();
    compute_grid_forces(GRID_ITER);
    grid_to_particles();

    if(LIS2DWHXY_Init()==0){}
    else{
        LED_SetPixel(120,LEDON);
        while(1);

    }

    LED_InitPeri();
    LED_Show();

// while(1)
// {
//     int16_t xval,yval;
//     LIS2DWHXY_Get(&yval,&xval,NULL);
//     xval = xval<0?-xval:xval;
//     xval = xval>240?240:xval;
//     for (uint16_t i = 0; i < xval; i++)
//     {
//         LED_SetPixel(i,LEDON);
//         /* code */
//     }
//     Delay_Ms(20);
    
//     for (uint16_t i = 0; i < 240; i++)
//     {
//         LED_SetPixel(i,LEDOFF);
//         /* code */
//     }
// }

    Show();


    TIM_TimeBaseInitTypeDef timBaseCfg = {0};
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);
    timBaseCfg.TIM_Prescaler = SystemCoreClock/1000000 - 1;
    timBaseCfg.TIM_CounterMode = TIM_CounterMode_Up;
    timBaseCfg.TIM_Period = 0xFFFF;
    timBaseCfg.TIM_ClockDivision = TIM_CKD_DIV1;
    timBaseCfg.TIM_RepetitionCounter = 0;
    TIM_TimeBaseInit(TIM2, &timBaseCfg);
    TIM_Cmd(TIM2,ENABLE);

    uint32_t time = TIM2->CNT;
    _iq accex = _IQ(0);
    _iq accey = _IQ(9.8f);
    for (int i=0;i<3;i++){
        GetAcce(7000,&accex,&accey);
        ParticleIntegrate(accex, accey);
        PushParticlesApart(PUSH_ITER);
        particles_to_grid();
        density_update();
        compute_grid_forces(GRID_ITER);
        grid_to_particles();
        Show();
        while(timer ++ < 6)
        {__WFI();}
        timer = 0;
    }
    time = TIM2->CNT - time;
    uint32_t fps = 3*1000000/time;
    PRINT("fps: %d \r\n",fps);
    TIM_Cmd(TIM2,DISABLE);


    while(1)
    {   
        NVIC_DisableIRQ(TIM1_CC_IRQn);
        NVIC_DisableIRQ(TIM1_UP_IRQn);
        GetAcce(7000,&accex,&accey);
        ParticleIntegrate(accex, accey);
        PushParticlesApart(PUSH_ITER);
        particles_to_grid();
        density_update();
        compute_grid_forces(GRID_ITER);
        grid_to_particles();
        Show();
        NVIC_EnableIRQ(TIM1_CC_IRQn);
        NVIC_EnableIRQ(TIM1_UP_IRQn);
        while(timer ++ < 2)
        {__WFI();}
        timer = 0;
        if(GPIO_ReadInputDataBit(GPIOA,GPIO_Pin_0) == Bit_RESET){
            Delay_Ms(30);
            if(GPIO_ReadInputDataBit(GPIOA,GPIO_Pin_0) == Bit_RESET){
                while(GPIO_ReadInputDataBit(GPIOA,GPIO_Pin_0) == Bit_RESET);
                Delay_Ms(30);
                shutdown();
            }
        }
        //Delay_Us(500);

    }
}
