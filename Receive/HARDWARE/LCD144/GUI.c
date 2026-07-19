#include "main.h"
#include "Lcd_Driver.h"

#include "GUI.h"

#include "font.h"

//********************************************************************************

// GUI Drawing Functions Implementation
// Includes: circle (Bresenham), line, box, button, font display

//********************************************************************************

// Color conversion: BGR to RGB565
uint16_t LCD_BGR2RGB(uint16_t c)
{
  uint16_t  r,g,b,rgb;   
  b=(c>>0)&0x1f;
  g=(c>>5)&0x3f;
  r=(c>>11)&0x1f;	 
  rgb=(b<<11)+(g<<5)+(r<<0);		 
  return(rgb);

}

//********************************************************************************

// 函数功能: 使用Bresenham算法绘制圆形
// 输入参数: X,Y-圆心坐标；R-半径；fc-颜色

/********************************************************************************/

void Gui_Circle(uint16_t X,uint16_t Y,uint16_t R,uint16_t fc) 
{//Bresenham
    unsigned short  a,b; 
    int c; 
    a=0; 
    b=R; 
    c=3-2*R; 
    while (a<b) 
    { 
        Gui_DrawPoint(X+a,Y+b,fc);     //        7 
        Gui_DrawPoint(X-a,Y+b,fc);     //        6 
        Gui_DrawPoint(X+a,Y-b,fc);     //        2 
        Gui_DrawPoint(X-a,Y-b,fc);     //        3 
        Gui_DrawPoint(X+b,Y+a,fc);     //        8 
        Gui_DrawPoint(X-b,Y+a,fc);     //        5 
        Gui_DrawPoint(X+b,Y-a,fc);     //        1 
        Gui_DrawPoint(X-b,Y-a,fc);     //        4 

        if(c<0) c=c+4*a+6; 
        else 
        { 
            c=c+4*(a-b)+10; 
            b-=1; 
        } 
       a+=1; 
    } 
    if (a==b) 
    { 
        Gui_DrawPoint(X+a,Y+b,fc); 
        Gui_DrawPoint(X+a,Y+b,fc); 
        Gui_DrawPoint(X+a,Y-b,fc); 
        Gui_DrawPoint(X-a,Y-b,fc); 
        Gui_DrawPoint(X+b,Y+a,fc); 
        Gui_DrawPoint(X-b,Y+a,fc); 
        Gui_DrawPoint(X+b,Y-a,fc); 
        Gui_DrawPoint(X-b,Y-a,fc); 
    } 
	
} 

//********************************************************************************

// 函数功能: 使用Bresenham算法绘制直线
// 输入参数: x0,y0-起点坐标；x1,y1-终点坐标；Color-颜色

/********************************************************************************/

void Gui_DrawLine(uint16_t x0, uint16_t y0,uint16_t x1, uint16_t y1,uint16_t Color)   
{
int dx,             // difference in x's
    dy,             // difference in y's
    dx2,            // dx,dy * 2
    dy2, 
    x_inc,          // amount in pixel space to move during drawing
    y_inc,          // amount in pixel space to move during drawing
    error,          // the discriminant i.e. error i.e. decision variable
    index;          // used for looping	

	Lcd_SetXY(x0,y0);
	dx = x1-x0;//x
	dy = y1-y0;//y

	if (dx>=0)
	{
		x_inc = 1;
	}
	else
	{
		x_inc = -1;
		dx    = -dx;  
	} 
	
	if (dy>=0)
	{
		y_inc = 1;
	} 
	else
	{
		y_inc = -1;
		dy    = -dy; 
	} 

	dx2 = dx << 1;
	dy2 = dy << 1;

	if (dx > dy)//xyxy
	{//xx
		// initialize error term
		error = dy2 - dx; 

		// draw the line
		for (index=0; index <= dx; index++)//x
		{

			Gui_DrawPoint(x0,y0,Color);
			
			// test if error has overflowed
			if (error >= 0) //y
			{
				error-=dx2;

				// move to next line
				y0+=y_inc;//y
			} // end if error overflowed

			// adjust the error term
			error+=dy2;

			// move to the next pixel
			x0+=x_inc;//x1
		} // end for
	} // end if |slope| <= 1
	else//yxyx
	{//y
		// initialize error term
		error = dx2 - dy; 

		// draw the line
		for (index=0; index <= dy; index++)
		{
			// set the pixel
			Gui_DrawPoint(x0,y0,Color);

			// test if error overflowed
			if (error >= 0)
			{
				error-=dy2;

				// move to next line
				x0+=x_inc;
			} // end if error overflowed

			// adjust the error term
			error+=dx2;

			// move to the next pixel
			y0+=y_inc;
		} // end for
	} // end else |slope| > 1
}

