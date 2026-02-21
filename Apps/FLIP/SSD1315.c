#include "SSD1315.h"

uint8_t err=0;
//初始化输入
void SoftI2CInit()
{
	GPIO_InitTypeDef GPIO_InitStructure={0};
	I2C_InitTypeDef I2C_InitTSturcture={0};

	RCC_APB2PeriphClockCmd( RCC_APB2Periph_GPIOB | RCC_APB2Periph_AFIO, ENABLE );
	GPIO_PinRemapConfig(GPIO_Remap_I2C1, ENABLE);
	RCC_APB1PeriphClockCmd( RCC_APB1Periph_I2C1, ENABLE );

	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_8;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_OD;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init( GPIOB, &GPIO_InitStructure );

	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_OD;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init( GPIOB, &GPIO_InitStructure );

	I2C_InitTSturcture.I2C_ClockSpeed = 400000;
	I2C_InitTSturcture.I2C_Mode = I2C_Mode_I2C;
	I2C_InitTSturcture.I2C_DutyCycle = I2C_DutyCycle_16_9;
	I2C_InitTSturcture.I2C_OwnAddress1 = 0;
	I2C_InitTSturcture.I2C_Ack = I2C_Ack_Enable;
	I2C_InitTSturcture.I2C_AcknowledgedAddress = I2C_AcknowledgedAddress_7bit;
    I2C_Init( I2C1, &I2C_InitTSturcture );

	I2C_Cmd( I2C1, ENABLE );

	return;

}

void I2CStart()
{
	I2C_GenerateSTART( I2C1, ENABLE );
}

void I2CStop()
{
    I2C_GenerateSTOP( I2C1, ENABLE );
}


uint8_t I2C_Write(uint8_t dat)
{	
	I2C_SendData(I2C1,dat);
}

uint8_t I2C_Read(uint8_t ack)
{
	I2C_ReceiveData(I2C1);
}

//issue a command to the OLED
void OLED_CMD(uint8_t cmd)
{
	I2CStart();
	while( !I2C_CheckEvent( I2C1, I2C_EVENT_MASTER_MODE_SELECT ) );
	I2C_Write(0x78);//SSD1315 address
	while( I2C_GetFlagStatus( I2C1, I2C_FLAG_TXE ) ==  RESET );
	I2C_CheckEvent( I2C1, I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED );
	I2C_Write(0x80);//control bit, indicating the next data is a command.
	while( I2C_GetFlagStatus( I2C1, I2C_FLAG_TXE ) ==  RESET );
	I2C_Write(cmd);
	while( !I2C_CheckEvent( I2C1, I2C_EVENT_MASTER_BYTE_TRANSMITTED ) );
	I2CStop();
}

void OLED_DAT(uint8_t dat)
{
	I2CStart();
	while( !I2C_CheckEvent( I2C1, I2C_EVENT_MASTER_MODE_SELECT ) );
	I2C_Write(0x78);//SSD1315 address
	while( I2C_GetFlagStatus( I2C1, I2C_FLAG_TXE ) ==  RESET );
	I2C_CheckEvent( I2C1, I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED );
	I2C_Write(0xC0);//control bit, indicating the next data is a data
	while( I2C_GetFlagStatus( I2C1, I2C_FLAG_TXE ) ==  RESET );
	I2C_Write(dat);
	while( !I2C_CheckEvent( I2C1, I2C_EVENT_MASTER_BYTE_TRANSMITTED ) );
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
		for(uint16_t j = 0; j < 128; j++){
			OLED_DAT(0x00);
		}
	}	
}

void OLED_Init(void)
{
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
		while( !I2C_CheckEvent( I2C1, I2C_EVENT_MASTER_MODE_SELECT ) );
		I2C_Write(0x78);//SSD1315 address
		while( !I2C_CheckEvent( I2C1, I2C_EVENT_MASTER_BYTE_TRANSMITTED ) );
		I2C_Write(0x40);//control bit, indicating the next ton of data is going into ram.
		while( !I2C_CheckEvent( I2C1, I2C_EVENT_MASTER_BYTE_TRANSMITTED ) );
		for(uint16_t j = 0; j < 128; j++){
			while( !I2C_CheckEvent( I2C1, I2C_EVENT_MASTER_BYTE_TRANSMITTED ) );
			I2C_Write(data[i*128+j]);

		}
		while( !I2C_CheckEvent( I2C1, I2C_EVENT_MASTER_BYTE_TRANSMITTED ) );
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
		/*
		for(uint16_t j = 0; j < 16; j++){
			OLED_DAT(data[i*16+j]);
		}
		*/
		I2CStart();
		while( !I2C_CheckEvent( I2C1, I2C_EVENT_MASTER_MODE_SELECT ) );

		I2C_Write(0x78);//SSD1315 address
		while( !I2C_CheckEvent( I2C1, I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED ) );

		I2C_Write(0x40);//control bit, indicating the next ton of data is going into ram.
		while( !I2C_CheckEvent( I2C1, I2C_EVENT_MASTER_BYTE_TRANSMITTED ) );
		I2C_CheckEvent( I2C1, I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED );

		for(uint16_t j = 0; j < 16; j++){
			while( I2C_GetFlagStatus( I2C1, I2C_FLAG_TXE ) ==  RESET );			
			I2C_Write(data[i*16+j]);

		
		}
		while( !I2C_CheckEvent( I2C1, I2C_EVENT_MASTER_BYTE_TRANSMITTED ) );
		I2CStop();

	}	

}