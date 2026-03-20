#include "LIS2DWHXY.h"

#define I2CPort GPIOA
#define SDA_PIN GPIO_Pin_3
#define SCL_PIN GPIO_Pin_4

#define ACK 1
#define NOACK 0

//#define SDA_LOW I2CPort->BCR |= SDA_PIN;GPIOA->CFGLR&=~(0xF<<4*7);GPIOA->CFGLR|=(0x3<<4*7);
//#define SDA_HIGH I2CPort->BSHR |= SDA_PIN;GPIOA->CFGLR&=~(0xF<<4*7);GPIOA->CFGLR|=(0x8<<4*7);
#define SDA_LOW I2CPort->BCR |= SDA_PIN
#define SDA_HIGH I2CPort->BSHR |= SDA_PIN
#define SCL_LOW (I2CPort->BCR |= SCL_PIN)
#define SCL_HIGH (I2CPort->BSHR |= SCL_PIN)
#define GET_SDA (I2CPort->INDR & SDA_PIN)
#define I2C_DelayUS(x) Delay_Us(2)

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
static void LIS2_Read(uint8_t reg, uint8_t * buffer, uint8_t length)
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

uint8_t LIS2DWHXY_INTERRUT(void)
{
    uint8_t flag = 0;
    
    LIS2_Write(0x22,0x40);//AOI1 on INT1.
    LIS2_Write(0x30,0x0A);//x and y high enable
    LIS2_Write(0x32,0x18);//threshold is around 1g
    return flag;
}

//初始化SPI1及对应引脚，包括CS脚
uint8_t LIS2DWHXY_Init(void)
{
    uint8_t flag = 0;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    GPIO_InitTypeDef GPIO_InitStructure = {0};

    GPIOA->BSHR = SDA_PIN|SCL_PIN;
    
    GPIO_InitStructure.GPIO_Pin   = SDA_PIN;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_Out_OD; //从机无法接收的时候会强制拉低SCL
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure); 

    GPIO_InitStructure.GPIO_Pin   = SCL_PIN;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_Out_OD; //从机无法接收的时候会强制拉低SCL
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure); 

    SCL_HIGH;
    SDA_HIGH;

    uint8_t buf[10] = {0};
    LIS2_Read(0x0F,buf,1);//read whoami

    if (  buf[0] == 0x11 )
    {
        PRINT("Found LIS2DH!\r\n");
        LIS2_Write(0x20,0x67);//200Hz ODR,normal mode, xyz all enable
        LIS2_Read(0x20,buf,1);
        if(buf[0]!=0x67){flag = 1;}
        LIS2_Write(0x21,0x00);//disable filter
        LIS2_Write(0x22,0x00);//disable interrupt
        LIS2_Write(0x23,0x30);//16G FS
        LIS2_Read(0x23,buf,1);
        if(buf[0]!=0x30){flag = 1;}
        LIS2DWHXY_INTERRUT(); //init interrupt
        
    }
    else
    {
        PRINT("LIS2DH not found, read: %x\r\n",buf[0]);
        flag = 1;
    }

    return flag;

}

void LIS2DWHXY_Deinit(void)
{
    //LIS2_Write(0x24,0x80);//reboot memory content
    LIS2_Write(0x20,0x27);//LIS2DH at 10HzODR.

}

void LIS2DWHXY_Get(int16_t * x, int16_t * y, int16_t * z)
{
    uint8_t buf[10] = {0};
    LIS2_Read(0x28,buf,6);
    //LIS2_Read(0x29,buf+1,1);
    //LIS2_Read(0x2A,buf+2,1);
    //LIS2_Read(0x2B,buf+3,1);
    //LIS2_Read(0x2C,buf+4,1);
    //LIS2_Read(0x2D,buf+5,1);

uint8_t X_H,X_L,Y_H,Y_L,Z_H, Z_L; // Three-axis data (high and low)
X_H=buf[1];
X_L=buf[0];
Y_H=buf[3];
Y_L=buf[2];
Z_H=buf[5];
Z_L=buf[4];
int16_t SL_ACCEL_X,SL_ACCEL_Y,SL_ACCEL_Z ; // Three-axis data
SL_ACCEL_X = (int16_t)((X_H<< 8) | X_L); // Merging data
SL_ACCEL_Y = (int16_t)((Y_H<< 8) | Y_L); // Forcing data type conversion
SL_ACCEL_Z = (int16_t)((Z_H<< 8) | Z_L); // 16 bit signed integer data
SL_ACCEL_X = SL_ACCEL_X>>6;
SL_ACCEL_Y = SL_ACCEL_Y>>6;
SL_ACCEL_Z = SL_ACCEL_Z>>6;

SL_ACCEL_X-=83;
SL_ACCEL_Y-=83;

*x=SL_ACCEL_X;
*y=SL_ACCEL_Y;
*z=SL_ACCEL_Z;
}
