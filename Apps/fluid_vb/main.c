#include "debug.h"
#include "SandSim.h"
#include "LIS2DH.h"
#include "charlie.h"

/* Global typedef */

/* Global define */

#define PUSH_ITER 1
#define GRID_ITER 8
/* 帧节奏：每帧的WFI唤醒次数。TIM1每175us产生CC1/CC3/Update三个事件(72MHz/21/600)，
   50次唤醒约3ms。厂家确认DMA写GPIO时CPU必须处于唤醒状态，因此这三个中断必须
   保持使能（见charlie.c），CPU只能在两次DMA事件之间小睡。调大该值则帧率降低、
   睡眠占比升高；调小则仿真动作更快。 */
#define FRAME_WAKES 50U

/* Global Variable */

uint32_t sleepTimer = 0;

void EXTI0_INT_INIT(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};
    EXTI_InitTypeDef EXTI_InitStructure = {0};
    NVIC_InitTypeDef NVIC_InitStructure = {0};

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO | RCC_APB2Periph_GPIOA, ENABLE);

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_7;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    /* GPIOA7 ----> EXTI_Line7 */
    GPIO_EXTILineConfig(GPIO_PortSourceGPIOA, GPIO_Pin_7);
    EXTI_InitStructure.EXTI_Line = EXTI_Line7;
    EXTI_InitStructure.EXTI_Mode = EXTI_Mode_Interrupt;
    EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Rising;
    EXTI_InitStructure.EXTI_LineCmd = ENABLE;
    EXTI_Init(&EXTI_InitStructure);

    NVIC_InitStructure.NVIC_IRQChannel = EXTI9_5_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 2;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

}

void GPIOallPU(void){

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB | RCC_APB2Periph_GPIOA | 
        RCC_APB2Periph_GPIOC| RCC_APB2Periph_GPIOD |RCC_APB2Periph_AFIO, ENABLE);
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

void shutdown(void)
{
    GPIO_PinRemapConfig(GPIO_Remap_SWJ_Disable,ENABLE);
    LIS2DH_Deinit();
    GPIOallPU();
    EXTI0_INT_INIT();
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR,ENABLE);
    PWR_EnterSTOPMode(PWR_Regulator_LowPower,PWR_STOPEntry_WFI);
    NVIC_SystemReset();
    
}

void GetAcce(_iq * accex, _iq * accey)
{
    int16_t x,y,z;
    LIS2DH_Get(&x,&y,&z);

    float xp = (float) (-y * 0.35f);
    float yp = (float) (x * 0.35f);

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
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_1);
    SystemCoreClockUpdate();
    Delay_Init();
    GPIOallPU();
    USART_Printf_Init(115200);
    EXTI0_INT_INIT();

    PRINT("This is FLIP\r\n");

    InitParticles();
    LED_InitPeri();
    LED_Show();

try:
    if(LIS2DH_Init()!=0){
        for (uint16_t i = 0; i < 240; i++)
        {
            LED_SetPixel(i,LEDON);
        }
        Delay_Ms(1000);
        goto try;
    }

    Show();

    _iq accex = _IQ(0);
    _iq accey = _IQ(9.8f);

    while(1)
    {

        uint32_t count = 70;

        GetAcce(&accex,&accey);
        ParticleIntegrate(accex, accey);
        PushParticlesApart(PUSH_ITER);
        particles_to_grid();
        density_update();
        compute_grid_forces(GRID_ITER);
        grid_to_particles();
        Show();

        
        while(count--)
        {
           __WFI();
        }
        if(sleepTimer++>5*100)
        {
            LED_TurnOff();
            shutdown();
        }

    }

}

void EXTI9_5_IRQHandler (void) __attribute__((interrupt("WCH-Interrupt-fast")));
void EXTI9_5_IRQHandler (void)
{
  if(EXTI_GetITStatus(EXTI_Line7)!=RESET)
  {
    sleepTimer = 0;//reset timer.
    EXTI_ClearITPendingBit(EXTI_Line7);     /* Clear Flag */
  }
}
