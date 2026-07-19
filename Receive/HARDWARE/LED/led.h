#ifndef _LED_H
#define _LED_H

#include "main.h"

//////////////////////////////////////////////////////////////////////////////////	 

// LED Driver - DevEBox onboard LED definitions

// STM32G474 HAL Library Version
// Source: DevEBox (mcudev.taobao.com)
////////////////////////////////////////////////////////////////////////////////// 	

// LED1: PC13, LED2: PD2
// Macros for on/off and toggle

#define LED_D1(n)		   (n?HAL_GPIO_WritePin(GPIOC,GPIO_PIN_13,GPIO_PIN_SET):HAL_GPIO_WritePin(GPIOC,GPIO_PIN_13,GPIO_PIN_RESET))
#define LED_D1_Toggle  (HAL_GPIO_TogglePin(GPIOC,GPIO_PIN_13))                                    //LED1

#define LED_D2(n)		   (n?HAL_GPIO_WritePin(GPIOD,GPIO_PIN_2,GPIO_PIN_SET):HAL_GPIO_WritePin(GPIOD,GPIO_PIN_2,GPIO_PIN_RESET))
#define LED_D2_Toggle  (HAL_GPIO_TogglePin(GPIOD,GPIO_PIN_2))                                    //LED2

void LED_Init(void); //LED initialization

#endif

/**************************************************************************************/

// End of led.h - LED Driver Definitions (HAL library)

/**************************************************************************************/
