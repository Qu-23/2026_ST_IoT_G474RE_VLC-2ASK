/**
  ******************************************************************************
  * @file    lcd_display.c
  * @brief   LCD显示 - 业务/调试双模式
  *
  * 模式切换: PA0按键下降沿
  *   BIZ: 白色背景，自动按帧type还原显示，含待机动画
  *   DBG: 黑色背景，局部刷新调试信息
  * 两者均有LINK状态栏（5s心跳看门狗）
  ******************************************************************************
  */

#include "main.h"
#include "lcd_display.h"
#include "signal_processor.h"
#include "Lcd_Driver.h"
#include "GUI.h"
#include "SPI_FLASH.h"
#include <string.h>

/*============================================================================*
 *                          颜色定义                                          *
 *============================================================================*/
#ifndef LCDCYAN
#define LCDCYAN   0x07FF
#endif
#ifndef MAGENTA
#define MAGENTA   0xF81F
#endif

#define COLOR_TITLE_BG  BLUE
#define COLOR_TITLE_FG  WHITE
/* BIZ白底：标签用深灰GRAY2，值用黑色 */
#define COLOR_BIZ_LABEL GRAY2
#define COLOR_BIZ_VALUE BLACK
/* DBG黑底：标签用灰GRAY1，值用白/绿/红 */
#define COLOR_DBG_LABEL GRAY1
#define COLOR_DBG_VALUE WHITE
#define COLOR_OK        GREEN
#define COLOR_ERR       RED

/* 心跳看门狗超时 */
#define HEARTBEAT_TIMEOUT_MS  5000

/*============================================================================*
 *                          显示模式                                          *
 *============================================================================*/
#define MODE_BIZ    0
#define MODE_DBG    1
#define MODE_HZ     2   /* 隐藏汉字测试模式，PA1进入 */
static uint8_t  s_mode = MODE_BIZ;
static uint8_t  s_mode_prev = MODE_BIZ;   /* 进入HZ前的模式，用于PA1退出恢复 */
static uint8_t  s_force_redraw = 1;

/* 业务模式数据 */
#define BIZ_PAYLOAD_MAX  64
static uint8_t  s_biz_type = 0;
static uint8_t  s_biz_len = 0;
static uint8_t  s_biz_payload[BIZ_PAYLOAD_MAX];
static uint8_t  s_biz_updated = 0;
static uint32_t s_biz_frame_cnt = 0;
static uint8_t  s_biz_has_data = 0;
static uint8_t  s_biz_info_rows = 1;  /* 1=Type+Len same row, 2=Len wrapped to next line */

/* 待机动画 */
static uint8_t  s_anim_idx = 0;
static const char s_anim_chars[] = "|/-\\";

/* 连接状态（心跳看门狗） */
static uint8_t  s_link_ok = 0;          /* 1=LINK OK, 0=NO SIG */
static uint8_t  s_link_changed = 1;     /* 状态变化标志 */
static uint32_t s_last_hb_count = 0;
static uint32_t s_last_hb_tick = 0;

/*============================================================================*
 *                          绘图辅助                                          *
 *============================================================================*/
static void ShowChar(char ch, uint16_t x, uint16_t y, uint16_t fg, uint16_t bg)
{
    Gui_DrawChar_Ascii(x, y, fg, bg, ch);
}

static void ShowStr(const char *str, uint16_t x, uint16_t y, uint16_t fg, uint16_t bg)
{
    while (*str) { ShowChar(*str, x, y, fg, bg); str++; x += 8; }
}

static void ShowNum(uint32_t num, uint16_t x, uint16_t y, uint16_t fg, uint16_t bg)
{
    char buf[12]; int i = 10; buf[11] = '\0';
    if (num == 0) { ShowChar('0', x, y, fg, bg); return; }
    while (num > 0 && i >= 0) { buf[i] = (num % 10) + '0'; num /= 10; i--; }
    ShowStr(&buf[i + 1], x, y, fg, bg);
}

