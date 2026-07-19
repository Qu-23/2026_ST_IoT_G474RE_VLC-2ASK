 
#include "GBK_LibDrive.h"	
	
	uint16_t D_Color=BLUE; 
  uint16_t B_Color=WHITE; 
	
//////////////////////////////////////////////////////////////////////////////////	

// GBK Font Library Driver Implementation
// Uses SPI2 bus (shared with LCD), PB0 as font chip CS

//********************************************************************************

////////////////////////////////////////////////////////////////////////////////// 	 

void GBK_Lib_Init(void)
{
	 	
	 #if    Used_FontIO
	  
//	  GPIO_InitTypeDef  GPIO_InitStructure;	
//	
//	  RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOB | RCC_AHB1Periph_GPIOC | RCC_AHB1Periph_GPIOF, ENABLE);// Enable GPIO clocks
//
//	  GPIO_InitStructure.GPIO_Pin = GPIO_Pin_2;        // PB2 - Font MISO
//	  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN;     // Input mode
//	  GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;   // Push-pull
//    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz;//100MHz
//	  GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;      // Pull-up
//	  GPIO_Init(GPIOB, &GPIO_InitStructure);            // Init PB2
//		
//	  GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0;      // PB0 - Font CLK
//	  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;  // Output mode
//	  GPIO_Init(GPIOB, &GPIO_InitStructure);         // Init PB0
//		
//	  GPIO_InitStructure.GPIO_Pin = GPIO_Pin_13;     // PC13 - Font CS
//	  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;  // Output mode
//	  GPIO_Init(GPIOC, &GPIO_InitStructure);         // Init PC13
//		
//	  GPIO_InitStructure.GPIO_Pin = GPIO_Pin_11;     // PF11 - Font MOSI
//	  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;  // Output mode
//	  GPIO_Init(GPIOF, &GPIO_InitStructure);         // Init PF11
//				
//	  Font_CS_SN; // Disable font chip select
		
		#endif
		
		// PB0 as GBK font chip CS, initialize to high (disabled)
		{
			GPIO_InitTypeDef GPIO_InitStruct = {0};
			__HAL_RCC_GPIOB_CLK_ENABLE();
			GPIO_InitStruct.Pin = GPIO_PIN_0;
			GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
			GPIO_InitStruct.Pull = GPIO_PULLUP;
			GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
			HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
			FCS_SET; // CS init high (disabled)
		}
		
		GBK_ReadID();	// Read font chip ID
                  // Verify font chip communication
}

//////////////////////////////////////////////////////////////////////////////////	

// GBK delay function (software delay loop)

//********************************************************************************

////////////////////////////////////////////////////////////////////////////////// 	

void GBK_delay(unsigned char Time)
{
     unsigned char m,n;
	
     for(n=0;n<Time;n++)
	   {
      for(m=0;m<100;m++);
		 }
}

//////////////////////////////////////////////////////////////////////////////////	

// Legacy software SPI function (bit-bang) - replaced by hardware SPI2

//********************************************************************************

////////////////////////////////////////////////////////////////////////////////// 

#if    Used_FontIO

//unsigned char Font_SPI_WR(unsigned char byte)
//{
//	unsigned char bit_ctr;
//	
//    for(bit_ctr=0; bit_ctr<8; bit_ctr++)  // Send/receive 8 bits
//	{
//		
//		
//		if((byte&0x80)==0x80)FDI_SET; 			// MSB TO MOSI
//			else FDI_CLR; 
//
//		FCLK_CLR;
//		
//		byte=(byte<<1);					// shift next bit to MSB
//				
//		byte|=FDO_IN;	        		// capture current MISO bit
//		
//		FCLK_SET;
//		
//	}
//	
//    return byte;  // Return received byte
//	
//}

#endif

//********************************************************************************
// Read data from font library FLASH chip via SPI2
// Uses 24-bit address, max 65535 bytes per read
//********************************************************************************

void FontLib_Read(u8* pBuffer,u32 ReadAddr,u16 Num_Read)   
{ 
 	  u16 i;  
	
	  Font_CS_EN;                           
	
    Font_SPI_WR(FontRead_CMD);            
    Font_SPI_WR((u8)((ReadAddr)>>16));    //24bit
    Font_SPI_WR((u8)((ReadAddr)>>8));   
    Font_SPI_WR((u8)ReadAddr);   
    for(i=0;i<Num_Read;i++)
	  { 
        pBuffer[i]=Font_SPI_WR(0xFF);   
    }
		
 	  Font_CS_SN;  		                      
		
}

