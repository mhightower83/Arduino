/* Copyright (c) 2015 Ivan Grokhotkov. All rights reserved. 
 * This file is part of eboot bootloader.
 *
 * Redistribution and use is permitted according to the conditions of the
 * 3-clause BSD license to be found in the LICENSE file.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <c_types.h>    // spi_flash.h
#include <spi_flash.h>  // struct flashchip
#include "flash.h"


int SPIEraseAreaEx(const uint32_t start, const uint32_t size)
{
    if ((start & (FLASH_SECTOR_SIZE - 1)) != 0) {
        return 1;
    }

    const uint32_t sectors_per_block = FLASH_BLOCK_SIZE / FLASH_SECTOR_SIZE;
    uint32_t current_sector = start / FLASH_SECTOR_SIZE;
    uint32_t sector_count = (size + FLASH_SECTOR_SIZE - 1) / FLASH_SECTOR_SIZE;
    const uint32_t end = current_sector + sector_count;

    for (; current_sector < end && (current_sector & (sectors_per_block-1)); 
        ++current_sector, --sector_count) {
        if (SPIEraseSector(current_sector)) {
            return 2;
        }
    }

    for (;current_sector + sectors_per_block <= end; 
        current_sector += sectors_per_block, 
        sector_count -= sectors_per_block) {
        if (SPIEraseBlock(current_sector / sectors_per_block)) {
            return 3;
        }
    }

    for (; current_sector < end; 
        ++current_sector, --sector_count) {
        if (SPIEraseSector(current_sector)) {
            return 4;
        }
    }

    return 0;
}
#define DEBUG_ERASE_CONFIG    // Add some reassuring debug messages
#ifdef DEBUG_ERASE_CONFIG
#define ERASE_CFG__ETS_PRINTF(...) ets_printf(__VA_ARGS__)
#else
#define ERASE_CFG__ETS_PRINTF(...) do {} while(0)
#endif

uint8_t read_flash_byte(const uint32_t addr); // eboot.c
#define ERASE_CONFIG_ALL_DATA (0x1FU)

uint32_t esp_c_magic_flash_chip_size(uint8_t byte)
{
    uint32_t chip_size = 256 * 1024;
    if (9 < byte || (4 < byte && 8 > byte)) {
        return 0;
    }
    if (0 == byte) {
        byte = 1;
    } else if (1 == byte) {
        byte = 0;
    }
    if (8 <= byte) {
        byte -= 3;
    }
    chip_size <<= byte;
    return chip_size;
}


bool erase_config(const uint32_t flash_erase_mask) {
    /* This is really the active configured size */
    uint32_t flash_size = flashchip->chip_size;
    uint32_t erase_mask = (flash_erase_mask & (uint32_t)ERASE_CONFIG_ALL_DATA);
    uint32_t sector = flash_size/SPI_FLASH_SEC_SIZE - 1U;

    for (; !!erase_mask; erase_mask >>= 1U, sector--) {
        if ((erase_mask & 1U)) {
            if (0 != SPIEraseSector(sector)) {
                ERASE_CFG__ETS_PRINTF("Erase sector 0x%04X failed!\n", sector);
                return false;
            } else {
                ERASE_CFG__ETS_PRINTF("Erased sector 0x%04X\n", sector);
            }
        }
    }

    return true;
}

void do_erase_flash_option(uint32_t erase_flash_option) {
    if (0U == (erase_flash_option & ~ERASE_CONFIG_ALL_DATA)) {
        ERASE_CFG__ETS_PRINTF("\nerase_config(0x%03X)\n", erase_flash_option);
    } else {
        return;
    }

    /*
       We patch and restore chip_size here. It should be noted that the ROM APIs
       use both the chip_size and sector_size to validate calling parameters.
       From what I see at least with esptool.py and core, there is a general
       assumption that sector_size is always 4K.

       I don't see a need to set and restore sector_size at this time.
     */
    uint32_t old_flash_size = flashchip->chip_size;
    flashchip->chip_size = esp_c_magic_flash_chip_size(read_flash_byte(3) >> 4);
    ERASE_CFG__ETS_PRINTF("flash size: %u(0x%X)\n", flashchip->chip_size, flashchip->chip_size);
    if (flashchip->chip_size) {
        erase_config(erase_flash_option);
    }
    flashchip->chip_size = old_flash_size;
}
