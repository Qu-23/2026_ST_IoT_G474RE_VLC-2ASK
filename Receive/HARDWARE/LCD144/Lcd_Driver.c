
#include "main.h"
#include "Lcd_Driver.h"

#include "spi.h"

// LCD device structure
_lcd_dev lcddev;

//********************************************************************************

// LCD device structure definition
// Stores LCD width, height, ID, direction, and GRAM command settings

//********************************************************************************

// LCD Hardware Pin Definitions - SPI2 Interface
// PB15: SDA/MOSI   PB13: SCL/SCK   PB12: CS
// PB14: RST        PC6: DC          PC7: BLK
// Note: These pins are configured via SPI2 peripheral and GPIO

/******************************************************************************
�ӿڶ��壬����ݽ����޸Ĳ��޸����?IO��ʼ��LCD_GPIO_Init()

//  ----------------------------------------------------------------
// Software SPI pin definitions (legacy, replaced by hardware SPI2)
// #define LCD_SDI        	// PB15 -- MOSI (SPI2)
// #define LCD_SCL        	// PB13 -- SCK  (SPI2)
//	#define LCD_CS        	// PB12 -- CS   (GPIO)
// #define LCD_SDO     		// PB14 -- MISO (SPI2) / RST
// #define LCD_DC         	// PC6  -- DC   (GPIO)
// #define LCD_BLK         // PC7  -- BLK  (GPIO)
// ----------------------------------------------------------------

*******************************************************************************/

/**************************************************************************************

��������: void LCD_GPIO_Init(void)

��������: Һ��IO��ʼ������
��    ��: 
��    ��: 

// LCD GPIO initialization: configure CS, DC, BLK, RST pins

**************************************************************************************/

void LCD_GPIO_Init(void)
{

    GPIO_InitTypeDef GPIO_Initure;
	  
	  __HAL_RCC_GPIOA_CLK_ENABLE();					          // Enable GPIOA clock
    __HAL_RCC_GPIOB_CLK_ENABLE();					          // Enable GPIOB clock
	  __HAL_RCC_GPIOC_CLK_ENABLE();					          // Enable GPIOC clock
	
    GPIO_Initure.Pin=GPIO_PIN_6 |GPIO_PIN_7;	      // PC6--D/C, PC7--BLK
    GPIO_Initure.Mode=GPIO_MODE_OUTPUT_PP;  		    // Push-pull output
    GPIO_Initure.Pull=GPIO_PULLUP;         			    
    GPIO_Initure.Speed=GPIO_SPEED_FREQ_VERY_HIGH;  	
    HAL_GPIO_Init(GPIOC,&GPIO_Initure);     	     	

    GPIO_Initure.Pin= GPIO_PIN_12|GPIO_PIN_14;	    // PB12--CS, PB14--RST
    GPIO_Initure.Mode=GPIO_MODE_OUTPUT_PP;  		    // Push-pull output
    GPIO_Initure.Pull=GPIO_PULLUP;         			    
    GPIO_Initure.Speed=GPIO_SPEED_FREQ_VERY_HIGH;  	
    HAL_GPIO_Init(GPIOB,&GPIO_Initure);     	     	
	  
    HAL_GPIO_WritePin(GPIOC,GPIO_PIN_6,GPIO_PIN_SET);	  // PC6 D/C = 1
	  HAL_GPIO_WritePin(GPIOC,GPIO_PIN_7,GPIO_PIN_SET);	  // PC7 BLK = 1
	  HAL_GPIO_WritePin(GPIOB,GPIO_PIN_12,GPIO_PIN_SET);	// PB12 CS = 1 (deselect)
	  HAL_GPIO_WritePin(GPIOB,GPIO_PIN_14,GPIO_PIN_SET);	// PB14 RST = 1
	
		LCD_BLK_CLR;
		LCD_BLK_SET;
      
}

/************************************************************************************************/
// SPI2 transmit/receive one byte
// Sends TxData and returns received data via HAL_SPI_TransmitReceive

// STM32G474 HAL Library Version
// Source: DevEBox (mcudev.taobao.com)

/************************************************************************************************/

