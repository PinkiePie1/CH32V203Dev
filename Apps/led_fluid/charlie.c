#include "charlie.h"
#include <string.h>

static uint8_t LUT[] = {
15,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,

30,31,16,17,18,19,20,21,22,23,24,25,26,27,28,29,

45,46,47,32,33,34,35,36,37,38,39,40,41,42,43,44,

60,61,62,63,48,49,50,51,52,53,54,55,56,57,58,59,

75,76,77,78,79,64,65,66,67,68,69,70,71,72,73,74,

90,91,92,93,94,95,80,81,82,83,84,85,86,87,88,89,

105,106,107,108,109,110,111,96,97,98,99,100,101,102,103,104,

120,121,122,123,124,125,126,127,112,113,114,115,116,117,118,119,

135,136,137,138,139,140,141,142,143,128,129,130,131,132,133,134,

150,151,152,153,154,155,156,157,158,159,144,145,146,147,148,149,

165,166,167,168,169,170,171,172,173,174,175,160,161,162,163,164,

180,181,182,183,184,185,186,187,188,189,190,191,176,177,178,179,

195,196,197,198,199,200,201,202,203,204,205,206,207,192,193,194,

210,211,212,213,214,215,216,217,218,219,220,221,222,223,208,209,

225,226,227,228,229,230,231,232,233,234,235,236,237,238,239,224
};

static uint16_t bright[PinCount] = {offTime};//records the number of led for each row to adjust brightness

/* 行驱动基准图案:第 y 行的行引脚(阳极)必须恒为输出,LED 点亮只需再加第二个引脚。
   LED_SetPixel 只增/删第二个引脚,永远不会重建行引脚,所以清屏(LED_DisplayStop)
   必须恢复成这个基准图案而不是清零——清零会把行引脚一起抹掉,唤醒后整屏无法点亮 */
static const uint32_t gpioCFGLBase[PinCount] =
{0X00000003,0X00000030,0X00000300,0X00003000,
 0X00030000,0X00300000,0X03000000,0X30000000,
 0X00000000,0X00000000,0X00000000,0X00000000,
 0X00000000,0X00000000,0X00000000,0X00000000};

static const uint32_t gpioCFGHBase[PinCount] =
{0X00000000,0X00000000,0X00000000,0X00000000,
 0X00000000,0X00000000,0X00000000,0X00000000,
 0X00000003,0X00000030,0X00000300,0X00003000,
 0X00030000,0X00300000,0X03000000,0X30000000};

static uint32_t gpioCFGL[PinCount];
static uint32_t gpioCFGH[PinCount];

// 恢复扫描缓冲到行驱动基准(全灭,但行引脚保持输出)
static void LED_ResetScanBuffers(void)
{
    memcpy(gpioCFGL, gpioCFGLBase, sizeof(gpioCFGL));
    memcpy(gpioCFGH, gpioCFGHBase, sizeof(gpioCFGH));
    for (u8 y = 0; y < PinCount; y++) {
        bright[y] = offTime;
    }
}

static uint32_t dmaOutdrOn[PinCount];
static uint32_t dmaOutdrOff[PinCount];

