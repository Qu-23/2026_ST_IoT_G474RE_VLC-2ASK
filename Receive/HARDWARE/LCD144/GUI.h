#ifndef __GUI_H
#define __GUI_H

#include "stdint.h"

//********************************************************************************

// GUI Drawing Functions for LCD
// Includes: circle, line, box, button, font display

//********************************************************************************

// Color conversion: BGR to RGB565 format
// Drawing primitives and UI elements

uint16_t LCD_BGR2RGB(uint16_t c);
void Gui_Circle(uint16_t X,uint16_t Y,uint16_t R,uint16_t fc); 
void Gui_DrawLine(uint16_t x0, uint16_t y0,uint16_t x1, uint16_t y1,uint16_t Color);  
void Gui_box(uint16_t x, uint16_t y, uint16_t w, uint16_t h,uint16_t bc);
void Gui_box2(uint16_t x,uint16_t y,uint16_t w,uint16_t h, uint8_t mode);
void DisplayButtonDown(uint16_t x1,uint16_t y1,uint16_t x2,uint16_t y2);
void DisplayButtonUp(uint16_t x1,uint16_t y1,uint16_t x2,uint16_t y2);
void Gui_DrawFont_F16(uint16_t x, uint16_t y, uint16_t fc, uint16_t bc, uint8_t *s);
void Gui_DrawFont_F24(uint16_t x, uint16_t y, uint16_t fc, uint16_t bc, uint8_t *s);
void Gui_DrawFont_Num32(uint16_t x, uint16_t y, uint16_t fc, uint16_t bc, uint16_t num) ;

// 数值显示函数(使用8x16 ASCII字体)
void Gui_DrawChar_Ascii(uint16_t x, uint16_t y, uint16_t fc, uint16_t bc, char ch);
void Gui_DrawNum_Int(uint16_t x, uint16_t y, uint16_t fc, uint16_t bc, int32_t num, uint8_t len);
void Gui_DrawNum_Uint(uint16_t x, uint16_t y, uint16_t fc, uint16_t bc, uint32_t num, uint8_t len);
void Gui_DrawNum_Float(uint16_t x, uint16_t y, uint16_t fc, uint16_t bc, float num, uint8_t decimal_places, uint8_t len);

#endif

//********************************************************************************

// End of GUI.h - GUI Drawing Function Declarations

//********************************************************************************
