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

#define IDLE_TIMEOUT_MS     15000U
#define STILL_DELTA_THRESH  8U
#define SHAKE_DELTA_THRESH  10U
#define SLEEP_POLL_MS       25U
#define IMU_DELTA_LOG_EVERY 20U

uint8_t ticks=0;
/* Global Variable */

static int16_t imu_prev_x;
static int16_t imu_prev_y;
static int16_t imu_prev_z;
static uint8_t imu_prev_valid;
static uint32_t idle_ms;
static uint8_t display_asleep;
static uint32_t imu_log_frame;

static int16_t abs16(int16_t v)
{
    return (v < 0) ? (int16_t)(-v) : v;
}

static uint32_t systick_elapsed_ms(uint32_t t0)
{
    uint32_t elapsed = SysTick->CNT - t0;
    return (elapsed * 1000U) / (SystemCoreClock >> 3);
}

static uint32_t read_imu_delta(int16_t *x, int16_t *y, int16_t *z)
{
    LIS2DH_Get(x, y, z);
    if (!imu_prev_valid) {
        imu_prev_x = *x;
        imu_prev_y = *y;
        imu_prev_z = *z;
        imu_prev_valid = 1;
        return 0;
    }
    uint32_t delta = (uint32_t)abs16((int16_t)(*x - imu_prev_x))
                   + (uint32_t)abs16((int16_t)(*y - imu_prev_y))
                   + (uint32_t)abs16((int16_t)(*z - imu_prev_z));
    imu_prev_x = *x;
    imu_prev_y = *y;
    imu_prev_z = *z;
    return delta;
}

static void update_idle_timer(uint32_t delta, uint32_t frame_ms)
{
    if (delta < STILL_DELTA_THRESH) {
        idle_ms += frame_ms;
        if (idle_ms > IDLE_TIMEOUT_MS) {
            idle_ms = IDLE_TIMEOUT_MS;
        }
    } else {
        idle_ms = 0;
    }
}

static void enter_display_sleep(void)
{
    LED_DisplayStop();
    display_asleep = 1;
    idle_ms = 0;
    PRINT("display sleep\r\n");
}

static void wake_display(void)
{
    display_asleep = 0;
    idle_ms = 0;
    LED_DisplayStart();
    PRINT("wake\r\n");
}

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

static void check_shutdown_button(void)
{
    if (GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_0) == Bit_RESET) {
        Delay_Ms(30);
        if (GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_0) == Bit_RESET) {
            while (GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_0) == Bit_RESET)
                ;
            Delay_Ms(30);
            shutdown();
        }
    }
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

    imu_prev_valid = 0;
    idle_ms = 0;
    display_asleep = 0;

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
    imu_log_frame = 0;
    while(1)
    {
        if (!display_asleep) {
            /* Delay_Ms() 结尾会停掉 SysTick 计数器（睡眠分支每次轮询都会调），
               因此每帧开始时重启自由运行的 SysTick，保证 frame_ms 计时有效 */
            SysTick->CTLR = 0;
            SysTick->CNT = 0;
            SysTick->CTLR = 1;
            uint32_t t0 = SysTick->CNT;
            NVIC_DisableIRQ(TIM1_CC_IRQn);
            NVIC_DisableIRQ(TIM1_UP_IRQn);
            int16_t ix, iy, iz;
            uint32_t delta = read_imu_delta(&ix, &iy, &iz);
            float xp = (float) iy * -0.3f;
            float yp = (float) ix * 0.3f;
            accex = _IQ(xp);
            accey = _IQ(yp);
            ParticleIntegrate(accex, accey);
            PushParticlesApart(PUSH_ITER);
            particles_to_grid();
            density_update();
            compute_grid_forces(GRID_ITER);
            grid_to_particles();
            Show();
            NVIC_EnableIRQ(TIM1_CC_IRQn);
            NVIC_EnableIRQ(TIM1_UP_IRQn);
            while (timer++ < 10) {
                __WFI();
            }
            timer = 0;
            uint32_t frame_ms = systick_elapsed_ms(t0);
            if (frame_ms == 0U) {
                frame_ms = 1U;
            }
            update_idle_timer(delta, frame_ms);
            imu_log_frame++;
            if (imu_log_frame >= IMU_DELTA_LOG_EVERY) {
                imu_log_frame = 0;
                PRINT("delta=%u idle_ms=%u\r\n", delta, idle_ms);
            }
            if (idle_ms >= IDLE_TIMEOUT_MS) {
                enter_display_sleep();
            }
            check_shutdown_button();
        } else {
            Delay_Ms(SLEEP_POLL_MS);
            int16_t x, y, z;
            uint32_t delta = read_imu_delta(&x, &y, &z);
            if (delta >= SHAKE_DELTA_THRESH) {
                wake_display();
            }
            check_shutdown_button();
            /* 睡眠状态下 TIM1 中断已关闭、Delay_Ms 为轮询实现，
               没有任何中断源能唤醒 __WFI()，这里不能 WFI，
               只能依靠 Delay_Ms(SLEEP_POLL_MS) 定速轮询加速度计。 */
        }
    }
}
