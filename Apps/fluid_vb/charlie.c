#include "charlie.h"

const uint16_t LUT[240] = {
    101,  26, 181, 211,  28,  43, 212, 167,
     40, 160, 175,   9, 150,  60,   3,  68,
     20,  91, 222, 193,  31,  16, 178, 221,
    152,  39,  10, 165,  69, 154, 135,   8,
     46, 216, 103,  41, 182, 166,  25, 163,
    220,   1,  30,  70, 169, 144, 159,  12,
    213,  58,  35,  92, 176, 191, 151,  24,
     13, 210,  62,  33, 145, 174, 205, 162,
     88,  32,  47, 171, 100, 161, 190,   0,
     15,  73, 214, 137,  38, 206, 177, 158,
     34,  77, 168,  55, 156,  99,  11, 180,
     61,  18, 148, 219, 197,  42, 130, 173,
    107, 170,  85, 153,  54,   5,  90,  71,
    184, 136,  23, 208, 223, 122,  37, 179,
    172, 115, 155,  84,   2,  45,  65,  94,
    146, 189, 196,  27, 133, 218, 227,  44,
    236, 157, 114,   4,  75,  63,  48, 141,
     98, 207, 192, 121,  22, 239, 224,  36,
    164, 235,   6, 105,  64,  79, 138,  53,
    201, 102, 131, 188, 226,  29, 118, 217,
    129,  14, 225,  66, 109, 140,  83, 198,
     57, 126,  97, 237, 194, 106,  21, 215,
      7, 120,  74, 229, 142, 113, 200,  87,
    123,  52, 231, 104, 116, 187,  76,  19,
    195,  67, 124, 149, 234, 202, 117, 125,
     82, 228,  59, 111,  96,  86, 185,  17,
     72, 199, 143, 128, 209, 238, 127, 112,
    230,  89, 108,  51,  80,  95,  56, 183,
    139, 147, 204, 203, 132, 134, 233, 232,
    119, 110,  81,  78,  49,  50,  93, 186

};

static uint16_t bright[PinCount] = {0};//records the number of led for each row to adjust Compensation
static uint8_t mode = 0;//0 for auto adjust, 1 for fixed.

static uint32_t gpioCFGL[16] =
{0X00000003,0X00000030,0X00000300,0X00003000,
 0X00030000,0X00300000,0X03000000,0X30000000,
 0X00000000,0X00000000,0X00000000,0X00000000,
 0X00000000,0X00000000,0X00000000,0X00000000};

static uint32_t gpioCFGH[16] =
{0X00000000,0X00000000,0X00000000,0X00000000,
 0X00000000,0X00000000,0X00000000,0X00000000,
 0X00000003,0X00000030,0X00000300,0X00003000,
 0X00030000,0X00300000,0X03000000,0X30000000};

static uint32_t dmaOutdrOn[PinCount];
static uint32_t dmaOutdrOff[PinCount];

/* TIM1更新中断次数，作为帧节奏的打拍计数(每拍约175us) */
volatile uint32_t tim1Tick = 0;

static void LED_RebuildDMABuffer(void)
{
    for(u8 i = 0; i < PinCount; i++)
    {
        dmaOutdrOn[i] = (uint32_t)1U << i;
        dmaOutdrOff[i] = 0xFFFFFFFF;
        bright[i]=Period-Compensation;
    }
}

static void LED_InitDMAChannel(DMA_Channel_TypeDef *ch, uint32_t periph, uint32_t mem)
{
    DMA_InitTypeDef dmaCfg = {0};

    DMA_DeInit(ch);
    dmaCfg.DMA_PeripheralBaseAddr = periph;
    dmaCfg.DMA_MemoryBaseAddr = mem;
    dmaCfg.DMA_DIR = DMA_DIR_PeripheralDST;
    dmaCfg.DMA_BufferSize = PinCount;
    dmaCfg.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    dmaCfg.DMA_MemoryInc = DMA_MemoryInc_Enable;
    dmaCfg.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Word;
    dmaCfg.DMA_MemoryDataSize = DMA_MemoryDataSize_Word;
    dmaCfg.DMA_Mode = DMA_Mode_Circular;
    dmaCfg.DMA_Priority = DMA_Priority_High;
    dmaCfg.DMA_M2M = DMA_M2M_Disable;
    DMA_Init(ch, &dmaCfg);
}

