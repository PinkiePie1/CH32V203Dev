#include "charlie.h"

#define CHARLIE_ALL_PINS_MASK    ((uint16_t)0xFFFF)
#define CHARLIE_PINMODE_INPUT    ((uint32_t)0x4)
#define CHARLIE_PINMODE_OUTPUT   ((uint32_t)0x3)

#define CHARLIE_ROW_TIMER_PSC    ((uint16_t)(144 - 1))   /* 1 MHz timer tick @ 144 MHz SYSCLK */
#define CHARLIE_ROW_TIMER_ARR    ((uint16_t)(1000 - 1))  /* 1 kHz row refresh */
#define CHARLIE_PHASE_SHIFT      ((uint16_t)1)
#define CHARLIE_PHASE_BSHR       ((uint16_t)2)
#define CHARLIE_PHASE_BLANK      ((uint16_t)700)

static uint16_t led_framebuffer[CHARLIE_LED_SIZE];

/* 每一行扫描要发送到 GPIO 的序列 */
static uint32_t cfg_low_seq[CHARLIE_LED_SIZE];
static uint32_t cfg_high_seq[CHARLIE_LED_SIZE];
static uint32_t bshr_seq[CHARLIE_LED_SIZE];
static uint16_t bcr_seq[CHARLIE_LED_SIZE];

static void LED_RebuildRow(uint8_t row)
{
    uint32_t cfg_l = 0;
    uint32_t cfg_h = 0;
    uint16_t sinks = led_framebuffer[row] & (uint16_t)~(1U << row);
    uint16_t active_outputs = sinks | (uint16_t)(1U << row);

    for(uint8_t pin = 0; pin < CHARLIE_LED_SIZE; pin++)
    {
        uint32_t mode = ((active_outputs >> pin) & 0x1U) ? CHARLIE_PINMODE_OUTPUT : CHARLIE_PINMODE_INPUT;
        if(pin < 8U)
        {
            cfg_l |= (mode << (pin * 4U));
        }
        else
        {
            cfg_h |= (mode << ((pin - 8U) * 4U));
        }
    }

    cfg_low_seq[row] = cfg_l;
    cfg_high_seq[row] = cfg_h;
    bshr_seq[row] = (uint32_t)(1UL << row); /* 行阳极置高 */
    bcr_seq[row] = CHARLIE_ALL_PINS_MASK;   /* blank 阶段拉低全部输出 */
}

static void LED_RebuildAllRows(void)
{
    for(uint8_t row = 0; row < CHARLIE_LED_SIZE; row++)
    {
        LED_RebuildRow(row);
    }
}

/* 初始化 DMA、定时器和 GPIO 外设 */
void LED_InitPeri(void)
{
    GPIO_InitTypeDef gpio_init = {0};
    TIM_TimeBaseInitTypeDef tim_init = {0};
    DMA_InitTypeDef dma_init = {0};

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB | RCC_APB2Periph_TIM1 | RCC_APB2Periph_AFIO, ENABLE);
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);

    gpio_init.GPIO_Pin = GPIO_Pin_All;
    gpio_init.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    gpio_init.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &gpio_init);
    GPIO_ResetBits(GPIOB, GPIO_Pin_All);

    for(uint8_t row = 0; row < CHARLIE_LED_SIZE; row++)
    {
        led_framebuffer[row] = 0;
    }
    LED_RebuildAllRows();

    TIM_DeInit(TIM1);
    tim_init.TIM_Prescaler = CHARLIE_ROW_TIMER_PSC;
    tim_init.TIM_CounterMode = TIM_CounterMode_Up;
    tim_init.TIM_Period = CHARLIE_ROW_TIMER_ARR;
    tim_init.TIM_ClockDivision = TIM_CKD_DIV1;
    tim_init.TIM_RepetitionCounter = 0;
    TIM_TimeBaseInit(TIM1, &tim_init);

    TIM_SetCompare1(TIM1, CHARLIE_PHASE_SHIFT);
    TIM_SetCompare2(TIM1, CHARLIE_PHASE_BSHR);
    TIM_SetCompare3(TIM1, CHARLIE_PHASE_BLANK);

    TIM_DMACmd(TIM1, TIM_DMA_Update | TIM_DMA_CC1 | TIM_DMA_CC2 | TIM_DMA_CC3, ENABLE);

    DMA_DeInit(DMA1_Channel5); /* TIM1_UP  -> GPIOB_CFGLR */
    dma_init.DMA_PeripheralBaseAddr = (uint32_t)&GPIOB->CFGLR;
    dma_init.DMA_MemoryBaseAddr = (uint32_t)cfg_low_seq;
    dma_init.DMA_DIR = DMA_DIR_PeripheralDST;
    dma_init.DMA_BufferSize = CHARLIE_LED_SIZE;
    dma_init.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    dma_init.DMA_MemoryInc = DMA_MemoryInc_Enable;
    dma_init.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Word;
    dma_init.DMA_MemoryDataSize = DMA_MemoryDataSize_Word;
    dma_init.DMA_Mode = DMA_Mode_Circular;
    dma_init.DMA_Priority = DMA_Priority_VeryHigh;
    dma_init.DMA_M2M = DMA_M2M_Disable;
    DMA_Init(DMA1_Channel5, &dma_init);

    DMA_DeInit(DMA1_Channel2); /* TIM1_CH1 -> GPIOB_CFGHR */
    dma_init.DMA_PeripheralBaseAddr = (uint32_t)&GPIOB->CFGHR;
    dma_init.DMA_MemoryBaseAddr = (uint32_t)cfg_high_seq;
    dma_init.DMA_Priority = DMA_Priority_High;
    DMA_Init(DMA1_Channel2, &dma_init);

    DMA_DeInit(DMA1_Channel3); /* TIM1_CH2 -> GPIOB_BSHR */
    dma_init.DMA_PeripheralBaseAddr = (uint32_t)&GPIOB->BSHR;
    dma_init.DMA_MemoryBaseAddr = (uint32_t)bshr_seq;
    dma_init.DMA_Priority = DMA_Priority_Medium;
    DMA_Init(DMA1_Channel3, &dma_init);

    DMA_DeInit(DMA1_Channel6); /* TIM1_CH3 -> GPIOB_BCR */
    dma_init.DMA_PeripheralBaseAddr = (uint32_t)&GPIOB->BCR;
    dma_init.DMA_MemoryBaseAddr = (uint32_t)bcr_seq;
    dma_init.DMA_PeripheralDataSize = DMA_PeripheralDataSize_HalfWord;
    dma_init.DMA_MemoryDataSize = DMA_MemoryDataSize_HalfWord;
    dma_init.DMA_Priority = DMA_Priority_Low;
    DMA_Init(DMA1_Channel6, &dma_init);

    DMA_Cmd(DMA1_Channel5, ENABLE);
    DMA_Cmd(DMA1_Channel2, ENABLE);
    DMA_Cmd(DMA1_Channel3, ENABLE);
    DMA_Cmd(DMA1_Channel6, ENABLE);
}

