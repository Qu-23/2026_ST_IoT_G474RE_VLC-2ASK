#include "gui_menu.h"
#include "Lcd_Driver.h"
#include "GUI.h"
#include "TFT_demo.h"
#include "GBK_LibDrive.h"
#include "ask_protocol.h"
#include <string.h>
#include <stdio.h>

// extern declarations for image arrays defined in Picture.h
extern const unsigned char gImage_XHR128[32768];
extern const unsigned char gImage_XNH128[32768];
extern const unsigned char gImage_ATM128[32768];
extern const unsigned char gImage_qq[3200];

// Current mode and content index
static GUI_Mode_t current_mode = MODE_RAW_DATA;
static uint8_t content_idx = 0;

// Button state (for falling edge detection)
static uint8_t btn_mode_last = 1;
static uint8_t btn_idx_last = 1;

/*============================================================================*
 *  Decoded data from ASK receiver (filled by GUI_RX_OnFrame in ISR context,  *
 *  consumed by Draw_* functions in main loop).                               *
 *============================================================================*/

/* RAW data: up to 32 bytes displayed as hex dump */
static volatile uint8_t  rx_raw_updated  = 0;
static uint8_t           rx_raw_len      = 0;
static uint8_t           rx_raw_buf[32];

/* TEXT: null-terminated GBK string */
static volatile uint8_t  rx_text_updated = 0;
static uint8_t           rx_text_buf[ASK_MAX_PAYLOAD + 1];

/* GRAPHIC: 1-byte image index (0-based) */
static volatile uint8_t  rx_graphic_updated = 0;
static uint8_t           rx_graphic_idx     = 0;

/* AUDIO: circular buffer of 8-bit audio envelope levels for waveform */
#define AUDIO_HISTORY_LEN  64u
static volatile uint8_t  rx_audio_updated = 0;
static uint8_t           audio_history[AUDIO_HISTORY_LEN];
static volatile uint16_t audio_head  = 0;
static volatile uint16_t audio_count = 0;

/*============================================================================*
 *  GUI_RX_OnFrame - called from ASK_RX_OnFrameReceived (ISR context)         *
 *  Copies decoded frame payload into the display buffer for the matching     *
 *  type and sets a volatile flag.                                            *
 *============================================================================*/
void GUI_RX_OnFrame(uint8_t type, const uint8_t *payload, uint16_t len)
{
    switch (type)
    {
    case ASK_TYPE_RAW:
        if (len > sizeof(rx_raw_buf)) len = sizeof(rx_raw_buf);
        if (len > 0 && payload) memcpy(rx_raw_buf, payload, len);
        rx_raw_len = (uint8_t)len;
        rx_raw_updated = 1;
        break;

    case ASK_TYPE_TEXT:
        if (len > ASK_MAX_PAYLOAD) len = ASK_MAX_PAYLOAD;
        if (len > 0 && payload) memcpy(rx_text_buf, payload, len);
        rx_text_buf[len] = '\0';
        rx_text_updated = 1;
        break;

    case ASK_TYPE_GRAPHIC:
        if (len >= 1 && payload) rx_graphic_idx = payload[0];
        rx_graphic_updated = 1;
        break;

    case ASK_TYPE_AUDIO:
        if (len >= 1 && payload)
        {
            audio_history[audio_head] = payload[0];
            audio_head = (uint16_t)((audio_head + 1u) % AUDIO_HISTORY_LEN);
            if (audio_count < AUDIO_HISTORY_LEN) audio_count++;
            rx_audio_updated = 1;
        }
        break;

    default:
        break;
    }
}

// RAW DATA patterns (8-bit binary strings)
static const char *raw_patterns[] = {
	"",
};
#define RAW_PATTERN_COUNT (sizeof(raw_patterns) / sizeof(raw_patterns[0]))

// TEXT strings - user can replace with ANSI GB2312 encoded Chinese strings
static uint8_t *text_strings[] = {
	(uint8_t *)"",
};
#define TEXT_STRING_COUNT (sizeof(text_strings) / sizeof(text_strings[0]))

// GRAPHIC images
typedef struct {
    const unsigned char *data;
    uint8_t is_fullscreen;
} Graphic_Item_t;

static Graphic_Item_t graphic_items[] = {

};
#define GRAPHIC_COUNT (sizeof(graphic_items) / sizeof(graphic_items[0]))

