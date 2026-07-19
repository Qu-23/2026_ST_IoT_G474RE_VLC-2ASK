/**
  ******************************************************************************
  * @file    ask_protocol.c
  * @brief   2ASK optical communication - shared protocol implementation
  ******************************************************************************
  */

#include "ask_protocol.h"

/*============================================================================*
 *                              CRC-16-CCITT                                  *
 *============================================================================*/
uint16_t ASK_CRC16_Calc(const uint8_t *data, uint16_t len)
{
    uint16_t crc = 0xFFFFu;
    uint16_t i;
    uint8_t  j;

    for (i = 0; i < len; i++)
    {
        crc ^= (uint16_t)data[i] << 8;
        for (j = 0; j < 8; j++)
        {
            if (crc & 0x8000u)
                crc = (uint16_t)((crc << 1) ^ 0x1021u);
            else
                crc = (uint16_t)(crc << 1);
        }
    }
    return crc;
}