//********************************************************************************

// 函数功能: 绘制3D风格矩形框
// 输入参数: x,y-左上角坐标；w,h-宽高；bc-背景色

/********************************************************************************/

void Gui_box(uint16_t x, uint16_t y, uint16_t w, uint16_t h,uint16_t bc)
{
	Gui_DrawLine(x,y,x+w,y,0xEF7D);
	Gui_DrawLine(x+w-1,y+1,x+w-1,y+1+h,0x2965);
	Gui_DrawLine(x,y+h,x+w,y+h,0x2965);
	Gui_DrawLine(x,y,x,y+h,0xEF7D);
  Gui_DrawLine(x+1,y+1,x+1+w-2,y+1+h-2,bc);
}

//********************************************************************************

// 函数功能: 绘制3D风格矩形框(模式选择)
// 输入参数: x,y-左上角坐标；w,h-宽高；mode-模式(0:凸起, 1:凹陷, 2:白色边框)

/********************************************************************************/

void Gui_box2(uint16_t x,uint16_t y,uint16_t w,uint16_t h, uint8_t mode)
{
	if (mode==0)	{
		Gui_DrawLine(x,y,x+w,y,0xEF7D);
		Gui_DrawLine(x+w-1,y+1,x+w-1,y+1+h,0x2965);
		Gui_DrawLine(x,y+h,x+w,y+h,0x2965);
		Gui_DrawLine(x,y,x,y+h,0xEF7D);
		}
	if (mode==1)	{
		Gui_DrawLine(x,y,x+w,y,0x2965);
		Gui_DrawLine(x+w-1,y+1,x+w-1,y+1+h,0xEF7D);
		Gui_DrawLine(x,y+h,x+w,y+h,0xEF7D);
		Gui_DrawLine(x,y,x,y+h,0x2965);
	}
	if (mode==2)	{
		Gui_DrawLine(x,y,x+w,y,0xffff);
		Gui_DrawLine(x+w-1,y+1,x+w-1,y+1+h,0xffff);
		Gui_DrawLine(x,y+h,x+w,y+h,0xffff);
		Gui_DrawLine(x,y,x,y+h,0xffff);
	}
}

/**************************************************************************************

函数功能: 在屏幕上显示一个按下状态的按钮
输入参数: x1,y1-按钮左上角坐标；x2,y2-按钮右下角坐标
输出参数: 无

**************************************************************************************/
void DisplayButtonDown(uint16_t x1,uint16_t y1,uint16_t x2,uint16_t y2)
{
	Gui_DrawLine(x1,  y1,  x2,y1, GRAY2);  //H
	Gui_DrawLine(x1+1,y1+1,x2,y1+1, GRAY1);  //H
	Gui_DrawLine(x1,  y1,  x1,y2, GRAY2);  //V
	Gui_DrawLine(x1+1,y1+1,x1+1,y2, GRAY1);  //V
	Gui_DrawLine(x1,  y2,  x2,y2, WHITE);  //H
	Gui_DrawLine(x2,  y1,  x2,y2, WHITE);  //V
}

/**************************************************************************************

函数功能: 在屏幕上显示一个释放状态的按钮
输入参数: x1,y1-按钮左上角坐标；x2,y2-按钮右下角坐标
输出参数: 无

**************************************************************************************/
void DisplayButtonUp(uint16_t x1,uint16_t y1,uint16_t x2,uint16_t y2)
{
	Gui_DrawLine(x1,  y1,  x2,y1, WHITE); //H
	Gui_DrawLine(x1,  y1,  x1,y2, WHITE); //V
	
	Gui_DrawLine(x1+1,y2-1,x2,y2-1, GRAY1);  //H
	Gui_DrawLine(x1,  y2,  x2,y2, GRAY2);  //H
	Gui_DrawLine(x2-1,y1+1,x2-1,y2, GRAY1);  //V
  Gui_DrawLine(x2  ,y1  ,x2,y2, GRAY2); //V
}

