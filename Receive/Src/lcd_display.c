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
#include "Font.h"
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
static uint8_t  s_hz_page = 0;            /* HZ页码: 0=ONCHIP, 1+=FLASH分页 */
#define HZ_PAGE_MAX  3                     /* 139字/56每页=3页: C1 F2 F3 */
static uint8_t  s_force_redraw = 1;

/* 业务模式数据 */
#define BIZ_PAYLOAD_MAX  128
static uint8_t  s_biz_type = 0;
static uint8_t  s_biz_len = 0;
static uint8_t  s_biz_payload[BIZ_PAYLOAD_MAX];
static uint8_t  s_biz_updated = 0;
static uint32_t s_biz_frame_cnt = 0;
static uint8_t  s_biz_has_data = 0;
static uint8_t  s_biz_info_rows = 1;  /* 1=Type+Len same row, 2=Len wrapped to next line */

/* 图像接收缓存 */
#define IMAGE_WIDTH  128
#define IMAGE_HEIGHT 128
#define IMAGE_SIZE   2048
#define IMAGE_FRAMES 32  /* 2048 / 64 = 32 帧 (seq 0..31) */
static uint8_t s_image_buf[IMAGE_SIZE];
static uint8_t s_image_rx_mask[IMAGE_FRAMES];  /* 每帧是否已接收 */
static uint8_t s_image_complete = 0;
static uint8_t s_image_rx_count = 0; /* 已接收帧数 */

/* 待机动画 */
static uint8_t  s_anim_idx = 0;
static const char s_anim_chars[] = "|/-\\";

/* 连接状态（心跳看门狗） */
static uint8_t  s_link_ok = 0;          /* 1=LINK OK, 0=NO SIG */
static uint8_t  s_link_changed = 1;     /* 状态变化标志 */
static uint32_t s_last_hb_count = 0;
static uint32_t s_last_hb_tick = 0;

/* 前向声明：FLASH 字库子系统（定义在后面，BIZ 模式需提前引用） */
static uint8_t  flash_font_ready;
static void FlashFont_DrawTextBlock(uint16_t x, uint16_t y, const uint8_t *s,
                                     uint16_t fc, uint16_t bc,
                                     uint16_t max_x, uint16_t max_y);

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

/*============================================================================*
 *                          图像渲染辅助函数                                   *
 *============================================================================*/
/* 画线（Bresenham算法） */
static void DrawLine(int x0, int y0, int x1, int y1, uint16_t color)
{
    int dx = (x1 > x0) ? (x1 - x0) : (x0 - x1);
    int dy = (y1 > y0) ? (y1 - y0) : (y0 - y1);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;

    while (1)
    {
        Gui_DrawPoint(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 < dx) { err += dx; y0 += sy; }
    }
}

/* 画圆（中点圆算法） */
static void DrawCircle(int cx, int cy, int r, uint16_t color)
{
    int x = 0, y = r;
    int d = 3 - 2 * r;
    while (x <= y)
    {
        Gui_DrawPoint(cx + x, cy + y, color);
        Gui_DrawPoint(cx - x, cy + y, color);
        Gui_DrawPoint(cx + x, cy - y, color);
        Gui_DrawPoint(cx - x, cy - y, color);
        Gui_DrawPoint(cx + y, cy + x, color);
        Gui_DrawPoint(cx - y, cy + x, color);
        Gui_DrawPoint(cx + y, cy - x, color);
        Gui_DrawPoint(cx - y, cy - x, color);
        if (d < 0)
            d += 4 * x + 6;
        else
        {
            d += 4 * (x - y) + 10;
            y--;
        }
        x++;
    }
}

/* 画三角形（三条线） */
static void DrawTriangle(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t color)
{
    DrawLine(x0, y0, x1, y1, color);
    DrawLine(x1, y1, x2, y2, color);
    DrawLine(x2, y2, x0, y0, color);
}

