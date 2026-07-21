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
    printf("R0x...    RAW frame, HEX bytes  (e.g. R0xAABBCC)\r\n");
    printf("R0b...    RAW frame, BIN bits   (e.g. R0b01010101)\r\n");
    printf("T<string> TEXT frame (ASCII + UTF-8 Chinese, auto GB2312)\r\n");
    printf("V         Scope test frame (payload=0x55)\r\n");
    printf("M<文本>   汉字文本帧发送 (same as T, specialized for Chinese input)\r\n");
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
 *          UTF-8 -> GB2312 transcoding for T command                         *
 *                                                                           *
 * 上位机串口工具默认 UTF-8 编码，每个汉字 3 字节；RX 端字库用 GB2312（2 字节）。
 * 这里在 TX 端自动把 UTF-8 汉字转成 GB2312 再发送，对上位机用户完全透明。
 * 映射表覆盖 RX 端 237 字字库（光通信可见接收发送号链路正常异文本图像音频
 * 调试待机数据帧错对率 + 系统设备硬件版本功能显示状态传输误码稳定无线应用
 * 按键切换模式成 + 嵌入式物联网处理算法协议编解感采集波特灵敏距离范围滤
 * 放校验实时中断低耗节点终端服务演方案目标环境安全电子开关灯温湿源压流
 * 识别有效优化停失度 + 标点 ，。？！：；、（）—），其它汉字会被替换为 '?'。  *
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
    /* 69 新增字（嵌入式物联网处理算法协议编解感采集波特灵敏距离范围滤放校验实时中断低耗节点终端服务演方案目标环境安全电子开关灯温湿源压流识别有效优化停失度）*/
    {{0xE5,0xB5,0x8C},{0xC7,0xB6}},  /* 嵌 */
    {{0xE5,0x85,0xA5},{0xC8,0xEB}},  /* 入 */
    {{0xE5,0xBC,0x8F},{0xCA,0xBD}},  /* 式 */
    {{0xE7,0x89,0xA9},{0xCE,0xEF}},  /* 物 */
    {{0xE8,0x81,0x94},{0xC1,0xAA}},  /* 联 */
    {{0xE7,0xBD,0x91},{0xCD,0xF8}},  /* 网 */
    {{0xE5,0xA4,0x84},{0xB4,0xA6}},  /* 处 */
    {{0xE7,0x90,0x86},{0xC0,0xED}},  /* 理 */
    {{0xE7,0xAE,0x97},{0xCB,0xE3}},  /* 算 */
    {{0xE6,0xB3,0x95},{0xB7,0xA8}},  /* 法 */
    {{0xE5,0x8D,0x8F},{0xD0,0xAD}},  /* 协 */
    {{0xE8,0xAE,0xAE},{0xD2,0xE9}},  /* 议 */
    {{0xE7,0xBC,0x96},{0xB1,0xE0}},  /* 编 */
    {{0xE8,0xA7,0xA3},{0xBD,0xE2}},  /* 解 */
    {{0xE6,0x84,0x9F},{0xB8,0xD0}},  /* 感 */
    {{0xE9,0x87,0x87},{0xB2,0xC9}},  /* 采 */
    {{0xE9,0x9B,0x86},{0xBC,0xAF}},  /* 集 */
    {{0xE6,0xB3,0xA2},{0xB2,0xA8}},  /* 波 */
    {{0xE7,0x89,0xB9},{0xCC,0xD8}},  /* 特 */
    {{0xE7,0x81,0xB5},{0xC1,0xE9}},  /* 灵 */
    {{0xE6,0x95,0x8F},{0xC3,0xF4}},  /* 敏 */
    {{0xE8,0xB7,0x9D},{0xBE,0xE0}},  /* 距 */
    {{0xE7,0xA6,0xBB},{0xC0,0xEB}},  /* 离 */
    {{0xE8,0x8C,0x83},{0xB7,0xB6}},  /* 范 */
    {{0xE5,0x9B,0xB4},{0xCE,0xA7}},  /* 围 */
    {{0xE6,0xBB,0xA4},{0xC2,0xCB}},  /* 滤 */
    {{0xE6,0x94,0xBE},{0xB7,0xC5}},  /* 放 */
    {{0xE6,0xA0,0xA0},{0xD0,0xA3}},  /* 校 */
    {{0xE9,0xAA,0x8C},{0xD1,0xE9}},  /* 验 */
    {{0xE5,0xAE,0x9E},{0xCA,0xB5}},  /* 实 */
    {{0xE6,0x97,0xB6},{0xCA,0xB1}},  /* 时 */
    {{0xE4,0xB8,0xAD},{0xD6,0xD0}},  /* 中 */
    {{0xE6,0x96,0xAD},{0xB6,0xCF}},  /* 断 */
    {{0xE4,0xBD,0x8E},{0xB5,0xCD}},  /* 低 */
    {{0xE8,0x80,0x97},{0xBA,0xC4}},  /* 耗 */
    {{0xE8,0x8A,0x82},{0xBD,0xDA}},  /* 节 */
    {{0xE7,0x82,0xB9},{0xB5,0xE3}},  /* 点 */
    {{0xE7,0xBB,0x88},{0xD6,0xD5}},  /* 终 */
    {{0xE7,0xAB,0xAF},{0xB6,0xCB}},  /* 端 */
    {{0xE6,0x9C,0x8D},{0xB7,0xFE}},  /* 服 */
    {{0xE5,0x8A,0xA1},{0xCE,0xF1}},  /* 务 */
    {{0xE6,0xBC,0x94},{0xD1,0xDD}},  /* 演 */
    {{0xE6,0x96,0xB9},{0xB7,0xBD}},  /* 方 */
    {{0xE6,0xA1,0x88},{0xB0,0xB8}},  /* 案 */
    {{0xE7,0x9B,0xAE},{0xC4,0xBF}},  /* 目 */
    {{0xE6,0xA0,0x87},{0xB1,0xEA}},  /* 标 */
    {{0xE7,0x8E,0xAF},{0xBB,0xB7}},  /* 环 */
    {{0xE5,0xA2,0x83},{0xBE,0xB3}},  /* 境 */
    {{0xE5,0xAE,0x89},{0xB0,0xB2}},  /* 安 */
    {{0xE5,0x85,0xA8},{0xC8,0xAB}},  /* 全 */
    {{0xE7,0x94,0xB5},{0xB5,0xE7}},  /* 电 */
    {{0xE5,0xAD,0x90},{0xD7,0xD3}},  /* 子 */
    {{0xE5,0xBC,0x80},{0xBF,0xAA}},  /* 开 */
    {{0xE5,0x85,0xB3},{0xB9,0xD8}},  /* 关 */
    {{0xE7,0x81,0xAF},{0xB5,0xC6}},  /* 灯 */
    {{0xE6,0xB8,0xA9},{0xCE,0xC2}},  /* 温 */
    {{0xE6,0xB9,0xBF},{0xCA,0xAA}},  /* 湿 */
    {{0xE6,0xBA,0x90},{0xD4,0xB4}},  /* 源 */
    {{0xE5,0x8E,0x8B},{0xD1,0xB9}},  /* 压 */
    {{0xE6,0xB5,0x81},{0xC1,0xF7}},  /* 流 */
    {{0xE8,0xAF,0x86},{0xCA,0xB6}},  /* 识 */
    {{0xE5,0x88,0xAB},{0xB1,0xF0}},  /* 别 */
    {{0xE6,0x9C,0x89},{0xD3,0xD0}},  /* 有 */
    {{0xE6,0x95,0x88},{0xD0,0xA7}},  /* 效 */
    {{0xE4,0xBC,0x98},{0xD3,0xC5}},  /* 优 */
    {{0xE5,0x8C,0x96},{0xBB,0xAF}},  /* 化 */
    {{0xE5,0x81,0x9C},{0xCD,0xA3}},  /* 停 */
    {{0xE5,0xA4,0xB1},{0xCA,0xA7}},  /* 失 */
    {{0xE5,0xBA,0xA6},{0xB6,0xC8}},  /* 度 */
    {{0xEF,0xBC,0x8C},{0xA3,0xAC}},  /* ， */
    {{0xE3,0x80,0x82},{0xA1,0xA3}},  /* 。 */
    {{0xEF,0xBC,0x9F},{0xA3,0xBF}},  /* ？ */
    {{0xEF,0xBC,0x81},{0xA3,0xA1}},  /* ！ */
    {{0xEF,0xBC,0x9A},{0xA3,0xBA}},  /* ： */
    {{0xEF,0xBC,0x9B},{0xA3,0xBB}},  /* ； */
    {{0xE3,0x80,0x81},{0xA1,0xA2}},  /* 、 */
    {{0xEF,0xBC,0x88},{0xA3,0xA8}},  /* （ */
    {{0xEF,0xBC,0x89},{0xA3,0xA9}},  /* ） */
    {{0xE2,0x80,0x94},{0xA1,0xAA}},  /* — */
    /* 98 新增字（赋计亮你好谢观参配置界面菜单返回确认出保存默败警告息提帮助日期间名称作者类型大小长颜色红绿蓝黄黑白背景齐居左右上下击触摸屏幕分辨素钮签列表进滚动窗口内容布局响交互画消知加载等连络强量储位速向运健康）*/
    {{0xE8,0xB5,0x8B},{0xB8,0xB3}},  /* 赋 */
    {{0xE8,0xAE,0xA1},{0xBC,0xC6}},  /* 计 */
    {{0xE4,0xBA,0xAE},{0xC1,0xC1}},  /* 亮 */
    {{0xE4,0xBD,0xA0},{0xC4,0xE3}},  /* 你 */
    {{0xE5,0xA5,0xBD},{0xBA,0xC3}},  /* 好 */
    {{0xE8,0xB0,0xA2},{0xD0,0xBB}},  /* 谢 */
    {{0xE8,0xA7,0x82},{0xB9,0xDB}},  /* 观 */
    {{0xE5,0x8F,0x82},{0xB2,0xCE}},  /* 参 */
    {{0xE9,0x85,0x8D},{0xC5,0xE4}},  /* 配 */
    {{0xE7,0xBD,0xAE},{0xD6,0xC3}},  /* 置 */
    {{0xE7,0x95,0x8C},{0xBD,0xE7}},  /* 界 */
    {{0xE9,0x9D,0xA2},{0xC3,0xE6}},  /* 面 */
    {{0xE8,0x8F,0x9C},{0xB2,0xCB}},  /* 菜 */
    {{0xE5,0x8D,0x95},{0xB5,0xA5}},  /* 单 */
    {{0xE8,0xBF,0x94},{0xB7,0xB5}},  /* 返 */
    {{0xE5,0x9B,0x9E},{0xBB,0xD8}},  /* 回 */
    {{0xE7,0xA1,0xAE},{0xC8,0xB7}},  /* 确 */
    {{0xE8,0xAE,0xA4},{0xC8,0xCF}},  /* 认 */
    {{0xE5,0x87,0xBA},{0xB3,0xF6}},  /* 出 */
    {{0xE4,0xBF,0x9D},{0xB1,0xA3}},  /* 保 */
    {{0xE5,0xAD,0x98},{0xB4,0xE6}},  /* 存 */
    {{0xE9,0xBB,0x98},{0xC4,0xAC}},  /* 默 */
    {{0xE8,0xB4,0xA5},{0xB0,0xDC}},  /* 败 */
    {{0xE8,0xAD,0xA6},{0xBE,0xAF}},  /* 警 */
    {{0xE5,0x91,0x8A},{0xB8,0xE6}},  /* 告 */
    {{0xE6,0x81,0xAF},{0xCF,0xA2}},  /* 息 */
    {{0xE6,0x8F,0x90},{0xCC,0xE1}},  /* 提 */
    {{0xE5,0xB8,0xAE},{0xB0,0xEF}},  /* 帮 */
    {{0xE5,0x8A,0xA9},{0xD6,0xFA}},  /* 助 */
    {{0xE6,0x97,0xA5},{0xC8,0xD5}},  /* 日 */
    {{0xE6,0x9C,0x9F},{0xC6,0xDA}},  /* 期 */
    {{0xE9,0x97,0xB4},{0xBC,0xE4}},  /* 间 */
    {{0xE5,0x90,0x8D},{0xC3,0xFB}},  /* 名 */
    {{0xE7,0xA7,0xB0},{0xB3,0xC6}},  /* 称 */
    {{0xE4,0xBD,0x9C},{0xD7,0xF7}},  /* 作 */
    {{0xE8,0x80,0x85},{0xD5,0xDF}},  /* 者 */
    {{0xE7,0xB1,0xBB},{0xC0,0xE0}},  /* 类 */
    {{0xE5,0x9E,0x8B},{0xD0,0xCD}},  /* 型 */
    {{0xE5,0xA4,0xA7},{0xB4,0xF3}},  /* 大 */
    {{0xE5,0xB0,0x8F},{0xD0,0xA1}},  /* 小 */
    {{0xE9,0x95,0xBF},{0xB3,0xA4}},  /* 长 */
    {{0xE9,0xA2,0x9C},{0xD1,0xD5}},  /* 颜 */
    {{0xE8,0x89,0xB2},{0xC9,0xAB}},  /* 色 */
    {{0xE7,0xBA,0xA2},{0xBA,0xEC}},  /* 红 */
    {{0xE7,0xBB,0xBF},{0xC2,0xCC}},  /* 绿 */
    {{0xE8,0x93,0x9D},{0xC0,0xB6}},  /* 蓝 */
    {{0xE9,0xBB,0x84},{0xBB,0xC6}},  /* 黄 */
    {{0xE9,0xBB,0x91},{0xBA,0xDA}},  /* 黑 */
    {{0xE7,0x99,0xBD},{0xB0,0xD7}},  /* 白 */
    {{0xE8,0x83,0x8C},{0xB1,0xB3}},  /* 背 */
    {{0xE6,0x99,0xAF},{0xBE,0xB0}},  /* 景 */
    {{0xE9,0xBD,0x90},{0xC6,0xEB}},  /* 齐 */
    {{0xE5,0xB1,0x85},{0xBE,0xD3}},  /* 居 */
    {{0xE5,0xB7,0xA6},{0xD7,0xF3}},  /* 左 */
    {{0xE5,0x8F,0xB3},{0xD3,0xD2}},  /* 右 */
    {{0xE4,0xB8,0x8A},{0xC9,0xCF}},  /* 上 */
    {{0xE4,0xB8,0x8B},{0xCF,0xC2}},  /* 下 */
    {{0xE5,0x87,0xBB},{0xBB,0xF7}},  /* 击 */
    {{0xE8,0xA7,0xA6},{0xB4,0xA5}},  /* 触 */
    {{0xE6,0x91,0xB8},{0xC3,0xFE}},  /* 摸 */
    {{0xE5,0xB1,0x8F},{0xC6,0xC1}},  /* 屏 */
    {{0xE5,0xB9,0x95},{0xC4,0xBB}},  /* 幕 */
    {{0xE5,0x88,0x86},{0xB7,0xD6}},  /* 分 */
    {{0xE8,0xBE,0xA8},{0xB1,0xE6}},  /* 辨 */
    {{0xE7,0xB4,0xA0},{0xCB,0xD8}},  /* 素 */
    {{0xE9,0x92,0xAE},{0xC5,0xA5}},  /* 钮 */
    {{0xE7,0xAD,0xBE},{0xC7,0xA9}},  /* 签 */
    {{0xE5,0x88,0x97},{0xC1,0xD0}},  /* 列 */
    {{0xE8,0xA1,0xA8},{0xB1,0xED}},  /* 表 */
    {{0xE8,0xBF,0x9B},{0xBD,0xF8}},  /* 进 */
    {{0xE6,0xBB,0x9A},{0xB9,0xF6}},  /* 滚 */
    {{0xE5,0x8A,0xA8},{0xB6,0xAF}},  /* 动 */
    {{0xE7,0xAA,0x97},{0xB4,0xB0}},  /* 窗 */
    {{0xE5,0x8F,0xA3},{0xBF,0xDA}},  /* 口 */
    {{0xE5,0x86,0x85},{0xC4,0xDA}},  /* 内 */
    {{0xE5,0xAE,0xB9},{0xC8,0xDD}},  /* 容 */
    {{0xE5,0xB8,0x83},{0xB2,0xBC}},  /* 布 */
    {{0xE5,0xB1,0x80},{0xBE,0xD6}},  /* 局 */
    {{0xE5,0x93,0x8D},{0xCF,0xEC}},  /* 响 */
    {{0xE4,0xBA,0xA4},{0xBD,0xBB}},  /* 交 */
    {{0xE4,0xBA,0x92},{0xBB,0xA5}},  /* 互 */
    {{0xE7,0x94,0xBB},{0xBB,0xAD}},  /* 画 */
    {{0xE6,0xB6,0x88},{0xCF,0xFB}},  /* 消 */
    {{0xE7,0x9F,0xA5},{0xD6,0xAA}},  /* 知 */
    {{0xE5,0x8A,0xA0},{0xBC,0xD3}},  /* 加 */
    {{0xE8,0xBD,0xBD},{0xD4,0xD8}},  /* 载 */
    {{0xE7,0xAD,0x89},{0xB5,0xC8}},  /* 等 */
    {{0xE8,0xBF,0x9E},{0xC1,0xAC}},  /* 连 */
    {{0xE7,0xBB,0x9C},{0xC2,0xE7}},  /* 络 */
    {{0xE5,0xBC,0xBA},{0xC7,0xBF}},  /* 强 */
    {{0xE9,0x87,0x8F},{0xC1,0xBF}},  /* 量 */
    {{0xE5,0x82,0xA8},{0xB4,0xA2}},  /* 储 */
    {{0xE4,0xBD,0x8D},{0xCE,0xBB}},  /* 位 */
    {{0xE9,0x80,0x9F},{0xCB,0xD9}},  /* 速 */
    {{0xE5,0x90,0x91},{0xCF,0xF2}},  /* 向 */
    {{0xE8,0xBF,0x90},{0xD4,0xCB}},  /* 运 */
    {{0xE5,0x81,0xA5},{0xBD,0xA1}},  /* 健 */
    {{0xE5,0xBA,0xB7},{0xBF,0xB5}},  /* 康 */
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
//   T<string> -> TEXT frame (ASCII + UTF-8 Chinese, auto GB2312)
//   V         -> scope test frame (payload=0x55)
//   M<string> -> Chinese TEXT frame (same as T, for Chinese input)
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
        /* Chinese TEXT frame: same as T, auto UTF-8 → GB2312 transcoding.
         * Provided as an alias for convenient Chinese input. */
        if (arglen == 0) { printf("M: empty string\r\n"); break; }
        {
            uint8_t  payload_buf[ASK_MAX_PAYLOAD];
            uint16_t plen = TranscodeUTF8toGB2312(arg, (uint16_t)arglen,
                                                   payload_buf, ASK_MAX_PAYLOAD);
            if (plen == 0) { printf("M: transcode empty\r\n"); break; }
            ASK_TX_FIFO_Clear();
            if (ASK_TX_SendFrame(ASK_TYPE_TEXT, payload_buf, plen) == 0)
            {
                tx_frame_pending = 1;
                printf("M OK in=%u out=%u\r\n", (uint16_t)arglen, plen);
                PrintEncodedBytes(payload_buf, plen);
            }
            else
                printf("M FULL\r\n");
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
