#include "templogpro_storage_layout.h"

uint32_t templogpro_storage_is_record_address(uint32_t address)
{
    uint32_t sector_offset;

    if (address < TEMPLOGPRO_RECORDS_START_ADDRESS) {
        return 0u;
    }
    if (address >= TEMPLOGPRO_RECORDS_END_ADDRESS) {
        return 0u;
    }
    sector_offset = address & (TEMPLOGPRO_FLASH_SECTOR_SIZE - 1u);
    if ((sector_offset + TEMPLOGPRO_LOG_RECORD_ENCODED_SIZE) > TEMPLOGPRO_FLASH_SECTOR_SIZE) {
        return 0u;
    }
    if ((sector_offset % TEMPLOGPRO_LOG_RECORD_ENCODED_SIZE) != 0u) {
        return 0u;
    }
    return 1u;
}

uint32_t templogpro_storage_record_address(uint32_t index)
{
    uint32_t sector = index / TEMPLOGPRO_LOG_RECORDS_PER_SECTOR;
    uint32_t slot = index % TEMPLOGPRO_LOG_RECORDS_PER_SECTOR;

    return TEMPLOGPRO_RECORDS_START_ADDRESS +
           (sector * TEMPLOGPRO_FLASH_SECTOR_SIZE) +
           (slot * TEMPLOGPRO_LOG_RECORD_ENCODED_SIZE);
}

uint32_t templogpro_storage_sector_base(uint32_t address)
{
    return address & ~(TEMPLOGPRO_FLASH_SECTOR_SIZE - 1u);
}