// Button scan with debounce (falling edge detection)
static uint8_t Button_Scan(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin, uint8_t *last_state)
{
    uint8_t current = (HAL_GPIO_ReadPin(GPIOx, GPIO_Pin) == GPIO_PIN_RESET) ? 0 : 1;
    if (*last_state == 1 && current == 0)
    {
        HAL_Delay(50);
        if (HAL_GPIO_ReadPin(GPIOx, GPIO_Pin) == GPIO_PIN_RESET)
        {
            *last_state = 0;
            return 1;
        }
    }
    *last_state = current;
    return 0;
}

// Draw title bar
static void Draw_Title(const char *title)
{
    Lcd_fill(0, 0, 128, 16, BLUE);
    Gui_DrawFont_F16(0, 0, WHITE, BLUE, (uint8_t *)title);
}

// MODE 1: RAW DATA display
// Shows decoded raw bytes from ASK link as hex + binary, or demo patterns.
static void Draw_RawData(uint8_t idx)
{
    char buf[24];
    uint8_t i;

    Lcd_Clear(GRAY0);
    Draw_Title("RAW DATA Mode");

    if (rx_raw_updated)
    {
        uint8_t n = rx_raw_len;
        uint8_t show_bytes = (n > 8) ? 8 : n;

        /* Show byte count */
        sprintf(buf, "RX %d bytes", n);
        Gui_DrawFont_F16(0, 20, YELLOW, GRAY0, (uint8_t *)buf);

        /* Hex dump of first 8 bytes */
        for (i = 0; i < show_bytes; i++)
        {
            sprintf(buf, "%02X", rx_raw_buf[i]);
            Gui_DrawFont_F16(0 + i * 16, 40, GREEN, GRAY0, (uint8_t *)buf);
        }

        /* Binary display of first byte (like original pattern display) */
        if (n > 0)
        {
            Gui_DrawFont_F16(0, 64, WHITE, GRAY0, (uint8_t *)"BIN:");
            for (i = 0; i < 8; i++)
            {
                buf[0] = (rx_raw_buf[0] & (0x80 >> i)) ? '1' : '0';
                buf[1] = '\0';
                Gui_DrawFont_F16(32 + i * 12, 64, WHITE, GRAY0, (uint8_t *)buf);
            }
        }
    }
    else
    {
        /* No data yet - show demo pattern */
        Gui_DrawFont_F16(0, 20, YELLOW, GRAY0, (uint8_t *)"Waiting RX...");

        for (i = 0; i < 8; i++)
        {
            buf[0] = raw_patterns[idx][i];
            buf[1] = '\0';
            Gui_DrawFont_F16(8 + i * 14, 40, GREEN, GRAY0, (uint8_t *)buf);
        }
    }
}

// MODE 2: TEXT display
// Shows decoded GBK/ANSI text string from ASK link, or demo strings.
static void Draw_Text(uint8_t idx)
{
    Lcd_Clear(GRAY0);
    Draw_Title("TEXT Mode");

    if (rx_text_updated)
    {
        /* Show received text (supports GBK Chinese + ASCII mixed) */
        Gui_DrawFont_F16(0, 30, RED, GRAY0, rx_text_buf);
    }
    else
    {
        /* No data yet - show demo string */
        Gui_DrawFont_F16(0, 20, YELLOW, GRAY0, (uint8_t *)"Waiting RX...");
        Gui_DrawFont_F16(8, 40, RED, GRAY0, text_strings[idx]);
    }
}

// MODE 3: GRAPHIC display
// Shows image at index received from ASK link, or cycles demo images.
static void Draw_Graphic(uint8_t idx)
{
    uint8_t use_idx = idx;

    /* If we received a graphic index from ASK, use it (mod count) */
    if (rx_graphic_updated)
    {
        use_idx = (rx_graphic_idx % (GRAPHIC_COUNT + 1));
    }

    if (use_idx == 0)
    {
        // Default: standby screen with title
        Lcd_Clear(GRAY0);
        Draw_Title("GRAPHIC Mode");
        if (rx_graphic_updated)
            Gui_DrawFont_F16(0, 30, YELLOW, GRAY0, (uint8_t *)"RX img: standby");
    }
    else if (use_idx <= GRAPHIC_COUNT)
    {
        Graphic_Item_t *item = &graphic_items[use_idx - 1];
        if (item->is_fullscreen)
        {
            Fullscreen_showimage(item->data);
        }
        else
        {
            // Display small image (40x40) centered, no tiling
            Lcd_Clear(GRAY0);
            Draw_Title("GRAPHIC Mode");
            showimage_single(item->data, 44, 44, 40, 40); // centered at (44,44)
        }
    }
}

