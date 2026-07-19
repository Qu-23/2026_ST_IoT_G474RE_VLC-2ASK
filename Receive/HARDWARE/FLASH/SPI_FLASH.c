
/******************************************************************************************

// SPI Flash Driver (W25Q16/W25Qxx series)

// STM32G474 HAL Library Version
// Source: DevEBox (mcudev.taobao.com)


 *****************************************************************************************/

#include "SPI_FLASH.h"

#include "main.h"
#include "spi.h"

uint16_t SPI_Flash_Type = W25Q16;     //  W25Q16

/************************************************************************************************/

// SPI3 transmit/receive one byte via HAL
// Sends TxData and returns received data

// STM32G474 HAL Library Version
// Source: DevEBox (mcudev.taobao.com)

/************************************************************************************************/

uint8_t SPI3_FLASH_ReadWriteByte(uint8_t TxData)
{
    uint8_t Rxdata;
	
    HAL_SPI_TransmitReceive(&hspi3,&TxData,&Rxdata,1, 1000);  
	
 	  return Rxdata;          		    
	
}

/************************************************************************************************/

// Initialize SPI FLASH

// STM32G474 HAL Library Version
// Source: DevEBox (mcudev.taobao.com)

/************************************************************************************************/

void SPI_Flash_init(void)
{
    uint8_t temp;

    // PA15 as Flash CS, output push-pull
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    __HAL_RCC_GPIOA_CLK_ENABLE();
    GPIO_InitStruct.Pin = GPIO_PIN_15;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    SPI_FLASH_CS(1);                                         

    SPI_Flash_Type = SPI_Flash_read_id();                    // FLASH ID.
    
    if (SPI_Flash_Type == W25Q256)                           // SPI FLASHW25Q256, 4
    {
        temp = SPI_Flash_read_sr(3);                         // 3

        if ((temp & 0x01) == 0)                             // ?4,?4
        {
            SPI_Flash_write_enable();                       //  */
            temp |= 1 << 1;                                 // ADP=1, 4
            SPI_Flash_write_sr(3, temp);                    // SR3 */
            
            SPI_FLASH_CS(0);
            SPI3_FLASH_ReadWriteByte(FLASH_Enable4ByteAddr);    // 4
            SPI_FLASH_CS(1);
        }
    }
}

/************************************************************************************************/

// Wait for Flash busy flag to clear (poll SR1 BUSY bit)

// STM32G474 HAL Library Version
// Source: DevEBox (mcudev.taobao.com)

/************************************************************************************************/

static void SPI_Flash_wait_busy(void)
{
    while ((SPI_Flash_read_sr(1) & 0x01) == 0x01);   // BUSY?
}

/************************************************************************************************/

// Send Write Enable command (06h) - set WEL bit in SR1

// STM32G474 HAL Library Version
// Source: DevEBox (mcudev.taobao.com)

/************************************************************************************************/

void SPI_Flash_write_enable(void)
{

    SPI_FLASH_CS(0);
    SPI3_FLASH_ReadWriteByte(FLASH_WriteEnable);        
    SPI_FLASH_CS(1);

}

/************************************************************************************************/

// Send 24-bit or 32-bit address to Flash (W25Q256 uses 32-bit address)

// STM32G474 HAL Library Version
// Source: DevEBox (mcudev.taobao.com)

/************************************************************************************************/

static void SPI_Flash_send_address(uint32_t address)
{
    if (SPI_Flash_Type == W25Q256)                     // W25Q2564
    {
        SPI3_FLASH_ReadWriteByte((uint8_t)((address)>>24)); //  bit31 ~ bit24
    } 
    SPI3_FLASH_ReadWriteByte((uint8_t)((address)>>16));     //  bit23 ~ bit16
    SPI3_FLASH_ReadWriteByte((uint8_t)((address)>>8));      //  bit15 ~ bit8
    SPI3_FLASH_ReadWriteByte((uint8_t)address);             //  bit7  ~ bit0
}

 /************************************************************************************************/

// Read Status Register (SR1/SR2/SR3) of W25Qxx Flash
// SR1 (05h): BIT7=SPR, BIT6=RV, BIT5=TB, BIT4=BP2, BIT3=BP1, BIT2=BP0, BIT1=WEL, BIT0=BUSY
// SR2 (35h): BIT7=SUS, BIT6=CMP, BIT5=LB3, BIT4=LB2, BIT3=LB1, BIT2=(R), BIT1=QE, BIT0=SRP1
// SR3 (15h): BIT7=HOLD/RST, BIT6=DRV1, BIT5=DRV0, BIT4=(R), BIT3=(R), BIT2=WPS, BIT1=ADP, BIT0=ADS
// Parameter: regno = 1~3, selects which status register to read

// STM32G474 HAL Library Version
// Source: DevEBox (mcudev.taobao.com)

/************************************************************************************************/