uint8_t LCD_SPI2_ReadWriteByte(uint8_t TxData)
{
    uint8_t Rxdata;
    HAL_SPI_TransmitReceive(&hspi2,&TxData,&Rxdata,1, 1000);       
 	return Rxdata;          		    
}

//********************************************************************************/
// LCD Hardware Reset via RST pin (PB14)
// Pull RST low, delay, then pull RST high
//*******************************************************************/
void LCD_HardwareRest(void)
{					   
	LCD_RST_Clr;          // Pull RST low to reset LCD
	LCD_Delay_ms(50);      // delay 50 ms 
	LCD_RST_Set;          // Pull RST high to release reset
	LCD_Delay_ms(30);      // delay 30 ms 
}	

/////**************************************************************************************
//// Legacy: Software SPI bit-bang implementation (replaced by hardware SPI2)
//// Function: void  LCD_SPI2_ReadWriteByte(uint8_t Data)
//// Sends one byte via software SPI (bit-bang), MSB first
////**************************************************************************************/

////void  LCD_SPI2_ReadWriteByte(uint8_t Data)
////{
////  	unsigned char i=0;
////		for(i=8;i>0;i--)
////		{
////			if(Data&0x80)	
////				LCD_SDA_SET; // Set MOSI high
////				else LCD_SDA_CLR;
////			 
////				LCD_SCL_CLR;       // SCL low
////				LCD_SCL_SET;       // SCL high (rising edge)
////				Data<<=1; 
////		}
////}

/**************************************************************************************

��������: void  LCD_SPI2_ReadWriteByte(uint8_t Data)

��������: ��Һ����дһ��8λָ��
��    ��: 
��    ��: 

// Send command byte to LCD (DC=0)

**************************************************************************************/

void Lcd_WriteIndex(uint8_t Index)
{
   // CS=0, DC=0 -> send command byte
   LCD_CS_CLR;
   LCD_DC_CLR;
	 LCD_SPI2_ReadWriteByte(Index);
   LCD_CS_SET;
}

/**************************************************************************************

��������: void Lcd_WriteData(uint8_t Data)

��������: ��Һ����дһ��8λ����
��    ��: 
��    ��: 

// Send data byte to LCD (DC=1)

**************************************************************************************/

void Lcd_WriteData(uint8_t Data)
{
   LCD_CS_CLR;
   LCD_DC_SET;
   LCD_SPI2_ReadWriteByte(Data);
   LCD_CS_SET; 
}

/**************************************************************************************

��������: void LCD_WriteData_16Bit(uint16_t Data)

��������: ��Һ����дһ��16λ����
��    ��: 
��    ��: 

// Send 16-bit data to LCD (high byte first)

**************************************************************************************/

void LCD_WriteData_16Bit(uint16_t Data)
{
   LCD_CS_CLR;
   LCD_DC_SET;
	 LCD_SPI2_ReadWriteByte(Data>>8); 	// Send high byte first
	 LCD_SPI2_ReadWriteByte(Data); 			// Send low byte
   LCD_CS_SET; 
}

/**************************************************************************************

��������: void Lcd_WriteReg(uint8_t Index,uint8_t Data)

��������: дҺ�����Ĵ���
��    ��: 
��    ��: 

// Write LCD register: send index then data

**************************************************************************************/

void Lcd_WriteReg(uint8_t Index,uint8_t Data)
{
	Lcd_WriteIndex(Index);
  Lcd_WriteData(Data);
}

/**************************************************************************************

��������: void Lcd_Reset(void)

��������: Ӳ��IO����Һ������λ�����ʹ�õ������ֿ�汾����Ļ���˺�����Ч
��    ��: 
��    ��: 

// Hardware reset via RST pin: pull low, delay, pull high

**************************************************************************************/

void Lcd_Reset(void)
{
	LCD_RST_Clr;        // Hardware reset: pull RST low
	LCD_Delay_ms(300);
	LCD_RST_Set;        // Hardware reset: pull RST high
	LCD_Delay_ms(100);
}

/**************************************************************************************

��������: void Lcd_Init(void)

��������: 1.44�� Һ������ʼ��������Һ�����������ǣ�ST7735R.
��    ��: 
��    ��: 

// ST7735R LCD initialization sequence
// Includes hardware reset, software reset, frame rate, power, gamma settings

**************************************************************************************/

