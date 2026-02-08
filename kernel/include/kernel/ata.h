#ifndef _KERNEL_ATA_H
#define _KERNEL_ATA_H

#include <stdint.h>
#include <stddef.h>


#define ATA_SECTOR_SIZE 512

#define ATA_SR_BSY  0x80    /* Busy */
#define ATA_SR_DRDY 0x40    /* Drive ready */
#define ATA_SR_DF   0x20    /* Drive write fault */
#define ATA_SR_DSC  0x10    /* Drive seek complete */
#define ATA_SR_DRQ  0x08    /* Data request ready */
#define ATA_SR_CORR 0x04    /* Corrected data */
#define ATA_SR_IDX  0x02    /* Index */
#define ATA_SR_ERR  0x01    /* Error */

#define ATA_ER_BBK   0x80   /* Bad block */
#define ATA_ER_UNC   0x40   /* Uncorrectable data */
#define ATA_ER_MC    0x20   /* Media changed */
#define ATA_ER_IDNF  0x10   /* ID mark not found */
#define ATA_ER_MCR   0x08   /* Media change request */
#define ATA_ER_ABRT  0x04   /* Command aborted */
#define ATA_ER_TK0NF 0x02   /* Track 0 not found */
#define ATA_ER_AMNF  0x01   /* No address mark */

#define ATA_CMD_READ_PIO        0x20
#define ATA_CMD_READ_PIO_EXT    0x24
#define ATA_CMD_WRITE_PIO       0x30
#define ATA_CMD_WRITE_PIO_EXT   0x34
#define ATA_CMD_CACHE_FLUSH     0xE7
#define ATA_CMD_CACHE_FLUSH_EXT 0xEA
#define ATA_CMD_IDENTIFY        0xEC

int ata_initialize(void);

int ata_read_sectors(uint32_t lba, uint8_t sector_count, uint8_t* buffer);

int ata_write_sectors(uint32_t lba, uint8_t sector_count, const uint8_t* buffer);

int ata_flush(void);

int ata_is_available(void);

#endif
