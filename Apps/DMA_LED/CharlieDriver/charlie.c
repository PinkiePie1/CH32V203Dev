#include "charlie.h"

uint32_t gpioCFGL[16] = 
{0X00000003,0X00000030,0X00000300,0X00003000,
 0X00030000,0X00300000,0X03000000,0X30000000,
 0X00000000,0X00000000,0X00000000,0X00000000,
 0X00000000,0X00000000,0X00000000,0X00000000  
};
uint32_t gpioCFGH[16] =
{0X00000000,0X00000000,0X00000000,0X00000000,
 0X00000000,0X00000000,0X00000000,0X00000000,
 0X00000003,0X00000030,0X00000300,0X00003000,
 0X00030000,0X00300000,0X03000000,0X30000000, 
};


//初始化DMA，激发DMA的定时器和GPIO外设
void LED_InitPeri(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_All;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &GPIO_InitStructure);
}

//设置显示内容缓冲区（不需要这个函数，可以直接把缓冲区写在本文件内，然后外部调用setpixel就行了）
void LED_SetBuffer(uint8_t * buffer[])
{
    return;
}

//点亮或熄灭某个LED
void LED_SetPixel(uint16_t num, uint8_t color)
{
    u16 x = num % (PinCount - 1);
    u16 y = num / (PinCount - 1);
    x = x >= y ? x + 1 : x;
    
    if (color == LEDON){
        if (x >= 8)
        {
            gpioCFGH[y] |= 0x3 << ((x-8)*4);
        }
        else
        {
            gpioCFGL[y] |= 0x3 << (x*4);
        }
    }
    else
    {
        if (x >= 8)
        {
            gpioCFGH[y] &= ~(0x3 << ((x-8)*4));
        }
        else
        {
            gpioCFGL[y] &= ~(0x3 << (x*4));
        }
    }

}

//开启显示，开启timer从而触发DMA驱动GPIO引脚进而驱动查理复用LED点阵
void LED_Show(void)
{
    for (u8 i = 0; i < 9; i++){
        GPIOB->OUTDR = 1 << i;
        GPIOB->CFGHR = gpioCFGH[i];
        GPIOB->CFGLR = gpioCFGL[i];
        Delay_Us(onTime);
        GPIOB->OUTDR = 0 ;
        Delay_Us(offTime);
    }
}

//关闭显示
void LED_TurnOff(void)
{

}