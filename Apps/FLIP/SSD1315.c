#include "SSD1315.h"

uint8_t err=0;
//初始化输入
void SoftI2CInit()
{
	return;

}

void SDA_IPU(void)
{
    GPIO_InitTypeDef  GPIO_InitStructure;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1 | RCC_APB2Periph_GPIOA, ENABLE);
    GPIO_InitStructure.GPIO_Pin = SDA_PIN;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(GPIOA, &GPIO_InitStructure);
}

void SDA_OUTLOW(void)
{
    GPIO_InitTypeDef  GPIO_InitStructure;
    GPIO_ResetBits(GPIOA,SDA_PIN);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1 | RCC_APB2Periph_GPIOA, ENABLE);
    GPIO_InitStructure.GPIO_Pin = SDA_PIN;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_OD;
    GPIO_Init(GPIOA, &GPIO_InitStructure);
}


void I2CStart()
{
	SCL_HIGH;
	I2CDelayUs(1);
	SDA_HIGH;
	I2CDelayUs(1);
	SDA_LOW;
	I2CDelayUs(1);
	SCL_LOW;
	
}

void I2CStop()
{
	SDA_LOW;
	I2CDelayUs(1);
	SCL_HIGH;
	I2CDelayUs(1);
	SDA_HIGH;
	
}


uint8_t I2C_Write(uint8_t dat)
{
	uint8_t data = dat;
	for (uint8_t i = 0; i<8;i++)
	{
		
		if(data&0x80){SDA_HIGH;}
		else{SDA_LOW;}
		SCL_HIGH;
		I2CDelayUs(1);
		SCL_LOW;
		data = (data << 1);
	}
	SDA_HIGH;//输入读取
	I2CDelayUs(1);
	SCL_HIGH;
	I2CDelayUs(1);
	if(READ_SDA()==0)
	{
		SCL_LOW;
		return ACK;
	} 
	else 
	{	
		SCL_LOW;
		return NACK;
	}
	
}

uint8_t I2C_Read(uint8_t ack)
{
	uint8_t data = 0;
	SDA_HIGH;
	for (uint8_t i = 0; i < 8 ; i++ )
	{
		data = (data << 1);
		SCL_HIGH;
		if(READ_SDA() == 0){;}
		else{data |= 0x01;}
		SCL_LOW;
		I2CDelayUs(1);
		
	}

	if(ack)
	{
		SDA_LOW;//ACK
	} 
	else 
	{
		SDA_HIGH;//NACK
	}
	SCL_HIGH;
	I2CDelayUs(1);
	SCL_LOW;
	SDA_HIGH;
	return data;

}

//issue a command to the OLED
void OLED_CMD(uint8_t cmd)
{
	I2CStart();
	I2C_Write(0x78);//SSD1315 address
	I2C_Write(0x80);//control bit, indicating the next data is a command.
	I2C_Write(cmd);
	I2CStop();
}

void OLED_DAT(uint8_t dat)
{
	I2CStart();
	I2C_Write(0x78);//SSD1315 address
	I2C_Write(0xC0);//control bit, indicating the next data is a data
	I2C_Write(dat);
	I2CStop();
}


void OLED_Clean(void)
{
	for(uint8_t i = 0 ; i < 8 ; i++ )
	{
		OLED_CMD(0xB0+i);//set page to ith page
		OLED_CMD(0x00); //set start position to be 0
		OLED_CMD(0x10); //set start position to be 0
		//burst write
		I2CStart();
		I2C_Write(0x78);//SSD1315 address
		I2C_Write(0x40);//control bit, indicating the next ton of data is going into ram.
		for(uint16_t j = 0; j < 128; j++){
			I2C_Write(0x00);
		}
		I2CStop();

	}	
}

void OLED_Init(void)
{
	SoftI2CInit();
	OLED_CMD( 0xAE );
	OLED_CMD( 0x00 );
	OLED_CMD( 0x10 );
	OLED_CMD( 0x40 );
	OLED_CMD( 0x81 );
	OLED_CMD( 0xA1 );
	OLED_CMD( 0xC8 );
	OLED_CMD( 0xA6 );
	OLED_CMD( 0xA8 );
	OLED_CMD( 0x3F );
	OLED_CMD( 0xD3 );
	OLED_CMD( 0x00 );
	OLED_CMD( 0xD5 );
	OLED_CMD( 0x80 );
	OLED_CMD( 0xD9 );
	OLED_CMD( 0xF1 );
	OLED_CMD( 0xDA );
	OLED_CMD( 0x12 );
	OLED_CMD( 0x40 );
	OLED_CMD( 0x20 );
	OLED_CMD( 0x02 );
	OLED_CMD( 0x8D );
	OLED_CMD( 0x14 );
	OLED_CMD( 0xA4 );
	OLED_CMD( 0xA6 );
	OLED_CMD( 0xAF );
	OLED_Clean();

}

void OLED_TurnOn(void)
{
	OLED_CMD( 0x8D );
	OLED_CMD( 0x14 );
	OLED_CMD( 0xAF );

}

void OLED_TurnOff(void)
{
	OLED_CMD( 0x8D );
	OLED_CMD( 0x10 );
	OLED_CMD( 0xAF );
	OLED_CMD( 0xAE );

}

//write to RAM,128x64,write all of them.
void OLED_GDDRAM(uint8_t * data)
{
	for(uint8_t i = 0 ; i < 8 ; i++ )
	{
		OLED_CMD(0xB0+i);//set page to ith page
		OLED_CMD(0x00); //set start position to be 0
		OLED_CMD(0x10); //set start position to be 0
		//burst write
		I2CStart();
		I2C_Write(0x78);//SSD1315 address
		I2C_Write(0x40);//control bit, indicating the next ton of data is going into ram.
		for(uint16_t j = 0; j < 128; j++){
			I2C_Write(data[i*128+j]);

		}
		I2CStop();

	}	

}

void OLED_16(uint8_t * data)
{
	for(uint8_t i = 0 ; i < 2 ; i++ )
	{
		OLED_CMD(0xB0+i+2);//set page to ith page
		OLED_CMD(0x0F); //set start position to be 0
		OLED_CMD(0x12); //set start position to be 0
		//burst write
		I2CStart();
		I2C_Write(0x78);//SSD1315 address
		I2C_Write(0x40);//control bit, indicating the next ton of data is going into ram.
		for(uint16_t j = 0; j < 16; j++){
			I2C_Write(data[i*16+j]);

		}
		I2CStop();

	}	

}