//********************************************************************************
// Read font chip ID and display on LCD
//********************************************************************************
void GBK_ReadID(void)
{
	u8 Temp[24];
	u8 i;
	
	Lcd_Clear(WHITE);//?
	
	for(i=0;i<24;i++)Temp[i]=0;

  FontLib_Read(Temp,0,17);  
  DrawFont_GBK16B(16,16,RED,Temp);
	
	for(i=0;i<24;i++)Temp[i]=0;
  FontLib_Read(Temp,20,8);   	
	DrawFont_GBK16B(16,32,RED,Temp);
	
	for(i=0;i<24;i++)Temp[i]=0;
  FontLib_Read(Temp,30,8);   	
	DrawFont_GBK16B(16,48,RED,Temp);

  delay_ms(500);	//	--??
	
} 

// Get ASCII font dot matrix from font library chip
// Reads ((font height/8)+1) * font width bytes

/********************************************************************************/

////////////////////////////////////////////////////////////////////////////////// 	 

void GBK_GetASC_Point(uint8_t *code, uint8_t *Pdot, uint8_t Font, uint16_t Num)
{		    
	 uint8_t QW;    //ASC??
	 uint32_t  ADDRESS;
	
	 QW=*code;//--??
	
	 // Function: 0))*(size/2);//
	
	switch(Font)
	{

		case 12:
			ADDRESS=((unsigned long)QW*Num)+ASCII6x12_ST;	  
			break;
		case 16:
			ADDRESS=((unsigned long)QW*Num)+ASCII8x16_ST;	  
			break;
		case 24:
			ADDRESS=((unsigned long)QW*Num)+ASCII12x24_ST;	
			break;
		case 32:
			ADDRESS=((unsigned long)QW*Num)+ASCII16x32_ST;	
			break;
		case 48:
			ADDRESS=((unsigned long)QW*Num)+ASCII24x48_ST;	
			break;
		case 64:
			ADDRESS=((unsigned long)QW*Num)+ASCII32x64_ST;	
			break;
		
    default: return;
		
	} 

	FontLib_Read(Pdot,ADDRESS,Num);
	
}  

// Display ASCII string using font library chip (12/16/24/32/48/64 sizes)
// Draws each character dot by dot on LCD

/********************************************************************************/

////////////////////////////////////////////////////////////////////////////////////////////////////////////////// 	

void GBK_ShowASCII(uint16_t x, uint16_t y, uint8_t *N_Word, uint8_t size, uint16_t D_Color, uint16_t B_Color, uint8_t mode)
{
	
	uint16_t csize; 
	
	uint8_t temp;
	
	uint16_t t,t1;
	
	uint16_t y0=y;
	
	uint8_t dzk[256];  
	
	csize=(size/8+((size%8)?1:0))*(size/2);		//ASCII??
	
	if(size!=12&&size!=16&&size!=24&&size!=32&&size!=48&&size!=64)return;	//size
	
	GBK_GetASC_Point(N_Word,dzk,size,csize);	              
	
	for(t=0;t<csize;t++)
	{
		
		temp=dzk[t];			
		
		for(t1=0;t1<8;t1++)//?
		{
			if(temp&0x80) Gui_DrawPoint(x,y,D_Color);   
			
			else if(mode==0)Gui_DrawPoint(x,y,B_Color); 
			
			temp<<=1;
			
			y++;
			
			if((y-y0)==size)
			{
				y=y0;
				x++;
				break;
			}
		}  	 
	}  
}

// Get GBK Chinese character dot matrix from font library chip
// Parameters: code = GBK code (2 bytes), Pdot = output buffer, Font = size (12/16/24/32), Num = bytes to read = (size/8)*size bytes
// Calculates flash address from GBK区位码: address = ((190 * qh + ql) * Num) + base_addr
//********************************************************************************

////////////////////////////////////////////////////////////////////////////////////////////////////////////////// 	

void GBK_Lib_GetHz(uint8_t *code, uint8_t *Pdot, uint8_t Font, uint16_t Num)
{		    
	 
	 uint8_t   qh,ql;
	 uint16_t   i;	
	 uint32_t  foffset;
	 uint32_t  ADDRESS;
	
	 // Function: 0))*(size);//
	
	 qh=*code;       //??
	 ql=*(++code);   //??
	
	 if(qh<0x81||ql<0x40||ql==0xff||qh==0xff)
		{   		    
				for(i=0;i<Num;i++)*Pdot++=0x00;   //??-0
				return;                            
		} 

		if(ql<0x7f)ql-=0x40;  //!
		  else ql-=0x41;

		  qh-=0x81;

	foffset=((unsigned long)190*qh+ql)*Num;	
	
	switch(Font)
	{
    case 12:
			ADDRESS=foffset+GBK12x12_ST;     //  16x16 ??
			break;
		case 16:
			ADDRESS=foffset+GBK16x16_ST;     //  16x16 ??
			break;
		case 24:
			ADDRESS=foffset+GBK24x24_ST;     //  24x24 ??
			break;
		case 32:
			ADDRESS=foffset+GBK32x32_ST;     //  32x32 ??
			break;
		 default: return;
			
	} 

	FontLib_Read(Pdot,ADDRESS,Num);
	
} 

