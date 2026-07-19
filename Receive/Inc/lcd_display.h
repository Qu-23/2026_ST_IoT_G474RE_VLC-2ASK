/**
  ******************************************************************************
  * @file    lcd_display.h
  * @brief   LCD显示模块 - 业务/调试双模式
  *
  * 两种模式，按键PA0切换：
  *   BIZ (业务) - 自动按帧类型还原显示 (TEXT/RAW/GRAPHIC/AUDIO)
  *   DBG (调试) - 显示FRM/BRK/CRC/BL/TH等调试信息
  ******************************************************************************
  */

#ifndef __LCD_DISPLAY_H
#define __LCD_DISPLAY_H

#ifdef __cplusplus
extern "C" {
#endif

#include "signal_processor.h"

/* 初始化LCD显示 */
void LCDDisplay_Init(void);

/* 调试模式：更新stats显示 */
void LCDDisplay_Update(const SignalStats_t *stats);

/* 业务模式：收到有效帧时调用，自动按type显示 */
void LCDDisplay_OnFrame(uint8_t type, const uint8_t *payload, uint8_t len);

/* 按键扫描 + 模式切换 (PA0下降沿切换) */
void LCDDisplay_Process(void);

#ifdef __cplusplus
}
#endif

#endif /* __LCD_DISPLAY_H */