uint8_t SPI_Flash_read_sr(uint8_t regno)
{
    uint8_t byte = 0, command = 0;

    switch (regno)
    {
        case 1:
            command = FLASH_ReadStatusReg1; // 1
            break;

        case 2:
            command = FLASH_ReadStatusReg2; // 2
            break;

        case 3:
            command = FLASH_ReadStatusReg3; // 3
            break;

        default:
            command = FLASH_ReadStatusReg1;
            break;
    }

    SPI_FLASH_CS(0);
    SPI3_FLASH_ReadWriteByte(command);          
    byte = SPI3_FLASH_ReadWriteByte(0xFF);      
    SPI_FLASH_CS(1);
    
    return byte;
}

/************************************************************************************************/

// Write Status Register (SR1/SR2/SR3) of W25Qxx Flash
// Parameter: regno = 1~3, sr = value to write

// STM32G474 HAL Library Version
// Source: DevEBox (mcudev.taobao.com)

/************************************************************************************************/

void SPI_Flash_write_sr(uint8_t regno, uint8_t sr)
{
    uint8_t command = 0;

    switch (regno)
    {
        case 1:
            command = FLASH_WriteStatusReg1; // 1
            break;

        case 2:
            command = FLASH_WriteStatusReg2; // 2
            break;

        case 3:
            command = FLASH_WriteStatusReg3; // 3
            break;

        default:
            command = FLASH_WriteStatusReg1;
            break;
    }

    SPI_FLASH_CS(0);
    SPI3_FLASH_ReadWriteByte(command);          
    SPI3_FLASH_ReadWriteByte(sr);               
    SPI_FLASH_CS(1);

}

/************************************************************************************************/

// Read Manufacturer/Device ID (90h command)
// Returns 16-bit ID: high byte = Manufacturer, low byte = Device ID
// Refer to SPI_Flash.h for known ID values (e.g. W25Q16 = 0xEF14)

// STM32G474 HAL Library Version
// Source: DevEBox (mcudev.taobao.com)

/************************************************************************************************/

uint16_t SPI_Flash_read_id(void)
{
    uint16_t deviceid;

    SPI_FLASH_CS(0);
    SPI3_FLASH_ReadWriteByte(FLASH_ManufactDeviceID);   //  ID
    SPI3_FLASH_ReadWriteByte(0);                        
    SPI3_FLASH_ReadWriteByte(0);
    SPI3_FLASH_ReadWriteByte(0);
    deviceid = SPI3_FLASH_ReadWriteByte(0xFF) << 8;     // 8
    deviceid |= SPI3_FLASH_ReadWriteByte(0xFF);         // 8
    SPI_FLASH_CS(1);

    return deviceid;
}

/************************************************************************************************/

// Read data from SPI Flash at specified address (03h command)
// Supports up to 32-bit address (for W25Q256), max 65535 bytes per call

// STM32G474 HAL Library Version
// Source: DevEBox (mcudev.taobao.com)

/************************************************************************************************/

void SPI_Flash_read(uint8_t *pbuf, uint32_t addr, uint16_t datalen)
{
    uint16_t i;

    SPI_FLASH_CS(0);
    SPI3_FLASH_ReadWriteByte(FLASH_ReadData);       
    SPI_Flash_send_address(addr);                   
    
    for (i = 0; i < datalen; i++)
    {
        pbuf[i] = SPI3_FLASH_ReadWriteByte(0xFF);   
    }
    
    SPI_FLASH_CS(1);
}

/************************************************************************************************/

// Write one page (up to 256 bytes) to SPI Flash at specified address (02h command)
// Supports 32-bit address for W25Q256; max 256 bytes per page (must not cross page boundary)

// STM32G474 HAL Library Version
// Source: DevEBox (mcudev.taobao.com)

/************************************************************************************************/

static void SPI_Flash_write_page(uint8_t *pbuf, uint32_t addr, uint16_t datalen)
{
    uint16_t i;

    SPI_Flash_write_enable();                       

    SPI_FLASH_CS(0);
    SPI3_FLASH_ReadWriteByte(FLASH_PageProgram);    
    SPI_Flash_send_address(addr);                   

    for (i = 0; i < datalen; i++)
    {
        SPI3_FLASH_ReadWriteByte(pbuf[i]);          
    }
    
    SPI_FLASH_CS(1);
    SPI_Flash_wait_busy();                          // Wait for write completion
}

/************************************************************************************************/

// Write data to SPI Flash without erase check (handles page boundary crossing)
// Splits data into page-aligned chunks (256 bytes per page)
// Supports 32-bit address for W25Q256

// STM32G474 HAL Library Version
// Source: DevEBox (mcudev.taobao.com)

/************************************************************************************************/

