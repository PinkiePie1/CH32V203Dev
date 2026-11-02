#include "charlie.h"

uint8_t clockUpdate = 1;
uint8_t clockSpeed = FAST;

const uint16_t LUT[240] = {
     97, 143, 128,  83, 140,  64,  79, 109,
     66, 157, 114,   9, 150,  30,   1,  31,
    141,  98,  82, 125,  68, 139, 110,  81,
    154,  69,   6, 105,  39, 152,  15,   0,
     53,  80,  95,  67, 124, 113, 142, 155,
     84,   3,  60,  36, 107,  24, 151,  10,
     78,  49,  65,  94, 112, 127, 159, 144,
      4,  75,  33,  62,  21, 106, 175, 160,
    215,  63,  48, 111,  96, 158, 129,   8,
    135,  34,  77,  18,  61, 172, 115, 161,
     73, 214, 108,  51, 156,  99,   7, 120,
     38, 137,  19,  76, 169,  70, 187, 116,
    199, 118, 217, 153,  54,   5,  90,  37,
    122,  23, 136, 170,  85, 184,  71, 119,
    117, 202, 163, 220,   2,  45,  35,  92,
     22, 121, 174, 145, 185,  86, 229,  74,
    232, 162, 205,  13, 210,  32,  47,  20,
     91, 173, 130, 189, 146, 230,  89,  72,
    164, 235,  12, 195,  43, 212,  17,  46,
    171, 100, 188, 131, 234, 149, 200,  87,
    190,  14, 225,  42, 197,  28, 211, 168,
     55, 186, 101, 233, 134, 204, 147,  88,
     11, 180,  44, 227,  27, 196, 178, 221,
    183,  56, 231, 104, 203, 132, 219, 148,
    165,  41, 182,  29, 226, 177, 206, 193,
    222, 228,  59, 201, 102, 218, 133, 138,
     40, 167,  26, 181, 179, 236, 192, 207,
    239, 224, 198,  57, 216, 103,  52, 123,
     16,  25, 166, 176, 191, 194, 237, 238,
    209, 208, 223, 213,  58,  50,  93, 126

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

    TIM_SetCompare1(TIM1,300); // cfgl
    TIM_SetCompare2(TIM1,300); // cfgh //these two needs to be higher or ghosting appears when switching clock signals
    TIM_SetCompare3(TIM1,Period); //odron
    TIM_SetCompare4(TIM1,0); // brightness

    TIM_DMACmd(TIM1, TIM_DMA_Update | TIM_DMA_CC1 | TIM_DMA_CC2 | TIM_DMA_CC3 |TIM_DMA_CC4, ENABLE);
    // update-> channel 5, odr off
    // CC1 -> channel 2
    // CC2 -> channel 3
    // CC3 -> channel 6
    // CC4 -> channel 4

    //enable support for sleep mode. 
    TIM_ITConfig(TIM1,TIM_IT_CC1 | TIM_IT_CC2 | TIM_IT_CC3 | TIM_IT_CC4 | TIM_IT_Update,ENABLE);
	NVIC_EnableIRQ(TIM1_CC_IRQn);
    NVIC_EnableIRQ(TIM1_UP_IRQn);



}

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
    if(mode == 0){
        u8 count = 1;
        for (u32 comp = 0x3; comp; comp<<=4)
        {
            count = comp & gpioCFGH[y]?count+1:count;
            count = comp & gpioCFGL[y]?count+1:count;
        }
        uint16_t pwm = Period-(count);
        pwm = pwm - (pwm>>Brightness);
        bright[y] = pwm;
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
    //has to update clock synchronously. otherwise brightness in rows will change
     TIM_ClearITPendingBit(TIM1, TIM_IT_Update);

   
    
}




void TIM1_CC_IRQHandler(void)
{
    if(TIM_GetITStatus(TIM1,TIM_IT_CC4) == SET){
        if(clockUpdate)
        {
            uint32_t dat = (RCC->CFGR0 & ~RCC_HPRE);
            dat |= clockSpeed == FAST ? RCC_HPRE_DIV1 : RCC_HPRE_DIV16; 
            RCC->CFGR0 = dat;
            clockUpdate = 0;
        }
    }
    TIM_ClearITPendingBit(TIM1,TIM_IT_CC1|TIM_IT_CC2|TIM_IT_CC3|TIM_IT_CC4);
}
