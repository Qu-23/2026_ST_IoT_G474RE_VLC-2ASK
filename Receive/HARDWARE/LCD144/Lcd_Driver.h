#ifndef __Lcd_Driver_H
#define __Lcd_Driver_H

#include "main.h"
#include "stdint.h"

#define u8 uint8_t
#define u16 uint16_t
#define u32 uint32_t

#define	LCD_Delay_ms  	HAL_Delay   
#define  delay_ms 	HAL_Delay

// LCD device structure type
typedef struct  
{										    
	u16 width;			//LCD
	u16 height;			//LCD
	u16 id;				  //LCD ID
	u8  dir;			  
	u16	wramcmd;		//gram
	u16  setxcmd;		//x
	u16  setycmd;		//y
}_lcd_dev; 	

//LCD
extern _lcd_dev lcddev;	//LCD

#define X_MAX_PIXEL	        128
#define Y_MAX_PIXEL	        128

//********************************************************************************

// Color definitions (16-bit RGB565 format)
// Basic colors for LCD display

//********************************************************************************

// Commonly used 16-bit colors

#define RED  	  0xf800
#define GREEN	  0x07e0
#define BLUE 	  0x001f
#define WHITE	  0xffff
#define BLACK	  0x0000
#define YELLOW  0xFFE0
#define GRAY0   0xEF7D   	//0
#define GRAY1   0x8410    //1
#define GRAY2   0x4208    //2

//  ----------------------------------------------------------------
// Hardware SPI2 pin mapping (ST7735R LCD)
// LCD_SDI = PB15 -- SPI2_MOSI
// LCD_SCL = PB13 -- SPI2_SCK
// LCD_CS  = PB12 -- GPIO output (software CS)
// LCD_SDO = PB14 -- SPI2_MISO / RST
// LCD_DC  = PC6  -- GPIO output (Data/Command)
// LCD_BLK = PC7  -- GPIO output (Backlight)
// ----------------------------------------------------------------

// GPIO Set macros - pull pin high
#define	LCD_SDA_SET  	HAL_GPIO_WritePin(GPIOB,GPIO_PIN_15,GPIO_PIN_SET) 	   //PB151

#define	LCD_SCL_SET  	HAL_GPIO_WritePin(GPIOB,GPIO_PIN_13,GPIO_PIN_SET)	     //PB131

#define	LCD_CS_SET  	HAL_GPIO_WritePin(GPIOB,GPIO_PIN_12,GPIO_PIN_SET)	     //PB121

#define LCD_RST_Set  HAL_GPIO_WritePin(GPIOB,GPIO_PIN_14,GPIO_PIN_SET)	     //PB141

#define	LCD_DC_SET  	HAL_GPIO_WritePin(GPIOC,GPIO_PIN_6,GPIO_PIN_SET)		   //PC61

#define	LCD_BLK_SET  	HAL_GPIO_WritePin(GPIOC,GPIO_PIN_7,GPIO_PIN_SET) 	     //PC71

// GPIO Clear macros - pull pin low

#define	LCD_SDA_CLR  	HAL_GPIO_WritePin(GPIOB,GPIO_PIN_15,GPIO_PIN_RESET)  	  //PB150 //DIN

#define	LCD_SCL_CLR  	HAL_GPIO_WritePin(GPIOB,GPIO_PIN_13,GPIO_PIN_RESET)	    //PB130 //CLK

#define	LCD_CS_CLR  	HAL_GPIO_WritePin(GPIOB,GPIO_PIN_12,GPIO_PIN_RESET)	   	//PB120 //CS

#define LCD_RST_Clr  HAL_GPIO_WritePin(GPIOB,GPIO_PIN_14,GPIO_PIN_RESET)	    //PB140 //RES

#define	LCD_DC_CLR  	HAL_GPIO_WritePin(GPIOC,GPIO_PIN_6,GPIO_PIN_RESET)		 	//PC60 //DC

#define	LCD_BLK_CLR  	HAL_GPIO_WritePin(GPIOC,GPIO_PIN_7,GPIO_PIN_RESET)  	  //PC70 //BLK

uint8_t LCD_SPI2_ReadWriteByte(uint8_t TxData);   //SPI

void LCD_GPIO_Init(void);
void Lcd_WriteIndex(uint8_t Index);
void Lcd_WriteData(uint8_t Data);
void Lcd_WriteReg(uint8_t Index,uint8_t Data);
uint16_t Lcd_ReadReg(uint8_t LCD_Reg);
void Lcd_Reset(void);
void Lcd_Init(void);
void Lcd_Clear(uint16_t Color);
void Lcd_SetXY(uint16_t x,uint16_t y);
void Gui_DrawPoint(uint16_t x,uint16_t y,uint16_t Data);
void Lcd_SetRegion(uint16_t x_start,uint16_t y_start,uint16_t x_end,uint16_t y_end);
void LCD_WriteData_16Bit(uint16_t Data);

void Lcd_fill(uint16_t x0,uint16_t y0, uint16_t x1,uint16_t y1, uint16_t Color) ;

#endif

//********************************************************************************

// End of Lcd_Driver.h - ST7735R LCD Driver Pin Definitions

//********************************************************************************