static void ShowHex2(uint8_t num, uint16_t x, uint16_t y, uint16_t fg, uint16_t bg)
{
    const char h[] = "0123456789ABCDEF";
    ShowChar(h[(num >> 4) & 0xF], x, y, fg, bg);
    ShowChar(h[num & 0xF], x + 8, y, fg, bg);
}

static void ShowHex4(uint16_t num, uint16_t x, uint16_t y, uint16_t fg, uint16_t bg)
{
    const char h[] = "0123456789ABCDEF";
    ShowChar(h[(num >> 12) & 0xF], x, y, fg, bg);
    ShowChar(h[(num >> 8) & 0xF], x + 8, y, fg, bg);
    ShowChar(h[(num >> 4) & 0xF], x + 16, y, fg, bg);
    ShowChar(h[num & 0xF], x + 24, y, fg, bg);
}

/*============================================================================*
 *                          连接状态栏（共用）                                 *
 *============================================================================*/
static void UpdateLinkStatus(const SignalStats_t *s)
{
    uint32_t hb_count = s->pattern_55 + s->pattern_AA;
    uint32_t now = HAL_GetTick();

    if (hb_count != s_last_hb_count)
    {
        s_last_hb_count = hb_count;
        s_last_hb_tick = now;
    }

    uint8_t new_ok = (hb_count > 0 && (now - s_last_hb_tick) <= HEARTBEAT_TIMEOUT_MS);
    if (new_ok != s_link_ok)
    {
        s_link_ok = new_ok;
        s_link_changed = 1;
    }
}

static void Draw_LinkBar(void)
{
    uint16_t bar_bg;
    uint16_t led_color;
    uint16_t text_color;
    const char *text;

    if (!s_link_changed && !s_force_redraw)
        return;

    /* Background matches current mode: BIZ/HZ=white, DBG=black.
     * Avoids the jarring black bar on the white BIZ/HZ screen. */
    bar_bg = (s_mode == MODE_DBG) ? BLACK : WHITE;

    if (s_link_ok)
    {
        led_color  = COLOR_OK;     /* green LED */
        text       = "LINK OK";
        /* BIZ/HZ white bg: use dark text (green has low contrast on white);
         * DBG black bg: use bright green text */
        text_color = (s_mode == MODE_DBG) ? COLOR_OK : COLOR_BIZ_VALUE;
    }
    else
    {
        led_color  = COLOR_ERR;    /* red LED */
        text       = "NO SIG";
        text_color = (s_mode == MODE_DBG) ? COLOR_ERR : COLOR_BIZ_VALUE;
    }

    /* Clear status bar background */
    Lcd_fill(0, 16, 128, 32, bar_bg);
    /* Status LED: 8x8 filled square at left (vertically centered in 16px bar) */
    Lcd_fill(4, 20, 12, 28, led_color);
    /* Status text (starts at x=16, leaving room for LED) */
    ShowStr(text, 16, 16, text_color, bar_bg);

    s_link_changed = 0;
}

/*============================================================================*
 *                          业务模式                                          *
 *============================================================================*/
static const char *TypeStr(uint8_t type)
{
    switch (type)
    {
        case ASK_TYPE_RAW:     return "RAW";
        case ASK_TYPE_TEXT:    return "TEXT";
        case ASK_TYPE_GRAPHIC: return "IMG";
        case ASK_TYPE_AUDIO:   return "AUDIO";
        default:               return "UNK";
    }
}

static uint16_t TypeColor(uint8_t type)
{
    switch (type)
    {
        case ASK_TYPE_TEXT:    return RED;
        case ASK_TYPE_RAW:     return GREEN;
        case ASK_TYPE_GRAPHIC: return BLUE;
        case ASK_TYPE_AUDIO:   return MAGENTA;
        default:               return RED;
    }
}

static void Draw_BizTitle(void)
{
    Lcd_fill(0, 0, 128, 16, COLOR_TITLE_BG);
    ShowStr("2ASK RX", 4, 0, COLOR_TITLE_FG, COLOR_TITLE_BG);
}

