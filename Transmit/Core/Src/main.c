/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "adc.h"
#include "dac.h"
#include "dma.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

#include "arm_math.h"
#include <stdio.h>
#include <string.h>
#include "ask_tx.h"

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define Carrier_Freq 	  50000
#define Baud_rate		  10000
#define SINE_POINT_NUM    50
#define Single_Byte       8
#define Single_Bit        1
#ifndef PI
#define PI                3.14159265358979f
#endif

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
static uint16_t sinx[SINE_POINT_NUM];     // carrier ON: sine wave + DC offset
static uint16_t zero[SINE_POINT_NUM];     // carrier OFF: DC level only
//uint8_t tail[4]={0,0,0,0};	//CRC:0000
uint8_t send[2*Single_Byte];	//
uint8_t Key[Single_Byte];	//data left->right:D7~D0
volatile uint8_t signal = 0;	// ASK symbol: 1=carrier on, 0=carrier off (mirrors DMA src)
//uint16_t adc_buff[2048];
extern TIM_HandleTypeDef htim2;
extern TIM_HandleTypeDef htim3;
extern DAC_HandleTypeDef hdac2;
extern DMA_HandleTypeDef hdma_dac2_ch1;  // actual DMA handle for DAC2, defined in dac.c

/*--- UART command line buffer (one byte at a time from the ring buffer) ----*/
static char     cmd_line[64];
static uint8_t  cmd_len = 0;

/*--- Audio DEMO state ------------------------------------------------------*/
static uint8_t  audio_demo_active = 0;    /* 1 = stream AUDIO frames       */
static uint32_t audio_last_send_tick = 0;

/*--- Frame TX state: pause idle filling while frame is being sent ----------*/
static volatile uint8_t tx_frame_pending = 0;  /* 1 = frame in FIFO, waiting to be sent */
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
void uWavetable_Init(void);
static void ProcessCommand(char *line);
static void HandleAudioDemo(void);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_ADC1_Init();
  MX_TIM3_Init();
  MX_LPUART1_UART_Init();
  MX_DAC1_Init();
  MX_TIM2_Init();
  MX_DAC2_Init();
  MX_TIM6_Init();
  /* USER CODE BEGIN 2 */

	// --- Pre-compute carrier ON/OFF wave tables (DC offset: 1.2V, amplitude: 1Vrms) ---
	uWavetable_Init();

	// --- Initialise ASK TX bit FIFO ---
	ASK_TX_Init();

	// --- TIM6: trigger DAC2 via TRGO, f_TIM6 = 85MHz/(PSC+1)/(ARR+1) = 85MHz/17/2 = 2.5MHz ---
	// --- Carrier frequency = 2.5MHz / SINE_POINT_NUM = 50kHz ---
    HAL_TIM_Base_Start(&htim6);

	// --- TIM3: trigger ADC1 at ~10 kHz for the audio DEMO (PA2 mic input) ---
	HAL_TIM_Base_Start(&htim3);

	// --- TIM2: ASK bit-rate clock, f_TIM2 = 85MHz/850/100 = 1kHz ---
	HAL_TIM_Base_Start_IT(&htim2);

	// --- DAC2 DMA circular mode: start with carrier OFF (idle), TIM2 ISR swaps buffer ---
	HAL_DAC_Start_DMA(&hdac2, DAC2_CHANNEL_1, (uint32_t *)zero, SINE_POINT_NUM, DAC_ALIGN_12B_R);

	// --- 启动 USART1 中断接收 ---
	UART_ReceiveInit();

	// --- Test UART TX: send startup message directly ---
	{
		uint8_t msg[] = "TX START\r\n";
		HAL_StatusTypeDef status = HAL_UART_Transmit(&hlpuart1, msg, sizeof(msg)-1, 1000);
		/* If error, blink LED to indicate */
		if (status != HAL_OK)
		{
			while(1)
			{
				HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_5);
				HAL_Delay(200);
			}
		}
	}

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
		// --- 1. 读取上位机串口命令 (line-based, '\n' terminated) ---
		while (UART_RxAvailable() > 0)
		{
			char c = (char)UART_GetRxByte();
			if (c == '\r') continue;            /* ignore CR */
			if (c == '\n')
			{
				cmd_line[cmd_len] = '\0';
				if (cmd_len > 0)
					ProcessCommand(cmd_line);
				cmd_len = 0;
			}
			else
			{
				if (cmd_len < (uint8_t)(sizeof(cmd_line) - 1))
					cmd_line[cmd_len++] = c;
			}
		}

		// --- 2. Auto-fill FIFO with idle 0x55 (Manchester-encoded, only when no frame pending) ---
		// --- Essential for: break detection, scope GAP visibility, bit sync ---
		if (!tx_frame_pending && ASK_TX_FIFO_Count() < 400)
		{
			ASK_TX_PushByte(0x55);
		}

		// --- 2b. Clear frame pending flag when FIFO is nearly empty ---
		// --- Manchester: each data byte = 16 symbols, threshold scaled up ---
		if (tx_frame_pending && ASK_TX_FIFO_Count() < 100)
		{
			tx_frame_pending = 0;
		}

		// --- 3. Audio DEMO: 每 100 ms 发送一帧 AUDIO ---
		if (audio_demo_active)
			HandleAudioDemo();

    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1_BOOST);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = RCC_PLLM_DIV4;
  RCC_OscInitStruct.PLL.PLLN = 85;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
  RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */
/*---------------------------------------*/
// --- printf重定向到USART1 (Keil MicroLib) ---
#ifdef __CC_ARM                  /* Keil MDK ARM Compiler */
#include <stdio.h>

/* MicroLib uses fputc() for printf retarget */
struct __FILE { int handle; };
FILE __stdout;
FILE __stdin;

int fputc(int ch, FILE *f)
{
    (void)f;
    HAL_UART_Transmit(&hlpuart1, (uint8_t *)&ch, 1, HAL_MAX_DELAY);
    return ch;
}

int fgetc(FILE *f)
{
    (void)f;
    uint8_t ch;
    HAL_UART_Receive(&hlpuart1, &ch, 1, HAL_MAX_DELAY);
    return ch;
}
#else
/* GCC/newlib uses __io_putchar */
int __io_putchar(int ch)
{
    HAL_UART_Transmit(&hlpuart1, (uint8_t *)&ch, 1, HAL_MAX_DELAY);
    return ch;
}
#endif
/*---------------------------------------*/
// --- Pre-compute carrier ON and OFF wave tables ---
// --- DC offset: 1.2V, Amplitude: 1Vrms, DAC 12-bit: 0-4095 ---
void uWavetable_Init(void){
    for (int i = 0; i < SINE_POINT_NUM; i++)
    {
        sinx[i] = (uint16_t)((sin(i * 2 * PI / SINE_POINT_NUM) + 2.0f) * 1200);
        zero[i] = (uint16_t)(2.0f * 1200);  // DC offset only, no carrier
    }
}

// --- TIM2 period elapsed: ASK bit-rate (10 kHz) ---
// --- Pop bit from FIFO, swap DAC2 DMA source accordingly ---
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim){
    if(htim->Instance == TIM2){
        uint8_t next_bit = ASK_TX_FIFO_PopBit();
        signal = next_bit;

        __HAL_DMA_DISABLE(&hdma_dac2_ch1);
        while(hdma_dac2_ch1.Instance->CCR & DMA_CCR_EN);  /* wait for DMA stop */
        hdma_dac2_ch1.Instance->CMAR = (uint32_t)(next_bit ? sinx : zero);
        hdma_dac2_ch1.Instance->CNDTR = SINE_POINT_NUM;
        __HAL_DMA_ENABLE(&hdma_dac2_ch1);
    }
}