// MODE 4: AUDIO display
// Shows a scrolling waveform of received audio envelope levels.
static void Draw_Audio(void)
{
    char buf[24];
    uint16_t i;
    uint16_t avail = audio_count;
    uint16_t bar_x, bar_h;
    uint8_t  level;

    Lcd_Clear(GRAY0);
    Draw_Title("AUDIO Mode");

    if (avail == 0)
    {
        Gui_DrawFont_F16(0, 30, YELLOW, GRAY0, (uint8_t *)"Waiting RX...");
        return;
    }

    /* Waveform area: y=18..127 (110 pixels high), x=0..127 (128 pixels) */
    /* Each audio sample = 2 pixels wide. Show up to 64 samples. */
    uint16_t start = (avail >= AUDIO_HISTORY_LEN)
                     ? audio_head   /* buffer full: start from oldest */
                     : 0;           /* buffer not full: start from 0   */

    for (i = 0; i < avail && i < AUDIO_HISTORY_LEN; i++)
    {
        uint16_t idx = (uint16_t)((start + i) % AUDIO_HISTORY_LEN);
        level = audio_history[idx];
        bar_x = i * 2;
        /* Map 0..255 to 0..100 pixels height */
        bar_h = (uint16_t)((uint32_t)level * 100u / 255u);
        if (bar_h < 1) bar_h = 1;

        /* Draw vertical bar from bottom up */
        Lcd_fill(bar_x, (uint16_t)(127 - bar_h), bar_x + 1, 127, GREEN);
    }

    /* Show latest level as number */
    {
        uint16_t last_idx = (uint16_t)((audio_head + AUDIO_HISTORY_LEN - 1) % AUDIO_HISTORY_LEN);
        sprintf(buf, "Lvl: %d", audio_history[last_idx]);
        Gui_DrawFont_F16(0, 18, YELLOW, GRAY0, (uint8_t *)buf);
    }
}

// Refresh current mode display
static void Refresh_Display(void)
{
    switch (current_mode)
    {
    case MODE_RAW_DATA:
        Draw_RawData(content_idx);
        break;
    case MODE_TEXT:
        Draw_Text(content_idx);
        break;
    case MODE_GRAPHIC:
        Draw_Graphic(content_idx);
        break;
    case MODE_AUDIO:
        Draw_Audio();
        break;
    }
}

// Initialize
void GUI_Menu_Init(void)
{
    Refresh_Display();
}

// Main loop process
void GUI_Menu_Process(void)
{
    /* PA1: cycle mode (RAW -> TEXT -> GRAPHIC -> AUDIO -> RAW...) */
    if (Button_Scan(BTN_MODE_PORT, BTN_MODE_PIN, &btn_mode_last))
    {
        content_idx = 0;
        current_mode = (GUI_Mode_t)((current_mode + 1) % 4);
        HAL_Delay(200);
        Refresh_Display();
    }

    /* PA0: increment content index within current mode */
    if (Button_Scan(BTN_IDX_PORT, BTN_IDX_PIN, &btn_idx_last))
    {
        switch (current_mode)
        {
        case MODE_RAW_DATA:
            content_idx = (uint8_t)((content_idx + 1) % RAW_PATTERN_COUNT);
            break;
        case MODE_TEXT:
            content_idx = (uint8_t)((content_idx + 1) % TEXT_STRING_COUNT);
            break;
        case MODE_GRAPHIC:
            content_idx = (uint8_t)((content_idx + 1) % (GRAPHIC_COUNT + 1));
            break;
        case MODE_AUDIO:
            /* no index cycling in audio mode */
            break;
        }
        HAL_Delay(200);
        Refresh_Display();
    }

    /* Live update: refresh display when new decoded data arrives for the
       currently active mode. Audio mode refreshes on every new sample. */
    switch (current_mode)
    {
    case MODE_RAW_DATA:
        if (rx_raw_updated) { rx_raw_updated = 0; Refresh_Display(); }
        break;
    case MODE_TEXT:
        if (rx_text_updated) { rx_text_updated = 0; Refresh_Display(); }
        break;
    case MODE_GRAPHIC:
        if (rx_graphic_updated) { rx_graphic_updated = 0; Refresh_Display(); }
        break;
    case MODE_AUDIO:
        if (rx_audio_updated) { rx_audio_updated = 0; Refresh_Display(); }
        break;
    }
}