static void Draw_BizInfo(void)
{
    const char *type_str = TypeStr(s_biz_type);
    uint8_t type_chars = (uint8_t)strlen(type_str);

    /* LEN < 100 (≤2 digits): same row as Type (fits 128px even for AUDIO+2-digit)
     * LEN >= 100 (3 digits): wrap Len to y=48 to avoid overflow */
    s_biz_info_rows = (s_biz_len >= 100) ? 2 : 1;

    Lcd_fill(0, 32, 128, 48, WHITE);
    ShowStr("Type:", 0, 32, COLOR_BIZ_LABEL, WHITE);
    ShowStr(type_str, 40, 32, TypeColor(s_biz_type), WHITE);

    if (s_biz_info_rows == 1)
    {
        /* Len on same row, right after TypeStr */
        uint16_t len_x = (uint16_t)(40 + type_chars * 8);
        ShowStr("Len:", len_x, 32, COLOR_BIZ_LABEL, WHITE);
        ShowNum(s_biz_len, len_x + 32, 32, COLOR_BIZ_VALUE, WHITE);
    }
    else
    {
        /* Len wrapped to y=48 */
        Lcd_fill(0, 48, 128, 64, WHITE);
        ShowStr("Len:", 0, 48, COLOR_BIZ_LABEL, WHITE);
        ShowNum(s_biz_len, 32, 48, COLOR_BIZ_VALUE, WHITE);
    }
}

static void Draw_BizContent(void)
{
    /* Content area: y=48 (Len same row, 4 rows) or y=64 (Len wrapped, 3 rows) */
    uint16_t y_start = (s_biz_info_rows == 1) ? 48 : 64;
    uint8_t  max_rows = (s_biz_info_rows == 1) ? 4 : 3;

    Lcd_fill(0, y_start, 128, 112, WHITE);

    if (!s_biz_has_data || s_biz_len == 0)
    {
        ShowStr("Waiting", 36, 80, COLOR_BIZ_LABEL, WHITE);
        ShowChar(s_anim_chars[s_anim_idx], 84, 80, COLOR_BIZ_VALUE, WHITE);
        return;
    }

    uint8_t show_len = s_biz_len > BIZ_PAYLOAD_MAX ? BIZ_PAYLOAD_MAX : s_biz_len;

    if (s_biz_type == ASK_TYPE_TEXT)
    {
        /* TEXT: 16 chars/row */
        int row = 0, col = 0;
        for (int i = 0; i < show_len && row < max_rows; i++)
        {
            char c = (char)s_biz_payload[i];
            if (c < 32 || c > 126) c = '.';
            ShowChar(c, col * 8, y_start + row * 16, COLOR_BIZ_VALUE, WHITE);
            col++;
            if (col >= 16) { col = 0; row++; }
        }
    }
    else
    {
        /* RAW/GRAPHIC/AUDIO: hex dump.
         * Format: "0xAA BB CC DD EE" - "0x" prefix once at line start,
         * subsequent bytes separated by single space.
         * Byte 0: '0''x'HH = 32px; Byte 1-4: ' 'HH = 24px each.
         * Total: 32 + 4×24 = 128px → 5 bytes/row. */
        int row = 0, col = 0;
        uint16_t x = 0;
        for (int i = 0; i < show_len && row < max_rows; i++)
        {
            uint16_t y = (uint16_t)(y_start + row * 16);
            if (col == 0)
            {
                ShowChar('0', x, y, COLOR_BIZ_VALUE, WHITE);
                ShowChar('x', x + 8, y, COLOR_BIZ_VALUE, WHITE);
                x += 16;
            }
            else
            {
                x += 8;  /* space separator */
            }
            ShowHex2(s_biz_payload[i], x, y, COLOR_BIZ_VALUE, WHITE);
            x += 16;
            col++;
            if (col >= 5) { col = 0; x = 0; row++; }
        }
    }
}

static void Draw_BizFooter(void)
{
    /* Footer: only [PA0]DBG hint, FRM counter hidden for cleaner look */
    Lcd_fill(0, 112, 128, 128, COLOR_TITLE_BG);
    ShowStr("[PA0]DBG", 4, 112, COLOR_TITLE_FG, COLOR_TITLE_BG);
}