/*---------------------------------------*/
// --- UART command parser (Step 1 + Debug) ---
//   H  -> show FIFO status
//   Z  -> push raw 0x55 byte (debug)
//   G  -> send GAP test pattern (48x0 + 48x1 + 48x0) for scope verification
//   D  -> show frame encoding demo (no actual send)
//   R<data> -> send RAW frame + show encoding
//   T<string> -> send TEXT frame + show encoding
/*---------------------------------------------------------------------------*/
static void ProcessCommand(char *line)
{
    char cmd = line[0];
    char *arg = &line[1];
    size_t arglen = strlen(arg);

    switch (cmd)
    {
    case 'H':
    case 'h':
        /* Direct test: send without printf */
        HAL_UART_Transmit(&hlpuart1, (uint8_t *)"OK-H\r\n", 6, 1000);
        break;

    case 'Z':
    case 'z':
        ASK_TX_PushByte(0x55);
        printf("Pushed raw byte 0x55 (01010101)\r\n");
        break;

    case 'G':
    case 'g':
        /* GAP test: 48 zero bits + 48 one bits + 48 zero bits
         * Scope should show: 4.8ms OFF -> 4.8ms ON -> 4.8ms OFF */
        {
            uint16_t i;
            ASK_TX_FIFO_Clear();
            tx_frame_pending = 1;
            for (i = 0; i < 48; i++) ASK_TX_PushByte(0x00);  /* 48 bytes of 0x00 = 384 zero bits */
            for (i = 0; i < 48; i++) ASK_TX_PushByte(0xFF);  /* 48 bytes of 0xFF = 384 one bits */
            for (i = 0; i < 48; i++) ASK_TX_PushByte(0x00);  /* 48 bytes of 0x00 = 384 zero bits */
            printf("GAP test: 38.4ms OFF + 38.4ms ON + 38.4ms OFF\r\n");
        }
        break;

    case 'D':
    case 'd':
        /* Demo: Show frame encoding without actually sending */
        printf("\r\n=== Encoding Demo ===\r\n");
        ASK_TX_PrintFrameDebug(ASK_TYPE_RAW, (const uint8_t *)"Hello", 5);
        break;

    case 'R':
    case 'r':
        /* Send RAW frame */
        if (arglen == 0) return;
        if (arglen > ASK_MAX_PAYLOAD) arglen = ASK_MAX_PAYLOAD;
        ASK_TX_FIFO_Clear();
        if (ASK_TX_SendFrame(ASK_TYPE_RAW, (const uint8_t *)arg, (uint16_t)arglen) == 0)
        {
            tx_frame_pending = 1;
            printf("R%u OK\r\n", arglen);
        }
        else
            printf("R FULL\r\n");
        break;

    case 'T':
    case 't':
        /* Send TEXT frame */
        if (arglen == 0) return;
        if (arglen > ASK_MAX_PAYLOAD) arglen = ASK_MAX_PAYLOAD;
        ASK_TX_FIFO_Clear();
        if (ASK_TX_SendFrame(ASK_TYPE_TEXT, (const uint8_t *)arg, (uint16_t)arglen) == 0)
        {
            tx_frame_pending = 1;
            printf("T%u OK\r\n", arglen);
        }
        else
            printf("T FULL\r\n");
        break;

    case 'M':
    case 'm':
        /* Minimal frame test: 10x frame with 1-byte payload */
        {
            uint8_t n;
            for (n = 0; n < 10; n++)
            {
                ASK_TX_FIFO_Clear();
                tx_frame_pending = 1;
                ASK_TX_SendFrame(ASK_TYPE_RAW, (const uint8_t *)"A", 1);
                /* Wait for FIFO to drain (frame fully sent) before next */
                while (ASK_TX_FIFO_Count() > 10) {}
                HAL_Delay(500);  /* 500ms silence between frames */
            }
            printf("M x10 done\r\n");
        }
        break;

    case 'F':
    case 'f':
        /* Frame header only test: 10x header (TYPE+LEN+CRC, no payload) */
        {
            uint8_t n;
            for (n = 0; n < 10; n++)
            {
                ASK_TX_FIFO_Clear();
                tx_frame_pending = 1;
                ASK_TX_SendFrame(ASK_TYPE_RAW, NULL, 0);
                while (ASK_TX_FIFO_Count() > 10) {}
                HAL_Delay(1000);  /* 1s silence - verify long-term bit sync */
            }
            printf("F x10 done\r\n");
        }
        break;

    case 'V':
    case 'v':
        /* Scope verify: send frame with payload 0x55
         * 0x55 = 01010101 alternating pattern - any bit shift produces
         * a different value, making PAY alignment errors clearly visible */
        ASK_TX_FIFO_Clear();
        tx_frame_pending = 1;
        {
            uint8_t v = 0x55;
            ASK_TX_SendFrame(ASK_TYPE_RAW, &v, 1);
        }
        printf("V: 0x55 sent\r\n");
        break;

    default:
        printf("H Z G D R<data> T<string> M F V=scope\r\n");
        break;
    }
}

/*---------------------------------------*/
// --- Audio DEMO disabled for testing ---
/*---------------------------------------------------------------------------*/
static void HandleAudioDemo(void)
{
    /* Audio demo disabled for debugging
    uint32_t now = HAL_GetTick();
    if ((now - audio_last_send_tick) >= 100u)
    {
        uint8_t level = audio_level;
        ASK_TX_SendFrame(ASK_TYPE_AUDIO, &level, 1);
        audio_last_send_tick = now;
    }
    */
}
/*---------------------------------------*/
/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
