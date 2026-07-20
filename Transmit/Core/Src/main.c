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

/*============================================================================*
 *                      UART command-line helpers                             *
 *============================================================================*/

/* Print command list (used at boot and on 'C' command) */
static void PrintCommandList(void)
{
    printf("\r\n=== 2ASK TX Commands ===\r\n");
    printf("H         Link check (reply OK-H)\r\n");
    printf("R<data>   RAW frame (ASCII payload, e.g. RHello)\r\n");
    printf("R0x..     RAW frame, HEX bytes  (e.g. R0xAABBCC)\r\n");
    printf("R0b..     RAW frame, BIN bits   (e.g. R0b01010101)\r\n");
    printf("T<string> TEXT frame (ASCII + UTF-8 Chinese, auto GB2312)\r\n");
    printf("V         Scope test frame (payload=0x55)\r\n");
    printf("M<n>      Send predefined GB2312 Chinese TEXT frame (n=1..5)\r\n");
    printf("C         Show this command list\r\n");
    printf("========================\r\n");
}

/* Parse hex string "AABBCC" -> bytes {0xAA,0xBB,0xCC}.
 * Returns number of bytes parsed (0 on error / empty). */
static uint16_t ParseHexBytes(const char *s, uint8_t *out, uint16_t max)
{
    uint16_t n = 0;
    while (s[0] && s[1] && n < max)
    {
        int hi = -1, lo = -1;
        char c1 = s[0], c2 = s[1];
        if (c1 >= '0' && c1 <= '9') hi = c1 - '0';
        else if (c1 >= 'a' && c1 <= 'f') hi = c1 - 'a' + 10;
        else if (c1 >= 'A' && c1 <= 'F') hi = c1 - 'A' + 10;
        if (c2 >= '0' && c2 <= '9') lo = c2 - '0';
        else if (c2 >= 'a' && c2 <= 'f') lo = c2 - 'a' + 10;
        else if (c2 >= 'A' && c2 <= 'F') lo = c2 - 'A' + 10;
        if (hi < 0 || lo < 0) break;            /* invalid char -> stop */
        out[n++] = (uint8_t)((hi << 4) | lo);
        s += 2;
    }
    return n;
}

/* Parse binary string "01010101" -> bytes {0x55}.
 * Bits are packed MSB-first; trailing bits (<8) are dropped.
 * Returns number of bytes parsed. */
static uint16_t ParseBinBytes(const char *s, uint8_t *out, uint16_t max)
{
    uint16_t n = 0;
    uint8_t  byte = 0;
    uint8_t  bit_count = 0;
    while (*s && n < max)
    {
        if (*s == '0' || *s == '1')
        {
            byte = (uint8_t)((byte << 1) | (*s - '0'));
            if (++bit_count == 8)
            {
                out[n++] = byte;
                byte = 0;
                bit_count = 0;
            }
        }
        /* silently skip non-binary chars (spaces, etc.) */
        s++;
    }
    return n;
}

/* Print payload bytes as "Enc: 0xHH 0xHH ..." for answer-back verification */
static void PrintEncodedBytes(const uint8_t *buf, uint16_t len)
{
    uint16_t i;
    printf("Enc:");
    for (i = 0; i < len; i++)
        printf(" 0x%02X", buf[i]);
    printf("\r\n");
}

/*============================================================================*
 *          Predefined GB2312 Chinese messages for M command                  *
 *                                                                           *
 * 串口工具以 UTF-8 发送汉字会变成 3 字节/字，而 RX 端字库用 GB2312（2 字节/字）。
 * 这里在源码中用 GB2312 转义序列预定义，通过 M 命令发送即可避免编码错位。       *
 * 选字范围限于 RX 端 31 字字库（光通信可见接收发送号链路正常异文本图像音频    *
 * 调试待机数据帧错对率）。                                                   *
 *============================================================================*/
