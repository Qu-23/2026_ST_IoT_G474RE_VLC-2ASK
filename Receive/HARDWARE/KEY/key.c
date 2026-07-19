

#include "key.h"


//////////////////////////////////////////////////////////////////////////////////	 

//KEY驱动代码	   


//STM32G474 工程模板-HAL库函数版本
//DevEBox  大越创新
//淘宝店铺：mcudev.taobao.com
//淘宝店铺：devebox.taobao.com								  
//////////////////////////////////////////////////////////////////////////////////


/************************************************************************************/

//按键初始化函数


//STM32G474 工程模板-HAL库函数版本
//DevEBox  大越创新
//淘宝店铺：mcudev.taobao.com
//淘宝店铺：devebox.taobao.com	

//在  MX_GPIO_Init();  中已经初始化过IO，使用外部中断方式获取 IO的状态，此函数不再使用

/************************************************************************************/

void KEY_Init(void)
{
    GPIO_InitTypeDef GPIO_Initure;

	  
    __HAL_RCC_GPIOA_CLK_ENABLE();           //开启GPIOA时钟
    

    
	  GPIO_Initure.Pin=GPIO_PIN_0;            //PA0  :对应 K1
    GPIO_Initure.Mode=GPIO_MODE_INPUT;      //输入
    GPIO_Initure.Pull=GPIO_PULLUP;          //上拉
    GPIO_Initure.Speed=GPIO_SPEED_FREQ_VERY_HIGH;     //高速
    HAL_GPIO_Init(GPIOA,&GPIO_Initure);
	

		
    

}


/************************************************************************************/

//按键处理函数
//返回按键值
//mode:0,不支持连续按;1,支持连续按;
//0，没有任何按键按下
//1，WKUP按下 WK_UP
//注意此函数有响应优先级,KEY1>KEY2


//STM32G474 工程模板-HAL库函数版本
//DevEBox  大越创新
//淘宝店铺：mcudev.taobao.com
//淘宝店铺：devebox.taobao.com	


/************************************************************************************/

static uint8_t key_up=1;     //按键松开标志

uint8_t KEY_Scan(uint8_t mode)
{
    
    if(mode==1)key_up=1;    //支持连按
    if(key_up&&KEY1==0)
    {
        delay_ms(10);
        key_up=0;
        if(KEY1==0)  return KEY1_PRES;
        
        
    }
		else if(KEY1==1)key_up=1;
		
    return 0;   //无按键按下
}


/************************************************************************************/

//按键测试程序，放主循环查询按键键值


//STM32G474 工程模板-HAL库函数版本
//DevEBox  大越创新
//淘宝店铺：mcudev.taobao.com
//淘宝店铺：devebox.taobao.com	


/************************************************************************************/

uint8_t key;

void KEY_Test(void)
{

		key=KEY_Scan(0); 		//得到键值 ---屏蔽，不使用扫描方式获取键值
		
	  if(key)
		{						   
			switch(key)
			{	
				case KEY1_PRES:	//控制LED2点亮
				  
				  LED_D1_Toggle; // LED灯状态 翻转状态
				
				  printf("\r\n KEY1 按键按下一次 \r\n");
					break;

				case KEY2_PRES:	//控制LED2翻转	 
				 
				  LED_D2_Toggle; // LED灯状态 翻转状态
				
				  printf("\r\n KEY2 按键按下一次 \r\n");
				
					break;

			}
			
		}
		else 
			delay_ms(10); 

	}

	
	
	
	
	
	
/***************************************************************************************/

//中断服务程序中需要做的事情
//在HAL库中所有的外部中断服务函数都会调用此函数
//GPIO_Pin:中断引脚号

//STM32G474 工程模板-HAL库函数版本
//DevEBox  大越创新
//淘宝店铺：mcudev.taobao.com
//淘宝店铺：devebox.taobao.com			

/***************************************************************************************/

uint8_t KEY_value;

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    	 		
    delay_ms(20);   //消抖，此处为了方便使用了延时函数，实际代码中禁止在中断服务函数中调用任何delay之类的延时函数！！！ 
	
    switch(GPIO_Pin)
    {
        case GPIO_PIN_0:
            if(KEY1==0) 	//控制-获取键值
            {
              KEY_value=KEY1_PRES;					
            };
            break;
         case GPIO_PIN_1:
            if(KEY2==0) 	//控制-获取键值
            {
              KEY_value=KEY2_PRES;					
            };
            break;

    }
}	
	

/***************************************************************************************/

// 使用中断方式获取键值  测试外部中断按键状态测试

//STM32G474 工程模板-HAL库函数版本
//DevEBox  大越创新
//淘宝店铺：mcudev.taobao.com
//淘宝店铺：devebox.taobao.com			

/***************************************************************************************/
	
void EXTI_KEY_Test(void)
{

				
	  if(KEY_value!=KEY_RESET)
		{						   
			switch(KEY_value)
			{	
				case KEY1_PRES:	//控制LED2点亮
					
				  LED_D1_Toggle; // LED灯状态 翻转状态
				
				  printf("\r\n KEY1 按键按下一次 \r\n");
				
				  KEY_value = KEY_RESET;// 清除键值标记
					break;

				case KEY2_PRES:	//控制LED2翻转	 
					
	 				LED_D2_Toggle; // LED灯状态 翻转状态
				
				  printf("\r\n KEY2 按键按下一次 \r\n");
				
				  KEY_value = KEY_RESET;// 清除键值标记
				
					break;

			}

		}
		else 
			delay_ms(10); 

	}	
	
	
	

//STM32G474 工程模板-HAL库函数版本
//DevEBox  大越创新
//淘宝店铺：mcudev.taobao.com
//淘宝店铺：devebox.taobao.com	