// Display Chinese character (GBK) using font library chip
// Supports 12/16/24/32 dot matrix sizes

/********************************************************************************/

////////////////////////////////////////////////////////////////////////////////////////////////////////////////// 	

void GBK_Show_Font(uint16_t x, uint16_t y, uint8_t *font, uint8_t size, uint16_t D_Color, uint16_t B_Color, uint8_t mode)
{
	
	uint16_t  csize;                                    
	uint8_t   temp,t,t1;
	uint16_t  y0=y;
	uint8_t   dzk[128];  
	
	csize=(size/8+((size%8)?1:0))*(size);              
	
	if(size!=12&&size!=16&&size!=24&&size!=32)return;	 //size
	
	GBK_Lib_GetHz(font,dzk,size,csize);	                   
	
	for(t=0;t<csize;t++)
	 {   												   
		  temp=dzk[t];			                             
		
			for(t1=0;t1<8;t1++)
			{
				if(temp&0x80)Gui_DrawPoint(x,y,D_Color);
				
				else if(mode==0)Gui_DrawPoint(x,y,B_Color); 
				
				temp<<=1;
				
				y++;
				
				if((y-y0)==size)
				{
					y=y0;
					x++;
					break;
				}
			}  	 
	}  
}

// Display GBK string with auto word-wrap and line break
// Supports ASCII and Chinese mixed text, auto line-break at width/height limits
//********************************************************************************

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void GBK_Show_Str(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint8_t *str, uint8_t size, uint16_t D_Color, uint16_t B_Color, uint8_t mode)
{					
	uint16_t x0=x;
	uint16_t y0=y;							  	  
  uint8_t bHz=0;     

    while(*str!=0)
    { 
        if(!bHz)
        {
	        if(*str>0x80)bHz=1;
	        else              
	        {      
            if(x>(x0+width-size/2))
							{				   
								y+=size;
								x=x0;	   
							}	
							
		        if(y>(y0+height-size))break;
							
		        if(*str==13)
							{         
									y+=size;
									x=x0;
									str++; 
							}  
		        else GBK_ShowASCII(x, y, str, size, D_Color, B_Color, mode);  //LCD_ShowChar(x,y,*str,size,mode);//
				    str++; 
		        x+=size/2; //,
	        }
        }
				else
        {     
          bHz=0;
										
          if(x>(x0+width-size))
					{	    
						y+=size;
						x=x0;		  
					}
	        
					if(y>(y0+height-size))break;  
					
	        GBK_Show_Font(x,y,str,size, D_Color, B_Color, mode); //??
					
	        str+=2; 
	        x+=size;
        }						 
    }   
}  

// Display GBK string centered horizontally within given width
// Calculates string pixel width, then centers by offsetting x position
//********************************************************************************

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void GBK_Show_StrMid(uint16_t x,uint16_t y, uint8_t size, uint8_t len, uint16_t D_Color, uint16_t B_Color,uint8_t*str)
{
	uint16_t strlenth=0;
		
  strlenth=strlen((const char*)str);
	strlenth*=size/2;
	if(strlenth>len)GBK_Show_Str(x,y,lcddev.width,lcddev.height,str,size,D_Color,B_Color,1);
	else
	{
		strlenth=(len-strlenth)/2;
	  GBK_Show_Str(strlenth+x,y,lcddev.width,lcddev.height,str,size,D_Color,B_Color,1);
	}
}   

//**************************************************************************************/
// Draw GBK 12x12 font string at (x,y) with specified color
// Wrapper for GBK_Show_Str with 12px font size, white background
//**************************************************************************************/

void DrawFont_GBK12B(u16 x,u16 y, u16 color, u8*str)
{
	u16 width;
	u16 height;
	
	width=lcddev.width-x;
	height=lcddev.height-y;
	
	GBK_Show_Str(x,y,width,height, str, 12, color,B_Color, 0);
	
}

//**************************************************************************************/
// Draw GBK 16x16 font string at (x,y) with specified color
// Wrapper for GBK_Show_Str with 16px font size, white background
//**************************************************************************************/

