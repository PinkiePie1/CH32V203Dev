#include "charlie.h"

uint8_t row = 0;

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

static uint8_t bright[PinCount] = {0};//records the number of led for each row to adjust brightness

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
static uint8_t  ledRunning = 0;

static void LED_RebuildDMABuffer(void)
{
    for(u8 i = 0; i < PinCount; i++)
    {
        dmaOutdrOn[i] = (uint32_t)1U << i;
        dmaOutdrOff[i] = 0;
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
    LED_InitDMAChannel(DMA1_Channel5, (uint32_t)&GPIOB->OUTDR, (uint32_t)dmaOutdrOff);

    timBaseCfg.TIM_Prescaler = (SystemCoreClock / 8000000U) - 1U;
    timBaseCfg.TIM_CounterMode = TIM_CounterMode_Up;
    timBaseCfg.TIM_Period = (onTime + offTime) - 1U;
    timBaseCfg.TIM_ClockDivision = TIM_CKD_DIV1;
    timBaseCfg.TIM_RepetitionCounter = 0;
    TIM_TimeBaseInit(TIM1, &timBaseCfg);

    TIM_SetCompare1(TIM1,10);
    TIM_SetCompare2(TIM1,10);
    TIM_SetCompare3(TIM1,offTime);


    TIM_DMACmd(TIM1, TIM_DMA_Update | TIM_DMA_CC1 | TIM_DMA_CC2 | TIM_DMA_CC3, ENABLE);
    // update-> channel 5
    // CC1 -> channel 2
    // CC2 -> channel 3
    // CC3 -> channel 6

    //enable support for sleep mode. 
    TIM_ITConfig(TIM1,TIM_IT_CC1 | TIM_IT_CC3 | TIM_IT_Update,ENABLE);
	NVIC_EnableIRQ(TIM1_CC_IRQn);
    NVIC_EnableIRQ(TIM1_UP_IRQn);
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
            gpioCFGH[y] &= ~(0x3 << ((x - 8) * 4));
        }
        else
        {
            gpioCFGL[y] &= ~(0x3 << (x * 4));
        }
    }
    u8 count = 0;
    for (u8 i = 0; i < 8; i++){
        if( (gpioCFGH[y]>>(i*4)) & 0x3){count +=1;}
        if( (gpioCFGL[y]>>(i*4)) & 0x3){count +=1;}
    }
    bright[y] = count-1;
    //PRINT("birght:[%d] is : %d\r\n",y,bright[y]);
}

// 开启显示，启动timer触发DMA自动刷新GPIO寄存器
void LED_Show(void)
{
    if(ledRunning)
    {
        return;
    }

    DMA_SetCurrDataCounter(DMA1_Channel5, PinCount);
    DMA_SetCurrDataCounter(DMA1_Channel2, PinCount);
    DMA_SetCurrDataCounter(DMA1_Channel3, PinCount);
    DMA_SetCurrDataCounter(DMA1_Channel6, PinCount);

    DMA_Cmd(DMA1_Channel5, ENABLE);
    DMA_Cmd(DMA1_Channel2, ENABLE);
    DMA_Cmd(DMA1_Channel3, ENABLE);
    DMA_Cmd(DMA1_Channel6, ENABLE);

    TIM_SetCounter(TIM1, 0);
    TIM_Cmd(TIM1, ENABLE);

    ledRunning = 1;
}

//

void TIM1_CC_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void TIM1_UP_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));

void TIM1_UP_IRQHandler(void)
{
    GPIOB->BSHR=0xFFFFFFFF;
    GPIOB->CFGHR=0x33333333;
    GPIOB->CFGLR=0x33333333;
    u8 rowNum = 16-(uint16_t)(DMA1_Channel3->CNTR);
    u16 adj = offTime-(bright[rowNum]<<1);
    TIM_SetCompare3(TIM1,adj);
    TIM_ClearITPendingBit(TIM1, TIM_IT_Update);
}

void TIM1_CC_IRQHandler(void)
{
    TIM_ClearITPendingBit(TIM1,TIM_IT_CC1|TIM_IT_CC3);
}