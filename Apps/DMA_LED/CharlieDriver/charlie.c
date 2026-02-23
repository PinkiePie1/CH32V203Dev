#include "charlie.h"

//初始化DMA，激发DMA的定时器和GPIO外设
void LED_InitPeri(void)
{

}

//设置显示内容缓冲区（不需要这个函数，可以直接把缓冲区写在本文件内，然后外部调用setpixel就行了）
void LED_SetBuffer(uint8_t * buffer[])
{
    return;
}

//点亮或熄灭某个LED
void LED_SetPixel(uint8_t x, uint8_t y, uint8_t color)
{

}

//开启显示，开启timer从而触发DMA驱动GPIO引脚进而驱动查理复用LED点阵
void LED_Show(void)
{

}

//关闭显示
void LED_TurnOff(void)
{

}