static void LED_RebuildDMABuffer(void)
{
    for(u8 i = 0; i < PinCount; i++)
    {
        dmaOutdrOn[i] = (uint32_t)1U << i;
        dmaOutdrOff[i] = 0xFFFFFFFF;
    }
    LED_ResetScanBuffers();
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

    LED_InitDMAChannel(DMA1_Channel6, (uint32_t)&GPIOB->OUTDR, (uint32_t)dmaOutdrOn);
    LED_InitDMAChannel(DMA1_Channel2, (uint32_t)&GPIOB->CFGLR, (uint32_t)gpioCFGL);
    LED_InitDMAChannel(DMA1_Channel3, (uint32_t)&GPIOB->CFGHR, (uint32_t)gpioCFGH);
    LED_InitDMAChannel(DMA1_Channel5, (uint32_t)&GPIOB->BSHR, (uint32_t)dmaOutdrOff);
    LED_InitDMAChannelHalfWord(DMA1_Channel4, (uint32_t)&TIM1->CH3CVR, (uint32_t)bright);

    timBaseCfg.TIM_Prescaler = 20;
    timBaseCfg.TIM_CounterMode = TIM_CounterMode_Up;
    timBaseCfg.TIM_Period = (onTime + offTime) - 1U;
    timBaseCfg.TIM_ClockDivision = TIM_CKD_DIV1;
    timBaseCfg.TIM_RepetitionCounter = 0;
    TIM_TimeBaseInit(TIM1, &timBaseCfg);

    TIM_SetCompare1(TIM1,1);
    TIM_SetCompare2(TIM1,1);
    TIM_SetCompare3(TIM1,offTime);
    TIM_SetCompare4(TIM1,1);


    TIM_DMACmd(TIM1, TIM_DMA_Update | TIM_DMA_CC1 | TIM_DMA_CC2 | TIM_DMA_CC3 |TIM_DMA_CC4, ENABLE);
    // update-> channel 5
    // CC1 -> channel 2
    // CC2 -> channel 3
    // CC3 -> channel 6
    // CC4 -> channel 4

    //enable support for sleep mode. 
    TIM_ITConfig(TIM1,TIM_IT_CC1 | TIM_IT_CC3 | TIM_IT_Update,ENABLE);
	NVIC_EnableIRQ(TIM1_CC_IRQn);
    NVIC_EnableIRQ(TIM1_UP_IRQn);

    /* 扫描在这里一次启动后持续运行;显示睡眠只清空画面缓冲(LED_DisplayStop),
       不再停止 TIM1/DMA,避免停机重启带来的相位/状态问题 */
    DMA_Cmd(DMA1_Channel5, ENABLE);
    DMA_Cmd(DMA1_Channel2, ENABLE);
    DMA_Cmd(DMA1_Channel4, ENABLE);
    DMA_Cmd(DMA1_Channel3, ENABLE);
    DMA_Cmd(DMA1_Channel6, ENABLE);

    TIM_SetCounter(TIM1, 0);
    TIM_Cmd(TIM1, ENABLE);
}

// 点亮或熄灭某个LED
void LED_SetPixel(uint16_t num, uint8_t color)
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
    u8 count = 0;
    for (u32 comp = 0x3; comp; comp<<=4)
    {
        count = comp & gpioCFGH[y]?count+1:count;
        count = comp & gpioCFGL[y]?count+1:count;
    }
    count -= 1;
    bright[y] = offTime-count;
    //PRINT("birght:[%d] is : %d\r\n",y,bright[y]);
}

void LED_DisplayStart(void)
{
    /* 唤醒显示:TIM1/DMA 一直在运行,画面由下一帧 screen_update() 重填,
       这里只需恢复 TIM1 中断(主循环 WFI 帧节奏用) */
    TIM_ClearITPendingBit(TIM1, TIM_IT_CC1 | TIM_IT_CC3 | TIM_IT_CC4 | TIM_IT_Update);
    NVIC_EnableIRQ(TIM1_CC_IRQn);
    NVIC_EnableIRQ(TIM1_UP_IRQn);
}

// 开启显示，启动timer触发DMA自动刷新GPIO寄存器
void LED_Show(void)
{
    LED_DisplayStart();
}

// 显示休眠:扫描缓冲恢复到行驱动基准图案(DMA 继续扫,所有灯灭),
// 并关掉 TIM1 中断;TIM1/DMA 本身不停,唤醒时零成本恢复。
// 注意绝不能把 gpioCFGL/CFGH 清零:行引脚图案被抹掉后 LED_SetPixel 不会重建,唤醒会黑屏
void LED_DisplayStop(void)
{
    NVIC_DisableIRQ(TIM1_CC_IRQn);
    NVIC_DisableIRQ(TIM1_UP_IRQn);

    LED_ResetScanBuffers();
}

void TIM1_CC_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void TIM1_UP_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));

void TIM1_UP_IRQHandler(void)
{
    TIM_ClearITPendingBit(TIM1, TIM_IT_Update);
}

void TIM1_CC_IRQHandler(void)
{
    TIM_ClearITPendingBit(TIM1,TIM_IT_CC1|TIM_IT_CC3|TIM_IT_CC4);
}