static void Draw_Biz(void)
{
    if (s_force_redraw)
    {
        Draw_BizTitle();
        Draw_BizInfo();
        Draw_BizContent();
        Draw_BizFooter();
        s_force_redraw = 0;
        s_biz_updated = 0;
        s_link_changed = 1;
    }
    Draw_LinkBar();
    if (s_biz_updated)
    {
        Draw_BizInfo();
        Draw_BizContent();
        /* Footer is static now (FRM hidden), only drawn on force_redraw */
        s_biz_updated = 0;
    }
    else if (!s_biz_has_data)
    {
        /* 仅刷新动画字符 (y=80 matches Draw_BizContent waiting position) */
        ShowChar(s_anim_chars[s_anim_idx], 84, 80, COLOR_BIZ_VALUE, WHITE);
    }
}

/*============================================================================*
 *                          调试模式（局部刷新）                               *
 *============================================================================*/
static void Draw_DbgTitle(void)
{
    Lcd_fill(0, 0, 128, 16, COLOR_TITLE_BG);
    ShowStr("DEBUG", 4, 0, COLOR_TITLE_FG, COLOR_TITLE_BG);
}

/* 调试模式局部刷新缓存 */
static uint32_t p_frm = 0xFFFFFFFF, p_brk = 0xFFFFFFFF;
static uint8_t  p_ty = 0xFF, p_ln = 0xFF;
static uint16_t p_crx = 0xFFFF, p_ccl = 0xFFFF;

static void Draw_DbgContent(const SignalStats_t *s)
{
    if (s_force_redraw)
    {
        /* y=32~111 debug content area (skip y=16~31 status bar) */
        Lcd_fill(0, 32, 128, 112, BLACK);
        /* 画固定标签 */
        ShowStr("FRM:", 0, 32, COLOR_DBG_LABEL, BLACK);
        ShowStr("BRK:", 72, 32, COLOR_DBG_LABEL, BLACK);
        ShowStr("T:", 0, 48, COLOR_DBG_LABEL, BLACK);
        ShowStr("L:", 56, 48, COLOR_DBG_LABEL, BLACK);
        ShowStr("RX:", 0, 64, COLOR_DBG_LABEL, BLACK);
        ShowStr("CL:", 0, 80, COLOR_DBG_LABEL, BLACK);
        p_frm = p_brk = 0xFFFFFFFF;
        p_ty = p_ln = 0xFF;
        p_crx = p_ccl = 0xFFFF;
    }

    /* y=32: FRM / BRK (局部刷新) */
    if (s->frame_count != p_frm)
    {
        Lcd_fill(32, 32, 72, 48, BLACK);
        ShowNum(s->frame_count, 32, 32, COLOR_OK, BLACK);
        p_frm = s->frame_count;
    }
    if (s->break_count != p_brk)
    {
        Lcd_fill(104, 32, 128, 48, BLACK);
        ShowNum(s->break_count, 104, 32, COLOR_DBG_VALUE, BLACK);
        p_brk = s->break_count;
    }

    /* y=48: T / L */
    if (s->last_frame_type != p_ty)
    {
        Lcd_fill(16, 48, 48, 64, BLACK);
        ShowHex2(s->last_frame_type, 16, 48, COLOR_DBG_VALUE, BLACK);
        p_ty = s->last_frame_type;
    }
    if (s->last_frame_len != p_ln)
    {
        Lcd_fill(72, 48, 104, 64, BLACK);
        ShowHex2(s->last_frame_len, 72, 48, COLOR_DBG_VALUE, BLACK);
        p_ln = s->last_frame_len;
    }

    /* y=64: RX (接收CRC) */
    if (s->last_crc_rx != p_crx)
    {
        Lcd_fill(24, 64, 56, 80, BLACK);
        ShowHex4(s->last_crc_rx, 24, 64, COLOR_ERR, BLACK);
        p_crx = s->last_crc_rx;
    }

    /* y=80: CL (计算CRC) */
    if (s->last_crc_calc != p_ccl)
    {
        Lcd_fill(24, 80, 56, 96, BLACK);
        ShowHex4(s->last_crc_calc, 24, 80, YELLOW, BLACK);
        p_ccl = s->last_crc_calc;
    }
}