void Lcd_Init(void)
{	
	LCD_GPIO_Init();
	
	Lcd_Reset();     //Hardware reset before LCD Init
	
	// Software reset for safety
	Lcd_WriteIndex(0x01); //Software Reset
	LCD_Delay_ms(150);
	
	lcddev.width=X_MAX_PIXEL;
  lcddev.height=Y_MAX_PIXEL;

	//LCD Init For 1.44Inch LCD Panel with ST7735R.
	Lcd_WriteIndex(0x11);//Sleep exit 
	
	LCD_Delay_ms (200);
		
	//ST7735R Frame Rate
	Lcd_WriteIndex(0xB1); 
	Lcd_WriteData(0x01); 
	Lcd_WriteData(0x2C); 
	Lcd_WriteData(0x2D); 

	Lcd_WriteIndex(0xB2); 
	Lcd_WriteData(0x01); 
	Lcd_WriteData(0x2C); 
	Lcd_WriteData(0x2D); 

	Lcd_WriteIndex(0xB3); 
	Lcd_WriteData(0x01); 
	Lcd_WriteData(0x2C); 
	Lcd_WriteData(0x2D); 
	Lcd_WriteData(0x01); 
	Lcd_WriteData(0x2C); 
	Lcd_WriteData(0x2D); 
	
	Lcd_WriteIndex(0xB4); //Column inversion 
	Lcd_WriteData(0x07); 
	
	//ST7735R Power Sequence
	Lcd_WriteIndex(0xC0); 
	Lcd_WriteData(0xA2); 
	Lcd_WriteData(0x02); 
	Lcd_WriteData(0x84); 
	Lcd_WriteIndex(0xC1); 
	Lcd_WriteData(0xC5); 

	Lcd_WriteIndex(0xC2); 
	Lcd_WriteData(0x0A); 
	Lcd_WriteData(0x00); 

	Lcd_WriteIndex(0xC3); 
	Lcd_WriteData(0x8A); 
	Lcd_WriteData(0x2A); 
	Lcd_WriteIndex(0xC4); 
	Lcd_WriteData(0x8A); 
	Lcd_WriteData(0xEE); 
	
	Lcd_WriteIndex(0xC5); //VCOM 
	Lcd_WriteData(0x0E); 
	
	Lcd_WriteIndex(0x36); //MX, MY, RGB mode 
	Lcd_WriteData(0xC8); 
	
	//ST7735R Gamma Sequence
	Lcd_WriteIndex(0xe0); 
	Lcd_WriteData(0x0f); 
	Lcd_WriteData(0x1a); 
	Lcd_WriteData(0x0f); 
	Lcd_WriteData(0x18); 
	Lcd_WriteData(0x2f); 
	Lcd_WriteData(0x28); 
	Lcd_WriteData(0x20); 
	Lcd_WriteData(0x22); 
	Lcd_WriteData(0x1f); 
	Lcd_WriteData(0x1b); 
	Lcd_WriteData(0x23); 
	Lcd_WriteData(0x37); 
	Lcd_WriteData(0x00); 	
	Lcd_WriteData(0x07); 
	Lcd_WriteData(0x02); 
	Lcd_WriteData(0x10); 

	Lcd_WriteIndex(0xe1); 
	Lcd_WriteData(0x0f); 
	Lcd_WriteData(0x1b); 
	Lcd_WriteData(0x0f); 
	Lcd_WriteData(0x17); 
	Lcd_WriteData(0x33); 
	Lcd_WriteData(0x2c); 
	Lcd_WriteData(0x29); 
	Lcd_WriteData(0x2e); 
	Lcd_WriteData(0x30); 
	Lcd_WriteData(0x30); 
	Lcd_WriteData(0x39); 
	Lcd_WriteData(0x3f); 
	Lcd_WriteData(0x00); 
	Lcd_WriteData(0x07); 
	Lcd_WriteData(0x03); 
	Lcd_WriteData(0x10);  
	
	Lcd_WriteIndex(0x2a);
	Lcd_WriteData(0x00);
	Lcd_WriteData(0x00);
	Lcd_WriteData(0x00);
	Lcd_WriteData(0x7f);

	Lcd_WriteIndex(0x2b);
	Lcd_WriteData(0x00);
	Lcd_WriteData(0x00);
	Lcd_WriteData(0x00);
	Lcd_WriteData(0x9f);
	
	Lcd_WriteIndex(0xF0); //Enable test command  
	Lcd_WriteData(0x01); 
	Lcd_WriteIndex(0xF6); //Disable ram power save mode 
	Lcd_WriteData(0x00); 
	
	Lcd_WriteIndex(0x3A); //65k mode 
	Lcd_WriteData(0x05); 
	
	LCD_Delay_ms (200);
	
	Lcd_WriteIndex(0x29);//Display on	 
	
}

