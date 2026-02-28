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
//#include "SSD1315.h"
#include "LIS2DH.h"

/* Global typedef */

/* Global define */

#define PUSH_ITER 1
#define GRID_ITER 13
uint8_t ticks=0;
/* Global Variable */

void InitGPIO(void)
{
    GPIO_InitTypeDef  GPIO_InitStructure;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1 | RCC_APB2Periph_GPIOA, ENABLE);
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

}

void GetAcce(uint32_t i, _iq * accex, _iq * accey)
{
    if (i<300)
    {
        *accex = _IQ(0);
        *accey = _IQ(9.8f);
    } 
    else if (i < 600)
    {
        _iq t = _IQmpy(_IQ(0.02f),_IQ((i-590)));
        *accex = _IQmpy(_IQ(10.0f),_IQsin(t));
        *accey = _IQmpy(_IQ(10.0f),_IQcos(t));
    }
    else if (i < 900)
    {
        *accex = _IQ(9.8f);
        *accey = _IQ(0);
    }
    else if (i < 1200)
    {
        *accex = _IQ(-9.8f);
        *accey = _IQ(0);
    }
    else
    {
    	
        int16_t x,y,z;
        LIS2DH_Get(&x,&y,&z);
        float xp = (float) x * -0.32f;
        float yp = (float) y * 0.32f;

        *accex = _IQ(xp);
        *accey = _IQ(yp);
        
    }
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
    screen_update();

    InitGPIO();

    LED_InitPeri();
    LED_Show();


    //SoftI2CInit();
    LIS2DH_Init();


    //OLED_Init();
    //OLED_16(screen);
    //OLED_TurnOn();



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


    while(1)
    {   

            GetAcce(7000,&accex,&accey);
            ParticleIntegrate(accex, accey);
            PushParticlesApart(PUSH_ITER);
            particles_to_grid();
            density_update();
            compute_grid_forces(GRID_ITER);
            grid_to_particles();
            Show();
            Delay_Us(4900);

        

    }
}