static const uint8_t msg_zh1[] = "\xB9\xE2\xCD\xA8\xD0\xC5";             /* 光通信 */
static const uint8_t msg_zh2[] = "\xBD\xD3\xCA\xD5\xD5\xFD\xB3\xA3";     /* 接收正常 */
static const uint8_t msg_zh3[] = "\xB7\xA2\xCB\xCD\xCA\xFD\xBE\xDD";     /* 发送数据 */
static const uint8_t msg_zh4[] = "\xC1\xB4\xC2\xB7\xD5\xFD\xB3\xA3";     /* 链路正常 */
static const uint8_t msg_zh5[] = "\xB5\xF7\xCA\xD4\xBF\xC9\xBC\xFB";     /* 调试可见 */

static const uint8_t *const msg_zh_table[] = { msg_zh1, msg_zh2, msg_zh3, msg_zh4, msg_zh5 };
static const uint16_t  msg_zh_len[]   = { sizeof(msg_zh1)-1, sizeof(msg_zh2)-1,
                                          sizeof(msg_zh3)-1, sizeof(msg_zh4)-1,
                                          sizeof(msg_zh5)-1 };
#define MSG_ZH_COUNT  (sizeof(msg_zh_table) / sizeof(msg_zh_table[0]))

/*============================================================================*
 *          UTF-8 -> GB2312 transcoding for T command                         *
 *                                                                           *
 * 上位机串口工具默认 UTF-8 编码，每个汉字 3 字节；RX 端字库用 GB2312（2 字节）。
 * 这里在 TX 端自动把 UTF-8 汉字转成 GB2312 再发送，对上位机用户完全透明。
 * 映射表覆盖 RX 端 61 字字库（光通信可见接收发送号链路正常异文本图像音频
 * 调试待机数据帧错对率 + 系统设备硬件版本功能显示状态传输误码稳定无线应用
 * 按键切换模式成），其它汉字会被替换为 '?'。                              *
 *============================================================================*/
typedef struct {
    uint8_t utf8[3];   /* UTF-8 编码（3 字节） */
    uint8_t gb[2];     /* GB2312 编码（2 字节） */
} utf8_gb_t;

