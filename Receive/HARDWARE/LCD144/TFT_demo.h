#ifndef __TFT_demo_H
#define __TFT_demo_H 

/**************************************************************************************/

// TFT Demo Functions - Test and demonstration routines

/**************************************************************************************/

void Redraw_Mainmenu(void);
void Num_Test(void);
void Font_Test(void);
void Color_Test(void);
void showimage(const unsigned char *p);
void showimage_single(const unsigned char *p, uint16_t x, uint16_t y, uint16_t w, uint16_t h);
void Fullscreen_showimage(const unsigned char *p);
void Test_Demo(void);

#endif

/**************************************************************************************/

// End of TFT_demo.h - Demo Function Declarations

/**************************************************************************************/
