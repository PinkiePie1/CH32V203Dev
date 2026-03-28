#include "debug.h"
#include "SandSim.h"
#include "LIS2DH.h"
#include "charlie.h"

/* Global typedef */

/* Global define */

#define PUSH_ITER 1
#define GRID_ITER 8

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
    uint32_t timer = 0;
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
        LED_SetPixel(120,LEDON);
        Delay_Ms(1000);
        goto try;
    }

    Show();

    _iq accex = _IQ(0);
    _iq accey = _IQ(9.8f);

    while(1)
    {   
        NVIC_DisableIRQ(TIM1_CC_IRQn);
        NVIC_DisableIRQ(TIM1_UP_IRQn);

        GetAcce(&accex,&accey);
        ParticleIntegrate(accex, accey);
        PushParticlesApart(PUSH_ITER);
        particles_to_grid();
        density_update();
        compute_grid_forces(GRID_ITER);
        grid_to_particles();
        Show();

        NVIC_EnableIRQ(TIM1_CC_IRQn);
        NVIC_EnableIRQ(TIM1_UP_IRQn);
        while(timer ++ < 3)
        {
           __WFI();
        }
        timer = 0;
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
