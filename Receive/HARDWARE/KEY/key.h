#ifndef _KEY_H
#define _KEY_H

#include "main.h"
#include "led.h"
//////////////////////////////////////////////////////////////////////////////////	 

//KEY驱动代码	   

//STM32G474 工程模板-HAL库函数版本
//DevEBox  大越创新
//淘宝店铺：mcudev.taobao.com
//淘宝店铺：devebox.taobao.com	

//////////////////////////////////////////////////////////////////////////////////


#define KEY1        HAL_GPIO_ReadPin(GPIOA,GPIO_PIN_0)  //KEY1 按键 PA0
#define KEY2        HAL_GPIO_ReadPin(GPIOA,GPIO_PIN_1)  //KEY2 按键 PA1


#define KEY_RESET	  0	//KEY  无按键状态值

#define KEY1_PRES	  1	//KEY1按下后返回值
#define KEY2_PRES	  2	//KEY2按下后返回值



extern uint8_t KEY_value;


void KEY_Init(void);  //按键IO初始化函数

uint8_t KEY_Scan(uint8_t mode); //按键扫描函数

void SCAN_KEY_Test(void);  // 按键 IO  扫描测试函数

void EXTI_KEY_Test(void);  // 按键 外部IO 中断测试函数

#endif











//STM32G474 工程模板-HAL库函数版本
//DevEBox  大越创新
//淘宝店铺：mcudev.taobao.com
//淘宝店铺：devebox.taobao.com	





