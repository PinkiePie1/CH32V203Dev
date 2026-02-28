#include "LIS2DH.h"

#define CS_LOW GPIO_ResetBits(GPIOA,GPIO_Pin_4)
#define CS_HIGH GPIO_SetBits(GPIOA,GPIO_Pin_4)
#define SDA_LOW GPIO_ResetBits(GPIOA,GPIO_Pin_7)
#define SDA_HIGH GPIO_SetBits(GPIOA,GPIO_Pin_7)
#define SCL_LOW GPIO_ResetBits(GPIOA,GPIO_Pin_5)
#define SCL_HIGH GPIO_SetBits(GPIOA,GPIO_Pin_5)
#define Read_SDI (GPIO_ReadInputDataBit(GPIOA，GPIO_Pin_6) == BIT_SET ? 1 : 0)

static uint8_t SPI_readwrite(uint8_t data)
{

    while(SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_TXE) == RESET);
    SPI_I2S_SendData( SPI1, data ); //单个写入
    while(SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_RXNE) == RESET);
    return SPI_I2S_ReceiveData(SPI1);


}

//写LIS2DH.
static void LIS2DH_Write(uint8_t reg, uint16_t  dat)
{
    CS_LOW;
    SPI_readwrite(reg);
    SPI_readwrite(dat);
    CS_HIGH;


}

//读LIS2DH
static void LIS2DH_Read(uint8_t reg, uint16_t * buffer, uint8_t length)
{
    CS_LOW;
    SPI_readwrite(reg | 0xC0);
    for(uint8_t i = 0; i < length; i++)
    {   
        buffer[i] = SPI_readwrite(0xAA); //多读取
    }
    CS_HIGH;

}
//初始化SPI1及对应引脚，包括CS脚
void LIS2DH_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};
    SPI_InitTypeDef  SPI_InitStructure  = {0};

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_SPI1, ENABLE);

    CS_HIGH;
    SCL_HIGH;
    SDA_LOW;

    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_4;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure); // PA4 CS

    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_5;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure); //PA5 SCK

    GPIO_InitStructure.GPIO_Pin  = GPIO_Pin_6;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &GPIO_InitStructure); // PA6 MISO

    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_7;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure); // PA7 MOSI

    SPI_InitStructure.SPI_Direction = SPI_Direction_2Lines_FullDuplex;
    SPI_InitStructure.SPI_Mode      = SPI_Mode_Master;
    SPI_InitStructure.SPI_DataSize  = SPI_DataSize_8b;
    SPI_InitStructure.SPI_CPOL      = SPI_CPOL_High;
    SPI_InitStructure.SPI_CPHA      = SPI_CPHA_2Edge; // mode 11
    SPI_InitStructure.SPI_NSS       = SPI_NSS_Soft;

    SPI_InitStructure.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_8;

    SPI_InitStructure.SPI_FirstBit = SPI_FirstBit_MSB;

    SPI_Init(SPI1, &SPI_InitStructure);

    SPI_Cmd(SPI1, ENABLE);

    CS_HIGH;
    SCL_HIGH;

    SPI1->HSCR = 0;

    uint16_t buf[10] = {0};
    LIS2DH_Read(0x0F,buf,1);//read whoami

    if (  buf[0] == 0x33 )
    {
        PRINT("Found LIS2DH!\r\n");

        LIS2DH_Write(0x20,0x37);
        LIS2DH_Write(0x23,0x30);
        LIS2DH_Write(0x20,0x67);
    }
    else
    {
        PRINT("LIS2DH not found, read: %x\r\n",buf[0]);
    }

    return;

}

void LIS2DH_Get(int16_t * x, int16_t * y, int16_t * z)
{
    uint16_t buf[10] = {0};
    LIS2DH_Read(0x28,buf,6);

    int16_t reg;
    reg = (int16_t)((buf[1] << 8)| buf[0]);
    reg >>=6;
    *x = reg;

    reg = (int16_t)((buf[3] << 8)| buf[2]);
    reg >>=6;
    *y = reg;

    reg = (int16_t)((buf[5] << 8)| buf[4]);
    reg >>=6;
    *z = reg;

}
