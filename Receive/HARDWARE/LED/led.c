#include "led.h"
//////////////////////////////////////////////////////////////////////////////////	 

// LED Driver Implementation - DevEBox onboard LEDs

// STM32G474 HAL Library Version
// Source: DevEBox (mcudev.taobao.com)
////////////////////////////////////////////////////////////////////////////////// 	

// Initialize LED GPIOs: PC13 (LED1), PD2 (LED2), both active low
void LED_Init(void)
{
    GPIO_InitTypeDef GPIO_Initure;
	
    __HAL_RCC_GPIOC_CLK_ENABLE();					          // Enable GPIOC clock
	  __HAL_RCC_GPIOD_CLK_ENABLE();					          // Enable GPIOD clock
	
    GPIO_Initure.Pin=GPIO_PIN_13;			              // PC13 - LED1
    GPIO_Initure.Mode=GPIO_MODE_OUTPUT_PP;  		    
    GPIO_Initure.Pull=GPIO_PULLUP;         			    
    GPIO_Initure.Speed=GPIO_SPEED_FREQ_VERY_HIGH;  	
    HAL_GPIO_Init(GPIOC,&GPIO_Initure);     	     	// Init PC13
	
	  GPIO_Initure.Pin=GPIO_PIN_2;			              // PD2 - LED2
    GPIO_Initure.Mode=GPIO_MODE_OUTPUT_PP;  		    
    GPIO_Initure.Pull=GPIO_PULLUP;         			    
    GPIO_Initure.Speed=GPIO_SPEED_FREQ_VERY_HIGH;  	
    HAL_GPIO_Init(GPIOD,&GPIO_Initure);     	     	// Init PD2
	   
	  HAL_GPIO_WritePin(GPIOC,GPIO_PIN_13,GPIO_PIN_SET);	// LED1 off (active low)
	  HAL_GPIO_WritePin(GPIOD,GPIO_PIN_2,GPIO_PIN_SET);	// LED2 off (active low)
	
}

// STM32G474 HAL Library Version
// Source: DevEBox (mcudev.taobao.com)

