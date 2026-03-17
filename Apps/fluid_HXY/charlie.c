#include "charlie.h"
/*
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
*/
static uint16_t LUT[240] = {
    101,  26, 181,  61,  18,  33,  62, 167, 
     40, 160, 175,   9, 150, 210,  13, 219, 
     20,  91,  71, 184,  31,  16, 169,  70, 
    152,  39,  10, 165, 220, 163, 135,   8, 
     46,  65,  94,  41, 182, 166,  25, 154, 
     69,   1,  30, 221, 178, 144, 159,  12, 
     63,  48,  35,  92, 176, 191, 151,  24, 
      3,  60, 212,  43, 145, 174, 205, 162, 
     79,  32,  47, 171, 100, 161, 190,   0, 
     15, 214,  73, 137,  38, 206, 177, 158, 
     34,  77, 168,  55, 156,  99,  11, 180, 
    211,  28, 139,  68, 197,  42, 130, 173, 
    107, 170,  85, 153,  54,   5,  90, 222, 
    193, 136,  23, 199,  72, 122,  37, 179, 
    172, 115, 155,  84,   2,  45, 216, 103, 
    146, 189, 196,  27, 124,  67, 227,  44, 
    236, 157, 114,   4,  75, 213,  58, 141, 
     98, 207, 192, 121,  22, 229,  74,  36, 
    164, 235,   6, 105, 215,  88, 138,  53, 
    201, 102, 131, 188, 226,  29, 109,  66, 
    129,  14, 225, 217, 118, 140,  83, 198, 
     57, 126,  97, 237, 194, 106,  21,  64, 
      7, 120, 224, 239, 142, 113, 200,  87, 
    123,  52, 231, 104, 116, 187,  76,  19, 
    195, 218, 133, 149, 234, 202, 117, 125, 
     82, 228,  59, 111,  96,  86, 185,  17, 
    223, 208, 143, 128, 209, 238, 127, 112, 
    230,  89, 108,  51,  80,  95,  56, 183, 
    148, 147, 204, 203, 132, 134, 233, 232, 
    119, 110,  81,  78,  49,  50,  93, 186

};

static uint16_t bright[PinCount] = {0};//records the number of led for each row to adjust Compensation

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
    uint16_t pwm = Period-(count<<2);
    pwm= pwm - (pwm>>Brightness);
    bright[y] = pwm;
    //PRINT("birght:[%d] is : %d\r\n",y,bright[y]);
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
