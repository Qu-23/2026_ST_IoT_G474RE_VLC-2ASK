
/*********************************************************************************

// SPI Flash Driver (W25Q16/W25Qxx series) for STM32G474

// STM32G474 HAL Library Version

 *******************************************************************************/

#ifndef __SPI_FLASH_H
#define __SPI_FLASH_H

#include "main.h"

/******************************************************************************************/
// SPI Flash CS pin: PA15, active low
/******************************************************************************************/

// SPI Flash Chip Select macro (PA15)

#define SPI_FLASH_CS(x)      do{ x ? \
                                  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_15, GPIO_PIN_SET) : \
                                  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_15, GPIO_PIN_RESET); \
                            }while(0)

/* SPI_FLASH  */
														
#define W25Q80      0XEF13          /* W25Q80   оƬID */
#define W25Q16      0XEF14          /* W25Q16   оƬID */
#define W25Q32      0XEF15          /* W25Q32   оƬID */
#define W25Q64      0XEF16          /* W25Q64   оƬID */
#define W25Q128     0XEF17          /* W25Q128  оƬID */
#define W25Q256     0XEF18          /* W25Q256  оƬID */
														
////#define BY25Q64     0X6816          /* BY25Q64  ID */
////#define BY25Q128    0X6817          /* BY25Q128 ID */
////#define NM25Q64     0X5216          /* NM25Q64  ID */
////#define NM25Q128    0X5217          /* NM25Q128 ID */
														
extern uint16_t SPI_Flash_Type;      /* ����FLASHоƬ�ͺ� */
														
/*  */
#define FLASH_WriteEnable           0x06 
#define FLASH_WriteDisable          0x04 
#define FLASH_ReadStatusReg1        0x05 
#define FLASH_ReadStatusReg2        0x35 
#define FLASH_ReadStatusReg3        0x15 
#define FLASH_WriteStatusReg1       0x01 
#define FLASH_WriteStatusReg2       0x31 
#define FLASH_WriteStatusReg3       0x11 
#define FLASH_ReadData              0x03 
#define FLASH_FastReadData          0x0B 
#define FLASH_FastReadDual          0x3B 
#define FLASH_FastReadQuad          0xEB  
#define FLASH_PageProgram           0x02 
#define FLASH_PageProgramQuad       0x32 
#define FLASH_BlockErase            0xD8 
#define FLASH_SectorErase           0x20 
#define FLASH_ChipErase             0xC7 
#define FLASH_PowerDown             0xB9 
#define FLASH_ReleasePowerDown      0xAB 
#define FLASH_DeviceID              0xAB 
#define FLASH_ManufactDeviceID      0x90 
#define FLASH_JedecDeviceID         0x9F 
#define FLASH_Enable4ByteAddr       0xB7
#define FLASH_Exit4ByteAddr         0xE9
#define FLASH_SetReadParam          0xC0 
#define FLASH_EnterQPIMode          0x38
#define FLASH_ExitQPIMode           0xFF

static void SPI_Flash_wait_busy(void);                                   
static void SPI_Flash_send_address(uint32_t address);                    
static void SPI_Flash_write_page(uint8_t *pbuf, uint32_t addr, uint16_t datalen);    // page
static void SPI_Flash_write_nocheck(uint8_t *pbuf, uint32_t addr, uint16_t datalen); // flash,

uint8_t SPI3_FLASH_ReadWriteByte(uint8_t TxData);   

void SPI_Flash_init(void);                                               // 25QXX
uint16_t SPI_Flash_read_id(void);                                        // FLASH ID
void SPI_Flash_write_enable(void);                                       //  */
uint8_t SPI_Flash_read_sr(uint8_t regno);                                
void SPI_Flash_write_sr(uint8_t regno,uint8_t sr);                       

void SPI_Flash_erase_chip(void);                                         
void SPI_Flash_erase_sector(uint32_t saddr);                             
void SPI_Flash_read(uint8_t *pbuf, uint32_t addr, uint16_t datalen);     // flash
void SPI_Flash_write(uint8_t *pbuf, uint32_t addr, uint16_t datalen);    // flash

#endif