/***************************************************************************************

��������LCD_Set_Region
���ܣ�����lcd��ʾ�����ڴ�����д�������Զ�����
��ڲ�����xy�����յ�
����ֵ����

// Set column/row address window, then auto-increment write mode

**************************************************************************************/

void Lcd_SetRegion(uint16_t x_start,uint16_t y_start,uint16_t x_end,uint16_t y_end)
{		
	Lcd_WriteIndex(0x2a);
	Lcd_WriteData(0x00);
	Lcd_WriteData(x_start+2);
	Lcd_WriteData(0x00);
	Lcd_WriteData(x_end+2);

	Lcd_WriteIndex(0x2b);
	Lcd_WriteData(0x00);
	Lcd_WriteData(y_start+3);
	Lcd_WriteData(0x00);
	Lcd_WriteData(y_end+3);
	
	Lcd_WriteIndex(0x2c);

}

/***************************************************************************************

��������LCD_Set_XY
���ܣ�����lcd��ʾ��ʼ��
��ڲ�����xy����
����ֵ����

// Set LCD cursor position (wraps SetRegion for single point)

**************************************************************************************/
void Lcd_SetXY(uint16_t x,uint16_t y)
{
  	Lcd_SetRegion(x,y,x,y);
}

/***************************************************************************************

��������LCD_DrawPoint
���ܣ���һ����
��ڲ�������?
����ֵ����

// Draw a single pixel at (x, y) with specified color

**************************************************************************************/
void Gui_DrawPoint(uint16_t x,uint16_t y,uint16_t Data)
{
	Lcd_SetRegion(x,y,x+1,y+1);
	LCD_WriteData_16Bit(Data);

}    

/***************************************************************************************

��������Lcd_Clear
���ܣ�ȫ����������
��ڲ����������ɫCOLOR
����ֵ����

// Clear entire screen with specified color

**************************************************************************************/
void Lcd_Clear(uint16_t Color)               
{	
   unsigned int i,m;
	
   Lcd_SetRegion(0,0,X_MAX_PIXEL-1,Y_MAX_PIXEL-1);
	
   Lcd_WriteIndex(0x2C);
	
   for(i=0;i<X_MAX_PIXEL;i++)
    for(m=0;m<Y_MAX_PIXEL;m++)
    {	
	  	LCD_WriteData_16Bit(Color);
    }   
}

/***************************************************************************************

��������void Lcd_fill(uint16_t x0,uint16_t y0, uint16_t x1,uint16_t y1, uint16_t Color) 

���ܣ��������?
��ڲ����������ɫCOLOR
����ֵ����

// Fill a rectangular area with specified color

**************************************************************************************/
void Lcd_fill(uint16_t x0,uint16_t y0, uint16_t x1,uint16_t y1, uint16_t Color)               
{	
   uint16_t X_MAX;
	 uint16_t Y_MAX;
	
   uint16_t i,m;	
	  
	 X_MAX= x1-x0;
	 Y_MAX= y1-y0;
	
   Lcd_SetRegion(x0,y0,x1-1,y1-1);
	
   Lcd_WriteIndex(0x2C);
	
   for(i=0;i<X_MAX; i++)
    for(m=0;m<Y_MAX; m++)
    {	
	  	LCD_WriteData_16Bit(Color);
    }   
}

/**************************************************************************************

// End of Lcd_Driver.c - ST7735R LCD Driver for STM32G474

**************************************************************************************/