/**************************************************************************************

函数功能: 显示16x16中英文字符串(使用STM32芯片片上存储的字库)
输入参数: x,y-显示起始位置；fc-前景色；bc-背景色；*s-要显示的字符串
输出参数: 无

**************************************************************************************/

void Gui_DrawFont_F16(uint16_t x, uint16_t y, uint16_t fc, uint16_t bc, uint8_t *s)
{
	unsigned char i,j;
	unsigned short k,x0;
	x0=x;

	while(*s) 
	{	
		if((*s) < 128) 
		{
			k=*s;
			if (k==13) 
			{
				x=x0;
				y+=16;
			}
			else 
			{
				if (k>32) k-=32; else k=0;
	
			    for(i=0;i<16;i++)
				for(j=0;j<8;j++) 
					{
				    	if(asc16[k*16+i]&(0x80>>j))	Gui_DrawPoint(x+j,y+i,fc);
						else 
						{
							if (fc!=bc) Gui_DrawPoint(x+j,y+i,bc);
						}
					}
				x+=8;
			}
			s++;
		}
			
		else 
		{
		
			for (k=0;k<hz16_num;k++) 
			{
			  if ((hz16[k].Index[0]==*(s))&&(hz16[k].Index[1]==*(s+1)))
			  { 
				    for(i=0;i<16;i++)
				    {
						for(j=0;j<8;j++) 
							{
						    	if(hz16[k].Msk[i*2]&(0x80>>j))	Gui_DrawPoint(x+j,y+i,fc);
								else {
									if (fc!=bc) Gui_DrawPoint(x+j,y+i,bc);
								}
							}
						for(j=0;j<8;j++) 
							{
						    	if(hz16[k].Msk[i*2+1]&(0x80>>j))	Gui_DrawPoint(x+j+8,y+i,fc);
								else 
								{
									if (fc!=bc) Gui_DrawPoint(x+j+8,y+i,bc);
								}
							}
				    }
				}
			  }
			s+=2;x+=16;
		} 
		
	}
}

/**************************************************************************************

函数功能: 显示24x24中英文字符串(使用STM32芯片片上存储的字库)
输入参数: x,y-显示起始位置；fc-前景色；bc-背景色；*s-要显示的字符串
输出参数: 无

**************************************************************************************/

void Gui_DrawFont_F24(uint16_t x, uint16_t y, uint16_t fc, uint16_t bc, uint8_t *s)
{
	unsigned char i,j;
	unsigned short k;

	while(*s) 
	{
		if( *s < 0x80 ) 
		{
			k=*s;
			if (k>32) k-=32; else k=0;

		    for(i=0;i<16;i++)
			for(j=0;j<8;j++) 
				{
			    	if(asc16[k*16+i]&(0x80>>j))	
					Gui_DrawPoint(x+j,y+i,fc);
					else 
					{
						if (fc!=bc) Gui_DrawPoint(x+j,y+i,bc);
					}
				}
			s++;x+=8;
		}
		else 
		{

			for (k=0;k<hz24_num;k++) 
			{
			  if ((hz24[k].Index[0]==*(s))&&(hz24[k].Index[1]==*(s+1)))
			  { 
				    for(i=0;i<24;i++)
				    {
						for(j=0;j<8;j++) 
							{
						    	if(hz24[k].Msk[i*3]&(0x80>>j))
								Gui_DrawPoint(x+j,y+i,fc);
								else 
								{
									if (fc!=bc) Gui_DrawPoint(x+j,y+i,bc);
								}
							}
						for(j=0;j<8;j++) 
							{
						    	if(hz24[k].Msk[i*3+1]&(0x80>>j))	Gui_DrawPoint(x+j+8,y+i,fc);
								else {
									if (fc!=bc) Gui_DrawPoint(x+j+8,y+i,bc);
								}
							}
						for(j=0;j<8;j++) 
							{
						    	if(hz24[k].Msk[i*3+2]&(0x80>>j))	
								Gui_DrawPoint(x+j+16,y+i,fc);
								else 
								{
									if (fc!=bc) Gui_DrawPoint(x+j+16,y+i,bc);
								}
							}
				    }
			  }
			}
			s+=2;x+=24;
		}
	}
}