static void LED_InitDMAChannelHalfWord(DMA_Channel_TypeDef *ch, uint32_t periph, uint32_t mem)
{
    DMA_InitTypeDef dmaCfg = {0};

    DMA_DeInit(ch);
    dmaCfg.DMA_PeripheralBaseAddr = periph;
    dmaCfg.DMA_MemoryBaseAddr = mem;
    dmaCfg.DMA_DIR = DMA_DIR_PeripheralDST;
    dmaCfg.DMA_BufferSize = PinCount;
    dmaCfg.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    dmaCfg.DMA_MemoryInc = DMA_MemoryInc_Enable;
    dmaCfg.DMA_PeripheralDataSize = DMA_PeripheralDataSize_HalfWord;
    dmaCfg.DMA_MemoryDataSize = DMA_MemoryDataSize_HalfWord;
    dmaCfg.DMA_Mode = DMA_Mode_Circular;
    dmaCfg.DMA_Priority = DMA_Priority_High;
    dmaCfg.DMA_M2M = DMA_M2M_Disable;
    DMA_Init(ch, &dmaCfg);

}

// 初始化DMA、定时器和GPIO外设
void LED_InitPeri(void)
{
    GPIO_InitTypeDef gpioInit = {0};
    TIM_TimeBaseInitTypeDef timBaseCfg = {0};
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB | RCC_APB2Periph_TIM1, ENABLE);
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);

    gpioInit.GPIO_Pin = GPIO_Pin_All;
    gpioInit.GPIO_Mode = GPIO_Mode_AIN;
    gpioInit.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &gpioInit);

    LED_RebuildDMABuffer();
    for(uint32_t i = 0; i < 240; i++)
    {
        LED_SetPixel(i,LEDOFF);
    }


    LED_InitDMAChannel(DMA1_Channel6, (uint32_t)&GPIOB->OUTDR, (uint32_t)dmaOutdrOn);
    LED_InitDMAChannel(DMA1_Channel2, (uint32_t)&GPIOB->CFGLR, (uint32_t)gpioCFGL);
    LED_InitDMAChannel(DMA1_Channel3, (uint32_t)&GPIOB->CFGHR, (uint32_t)gpioCFGH);
    LED_InitDMAChannel(DMA1_Channel5, (uint32_t)&GPIOB->BSHR, (uint32_t)dmaOutdrOff);
    LED_InitDMAChannelHalfWord(DMA1_Channel4, (uint32_t)&TIM1->CH3CVR, (uint32_t)bright);

    timBaseCfg.TIM_Prescaler = 20;
    timBaseCfg.TIM_CounterMode = TIM_CounterMode_Up;
    timBaseCfg.TIM_Period = Period - 1U;
    timBaseCfg.TIM_ClockDivision = TIM_CKD_DIV1;
    timBaseCfg.TIM_RepetitionCounter = 0;
    TIM_TimeBaseInit(TIM1, &timBaseCfg);

    TIM_SetCompare1(TIM1,1);
    TIM_SetCompare2(TIM1,1);
    TIM_SetCompare3(TIM1,Period-Compensation);
    TIM_SetCompare4(TIM1,1);

    TIM_DMACmd(TIM1, TIM_DMA_Update | TIM_DMA_CC1 | TIM_DMA_CC2 | TIM_DMA_CC3 |TIM_DMA_CC4, ENABLE);
    // update-> channel 5
    // CC1 -> channel 2
    // CC2 -> channel 3
    // CC3 -> channel 6
    // CC4 -> channel 4

    //enable support for sleep mode.
    //只需要Update中断做帧节拍打拍；CC中断仅用于唤醒，DMA刷新不依赖它，关掉以减少唤醒次数
    TIM_ITConfig(TIM1,TIM_IT_Update,ENABLE);
    //NVIC_EnableIRQ(TIM1_CC_IRQn);
    NVIC_EnableIRQ(TIM1_UP_IRQn);



}

