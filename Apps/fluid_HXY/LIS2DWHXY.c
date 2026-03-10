#include "LIS2DWHXY.h"

#define I2CPort GPIOA
#define SDA_PIN GPIO_Pin_7
#define SCL_PIN GPIO_Pin_5

#define ACK 1
#define NOACK 0

 #define SDA_LOW I2CPort->BCR |= SDA_PIN;GPIOA->CFGLR&=~(0xF<<4*7);GPIOA->CFGLR|=(0x3<<4*7);
 #define SDA_HIGH I2CPort->BSHR |= SDA_PIN;GPIOA->CFGLR&=~(0xF<<4*7);GPIOA->CFGLR|=(0x8<<4*7);
//#define SDA_LOW I2CPort->BCR |= SDA_PIN
//#define SDA_HIGH I2CPort->BSHR |= SDA_PIN
#define SCL_LOW (I2CPort->BCR |= SCL_PIN)
#define SCL_HIGH (I2CPort->BSHR |= SCL_PIN)
#define GET_SDA (I2CPort->INDR & SDA_PIN)
#define I2C_DelayUS(x) Delay_Us(1)

static void I2CStart(void)
{
    I2C_DelayUS(5);
    SCL_HIGH;
    I2C_DelayUS(5);
    SDA_HIGH;
    I2C_DelayUS(5);
    SDA_LOW;
    I2C_DelayUS(5);
    SCL_LOW;
    I2C_DelayUS(5);

}

static void I2CStop(void)
{
    I2C_DelayUS(5); 
    SCL_LOW;
    I2C_DelayUS(5); 
    SDA_LOW;
    I2C_DelayUS(5);    
    SCL_HIGH;
    I2C_DelayUS(5);
    SDA_HIGH;
    I2C_DelayUS(5); 
}

static uint8_t I2CWrite(uint8_t dat)
{
    for(uint8_t i = 0; i < 8; i++)
    {
 
        if (0x80 & dat){
            SDA_HIGH;
        } else {
            SDA_LOW;
        }
        SCL_HIGH;
        I2C_DelayUS(5);
        SCL_LOW;
        I2C_DelayUS(5);
        dat = (dat << 1);
    }
    SDA_HIGH;
    I2C_DelayUS(5);
    SCL_HIGH;
    I2C_DelayUS(5);
    if(GET_SDA == 0){
        SCL_LOW;
        return ACK;
    } else {
        SCL_LOW;
        return NOACK;
    }

}

static uint8_t I2CRead(uint8_t ack)
{
    uint8_t dat = 0;
    SDA_HIGH;
    for(uint8_t i = 0; i < 8; i++)
    {
        dat = (dat << 1);
        I2C_DelayUS(5);
        SCL_HIGH;
        I2C_DelayUS(5);
        if (GET_SDA == 0){

        } else {
            dat |= 0x01;
        }
        SCL_LOW;
    I2C_DelayUS(5);    
    }
    if(ack == ACK){
        SDA_LOW;
    } else {
        SDA_HIGH;
    }
    I2C_DelayUS(5);
    SCL_HIGH;
    I2C_DelayUS(5);
    SCL_LOW;
    I2C_DelayUS(5);
    SDA_HIGH;
    return dat;

}


//写LIS2,0x32 addr
static void LIS2_Write(uint8_t reg, uint16_t  dat)
{
    I2CStart();
    I2CWrite(0x32);
    I2CWrite(reg);
    I2CWrite(dat);
    I2CStop();
}

//读LIS2,0x33 addr
static void LIS2_Read(uint8_t reg, uint16_t * buffer, uint8_t length)
{
    I2CStart();
    I2CWrite(0x32);
    I2CWrite(0x80|reg);
    I2CStart();//repeated start
    I2CWrite(0x33);
    for(int8_t i = 0; i < length-1;i++)
    {
        buffer[i] = I2CRead(ACK);
    }
    buffer[length-1] = I2CRead(NOACK);
    I2CStop();
    
    
}
//初始化SPI1及对应引脚，包括CS脚
void LIS2DWHXY_Init(void)
{
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    GPIO_InitTypeDef GPIO_InitStructure = {0};

    GPIOA->BSHR = GPIO_Pin_5|GPIO_Pin_4|GPIO_Pin_6|GPIO_Pin_7;
    
    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_5|GPIO_Pin_4|GPIO_Pin_6;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_Out_PP; //从机无法接收的时候会强制拉低SCL
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure); //PA5 SCK

    SCL_HIGH;
    SDA_HIGH;
    //GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_7;
    //GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_Out_OD;
    //GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    //GPIO_Init(GPIOA, &GPIO_InitStructure); // PA7 SDA


    uint16_t buf[10] = {0};
    LIS2_Read(0x0F,buf,1);//read whoami

    if (  buf[0] == 0x33 )
    {
        PRINT("Found LIS2DH!\r\n");
        LIS2_Write(0x23,0x30);//16G FS
        LIS2_Write(0x20,0x67);//200Hz ODR,normal mode, xyz all enable
        LIS2_Write(0x1E,0x90);//enable pullup
        LIS2_Write(0x25,0x02);//INT active low
        LIS2_Write(0x1F,0x00);//disable adc and temperature

    }
    else
    {
        PRINT("LIS2DH not found, read: %x\r\n",buf[0]);
    }

    return;

}

void LIS2DWHXY_Deinit(void)
{
    //LIS2_Write(0x24,0x80);//reboot memory content
    LIS2_Write(0x20,0x07);//powerdown LIS2DH

}

void LIS2DWHXY_Get(int16_t * x, int16_t * y, int16_t * z)
{
    uint16_t buf[10] = {0};
    LIS2_Read(0x28,buf,6);
    //LIS2_Read(0x28+1,buf+1,1);
    //LIS2_Read(0x28+2,buf+2,1);
    //LIS2_Read(0x28+3,buf+3,1);
    //LIS2_Read(0x28+4,buf+4,1);
    //LIS2_Read(0x28+5,buf+5,1);

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