void DrawFont_GBK16B(u16 x,u16 y, u16 color, u8*str)
{
	u16 width;
	u16 height;
	
	width=lcddev.width-x;
	height=lcddev.height-y;
	
	GBK_Show_Str(x,y,width,height, str, 16, color,B_Color, 0);
	
}

//**************************************************************************************/
// Draw GBK 24x24 font string at (x,y) with specified color
// Wrapper for GBK_Show_Str with 24px font size, white background
//**************************************************************************************/

void DrawFont_GBK24B(u16 x,u16 y, u16 color, u8*str)
{
	u16 width;
	u16 height;
	
	width=lcddev.width-x;
	height=lcddev.height-y;
	
	GBK_Show_Str(x,y,width,height, str, 24, color,B_Color, 0);
	
}

//**************************************************************************************/
// Draw GBK 32x32 font string at (x,y) with specified color
// Wrapper for GBK_Show_Str with 32px font size, white background
//**************************************************************************************/

void DrawFont_GBK32B(u16 x,u16 y, u16 color, u8*str)
{
	u16 width;
	u16 height;
	
	width=lcddev.width-x;
	height=lcddev.height-y;
	
	GBK_Show_Str(x,y,width,height, str, 32, color,B_Color, 0);
	
}

//**************************************************************************************/
// Draw ASCII 24x48 font string at (x,y) with specified color
// Wrapper for GBK_Show_Str with 48px font size, white background, ASCII only
//**************************************************************************************/

void DrawFontASC_GBK48B(u16 x,u16 y, u16 color, u8*str)
{
	u16 width;
	u16 height;
	
	width=lcddev.width-x;
	height=lcddev.height-y;
	
	GBK_Show_Str(x,y,width,height, str, 48, color,B_Color, 0);
	
}

//**************************************************************************************/
// Draw ASCII 32x64 font string at (x,y) with specified color
// Wrapper for GBK_Show_Str with 64px font size, white background, ASCII only
//**************************************************************************************/

void DrawFontASC_GBK64B(u16 x,u16 y, u16 color, u8*str)
{
	u16 width;
	u16 height;
	
	width=lcddev.width-x;
	height=lcddev.height-y;
	
	GBK_Show_Str(x,y,width,height, str, 64, color,B_Color, 0);
	
}

//****************************************************************************************/
void GBK_LibFont_Test(void)
{
	
	Lcd_Clear(WHITE);
	
	DrawFont_GBK16B(4,8,BLUE,"2: GBK Font Lib");
	DrawFont_GBK16B(4,30,BLUE," Display Test");
	
	delay_ms(1000);
	
	Lcd_Clear(WHITE);
	
	GBK_Show_Str(0,0,128,32,"32x32 Font",32,D_Color,B_Color,0);	
	GBK_Show_Str(4,40,128,16,"Welcome DevEBox",16,D_Color,B_Color,0);	
	GBK_Show_Str(4,60,128,16,"From:",16,D_Color,B_Color,0);
	GBK_Show_Str(4,80,128,16,"  mcudev.taobao",16,D_Color,B_Color,0);
	
	DrawFont_GBK12B(4,100,BLUE,"Date: 2023/05/08");
	
	delay_ms(1000);
	
	Lcd_Clear(WHITE);
	
	GBK_Show_StrMid(4,4,12,128,BLUE,GRAY0,"Chinese Test");//Chinese Test	
								
	GBK_Show_Str(4,20,128,12,"12x12 GBK Font:",12,D_Color,B_Color,0);	//show string
				
	GBK_Show_Str(4,40,128,16,"16x16 Font:",16,D_Color,B_Color,0);	//show string
				
	GBK_Show_Str(4,60,128,24,"24x24 Font:",24,D_Color,B_Color,0);	//show string
					
	DrawFont_GBK32B(4,90,BLUE,"32x32 Font:");	                //show string
	
  delay_ms(2000);	
	
  Lcd_Clear(WHITE);
	
	GBK_Show_Str(4,0,128,24,"ASCII Font:",24,D_Color,B_Color,0);	//show string
	
	GBK_Show_Str(4,30,128,24,"48x24 Font",24,D_Color,B_Color,0);	//show string
	
  DrawFontASC_GBK48B(8,60,BLUE,"48");

  delay_ms(2000);	
	
  Lcd_Clear(WHITE);
	
  GBK_Show_Str(4,0,128,24,"ASCII Font:",24,D_Color,B_Color,0);	//show string
	GBK_Show_Str(4,30,128,24,"64x32 Font",24,D_Color,B_Color,0);	//show string
	
  DrawFontASC_GBK64B(8,60,BLUE,"64");
		
	delay_ms(2000);	
}
