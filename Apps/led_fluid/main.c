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
#include "charlie.h"
#include "LIS2DH.h"

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
    LIS2DH_Deinit();
    GPIOallPU();
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR,ENABLE);
    PWR_EnterSTOPMode(PWR_Regulator_LowPower,PWR_STOPEntry_WFI);
    NVIC_SystemReset();
}


void GetAcce(uint32_t i, _iq * accex, _iq * accey)
{
    int16_t x,y,z;
    LIS2DH_Get(&x,&y,&z);
    float xp = (float) y * -0.3f;
    float yp = (float) x * 0.3f;

    *accex = _IQ(xp);
    *accey = _IQ(yp);

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
    
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_1);
    SystemCoreClockUpdate();
    Delay_Init();
    GPIOallPU();
    while(GPIO_ReadInputDataBit(GPIOA,GPIO_Pin_0) == Bit_RESET);
    USART_Printf_Init(115200);

    PRINT("SystemClk:%d\r\n", SystemCoreClock);
    PRINT( "ChipID:%08x\r\n", DBGMCU_GetCHIPID() );
    PRINT("This is FLIP example\r\n");

    InitParticles();
    ParticleIntegrate(0, _IQ(9.8f));
    PushParticlesApart(PUSH_ITER);
    particles_to_grid();
    density_update();
    compute_grid_forces(GRID_ITER);
    grid_to_particles();

    LED_InitPeri();
    LED_Show();
    
    Show();

    LIS2DH_Init();

    SysTick->CTLR = 0;
    SysTick->CNT = 0;
    SysTick->CTLR = 1;
    uint32_t time = SysTick->CNT;
    _iq accex = _IQ(0);
    _iq accey = _IQ(9.8f);
    for (int i=0;i<5;i++){
        GetAcce(7000,&accex,&accey);
        ParticleIntegrate(accex, accey);
        PushParticlesApart(PUSH_ITER);
        particles_to_grid();
        density_update();
        compute_grid_forces(GRID_ITER);
        grid_to_particles();
        Show();
    }

    time = SysTick->CNT - time;
    uint32_t fps = ( (5*SystemCoreClock) >> 3 )/time;
    PRINT("fps: %d \r\n",fps);



    uint32_t timer = 0;
    while(1)
    {   
        NVIC_DisableIRQ(TIM1_CC_IRQn);
        //
        GetAcce(7000,&accex,&accey);
        ParticleIntegrate(accex, accey);
        PushParticlesApart(PUSH_ITER);
        particles_to_grid();
        density_update();
        compute_grid_forces(GRID_ITER);
        grid_to_particles();
        Show();
        NVIC_EnableIRQ(TIM1_CC_IRQn);
        //NVIC_EnableIRQ(TIM1_UP_IRQn);
        while(timer ++ < 330)
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