/**************************************************************************************

函数功能: 显示32x32单个数字(使用STM32芯片片上存储的字库)
输入参数: x,y-显示起始位置；fc-前景色；bc-背景色；num-要显示的数字(0-9)
输出参数: 无

**************************************************************************************/

void Gui_DrawFont_Num32(uint16_t x, uint16_t y, uint16_t fc, uint16_t bc, uint16_t num)
{
	unsigned char i,j,k,c;
	//lcd_text_any(x+94+i*42,y+34,32,32,0x7E8,0x0,sz32,knum[i]);
//	w=w/8;

    for(i=0;i<32;i++)
	{
		for(j=0;j<4;j++) 
		{
			c=*(sz32+num*32*4+i*4+j);
			for (k=0;k<8;k++)	
			{
	
		    	if(c&(0x80>>k))	Gui_DrawPoint(x+j*8+k,y+i,fc);
				else {
					if (fc!=bc) Gui_DrawPoint(x+j*8+k,y+i,bc);
				}
			}
		}
	}
}

//**************************************************************************************/

// 数值显示函数实现

//**************************************************************************************/

/**************************************************************************************

函数功能: 在指定位置显示一个ASCII字符(使用8x16字体)
输入参数: x,y-显示起始位置；fc-前景色；bc-背景色；ch-要显示的ASCII字符
输出参数: 无

**************************************************************************************/
void Gui_DrawChar_Ascii(uint16_t x, uint16_t y, uint16_t fc, uint16_t bc, char ch)
{
	unsigned char i, j, k;
	
	if (ch >= 32) 
		k = ch - 32; 
	else 
		k = 0;
	
	for (i = 0; i < 16; i++)
	{
		for (j = 0; j < 8; j++)
		{
			if (asc16[k * 16 + i] & (0x80 >> j))
				Gui_DrawPoint(x + j, y + i, fc);
			else
			{
				if (fc != bc) Gui_DrawPoint(x + j, y + i, bc);
			}
		}
	}
}

/**************************************************************************************

函数功能: 在指定位置显示有符号32位整数
输入参数: x,y-显示起始位置；fc-前景色；bc-背景色；num-要显示的数值；len-最小字段宽度(不足时左侧补空格)
输出参数: 无
调用示例: Gui_DrawNum_Int(10, 20, RED, BLACK, -12345, 8);
          // 显示"-12345  " 共8位

**************************************************************************************/
void Gui_DrawNum_Int(uint16_t x, uint16_t y, uint16_t fc, uint16_t bc, int32_t num, uint8_t len)
{
	char buf[12];
	uint8_t i = 0, actual_len = 0;
	uint8_t is_negative = 0;
	
	if (num < 0)
	{
		is_negative = 1;
		num = -num;
	}
	
	// 转换数字为字符串(逆序)
	if (num == 0)
	{
		buf[i++] = '0';
	}
	else
	{
		while (num > 0)
		{
			buf[i++] = '0' + (num % 10);
			num /= 10;
		}
	}
	actual_len = i + is_negative;
	
	// 左侧补空格(右对齐)
	if (len > actual_len)
	{
		uint8_t pad = len - actual_len;
		uint8_t p;
		for (p = 0; p < pad; p++)
		{
			Gui_DrawChar_Ascii(x, y, fc, bc, ' ');
			x += 8;
		}
	}
	
	// 显示负号
	if (is_negative)
	{
		Gui_DrawChar_Ascii(x, y, fc, bc, '-');
		x += 8;
	}
	
	// 显示数字(逆序输出)
	while (i > 0)
	{
		i--;
		Gui_DrawChar_Ascii(x, y, fc, bc, buf[i]);
		x += 8;
	}
}

