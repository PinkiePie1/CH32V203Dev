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
#include "SSD1315.h"

/* Global typedef */

/* Global define */

#define PUSH_ITER 1
#define GRID_ITER 9
uint8_t ticks=0;
/* Global Variable */


_iq accx = 0;
_iq accy = 0;

void InitGPIO(void)
{
    GPIO_InitTypeDef  GPIO_InitStructure;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1 | RCC_APB2Periph_GPIOA, ENABLE);
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

}

void Show(void)
{
    if(ticks++%1==0){
        screen_update();
        OLED_16(screen);
    }
}

void GetAcce(uint32_t i)
{
    if (i<20){
        accx = ((GPIOA->INDR & GPIO_Pin_10) != (uint32_t)Bit_RESET) ? _IQ(3) : _IQ(-3) ;
        accy = _IQ(9.8f);
    } else if (i>=20 && i < 40){
        accx = _IQ(16U);
        accy = _IQ(9.8f);
    } else if (i>=40 && i < 110){
        accx = _IQ(-0.5f);
        accy = _IQ(9.0f);
    } else if (i>=110 && i < 130){
        accx = _IQ(-16);
        accy = _IQ(9.8f);       
    } else if (i>=130 && i < 830) {
        accx = _IQ(0);
        accy = _IQ(9.8f);
    } else if (i>=830 && i<1200) {
        _iq t = _IQmpy(_IQ(0.02f),_IQ((i-830)));
        accx = _IQmpy(_IQ(10.0f),_IQsin(t));
        accy = _IQmpy(_IQ(10.0f),_IQcos(t));
    } else if (i>=1200 && i<1600) {
        accx = (GPIO_ReadInputDataBit(GPIOA,GPIO_Pin_10) == Bit_SET) ? _IQ(3) : _IQ(-3) ;
        accy = _IQ(5.8f);
    } else {
        return;
    }

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

    SoftI2CInit();
    OLED_Init();
    OLED_16(screen);
    OLED_TurnOn();

    SysTick->CTLR = 0;
    SysTick->CNT = 0;
    SysTick->CTLR = 1;
    uint32_t time = SysTick->CNT;
    for (int i=0;i<10;i++){
        GetAcce(i);
        ParticleIntegrate(accx,accy);
        PushParticlesApart(PUSH_ITER);
        particles_to_grid();
        density_update();
        compute_grid_forces(GRID_ITER);
        grid_to_particles();
        Show();
    }

    time = SysTick->CNT - time;
    uint32_t fps = ( (10*SystemCoreClock) >> 3 )/time;
    PRINT("fps: %d \r\n",fps);


    while(1)
    {   

        for (int i=0;i<1600;i++){
            GetAcce(i);
            ParticleIntegrate(accx,accy);
            PushParticlesApart(PUSH_ITER);
            particles_to_grid();
            density_update();
            compute_grid_forces(GRID_ITER);
            grid_to_particles();
            Show();

        }

    }

}