static const utf8_gb_t utf8_gb_table[] = {
    {{0xE5,0x85,0x89},{0xB9,0xE2}},  /* 光 */
    {{0xE9,0x80,0x9A},{0xCD,0xA8}},  /* 通 */
    {{0xE4,0xBF,0xA1},{0xD0,0xC5}},  /* 信 */
    {{0xE5,0x8F,0xAF},{0xBF,0xC9}},  /* 可 */
    {{0xE8,0xA7,0x81},{0xBC,0xFB}},  /* 见 */
    {{0xE6,0x8E,0xA5},{0xBD,0xD3}},  /* 接 */
    {{0xE6,0x94,0xB6},{0xCA,0xD5}},  /* 收 */
    {{0xE5,0x8F,0x91},{0xB7,0xA2}},  /* 发 */
    {{0xE9,0x80,0x81},{0xCB,0xCD}},  /* 送 */
    {{0xE5,0x8F,0xB7},{0xBA,0xC5}},  /* 号 */
    {{0xE9,0x93,0xBE},{0xC1,0xB4}},  /* 链 */
    {{0xE8,0xB7,0xAF},{0xC2,0xB7}},  /* 路 */
    {{0xE6,0xAD,0xA3},{0xD5,0xFD}},  /* 正 */
    {{0xE5,0xB8,0xB8},{0xB3,0xA3}},  /* 常 */
    {{0xE5,0xBC,0x82},{0xD2,0xEC}},  /* 异 */
    {{0xE6,0x96,0x87},{0xCE,0xC4}},  /* 文 */
    {{0xE6,0x9C,0xAC},{0xB1,0xBE}},  /* 本 */
    {{0xE5,0x9B,0xBE},{0xCD,0xBC}},  /* 图 */
    {{0xE5,0x83,0x8F},{0xCF,0xF1}},  /* 像 */
    {{0xE9,0x9F,0xB3},{0xD2,0xF4}},  /* 音 */
    {{0xE9,0xA2,0x91},{0xC6,0xB5}},  /* 频 */
    {{0xE8,0xB0,0x83},{0xB5,0xF7}},  /* 调 */
    {{0xE8,0xAF,0x95},{0xCA,0xD4}},  /* 试 */
    {{0xE5,0xBE,0x85},{0xB4,0xFD}},  /* 待 */
    {{0xE6,0x9C,0xBA},{0xBB,0xFA}},  /* 机 */
    {{0xE6,0x95,0xB0},{0xCA,0xFD}},  /* 数 */
    {{0xE6,0x8D,0xAE},{0xBE,0xDD}},  /* 据 */
    {{0xE5,0xB8,0xA7},{0xD6,0xA1}},  /* 帧 */
    {{0xE9,0x94,0x99},{0xB4,0xED}},  /* 错 */
    {{0xE5,0xAF,0xB9},{0xB6,0xD4}},  /* 对 */
    {{0xE7,0x8E,0x87},{0xC2,0xCA}},  /* 率 */
    /* 30 新增字（系统设备硬件版本功能显示状态传输误码稳定无线应用按键切换模式成）*/
    {{0xE7,0xB3,0xBB},{0xCF,0xB5}},  /* 系 */
    {{0xE7,0xBB,0x9F},{0xCD,0xB3}},  /* 统 */
    {{0xE8,0xAE,0xBE},{0xC9,0xE8}},  /* 设 */
    {{0xE5,0xA4,0x87},{0xB1,0xB8}},  /* 备 */
    {{0xE7,0xA1,0xAC},{0xD3,0xB2}},  /* 硬 */
    {{0xE4,0xBB,0xB6},{0xBC,0xFE}},  /* 件 */
    {{0xE7,0x89,0x88},{0xB0,0xE6}},  /* 版 */
    {{0xE5,0x8A,0x9F},{0xB9,0xA6}},  /* 功 */
    {{0xE8,0x83,0xBD},{0xC4,0xDC}},  /* 能 */
    {{0xE6,0x98,0xBE},{0xCF,0xD4}},  /* 显 */
    {{0xE7,0xA4,0xBA},{0xCA,0xBE}},  /* 示 */
    {{0xE7,0x8A,0xB6},{0xD7,0xB4}},  /* 状 */
    {{0xE6,0x80,0x81},{0xCC,0xAC}},  /* 态 */
    {{0xE4,0xBC,0xA0},{0xB4,0xAB}},  /* 传 */
    {{0xE8,0xBE,0x93},{0xCA,0xE4}},  /* 输 */
    {{0xE8,0xAF,0xAF},{0xCE,0xF3}},  /* 误 */
    {{0xE7,0xA0,0x81},{0xC2,0xEB}},  /* 码 */
    {{0xE7,0xA8,0xB3},{0xCE,0xC8}},  /* 稳 */
    {{0xE5,0xAE,0x9A},{0xB6,0xA8}},  /* 定 */
    {{0xE6,0x97,0xA0},{0xCE,0xDE}},  /* 无 */
    {{0xE7,0xBA,0xBF},{0xCF,0xDF}},  /* 线 */
    {{0xE5,0xBA,0x94},{0xD3,0xA6}},  /* 应 */
    {{0xE7,0x94,0xA8},{0xD3,0xC3}},  /* 用 */
    {{0xE6,0x8C,0x89},{0xB0,0xB4}},  /* 按 */
    {{0xE9,0x94,0xAE},{0xBC,0xFC}},  /* 键 */
    {{0xE5,0x88,0x87},{0xC7,0xD0}},  /* 切 */
    {{0xE6,0x8D,0xA2},{0xBB,0xBB}},  /* 换 */
    {{0xE6,0xA8,0xA1},{0xC4,0xA3}},  /* 模 */
    {{0xE5,0xBC,0x8F},{0xCA,0xBD}},  /* 式 */
    {{0xE6,0x88,0x90},{0xB3,0xC9}},  /* 成 */
};
#define UTF8_GB_TABLE_SIZE  (sizeof(utf8_gb_table) / sizeof(utf8_gb_table[0]))