static void SPI_Flash_write_nocheck(uint8_t *pbuf, uint32_t addr, uint16_t datalen)
{
    uint16_t pageremain;
	
    pageremain = 256 - addr % 256;      // Bytes remaining in current page

    if (datalen <= pageremain)          // Fits within current page
    {
        pageremain = datalen;
    }

    while (1)
    {
        // Write current page chunk, then advance pointer and address
        
        SPI_Flash_write_page(pbuf, addr, pageremain);

        if (datalen == pageremain)      // All data written
        {
            break;
        }
        else                            // More data remains
        {
            pbuf += pageremain;         // Advance buffer pointer
            addr += pageremain;         // Advance flash address
            datalen -= pageremain;      

            if (datalen > 256)          // More than one full page left
            {
                pageremain = 256;       // Write full page
            }
            else                        // Less than one page left
            {
                pageremain = datalen;   // Write remaining bytes
            }
        }
    }
}

/************************************************************************************************/

// Write data to SPI Flash with automatic erase-before-write
// Flash memory layout: 256 bytes/Page, 4KB/Sector, 64KB/Block (16 sectors)
// Erases sector (4KB) if target area is not blank (0xFF)
// Supports 32-bit address for W25Q256

// STM32G474 HAL Library Version
// Source: DevEBox (mcudev.taobao.com)

/************************************************************************************************/

uint8_t WR_Flash_buf[4096];   // 4KB sector buffer

void SPI_Flash_write(uint8_t *pbuf, uint32_t addr, uint16_t datalen)
{
    uint32_t secpos;
    uint16_t secoff;
    uint16_t secremain;
    uint16_t i;
	
    uint8_t *SPI_Flash_buf;

    SPI_Flash_buf = WR_Flash_buf;
	
    secpos = addr / 4096;                       // Sector number
    secoff = addr % 4096;                       // Offset within sector
    secremain = 4096 - secoff;                  // Bytes remaining in sector

    if (datalen <= secremain)
    {
        secremain = datalen;                    // Fits within one sector
    }

    while (1)
    {
			
        SPI_Flash_read(SPI_Flash_buf, secpos * 4096, 4096);   // Read entire sector

        for (i = 0; i < secremain; i++)                     // Check if target area is blank
        {
            if (SPI_Flash_buf[secoff + i] != 0XFF)
            {
                break;                          // Non-blank byte found
            }
        }

        if (i < secremain)                      // Need to erase sector first
        {
            SPI_Flash_erase_sector(secpos);      // Erase sector (4KB)

            for (i = 0; i < secremain; i++)     // Merge new data into sector buffer
            {
                SPI_Flash_buf[i + secoff] = pbuf[i];
            }

            SPI_Flash_write_nocheck(SPI_Flash_buf, secpos * 4096, 4096);  // Write back sector
        }
        else                                    // Target area is blank, write directly
        {
            SPI_Flash_write_nocheck(pbuf, addr, secremain);              
        }

        if (datalen == secremain)
        {
            break;                              // All data written
        }
        else                                    
        {
            secpos++;                           // Next sector
            secoff = 0;                         // Offset = 0

            pbuf += secremain;                  
            addr += secremain;                  
            datalen -= secremain;               

            if (datalen > 4096)
            {
                secremain = 4096;               // Write full sector
            }
            else
            {
                secremain = datalen;            // Write remaining bytes
            }
        }
    }
}

/************************************************************************************************/

// Erase entire Flash chip (C7h command)
// WARNING: This erases ALL data on the chip!

// STM32G474 HAL Library Version
// Source: DevEBox (mcudev.taobao.com)

/************************************************************************************************/

void SPI_Flash_erase_chip(void)
{
    SPI_Flash_write_enable();                
    SPI_Flash_wait_busy();                   
    
    SPI_FLASH_CS(0);
    SPI3_FLASH_ReadWriteByte(FLASH_ChipErase);  // Chip erase command
    SPI_FLASH_CS(1);
    SPI_Flash_wait_busy();                   // Wait for erase completion
	
}

/**
 * @brief  Erase one sector (4KB) of SPI Flash
 * @note   Erase time: typically 150ms per sector
 * @param  saddr: sector address (must be 4KB aligned)
 * @retval None
 */

/************************************************************************************************/

// Erase one sector (4KB) at specified address (20h command)
// Erase time: typically 150ms per sector

// STM32G474 HAL Library Version
// Source: DevEBox (mcudev.taobao.com)

/************************************************************************************************/

void SPI_Flash_erase_sector(uint32_t saddr)
{
    // Function: %x\r\n", saddr);               // falsh?,
    saddr *= 4096;
    SPI_Flash_write_enable();                    
    SPI_Flash_wait_busy();                       

    SPI_FLASH_CS(0);
    SPI3_FLASH_ReadWriteByte(FLASH_SectorErase);    
    SPI_Flash_send_address(saddr);               
    SPI_FLASH_CS(1);
    SPI_Flash_wait_busy();                       // ?
	
}

/************************************************************************************************/

// STM32G474 HAL Library Version
// Source: DevEBox (mcudev.taobao.com)

/************************************************************************************************/
