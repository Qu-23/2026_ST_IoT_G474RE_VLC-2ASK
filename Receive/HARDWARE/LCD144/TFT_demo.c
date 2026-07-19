/**************************************************************************************/

// TFT Demo Functions - Test patterns for LCD display

/**************************************************************************************/

/* Includes ------------------------------------------------------------------*/

#include "Lcd_Driver.h"

#include "GUI.h"
#include "Picture.h"
#include "TFT_demo.h"

#include "GBK_LibDrive.h"	

unsigned char Num[10]={0,1,2,3,4,5,6,7,8,9};

/**************************************************************************************/

// Redraw main menu: device info, test options

/**************************************************************************************/

void Redraw_Mainmenu(void)
{

	Lcd_Clear(GRAY0);
	
	Gui_DrawFont_F16(8,0,BLUE,GRAY0,"STM32开发板");
	Gui_DrawFont_F16(16,20,RED,GRAY0,"LCD显示测试");

	DisplayButtonUp(15,38,113,58); //x1,y1,x2,y2
	
	Gui_DrawFont_F16(16,40,GREEN,GRAY0,"颜色测试");

	DisplayButtonUp(15,68,113,88); //x1,y1,x2,y2
	
	Gui_DrawFont_F16(16,70,BLUE,GRAY0,"字体显示测试");

	DisplayButtonUp(15,98,113,118); //x1,y1,x2,y2
	
	Gui_DrawFont_F16(16,100,RED,GRAY0,"图片显示测试");
	delay_ms(1500);
}

/**************************************************************************************/

// Numeric display test

/**************************************************************************************/

void Num_Test(void)
{
	u8 i=0;
	Lcd_Clear(GRAY0);
	Gui_DrawFont_F16(16,20,RED,GRAY0,"Num Test");
	delay_ms(1000);
	Lcd_Clear(GRAY0);

	for(i=0;i<9;i++)
	{
	  Gui_DrawFont_Num32((i%3)*40,32*(i/3)+5,RED,GRAY0,Num[i+1]);
	  delay_ms(100);
	}
	
}

/**************************************************************************************/

// Font display test (Chinese characters)

/**************************************************************************************/

void Font_Test(void)
{
	Lcd_Clear(GRAY0);
//	Gui_DrawFont_F16(16,10,BLUE,GRAY0,"字体显示测试");

	delay_ms(1000);
	Lcd_Clear(GRAY0);
//	Gui_DrawFont_F16(8,8,BLACK,GRAY0,"STM32开发板");
//	Gui_DrawFont_F16(16,28,GREEN,GRAY0,"专注液晶显示");
//	Gui_DrawFont_F16(16,48,RED,GRAY0, "全程技术支持");
//	Gui_DrawFont_F16(0,68,BLUE,GRAY0," Tel:1234567890");
//	Gui_DrawFont_F16(0,88,RED,GRAY0, " mcudev.taobao");	
	delay_ms(1800);	
}

/**************************************************************************************/

// Color fill test (white/black/red/green/blue)

/**************************************************************************************/

void Color_Test(void)
{
	u8 i=1;
	Lcd_Clear(GRAY0);
	
	Gui_DrawFont_F16(20,10,BLUE,GRAY0,"Color Test");
	delay_ms(500);

	while(i--)
	{
		Lcd_Clear(WHITE);
		delay_ms(500);
		Lcd_Clear(BLACK);
		delay_ms(500);
		Lcd_Clear(RED);
		delay_ms(500);
	  Lcd_Clear(GREEN);
		delay_ms(500);
	  Lcd_Clear(BLUE);
		delay_ms(500);
	}		
}

/**************************************************************************************

// Display 40x40 image tiled 3x3

**************************************************************************************/

void showimage(const unsigned char *p) //40*40 QQ tiled 3x3
{
  int i,j,k;
	unsigned char picH,picL;
	Lcd_Clear(WHITE);

	for(k=0;k<3;k++)
	{
	   	for(j=0;j<3;j++)
		{
			Lcd_SetRegion(40*j,40*k,40*j+39,40*k+39);
		    for(i=0;i<40*40;i++)
			 {
			 	picL=*(p+i*2);
				picH=*(p+i*2+1);
				LCD_WriteData_16Bit(picH<<8|picL);
			 }
		 }
	}
}

// Display single small image at specified position (no tiling)
void showimage_single(const unsigned char *p, uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
    uint32_t i;
    unsigned char picH, picL;

    Lcd_SetRegion(x, y, x + w - 1, y + h - 1);
    for(i = 0; i < w * h; i++)
    {
        picL = *(p + i * 2);
        picH = *(p + i * 2 + 1);
        LCD_WriteData_16Bit(picH << 8 | picL);
    }
}

/**************************************************************************************/
// Display full-screen 128x128 image

/**************************************************************************************/

void Fullscreen_showimage(const unsigned char *p) //128*128
{
  int i; 
	unsigned char picH,picL;
	
	Lcd_Clear(WHITE); 
	
			Lcd_SetRegion(0,0,127,127);		//:0127128
		    for(i=0;i<128*128;i++)
				 {	
					picL=*(p+i*2);	
					picH=*(p+i*2+1);				
					LCD_WriteData_16Bit(picH<<8|picL);  						
				 }	
		
}

/**************************************************************************************/

// Run all demo tests sequentially

/**************************************************************************************/

void Test_Demo(void)
{

		Redraw_Mainmenu();//()
		
		Color_Test();
		
		Num_Test();
		
		Font_Test();
	
	  GBK_LibFont_Test();
	
// Display image from Flash memory
////	
////	delay_ms(1500);
	
		Fullscreen_showimage(gImage_XHR128);// Display image from Flash
		delay_ms(1500);
	
//// Flash read test (commented out)
////	delay_ms(1500);
//// Flash write test (commented out)
////	delay_ms(1500);
	
}

/**************************************************************************************/

// End of TFT_demo.c - Demo Functions

/**************************************************************************************/