/* 把 UTF-8 字节流转换为 GB2312 字节流。
 * - ASCII (<0x80) 原样保留
 * - 3字节 UTF-8 汉字 → 2字节 GB2312（查表，未找到则替换为 '?'）
 * - 其他非法字节替换为 '?'
 * 返回输出字节数。 */
static uint16_t TranscodeUTF8toGB2312(const char *in, uint16_t in_len,
                                       uint8_t *out, uint16_t max_out)
{
    uint16_t i = 0, o = 0;
    while (i < in_len && o < max_out)
    {
        uint8_t b = (uint8_t)in[i];
        if (b < 0x80)
        {
            /* ASCII: 1 byte direct */
            out[o++] = b;
            i++;
        }
        else if ((b & 0xE0) == 0xC0 && i + 1 < in_len)
        {
            /* 2-byte UTF-8 (U+0080..U+07FF): not a Chinese char, skip as '?' */
            if (o < max_out) out[o++] = '?';
            i += 2;
        }
        else if ((b & 0xF0) == 0xE0 && i + 2 < in_len)
        {
            /* 3-byte UTF-8 (Chinese chars live here): lookup table */
            uint16_t k;
            uint8_t found = 0;
            for (k = 0; k < UTF8_GB_TABLE_SIZE; k++)
            {
                if (utf8_gb_table[k].utf8[0] == (uint8_t)in[i] &&
                    utf8_gb_table[k].utf8[1] == (uint8_t)in[i+1] &&
                    utf8_gb_table[k].utf8[2] == (uint8_t)in[i+2])
                {
                    if (o + 1 < max_out)
                    {
                        out[o++] = utf8_gb_table[k].gb[0];
                        out[o++] = utf8_gb_table[k].gb[1];
                    }
                    found = 1;
                    break;
                }
            }
            if (!found && o < max_out) out[o++] = '?';
            i += 3;
        }
        else
        {
            /* invalid byte */
            out[o++] = '?';
            i++;
        }
    }
    return o;
}

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

	// --- Test UART TX: send startup banner + command list ---
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
		PrintCommandList();
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
// --- UART command parser ---
//   H         -> link check (reply OK-H)
//   R<data>   -> RAW frame, ASCII payload (e.g. RHello)
//   R0x..     -> RAW frame, HEX bytes  (e.g. R0xAABBCC)
//   R0b..     -> RAW frame, BIN bits   (e.g. R0b01010101)
//   T<string> -> TEXT frame (ASCII)
//   V         -> scope test frame (payload=0x55)
//   M<n>      -> predefined GB2312 Chinese TEXT frame (n=1..5)
//   C         -> show command list
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
        /* Link check: direct UART reply (no printf overhead) */
        HAL_UART_Transmit(&hlpuart1, (uint8_t *)"OK-H\r\n", 6, 1000);
        break;

    case 'R':
    case 'r':
        /* RAW frame. Three payload formats:
         *   R0x..  -> HEX bytes   (e.g. R0xAABBCC -> payload {0xAA,0xBB,0xCC})
         *   R0b..  -> BIN bits    (e.g. R0b01010101 -> payload {0x55})
         *   R<...> -> ASCII chars (e.g. RHello -> payload "Hello") */
        {
            uint8_t  payload_buf[ASK_MAX_PAYLOAD];
            uint16_t plen = 0;

            if (arglen == 0)
            {
                printf("R: empty payload (try R0x.. / R0b.. / R<text>)\r\n");
                break;
            }
            if (arglen >= 2 && arg[0] == '0' && (arg[1] == 'x' || arg[1] == 'X'))
            {
                plen = ParseHexBytes(&arg[2], payload_buf, ASK_MAX_PAYLOAD);
                if (plen == 0) { printf("R0x: invalid hex\r\n"); break; }
            }
            else if (arglen >= 2 && arg[0] == '0' && (arg[1] == 'b' || arg[1] == 'B'))
            {
                plen = ParseBinBytes(&arg[2], payload_buf, ASK_MAX_PAYLOAD);
                if (plen == 0) { printf("R0b: need >=8 bits\r\n"); break; }
            }
            else
            {
                plen = (arglen > ASK_MAX_PAYLOAD) ? ASK_MAX_PAYLOAD : (uint16_t)arglen;
                memcpy(payload_buf, arg, plen);
            }

            ASK_TX_FIFO_Clear();
            if (ASK_TX_SendFrame(ASK_TYPE_RAW, payload_buf, plen) == 0)
            {
                tx_frame_pending = 1;
                printf("R%u OK\r\n", plen);
                PrintEncodedBytes(payload_buf, plen);
            }
            else
                printf("R FULL\r\n");
        }
        break;

    case 'T':
    case 't':
        /* TEXT frame: 自动 UTF-8 → GB2312 转码后发送。
         * 上位机可直接发汉字（UTF-8 编码），TX 转成 GB2312 再发送，
         * RX 端用 GB2312 字库还原显示。ASCII 字符原样保留。 */
        if (arglen == 0) { printf("T: empty string\r\n"); break; }
        {
            uint8_t  payload_buf[ASK_MAX_PAYLOAD];
            uint16_t plen = TranscodeUTF8toGB2312(arg, (uint16_t)arglen,
                                                   payload_buf, ASK_MAX_PAYLOAD);
            if (plen == 0) { printf("T: transcode empty\r\n"); break; }
            ASK_TX_FIFO_Clear();
            if (ASK_TX_SendFrame(ASK_TYPE_TEXT, payload_buf, plen) == 0)
            {
                tx_frame_pending = 1;
                printf("T OK in=%u out=%u\r\n", (uint16_t)arglen, plen);
                PrintEncodedBytes(payload_buf, plen);
            }
            else
                printf("T FULL\r\n");
        }
        break;

    case 'V':
    case 'v':
        /* Scope verify: payload=0x55 (alternating bits, alignment errors visible) */
        {
            uint8_t v = 0x55;
            ASK_TX_FIFO_Clear();
            tx_frame_pending = 1;
            if (ASK_TX_SendFrame(ASK_TYPE_RAW, &v, 1) == 0)
            {
                printf("V OK\r\n");
                PrintEncodedBytes(&v, 1);
            }
            else
                printf("V FULL\r\n");
        }
        break;

    case 'M':
    case 'm':
        /* Send predefined GB2312 Chinese text frame.
         *   M1..M5 -> send msg_zh1..msg_zh5
         *   M / M0 / M? -> list available messages */
        {
            uint8_t idx = 0;
            uint8_t valid = 0;
            if (arglen > 0 && arg[0] >= '0' && arg[0] <= '9')
            {
                idx = (uint8_t)(arg[0] - '0');
                valid = 1;
            }

            if (!valid || idx == 0 || idx > MSG_ZH_COUNT)
            {
                printf("M: predefined GB2312 messages\r\n");
                printf("  M1 = Guang Tong Xin      (6B)\r\n");
                printf("  M2 = Jie Shou Zheng Chang(8B)\r\n");
                printf("  M3 = Fa Song Shu Ju      (8B)\r\n");
                printf("  M4 = Lian Lu Zheng Chang (8B)\r\n");
                printf("  M5 = Tiao Shi Ke Jian    (8B)\r\n");
                break;
            }

            {
                const uint8_t *msg = msg_zh_table[idx - 1];
                uint16_t       mlen = msg_zh_len[idx - 1];
                ASK_TX_FIFO_Clear();
                if (ASK_TX_SendFrame(ASK_TYPE_TEXT, msg, mlen) == 0)
                {
                    tx_frame_pending = 1;
                    printf("M%u OK %u bytes\r\n", idx, mlen);
                    PrintEncodedBytes(msg, mlen);
                }
                else
                    printf("M%u FULL\r\n", idx);
            }
        }
        break;

    case 'C':
    case 'c':
        PrintCommandList();
        break;

    default:
        printf("Unknown cmd. Input C for help.\r\n");
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