static void LED_RecomputeRowBrightness(u16 y)
{
    if(mode == 0){
        u8 count = 0;
        for (u32 comp = 0x3; comp; comp<<=4)
        {
            count = comp & gpioCFGH[y]?count+1:count;
            count = comp & gpioCFGL[y]?count+1:count;
        }
        count -= 1;
        uint16_t pwm = Period-(count);
        pwm = pwm - (pwm>>Brightness);
        bright[y] = pwm;
    }
}

static void LED_SetPixelInternal(uint16_t num, uint8_t color, uint8_t updateBright)
{
    num = LUT[num];
    u16 x = num % (PinCount - 1);
    u16 y = num / (PinCount - 1);
    x = x >= y ? x + 1 : x;
    if(color == LEDON)
    {
        if(x >= 8)
        {
            gpioCFGH[y] |= 0x3 << ((x - 8) * 4);
        }
        else
        {
            gpioCFGL[y] |= 0x3 << (x * 4);
        }
    }
    else
    {
        if(x >= 8)
        {
            gpioCFGH[y] &= ~(0xF << ((x - 8) * 4));
        }
        else
        {
            gpioCFGL[y] &= ~(0xF << (x * 4));
        }
    }
    if(updateBright){
        LED_RecomputeRowBrightness(y);
    }
}

void LED_SetPixel(uint16_t num, uint8_t color)
{
    LED_SetPixelInternal(num, color, 1);
}

/* 只写像素不重算亮度，配合LED_CommitBrightness批量刷新一整帧 */
void LED_SetPixelFast(uint16_t num, uint8_t color)
{
    LED_SetPixelInternal(num, color, 0);
}

/* 整帧像素写完后统一重算所有行的亮度补偿 */
void LED_CommitBrightness(void)
{
    for(u16 y = 0; y < PinCount; y++)
    {
        LED_RecomputeRowBrightness(y);
    }
}

// 开启显示，启动timer触发DMA自动刷新GPIO寄存器
void LED_Show(void)
{
    DMA_Cmd(DMA1_Channel5, ENABLE);
    DMA_Cmd(DMA1_Channel2, ENABLE);
    DMA_Cmd(DMA1_Channel4, ENABLE);
    DMA_Cmd(DMA1_Channel3, ENABLE);
    DMA_Cmd(DMA1_Channel6, ENABLE);
    TIM_SetCounter(TIM1, 0);
    TIM_Cmd(TIM1, ENABLE);

}

void LED_TurnOff(void)
{
    TIM_Cmd(TIM1, DISABLE);
    GPIOB->BCR=0xFFFFFFFF;
    GPIOB->CFGLR = 0;
    GPIOB->CFGHR = 0;

}

//if it's -1, use auto compensation, else use fixed brightness
void LED_Brightness(int32_t brightness)
{
    if(brightness<=0){
        mode = 0;
    } else {
        mode = 1;
        for(u8 i = 0; i < PinCount; i++)
        {
            bright[i]=Period-brightness;
        }
    }
}


void TIM1_CC_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void TIM1_UP_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));

void TIM1_UP_IRQHandler(void)
{
    tim1Tick++;
    TIM_ClearITPendingBit(TIM1, TIM_IT_Update);
}

void TIM1_CC_IRQHandler(void)
{
    TIM_ClearITPendingBit(TIM1,TIM_IT_CC1|TIM_IT_CC3|TIM_IT_CC4);
}
