#include "debug.h"
#include "SandSim.h"
#include "LIS2DH.h"
#include "charlie.h"

/* Global typedef */

/* Global define */

#define PUSH_ITER FluidPushIterations
#define GRID_ITER FluidPressureIterations
#define FRAME_WAKES 60U



/* Global Variable */

volatile uint32_t sleepTimer = 0;



void EXTI0_INT_INIT(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};
    EXTI_InitTypeDef EXTI_InitStructure = {0};
    NVIC_InitTypeDef NVIC_InitStructure = {0};

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO | RCC_APB2Periph_GPIOA, ENABLE);

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_11;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    /* GPIOA11 ----> EXTI_Line11 */
    GPIO_EXTILineConfig(GPIO_PortSourceGPIOA, GPIO_PinSource11);
    EXTI_InitStructure.EXTI_Line = EXTI_Line11;
    EXTI_InitStructure.EXTI_Mode = EXTI_Mode_Interrupt;
    EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Rising;
    EXTI_InitStructure.EXTI_LineCmd = ENABLE;
    EXTI_Init(&EXTI_InitStructure);

    NVIC_InitStructure.NVIC_IRQChannel = EXTI15_10_IRQn;
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
    //PWR_EnterSTANDBYMode();
    NVIC_SystemReset();
    
}

static _iq acceleration_from_raw(int32_t value)
{
    if (value > 365) value = 365;
    if (value < -365) value = -365;
    return value * _IQ(0.35f);
}

void GetAcce(_iq *accex, _iq *accey)
{
    int16_t x, y, z;
    LIS2DH_Get(&x, &y, &z);
    *accex = acceleration_from_raw(-(int32_t)x - y);
    *accey = acceleration_from_raw((int32_t)y - x);
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


#define TIM1_PSC_FAST   15   
#define TIM1_PSC_SLOW   0 

volatile uint32_t g_clkState = 0;

static void clock_to_fast(void)
{
    if (clockUpdate){return;}
    TIM1->PSC = TIM1_PSC_FAST;
    clockSpeed = FAST;
    clockUpdate = 1;
   
    while(clockUpdate){__WFI();}
    

}    

static void clock_to_slow(void)
{
    if (clockUpdate){return;}
    clockSpeed = SLOW;
    clockUpdate = 1;  
    TIM1->PSC = TIM1_PSC_SLOW;
    //while(clockUpdate){__WFI();}
    

    
} 

int main(void)
{
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_1);
    SystemCoreClockUpdate();
    Delay_Init();
    GPIOallPU();
    EXTI0_INT_INIT();


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
    //clock_to_slow();

    clockUpdate = 1;
    clockSpeed = FAST;
    LED_Brightness(8);


    

    while(1)
    {

        clock_to_fast();
        
        GetAcce(&accex,&accey);
        ParticleIntegrate(accex, accey);
        PushParticlesApart(PUSH_ITER);
        particles_to_grid();
        density_update();
        compute_grid_forces(GRID_ITER);
        grid_to_particles();
        Show();

        clock_to_slow();


        uint32_t count = FRAME_WAKES;
        while(count--)
        {
           __WFI();
        }
#if 1   
        if(sleepTimer++>50*30)
        {
            LED_TurnOff();
            shutdown();
        }
#endif

    }

}

void EXTI15_10_IRQHandler (void) __attribute__((interrupt("WCH-Interrupt-fast")));
void EXTI15_10_IRQHandler (void)
{
  if(EXTI_GetITStatus(EXTI_Line11)!=RESET)
  {
    sleepTimer = 0;//reset timer.
    EXTI_ClearITPendingBit(EXTI_Line11);     /* Clear Flag */
  }
}
