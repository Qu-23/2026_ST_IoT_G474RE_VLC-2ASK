#ifndef __GUI_MENU_H
#define __GUI_MENU_H

#include "main.h"
#include "GBK_LibDrive.h"

// Four display modes
typedef enum {
    MODE_RAW_DATA = 0,
    MODE_TEXT,
    MODE_GRAPHIC,
    MODE_AUDIO
} GUI_Mode_t;

// Button pin definitions
// PA0: increment content index within current mode
#define BTN_IDX_PORT    GPIOA
#define BTN_IDX_PIN     GPIO_PIN_0

// PA1: cycle through modes (RAW_DATA -> TEXT -> GRAPHIC -> AUDIO -> RAW_DATA...)
#define BTN_MODE_PORT   GPIOA
#define BTN_MODE_PIN    GPIO_PIN_1

// PA2: not used

void GUI_Menu_Init(void);
void GUI_Menu_Process(void);

/*============================================================================*
 *  ASK receiver -> GUI data bridge
 *  Called from ASK_RX_OnFrameReceived (ISR context) when a valid frame is
 *  decoded. Copies the payload into the appropriate display buffer and sets
 *  a volatile "new data" flag. The main-loop GUI refreshes the screen when
 *  the flag is set for the currently active mode.
 *============================================================================*/
void GUI_RX_OnFrame(uint8_t type, const uint8_t *payload, uint16_t len);

#endif
