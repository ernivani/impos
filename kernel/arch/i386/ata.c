#include <kernel/ata.h>
#include <stdio.h>
#include <string.h>

#define ATA_PRIMARY_IO      0x1F0
#define ATA_PRIMARY_CONTROL 0x3F6

#define ATA_REG_DATA       0x00
#define ATA_REG_ERROR      0x01
#define ATA_REG_FEATURES   0x01
#define ATA_REG_SECCOUNT0  0x02
#define ATA_REG_LBA0       0x03
#define ATA_REG_LBA1       0x04
#define ATA_REG_LBA2       0x05
#define ATA_REG_HDDEVSEL   0x06
#define ATA_REG_COMMAND    0x07
#define ATA_REG_STATUS     0x07
#define ATA_REG_CONTROL    0x00  /* on control base */

/* Global (not static) to avoid BSS memory corruption from large fs arrays */
int ata_available = 0;

/* I/O port access functions */
static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    asm volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outb(uint16_t port, uint8_t val) {
    asm volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline void insw(uint16_t port, void* addr, uint32_t count) {
    asm volatile("rep insw" : "+D"(addr), "+c"(count) : "d"(port) : "memory");
}

static inline void outsw(uint16_t port, const void* addr, uint32_t count) {
    asm volatile("rep outsw" : "+S"(addr), "+c"(count) : "d"(port) : "memory");
}

static int ata_wait_bsy(void) {
    uint8_t status;
    for (int i = 0; i < 1000000; i++) {
        status = inb(ATA_PRIMARY_IO + ATA_REG_STATUS);
        if (!(status & ATA_SR_BSY))
            return 0;
    }
    return -1;
}

static int ata_wait_drq(void) {
    uint8_t status;
    for (int i = 0; i < 1000000; i++) {
        status = inb(ATA_PRIMARY_IO + ATA_REG_STATUS);
        if (status & ATA_SR_DRQ)
            return 0;
    }
    return -1;
}

static int ata_check_error(void) {
    uint8_t status = inb(ATA_PRIMARY_IO + ATA_REG_STATUS);
    if (status & ATA_SR_ERR) {
        uint8_t err = inb(ATA_PRIMARY_IO + ATA_REG_ERROR);
        printf("ATA error: status=0x%x error=0x%x\n", status, err);
        return -1;
    }
    if (status & ATA_SR_DF) {
        printf("ATA drive fault\n");
        return -1;
    }
    return 0;
}

static void ata_io_delay(void) {
    inb(ATA_PRIMARY_IO + ATA_REG_STATUS);
    inb(ATA_PRIMARY_IO + ATA_REG_STATUS);
    inb(ATA_PRIMARY_IO + ATA_REG_STATUS);
    inb(ATA_PRIMARY_IO + ATA_REG_STATUS);
}

int ata_initialize(void) {
    outb(ATA_PRIMARY_IO + ATA_REG_HDDEVSEL, 0xA0);
    ata_io_delay();

    if (ata_wait_bsy() != 0) {
        printf("ATA: No disk detected (BSY timeout)\n");
        ata_available = 0;
        return -1;
    }

    outb(ATA_PRIMARY_IO + ATA_REG_COMMAND, ATA_CMD_IDENTIFY);
    ata_io_delay();

    uint8_t status = inb(ATA_PRIMARY_IO + ATA_REG_STATUS);
    if (status == 0) {
        printf("ATA: No disk detected\n");
        ata_available = 0;
        return -1;
    }

    if (ata_wait_bsy() != 0 || ata_wait_drq() != 0) {
        printf("ATA: Disk identification failed\n");
        ata_available = 0;
        return -1;
    }

    uint16_t identify[256];
    insw(ATA_PRIMARY_IO + ATA_REG_DATA, identify, 256);

    ata_available = 1;
    return 0;
}

int ata_is_available(void) {
    return ata_available;
}

int ata_read_sectors(uint32_t lba, uint8_t sector_count, uint8_t* buffer) {
    if (!ata_available)
        return -1;

    if (sector_count == 0)
        return -1;

    if (ata_wait_bsy() != 0)
        return -1;

    outb(ATA_PRIMARY_IO + ATA_REG_HDDEVSEL, 0xE0 | ((lba >> 24) & 0x0F));
    ata_io_delay();

    outb(ATA_PRIMARY_IO + ATA_REG_SECCOUNT0, sector_count);
    outb(ATA_PRIMARY_IO + ATA_REG_LBA0, (uint8_t)lba);
    outb(ATA_PRIMARY_IO + ATA_REG_LBA1, (uint8_t)(lba >> 8));
    outb(ATA_PRIMARY_IO + ATA_REG_LBA2, (uint8_t)(lba >> 16));

    outb(ATA_PRIMARY_IO + ATA_REG_COMMAND, ATA_CMD_READ_PIO);
    ata_io_delay();

    for (int i = 0; i < sector_count; i++) {
        if (ata_wait_bsy() != 0)
            return -1;
        
        if (ata_check_error() != 0)
            return -1;

        if (ata_wait_drq() != 0)
            return -1;

        insw(ATA_PRIMARY_IO + ATA_REG_DATA, buffer + (i * ATA_SECTOR_SIZE), 256);
    }

    return 0;
}

int ata_write_sectors(uint32_t lba, uint8_t sector_count, const uint8_t* buffer) {
    if (!ata_available)
        return -1;

    if (sector_count == 0)
        return -1;

    if (ata_wait_bsy() != 0)
        return -1;

    outb(ATA_PRIMARY_IO + ATA_REG_HDDEVSEL, 0xE0 | ((lba >> 24) & 0x0F));
    ata_io_delay();

    outb(ATA_PRIMARY_IO + ATA_REG_SECCOUNT0, sector_count);
    outb(ATA_PRIMARY_IO + ATA_REG_LBA0, (uint8_t)lba);
    outb(ATA_PRIMARY_IO + ATA_REG_LBA1, (uint8_t)(lba >> 8));
    outb(ATA_PRIMARY_IO + ATA_REG_LBA2, (uint8_t)(lba >> 16));

    outb(ATA_PRIMARY_IO + ATA_REG_COMMAND, ATA_CMD_WRITE_PIO);
    ata_io_delay();

    for (int i = 0; i < sector_count; i++) {
        if (ata_wait_bsy() != 0)
            return -1;
        
        if (ata_check_error() != 0)
            return -1;

        if (ata_wait_drq() != 0)
            return -1;

        outsw(ATA_PRIMARY_IO + ATA_REG_DATA, buffer + (i * ATA_SECTOR_SIZE), 256);
    }

    if (ata_wait_bsy() != 0)
        return -1;

    return 0;
}

int ata_flush(void) {
    if (!ata_available)
        return -1;

    if (ata_wait_bsy() != 0)
        return -1;

    outb(ATA_PRIMARY_IO + ATA_REG_COMMAND, ATA_CMD_CACHE_FLUSH);
    ata_io_delay();

    if (ata_wait_bsy() != 0)
        return -1;

    if (ata_check_error() != 0)
        return -1;

    return 0;
}
