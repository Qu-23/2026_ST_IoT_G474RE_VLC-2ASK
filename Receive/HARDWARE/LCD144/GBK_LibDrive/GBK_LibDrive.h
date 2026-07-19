#ifndef __GBK_LibDrive_H__
#define __GBK_LibDrive_H__	 

#include "main.h"

#include "Lcd_Driver.h"

#include "string.h"												    
//#include "usart.h"	

//////////////////////////////////////////////////////////////////////////////////	 
 
// GBK Font Library Driver - SPI interface via SPI2
// PB0: Font CS (chip select), uses shared SPI2 bus with LCD

////////////////////////////////////////////////////////////////////////////////// 

#define   FontRead_CMD  0x03// Read command

#define   Used_FontIO   0 // 0=Hardware SPI, 1=Software GPIO bit-bang

#if    Used_FontIO
// Legacy software GPIO mode (not used, replaced by hardware SPI2)
// FCS_SET  	GPIO_SetBits(GPIOC,GPIO_Pin_13)     // Font CS high
// FCS_CLR  	GPIO_ResetBits(GPIOC,GPIO_Pin_13)   // Font CS low

// FDI_SET   GPIO_SetBits(GPIOF,GPIO_Pin_11)      // Font MOSI high
// FDI_CLR   GPIO_ResetBits(GPIOF,GPIO_Pin_11)    // Font MOSI low

// FDO_IN   GPIO_ReadInputDataBit(GPIOB,GPIO_Pin_2)   // Font MISO read

// FCLK_SET   GPIO_SetBits(GPIOB,GPIO_Pin_0)   // Font CLK high
// FCLK_CLR   GPIO_ResetBits(GPIOB,GPIO_Pin_0) // Font CLK low

// FCS_SET 		  GPIO_SetBits(GPIOB,GPIO_Pin_12)       // Font CS high (PB12)
// FCS_CLR 		  GPIO_ResetBits(GPIOB,GPIO_Pin_12)     // Font CS low (PB12)

// Font_CS_EN 		FCS_SET; GBK_delay(5);         // Font chip enable (active low)
// Font_CS_SN 		FCS_CLR; GBK_delay(5);  		    // Font chip disable

#else

#define	FCS_SET 		  HAL_GPIO_WritePin(GPIOB,GPIO_PIN_0,GPIO_PIN_SET)	      //PB0 CS=high (disable)
#define	FCS_CLR 		  HAL_GPIO_WritePin(GPIOB,GPIO_PIN_0,GPIO_PIN_RESET)	   	//PB0 CS=low (enable)

#define	Font_CS_EN 		 FCS_CLR; GBK_delay(5);               //Chip select enable (active low)
#define	Font_CS_SN 		 FCS_SET; GBK_delay(5);  		          //Chip select disable

#define	Font_SPI_WR    LCD_SPI2_ReadWriteByte

#endif

// Font library address definitions (start address in GBK font chip)

#define	ASCII6x12_ST   0x00080000
#define	ASCII8x16_ST   0x00080800
#define	ASCII12x24_ST  0x00081200
#define	ASCII16x32_ST  0x00082600

#define	ASCII24x48_ST  0x00084800
#define	ASCII32x64_ST  0x00089200

#define	GBK12x12_ST    0x00091400
#define	GBK16x16_ST    0x0011DD00
#define	GBK24x24_ST    0x001DA000
#define	GBK32x32_ST    0x00380000 

void GBK_Lib_Init(void);

void FontLib_Read(u8* pBuffer,u32 ReadAddr,u16 Num_Read);//?

void GBK_ReadID(void);//ID

void GBK_delay(unsigned char Time);

void GBK_GetASC_Point(uint8_t *code, uint8_t *Pdot, uint8_t Font, uint16_t Num);   // ASCII
void GBK_ShowASCII(uint16_t x, uint16_t y, uint8_t *N_Word, uint8_t size, uint16_t D_Color, uint16_t B_Color, uint8_t mode); // ASCII --
	
void GBK_Lib_GetHz(uint8_t *code, uint8_t *Pdot, uint8_t Font, uint16_t Num);			   
void GBK_Show_Font(uint16_t x, uint16_t y, uint8_t *font, uint8_t size, uint16_t D_Color, uint16_t B_Color, uint8_t mode);			 

void GBK_Show_Str(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint8_t *str, uint8_t size, uint16_t D_Color, uint16_t B_Color, uint8_t mode);	
void GBK_Show_StrMid(uint16_t x,uint16_t y, uint8_t size, uint8_t len, uint16_t D_Color, uint16_t B_Color,uint8_t*str); 

void DrawFont_GBK12B(u16 x,u16 y, u16 color, u8*str);//12x12?--(?)
void DrawFont_GBK16B(u16 x,u16 y, u16 color, u8*str);//16x16?--(?)
void DrawFont_GBK24B(u16 x,u16 y, u16 color, u8*str);//24x24?--(?)
void DrawFont_GBK32B(u16 x,u16 y, u16 color, u8*str);//32x32?--(?)

void DrawFontASC_GBK48B(u16 x,u16 y, u16 color, u8*str);//24x48?--ACSII

void DrawFontASC_GBK64B(u16 x,u16 y, u16 color, u8*str);//32x64?--ACSII

void GBK_LibFont_Test(void);//GBK?

#endif