/**************************************************************************************

函数功能: 在指定位置显示无符号32位整数
输入参数: x,y-显示起始位置；fc-前景色；bc-背景色；num-要显示的数值；len-最小字段宽度(不足时左侧补空格)
输出参数: 无
调用示例: Gui_DrawNum_Uint(10, 20, RED, BLACK, 65535, 6);
          // 显示" 65535" 共6位

**************************************************************************************/
void Gui_DrawNum_Uint(uint16_t x, uint16_t y, uint16_t fc, uint16_t bc, uint32_t num, uint8_t len)
{
	char buf[11];
	uint8_t i = 0, actual_len;
	
	// 转换数字为字符串(逆序)
	if (num == 0)
	{
		buf[i++] = '0';
	}
	else
	{
		while (num > 0)
		{
			buf[i++] = '0' + (num % 10);
			num /= 10;
		}
	}
	actual_len = i;
	
	// 左侧补空格(右对齐)
	if (len > actual_len)
	{
		uint8_t pad = len - actual_len;
		uint8_t p;
		for (p = 0; p < pad; p++)
		{
			Gui_DrawChar_Ascii(x, y, fc, bc, ' ');
			x += 8;
		}
	}
	
	// 显示数字(逆序输出)
	while (i > 0)
	{
		i--;
		Gui_DrawChar_Ascii(x, y, fc, bc, buf[i]);
		x += 8;
	}
}

/**************************************************************************************

函数功能: 在指定位置显示浮点数
输入参数: x,y-显示起始位置；fc-前景色；bc-背景色；num-要显示的浮点数；
          decimal_places-小数位数；len-最小字段宽度(不足时左侧补空格)
输出参数: 无
调用示例: Gui_DrawNum_Float(10, 20, RED, BLACK, 3.14159, 2, 8);
          // 显示"    3.14" 共8位(含小数点)

**************************************************************************************/
void Gui_DrawNum_Float(uint16_t x, uint16_t y, uint16_t fc, uint16_t bc, float num, uint8_t decimal_places, uint8_t len)
{
	uint8_t is_negative = 0;
	uint32_t int_part;
	uint32_t frac_part;
	uint8_t actual_len;
	char buf[12];
	uint8_t buf_len;
	uint8_t dp;
	
	if (num < 0)
	{
		is_negative = 1;
		num = -num;
	}
	
	// 计算小数部分
	frac_part = 1;
	for (dp = 0; dp < decimal_places; dp++)
		frac_part *= 10;
	
	int_part = (uint32_t)num;
	frac_part = (uint32_t)((num - int_part) * frac_part + 0.5f);
	
	// 处理进位(如 3.999 显示1位小数时为4.0)
	if (frac_part >= (uint32_t)(1))
	{
		uint32_t threshold = 1;
		for (dp = 0; dp < decimal_places; dp++)
			threshold *= 10;
		if (frac_part >= threshold)
		{
			int_part += 1;
			frac_part = 0;
		}
	}
	
	// 计算实际长度
	actual_len = is_negative + decimal_places + (decimal_places > 0 ? 1 : 0); // 符号 + 小数位 + 小数点
	{
		uint32_t temp = int_part;
		if (temp == 0) actual_len++;
		else
		{
			while (temp > 0) { actual_len++; temp /= 10; }
		}
	}
	
	// 左侧补空格(右对齐)
	if (len > actual_len)
	{
		uint8_t pad = len - actual_len;
		uint8_t p;
		for (p = 0; p < pad; p++)
		{
			Gui_DrawChar_Ascii(x, y, fc, bc, ' ');
			x += 8;
		}
	}
	
	// 显示负号
	if (is_negative)
	{
		Gui_DrawChar_Ascii(x, y, fc, bc, '-');
		x += 8;
	}
	
	// 显示整数部分
	if (int_part == 0)
	{
		Gui_DrawChar_Ascii(x, y, fc, bc, '0');
		x += 8;
	}
	else
	{
		buf_len = 0;
		while (int_part > 0)
		{
			buf[buf_len++] = '0' + (int_part % 10);
			int_part /= 10;
		}
		while (buf_len > 0)
		{
			buf_len--;
			Gui_DrawChar_Ascii(x, y, fc, bc, buf[buf_len]);
			x += 8;
		}
	}
	
	// 显示小数点和小数部分
	if (decimal_places > 0)
	{
		Gui_DrawChar_Ascii(x, y, fc, bc, '.');
		x += 8;
		
		buf_len = 0;
		for (dp = 0; dp < decimal_places; dp++)
		{
			buf[buf_len++] = '0' + (frac_part % 10);
			frac_part /= 10;
		}
		while (buf_len > 0)
		{
			buf_len--;
			Gui_DrawChar_Ascii(x, y, fc, bc, buf[buf_len]);
			x += 8;
		}
	}
}

//**************************************************************************************/

// End of GUI.c - GUI Drawing Functions Implementation

//**************************************************************************************/