/* 设置显示内容缓冲区：buffer[y][x] 非零表示点亮 */
void LED_SetBuffer(uint8_t *buffer[])
{
    if(buffer == NULL)
    {
        return;
    }

    for(uint8_t y = 0; y < CHARLIE_LED_SIZE; y++)
    {
        uint16_t row_mask = 0;
        if(buffer[y] != NULL)
        {
            for(uint8_t x = 0; x < CHARLIE_LED_SIZE; x++)
            {
                if((x != y) && (buffer[y][x] != 0U))
                {
                    row_mask |= (uint16_t)(1U << x);
                }
            }
        }
        led_framebuffer[y] = row_mask;
    }

    LED_RebuildAllRows();
}

/* 点亮/熄灭某个 LED */
void LED_SetPixel(uint8_t x, uint8_t y, uint8_t color)
{
    if((x >= CHARLIE_LED_SIZE) || (y >= CHARLIE_LED_SIZE) || (x == y))
    {
        return;
    }

    if(color != 0U)
    {
        led_framebuffer[y] |= (uint16_t)(1U << x);
    }
    else
    {
        led_framebuffer[y] &= (uint16_t)~(1U << x);
    }

    LED_RebuildRow(y);
}

/* 开启显示：启动定时器触发 DMA */
void LED_Show(void)
{
    DMA_SetCurrDataCounter(DMA1_Channel5, CHARLIE_LED_SIZE);
    DMA_SetCurrDataCounter(DMA1_Channel2, CHARLIE_LED_SIZE);
    DMA_SetCurrDataCounter(DMA1_Channel3, CHARLIE_LED_SIZE);
    DMA_SetCurrDataCounter(DMA1_Channel6, CHARLIE_LED_SIZE);

    TIM_SetCounter(TIM1, 0);
    TIM_Cmd(TIM1, ENABLE);
    TIM_CtrlPWMOutputs(TIM1, ENABLE);
}

/* 关闭显示 */
void LED_TurnOff(void)
{
    TIM_CtrlPWMOutputs(TIM1, DISABLE);
    TIM_Cmd(TIM1, DISABLE);

    DMA_Cmd(DMA1_Channel5, DISABLE);
    DMA_Cmd(DMA1_Channel2, DISABLE);
    DMA_Cmd(DMA1_Channel3, DISABLE);
    DMA_Cmd(DMA1_Channel6, DISABLE);

    GPIOB->BCR = CHARLIE_ALL_PINS_MASK;
    GPIOB->CFGLR = 0x44444444;
    GPIOB->CFGHR = 0x44444444;

    DMA_Cmd(DMA1_Channel5, ENABLE);
    DMA_Cmd(DMA1_Channel2, ENABLE);
    DMA_Cmd(DMA1_Channel3, ENABLE);
    DMA_Cmd(DMA1_Channel6, ENABLE);
}