static void Draw_DbgFooter(void)
{
    Lcd_fill(0, 112, 128, 128, COLOR_TITLE_BG);
    ShowStr("[PA0]BIZ", 4, 112, COLOR_TITLE_FG, COLOR_TITLE_BG);
}

static void Draw_Dbg(const SignalStats_t *s)
{
    if (s_force_redraw)
    {
        Draw_DbgTitle();
        Draw_DbgFooter();
    }
    Draw_LinkBar();
    Draw_DbgContent(s);
    if (s_force_redraw)
        s_force_redraw = 0;
}

/*============================================================================*
 *                          汉字测试模式（隐藏，PA1进入）                    *
 *============================================================================*/
/* GB2312转义序列避免源文件编码问题。每行8字×16px=128px，共31字分4行(8/8/8/7) */
static const uint8_t hz_line1[] = "\xB9\xE2\xCD\xA8\xD0\xC5\xBF\xC9\xBC\xFB\xBD\xD3\xCA\xD5\xB7\xA2"; /* 光通信可见接收发 */
static const uint8_t hz_line2[] = "\xCB\xCD\xBA\xC5\xC1\xB4\xC2\xB7\xD5\xFD\xB3\xA3\xD2\xEC\xCE\xC4"; /* 送号链路正常异文 */
static const uint8_t hz_line3[] = "\xB1\xBE\xCD\xBC\xCF\xF1\xD2\xF4\xC6\xB5\xB5\xF7\xCA\xD4\xB4\xFD"; /* 本图像音频调试待 */
static const uint8_t hz_line4[] = "\xBB\xFA\xCA\xFD\xBE\xDD\xD6\xA1\xB4\xED\xB6\xD4\xC2\xCA";         /* 机数据帧错对率 */

static void Draw_HzTitle(void)
{
    Lcd_fill(0, 0, 128, 16, COLOR_TITLE_BG);
    ShowStr("[PA1]EXIT", 4, 0, COLOR_TITLE_FG, COLOR_TITLE_BG);
}

static void Draw_HzFooter(void)
{
    Lcd_fill(0, 112, 128, 128, COLOR_TITLE_BG);
    ShowStr("HZ TEST", 4, 112, COLOR_TITLE_FG, COLOR_TITLE_BG);
}

static void Draw_HzContent(void)
{
    Lcd_fill(0, 32, 128, 112, WHITE);
    Gui_DrawFont_F16(0, 32, COLOR_BIZ_VALUE, WHITE, (uint8_t*)hz_line1);
    Gui_DrawFont_F16(0, 48, COLOR_BIZ_VALUE, WHITE, (uint8_t*)hz_line2);
    Gui_DrawFont_F16(0, 64, COLOR_BIZ_VALUE, WHITE, (uint8_t*)hz_line3);
    Gui_DrawFont_F16(0, 80, COLOR_BIZ_VALUE, WHITE, (uint8_t*)hz_line4);
}

static void Draw_Hz(void)
{
    if (s_force_redraw)
    {
        Draw_HzTitle();
        Draw_HzFooter();
        Draw_HzContent();
    }
    Draw_LinkBar();
    if (s_force_redraw)
        s_force_redraw = 0;
}

/*============================================================================*
 *                          公共接口                                          *
 *============================================================================*/