/* 渲染缓存中的图像 */
static void Draw_ImageBuffer(void)
{
    Lcd_fill(0, 0, 128, 128, WHITE);  /* 清屏 */

    for (int row = 0; row < 128; row++)
    {
        for (int col_byte = 0; col_byte < 16; col_byte++)
        {
            uint8_t byte = s_image_buf[row * 16 + col_byte];
            for (int bit = 0; bit < 8; bit++)
            {
                uint8_t pixel = (byte >> (7 - bit)) & 1;  /* MSB first */
                uint16_t x = (uint16_t)(col_byte * 8 + bit);
                uint16_t y = (uint16_t)row;
                /* 0=黑点，1=白点（不画） */
                if (pixel == 0)
                    Gui_DrawPoint(x, y, BLACK);
            }
        }
    }
}

/* 处理 GRAPHIC 帧 */
static void ProcessGraphicFrame(const uint8_t *payload, uint8_t len)
{
    if (len < 2) return;

    /* 用载荷长度区分：几何图样 len=2，图像帧 len=65 */
    if (len == 2)
    {
        /* 几何图样：payload = [shape, param] */
        uint8_t shape = payload[0];
        s_image_complete = 0;  /* 清除图像模式 */

        Lcd_fill(0, 32, 128, 112, WHITE);  /* 清除内容区 */

        if (shape == 0)
        {
            /* 圆：中心 (64,72)，半径 32 */
            DrawCircle(64, 72, 32, BLACK);
        }
        else if (shape == 1)
        {
            /* 方：左上 (32,40)，右下 (96,104) */
            Lcd_fill(32, 40, 96, 104, BLACK);
        }
        else if (shape == 2)
        {
            /* 三角形：顶点 (64,40)，左下 (32,104)，右下 (96,104) */
            DrawTriangle(64, 40, 32, 104, 96, 104, BLACK);
        }
    }
    else if (len == 65 && payload[0] < IMAGE_FRAMES)
    {
        /* Logo 图像帧：payload = [seq, data0..data63] */
        uint8_t seq = payload[0];
        uint16_t offset = (uint16_t)(seq * 64);

        memcpy(&s_image_buf[offset], &payload[1], 64);
        if (s_image_rx_mask[seq] == 0)
        {
            s_image_rx_mask[seq] = 1;
            s_image_rx_count++;
        }

        /* 检查是否所有帧都已接收 */
        if (s_image_rx_count >= IMAGE_FRAMES)
        {
            s_image_complete = 1;
            Draw_ImageBuffer();
        }
        else
        {
            /* 接收中：在内容区显示进度 */
            Lcd_fill(0, 32, 128, 112, WHITE);
            ShowStr("IMG", 4, 48, COLOR_BIZ_VALUE, WHITE);
            ShowNum(s_image_rx_count, 36, 64, COLOR_OK, WHITE);
            ShowChar('/', 52, 64, COLOR_BIZ_LABEL, WHITE);
            ShowNum(IMAGE_FRAMES, 60, 64, COLOR_BIZ_LABEL, WHITE);
            /* 简易进度条 y=88 */
            {
                uint16_t bar_w = (uint16_t)(s_image_rx_count * 120 / IMAGE_FRAMES);
                Lcd_fill(4, 88, 4 + bar_w, 96, COLOR_OK);
            }
        }
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
        /* TEXT: 支持 ASCII + GB2312 汉字混合显示，自动换行
         * 用 FLASH 字库渲染。若 FLASH 字库未就绪则回退到 ASCII 显示。
         * 注意：s_biz_payload 不一定以 null 结尾，需用栈缓冲区添加终止符 */
        if (flash_font_ready)
        {
            uint8_t buf[BIZ_PAYLOAD_MAX + 1];
            memcpy(buf, s_biz_payload, show_len);
            buf[show_len] = 0;
            FlashFont_DrawTextBlock(0, y_start, buf, COLOR_BIZ_VALUE, WHITE, 128, 112);
        }
        else
        {
            /* 无 FLASH 字库：纯 ASCII 回退（非 ASCII 字节显示为 '.'） */
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
    }
    else if (s_biz_type == ASK_TYPE_GRAPHIC)
    {
        /* GRAPHIC: 几何图样或Logo图像 */
        ProcessGraphicFrame(s_biz_payload, s_biz_len);
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
    /* Footer: K1=硬件丝印(PA0)，DBG切换提示；FRM counter hidden for cleaner look */
    Lcd_fill(0, 112, 128, 128, COLOR_TITLE_BG);
    ShowStr("K1:DBG", 4, 112, COLOR_TITLE_FG, COLOR_TITLE_BG);
}

static void Draw_Biz(void)
{
    /* 图像完成时全屏显示，跳过标题/状态/底部栏 */
    if (s_image_complete)
    {
        if (s_force_redraw)
        {
            Draw_ImageBuffer();
            s_force_redraw = 0;
        }
        return;
    }

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
    ShowStr("K1:BIZ", 4, 112, COLOR_TITLE_FG, COLOR_TITLE_BG);
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
 *                          W25Q16 FLASH 字库                                *
 *============================================================================*
 * 数据格式：                                                               *
 *   0x0000: 魔数 0xAA 0x55 (2字节) - 标识已烧录                            *
 *   0x0002: 字数 N (1字节)                                                *
 *   0x0010: 索引区 (N × 2字节 GB2312编码)                                 *
 *   0x0200: 数据区 (N × 32字节点阵数据)                                   *
 * 烧录内容来自 Font.c 中的 hz16[] 数组                                 *
 *============================================================================*/
#define FLASH_FONT_MAGIC_ADDR   0x000000u
#define FLASH_FONT_COUNT_ADDR   0x000002u
#define FLASH_FONT_INDEX_ADDR   0x000010u
#define FLASH_FONT_DATA_ADDR    0x000200u
#define FLASH_FONT_MAX_COUNT    256u    /* 索引区RAM缓存上限，支持最多256字 */

static uint8_t  flash_font_index[FLASH_FONT_MAX_COUNT * 2];  /* 索引区RAM缓存 */
static uint8_t  flash_font_count = 0;                         /* 已烧录字数 */

/* 检测 W25Q16 是否已烧录字库 */
static uint8_t FlashFont_IsProgrammed(void)
{
    uint8_t buf[2];
    SPI_Flash_read(buf, FLASH_FONT_MAGIC_ADDR, 2);
    return (buf[0] == 0xAA && buf[1] == 0x55);
}

/* 烧录 Font.c 中的 hz16[] 到 W25Q16
 * 用 static 数组避免堆分配（嵌入式 malloc 不可靠） */
static void FlashFont_Program(void)
{
    /* 0x200(索引区) + 129*32(数据区) = 4640 字节，对齐到 4KB 边界用 4608 */
    static uint8_t buf[0x200 + 128 * 32];
    uint16_t pos = 0;
    uint16_t i, j;

    /* 构建烧录数据 */
    buf[pos++] = 0xAA;          /* 魔数 */
    buf[pos++] = 0x55;
    buf[pos++] = (uint8_t)hz16_num;  /* 总字数 */
    while (pos < 0x10) buf[pos++] = 0xFF;  /* 填充到索引区 */

    /* 索引区：hz16 索引 */
    for (i = 0; i < hz16_num; i++)
    {
        buf[pos++] = hz16[i].Index[0];
        buf[pos++] = hz16[i].Index[1];
    }
    while (pos < 0x200) buf[pos++] = 0xFF;  /* 填充到数据区 */

    /* 数据区：hz16 点阵 */
    for (i = 0; i < hz16_num; i++)
    {
        for (j = 0; j < 32; j++)
            buf[pos++] = (uint8_t)hz16[i].Msk[j];
    }

    /* 一次性写入（SPI_Flash_write 自动处理擦除和分页） */
    SPI_Flash_write(buf, 0, pos);
}

/* 初始化 FLASH 字库：检测→烧录→加载索引
 * 若 FLASH 中字数与 hz16_num 不一致，自动重烧录 */
static void FlashFont_Init(void)
{
    uint16_t expected = hz16_num;

    if (SPI_Flash_Type != 0xEF14)
    {
        flash_font_ready = 0;
        return;
    }

    if (!FlashFont_IsProgrammed())
    {
        FlashFont_Program();
    }
    else
    {
        /* 已烧录但字数不匹配：版本升级需要重烧录 */
        uint8_t stored_count = 0;
        SPI_Flash_read(&stored_count, FLASH_FONT_COUNT_ADDR, 1);
        if (stored_count != (uint8_t)expected)
            FlashFont_Program();
    }

    /* 加载索引和字数到 RAM */
    SPI_Flash_read(&flash_font_count, FLASH_FONT_COUNT_ADDR, 1);
    if (flash_font_count > FLASH_FONT_MAX_COUNT)
        flash_font_count = FLASH_FONT_MAX_COUNT;
    SPI_Flash_read(flash_font_index, FLASH_FONT_INDEX_ADDR, flash_font_count * 2);
    flash_font_ready = 1;
}

/* 从 W25Q16 读取单个汉字点阵并显示。返回1=找到并显示，0=未找到 */
static uint8_t FlashFont_DrawChar(uint16_t x, uint16_t y, uint8_t gb1, uint8_t gb2,
                                   uint16_t fc, uint16_t bc)
{
    uint8_t msk[32];
    uint8_t i, row, col;

    if (!flash_font_ready) return 0;

    /* 在RAM索引中查找 */
    for (i = 0; i < flash_font_count; i++)
    {
        if (flash_font_index[i * 2] == gb1 && flash_font_index[i * 2 + 1] == gb2)
        {
            /* 读取32字节点阵数据 */
            SPI_Flash_read(msk, FLASH_FONT_DATA_ADDR + (uint32_t)i * 32, 32);

            /* 渲染到LCD（与Gui_DrawFont_F16一致的解码逻辑） */
            for (row = 0; row < 16; row++)
            {
                for (col = 0; col < 8; col++)
                {
                    if (msk[row * 2] & (0x80 >> col))
                        Gui_DrawPoint(x + col, y + row, fc);
                    else if (fc != bc)
                        Gui_DrawPoint(x + col, y + row, bc);
                }
                for (col = 0; col < 8; col++)
                {
                    if (msk[row * 2 + 1] & (0x80 >> col))
                        Gui_DrawPoint(x + col + 8, y + row, fc);
                    else if (fc != bc)
                        Gui_DrawPoint(x + col + 8, y + row, bc);
                }
            }
            return 1;
        }
    }
    return 0;
}

/* 从 W25Q16 读取汉字字符串并显示（GB2312双字节，混合ASCII） */
static void FlashFont_DrawString(uint16_t x, uint16_t y, const uint8_t *s,
                                  uint16_t fc, uint16_t bc)
{
    while (*s)
    {
        if (*s < 0x80)
        {
            /* ASCII: 用片上字库显示 */
            Gui_DrawChar_Ascii(x, y, fc, bc, (char)*s);
            x += 8;
            s++;
        }
        else
        {
            /* GB2312汉字: 从FLASH读取 */
            if (FlashFont_DrawChar(x, y, s[0], s[1], fc, bc))
                x += 16;
            else
            {
                /* 未找到，显示占位符 */
                Gui_DrawChar_Ascii(x, y, fc, bc, '?');
                x += 8;
            }
            s += 2;
        }
    }
}

/* 多行文本块渲染：支持自动换行（汉字16px / ASCII 8px，行高16px）
 * 用于 BIZ 模式 TEXT 帧显示。max_x=行宽上限, max_y=底部y上限(不绘制>=max_y的行) */
static void FlashFont_DrawTextBlock(uint16_t x, uint16_t y, const uint8_t *s,
                                     uint16_t fc, uint16_t bc,
                                     uint16_t max_x, uint16_t max_y)
{
    while (*s && y < max_y)
    {
        if (*s < 0x80)
        {
            /* ASCII: 8px 宽 */
            if (x + 8 > max_x) { x = 0; y += 16; if (y >= max_y) break; }
            Gui_DrawChar_Ascii(x, y, fc, bc, (char)*s);
            x += 8;
            s++;
        }
        else
        {
            /* GB2312 汉字: 16px 宽 */
            if (x + 16 > max_x) { x = 0; y += 16; if (y >= max_y) break; }
            if (FlashFont_DrawChar(x, y, s[0], s[1], fc, bc))
                x += 16;
            else
            {
                Gui_DrawChar_Ascii(x, y, fc, bc, '?');
                x += 8;
            }
            s += 2;
        }
    }
}

/*============================================================================*
 *                          汉字测试模式（隐藏，PA1进入）                    *
 *============================================================================*/
/* HZ模式分页显示 hz16[] 数组中的所有汉字。
 * 每页 56 字 (7行×8字)，第8行(y=112)为状态栏。
 * Page 0: ONCHIP (Gui_DrawFont_F16 从 hz16[] 查找)
 * Page 1+: FLASH (FlashFont_DrawChar 从 W25Q16 读取)
 * 129字 / 56每页 = 3页 (ONCHIP 1页 + FLASH 2页) */

static void Draw_HzTitle(void)
{
    /* HZ模式全屏显示汉字，不画标题栏 */
    (void)0;
}

static void Draw_HzFooter(void)
{
    /* HZ模式全屏显示汉字，不画底部栏 */
    (void)0;
}

static void Draw_HzContent(void)
{
    /* 每页 56 字 (7行×8字)，第8行(y=112)为状态栏
     * Page 0: ONCHIP (Gui_DrawFont_F16 从 hz16[] 查找)
     * Page 1+: FLASH (FlashFont_DrawChar 从 W25Q16 读取)
     * 129字 / 56每页 = 3页 (Page0: 0-55, Page1: 56-111, Page2: 112-128) */
    uint8_t page_start = s_hz_page * 56;
    uint8_t page_count = 56;
    if (page_start >= hz16_num) page_count = 0;
    else if (page_start + page_count > hz16_num)
        page_count = hz16_num - page_start;

    Lcd_fill(0, 0, 128, 112, WHITE);

    if (page_count == 0)
    {
        ShowStr("Empty", 48, 48, COLOR_BIZ_LABEL, WHITE);
    }
    else if (s_hz_page == 0)
    {
        /* ONCHIP: 手动分行渲染（Gui_DrawFont_F16 不自动换行）
         * 每行 8 字 × 16px = 128px，共 7 行 */
        uint8_t row, col;
        for (row = 0; row < 7; row++)
        {
            uint8_t line[17];  /* 8字 × 2 + 1 */
            uint8_t llen = 0;
            for (col = 0; col < 8; col++)
            {
                uint8_t idx = page_start + row * 8 + col;
                if (idx >= hz16_num) break;
                line[llen++] = hz16[idx].Index[0];
                line[llen++] = hz16[idx].Index[1];
            }
            if (llen == 0) break;
            line[llen] = 0;
            Gui_DrawFont_F16(0, row * 16, COLOR_BIZ_VALUE, WHITE, line);
        }
    }
    else if (flash_font_ready)
    {
        /* FLASH: 从 W25Q16 索引读取 GB2312 编码，逐字渲染 */
        uint8_t gb_str[113];
        uint8_t slen = 0;
        for (uint8_t i = 0; i < page_count && slen < 112; i++)
        {
            uint8_t idx = page_start + i;
            if (idx >= flash_font_count) break;
            gb_str[slen++] = flash_font_index[idx * 2];
            gb_str[slen++] = flash_font_index[idx * 2 + 1];
        }
        gb_str[slen] = 0;
        FlashFont_DrawTextBlock(0, 0, gb_str, COLOR_BIZ_VALUE, WHITE, 128, 112);
    }
    else
    {
        ShowStr("Flash NA", 24, 48, COLOR_ERR, WHITE);
    }

    /* 第8行(y=112): 页码 + 字数 + K1翻页提示 */
    Lcd_fill(0, 112, 128, 128, GRAY2);
    char page_tag = (s_hz_page == 0) ? 'C' : 'F';
    ShowChar(page_tag, 0, 112, WHITE, GRAY2);
    ShowChar('0' + s_hz_page + 1, 8, 112, WHITE, GRAY2);
    ShowChar('/', 16, 112, WHITE, GRAY2);
    ShowChar('0' + HZ_PAGE_MAX, 24, 112, WHITE, GRAY2);
    ShowStr("N=", 40, 112, WHITE, GRAY2);
    ShowNum(page_count, 56, 112, WHITE, GRAY2);
    ShowStr("K1:FLIP", 80, 112, WHITE, GRAY2);
}

static void Draw_Hz(void)
{
    if (s_force_redraw)
    {
        Draw_HzContent();  /* 全屏8行汉字，无标题/状态/底部栏 */
        s_force_redraw = 0;
    }
    /* HZ模式不调用 Draw_LinkBar，避免覆盖第2行汉字 */
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

    HAL_Delay(600);  /* 让用户看清硬件检测结果 */

    /* FLASH 字库初始化（首次自动烧录，字数变化时重烧录） */
    if (SPI_Flash_Type == 0xEF14)
    {
        uint8_t need_prog = !FlashFont_IsProgrammed();
        /* 字数不匹配也需要重编程 */
        if (!need_prog)
        {
            uint8_t stored = 0;
            SPI_Flash_read(&stored, FLASH_FONT_COUNT_ADDR, 1);
            if (stored != (uint8_t)hz16_num)
                need_prog = 1;
        }
        Lcd_fill(0, 112, 128, 128, WHITE);  /* 清除底部行 */
        if (need_prog)
        {
            ShowStr("Font Prog...", 8, 112, COLOR_ERR, WHITE);
            HAL_Delay(700);
        }
        else
        {
            ShowStr("Font Load...", 8, 112, COLOR_OK, WHITE);
            HAL_Delay(700);
        }

        FlashFont_Init();

        Lcd_fill(0, 112, 128, 128, WHITE);
        if (flash_font_ready)
        {
            ShowStr("Font OK N=", 8, 112, COLOR_OK, WHITE);
            ShowNum(flash_font_count, 88, 112, COLOR_OK, WHITE);
        }
        else
            ShowStr("Font NA", 8, 112, COLOR_ERR, WHITE);

        HAL_Delay(600);  /* 让用户看清字库状态 */
    }

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

    /* 非图像帧或几何图样帧清除图像全屏状态 */
    if (type != ASK_TYPE_GRAPHIC || len == 2)
    {
        s_image_complete = 0;
        memset(s_image_rx_mask, 0, sizeof(s_image_rx_mask));
        s_image_rx_count = 0;
    }

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

    /* PA0: BIZ <-> DBG (非HZ模式) 或 HZ翻页 */
    uint8_t btn0 = HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0);
    if (last_btn0 == 1 && btn0 == 0)
    {
        if (s_mode == MODE_HZ)
        {
            s_hz_page = (s_hz_page + 1) % HZ_PAGE_MAX;
            s_force_redraw = 1;
        }
        else
        {
            s_mode = (s_mode == MODE_BIZ) ? MODE_DBG : MODE_BIZ;
            s_image_complete = 0;  /* 切换模式时清除图像全屏 */
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