void LCDDisplay_Init(void)
{
    Lcd_Init();

    /* ===== 启动加载动画界面（含W25Q16硬件检测）===== */
    Lcd_Clear(WHITE);

    /* 标题栏 */
    Lcd_fill(0, 0, 128, 16, COLOR_TITLE_BG);
    ShowStr("2ASK RX Boot", 4, 0, COLOR_TITLE_FG, COLOR_TITLE_BG);

    /* "Loading" + 旋转动画 |/-\ */
    ShowStr("Loading", 32, 48, COLOR_BIZ_VALUE, WHITE);
    const char anim[] = "|/-\\";
    for (int i = 0; i < 12; i++)
    {
        ShowChar(anim[i % 4], 96, 48, COLOR_BIZ_VALUE, WHITE);
        HAL_Delay(80);
    }

    /* 进度条 */
    Lcd_fill(8, 70, 120, 80, GRAY2);    /* 边框 */
    Lcd_fill(10, 72, 118, 78, WHITE);   /* 内部白底 */
    for (int i = 0; i <= 100; i += 5)
    {
        uint16_t fill_w = (uint16_t)(i * 108 / 100);
        Lcd_fill(10, 72, (uint16_t)(10 + fill_w), 78, BLUE);
        HAL_Delay(15);
    }

    /* W25Q16 ID 检测结果显示 */
    ShowStr("Flash ID:", 8, 96, COLOR_BIZ_LABEL, WHITE);
    ShowHex4(SPI_Flash_Type, 80, 96, COLOR_BIZ_VALUE, WHITE);
    if (SPI_Flash_Type == 0xEF14)
        ShowStr("W25Q16 OK", 8, 112, COLOR_OK, WHITE);
    else
        ShowStr("Flash NA", 8, 112, COLOR_ERR, WHITE);

    HAL_Delay(800);  /* 让用户看清硬件检测结果 */

    /* ===== 加载完成，进入BIZ模式 ===== */
    Lcd_Clear(BLACK);
    s_mode = MODE_BIZ;
    s_force_redraw = 1;
    s_biz_updated = 0;
    s_biz_frame_cnt = 0;
    s_biz_has_data = 0;
    s_anim_idx = 0;
    s_link_ok = 0;
    s_link_changed = 1;
    s_last_hb_count = 0;
    s_last_hb_tick = 0;
}

void LCDDisplay_Update(const SignalStats_t *stats)
{
    /* 节流：200ms刷新一次 */
    static uint32_t last_tick = 0;
    uint32_t now = HAL_GetTick();
    if (now - last_tick < 200)
        return;
    last_tick = now;

    /* 更新连接状态 */
    UpdateLinkStatus(stats);

    /* 动画帧推进 */
    s_anim_idx = (s_anim_idx + 1) % 4;

    if (s_mode == MODE_BIZ)
        Draw_Biz();
    else if (s_mode == MODE_DBG)
        Draw_Dbg(stats);
    else if (s_mode == MODE_HZ)
        Draw_Hz();
}

void LCDDisplay_OnFrame(uint8_t type, const uint8_t *payload, uint8_t len)
{
    s_biz_type = type;
    s_biz_len = len;
    if (len > 0 && payload)
    {
        uint8_t copy_len = len > BIZ_PAYLOAD_MAX ? BIZ_PAYLOAD_MAX : len;
        memcpy(s_biz_payload, payload, copy_len);
    }
    s_biz_frame_cnt++;
    s_biz_has_data = 1;
    s_biz_updated = 1;

    /* 收到帧说明链路正常 */
    if (!s_link_ok)
    {
        s_link_ok = 1;
        s_link_changed = 1;
    }

    if (s_mode == MODE_BIZ)
        Draw_Biz();
}

void LCDDisplay_Process(void)
{
    static uint8_t  last_btn0 = 1;
    static uint8_t  last_btn1 = 1;
    static uint32_t last_tick = 0;
    uint32_t now = HAL_GetTick();

    if (now - last_tick < 20)
        return;
    last_tick = now;

    /* PA0: BIZ <-> DBG (仅在非HZ模式下生效，避免在HZ中误切换) */
    uint8_t btn0 = HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0);
    if (last_btn0 == 1 && btn0 == 0)
    {
        if (s_mode != MODE_HZ)
        {
            s_mode = (s_mode == MODE_BIZ) ? MODE_DBG : MODE_BIZ;
            s_force_redraw = 1;
        }
    }
    last_btn0 = btn0;

    /* PA1: 进入/退出 HZ 汉字测试模式 */
    uint8_t btn1 = HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_1);
    if (last_btn1 == 1 && btn1 == 0)
    {
        if (s_mode == MODE_HZ)
        {
            s_mode = s_mode_prev;   /* 退出，恢复之前模式 */
        }
        else
        {
            s_mode_prev = s_mode;   /* 进入，保存当前模式 */
            s_mode = MODE_HZ;
        }
        s_force_redraw = 1;
    }
    last_btn1 = btn1;
}
