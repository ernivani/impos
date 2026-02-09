#include <kernel/acpi.h>
#include <kernel/io.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>

/* Cached ACPI state */
static int acpi_ready;
static uint32_t pm1a_cnt_blk;
static uint32_t pm1b_cnt_blk;
static uint16_t slp_typa;
static uint16_t slp_typb;
static uint32_t smi_cmd;
static uint8_t acpi_enable_val;

static int acpi_checksum(void *ptr, uint32_t len) {
    uint8_t sum = 0;
    uint8_t *p = (uint8_t *)ptr;
    for (uint32_t i = 0; i < len; i++)
        sum += p[i];
    return sum == 0;
}

/* Scan memory for "RSD PTR " on 16-byte boundaries */
static struct rsdp_descriptor *acpi_scan_region(uint32_t start, uint32_t length) {
    uint8_t *mem = (uint8_t *)(uintptr_t)start;
    for (uint32_t off = 0; off < length; off += 16) {
        struct rsdp_descriptor *rsdp = (struct rsdp_descriptor *)(mem + off);
        if (memcmp(rsdp->signature, "RSD PTR ", 8) == 0) {
            if (acpi_checksum(rsdp, 20))
                return rsdp;
        }
    }
    return NULL;
}

static struct rsdp_descriptor *acpi_find_rsdp(void) {
    /* Search EBDA: BDA at 0x040E contains EBDA segment >> 4 */
    uint16_t ebda_seg = *(uint16_t *)(uintptr_t)0x040E;
    uint32_t ebda_addr = (uint32_t)ebda_seg << 4;
    if (ebda_addr >= 0x80000 && ebda_addr < 0xA0000) {
        struct rsdp_descriptor *r = acpi_scan_region(ebda_addr, 1024);
        if (r) return r;
    }

    /* Search main BIOS area */
    return acpi_scan_region(0xE0000, 0x20000);
}

/* Find an ACPI table by 4-char signature in the RSDT */
static struct acpi_sdt_header *acpi_find_table(struct acpi_sdt_header *rsdt,
                                                const char *sig) {
    uint32_t entries = (rsdt->length - sizeof(struct acpi_sdt_header)) / 4;
    uint32_t *ptrs = (uint32_t *)((uint8_t *)rsdt + sizeof(struct acpi_sdt_header));

    for (uint32_t i = 0; i < entries; i++) {
        struct acpi_sdt_header *h = (struct acpi_sdt_header *)(uintptr_t)ptrs[i];
        if (memcmp(h->signature, sig, 4) == 0) {
            if (acpi_checksum(h, h->length))
                return h;
        }
    }
    return NULL;
}

/* Parse the \_S5 object from DSDT AML bytecode to get SLP_TYPa/b */
static int acpi_parse_s5(struct acpi_sdt_header *dsdt) {
    uint8_t *start = (uint8_t *)dsdt + sizeof(struct acpi_sdt_header);
    uint32_t len = dsdt->length - sizeof(struct acpi_sdt_header);
    uint8_t *end = start + len;

    /* Search for "_S5_" in the AML stream */
    for (uint8_t *p = start; p < end - 4; p++) {
        if (memcmp(p, "_S5_", 4) != 0)
            continue;

        /* Back up to check for NameOp (0x08) or scope prefix */
        if (p > start && *(p - 1) == 0x08) {
            /* NameOp "_S5_" PackageOp ... */
        } else if (p > start + 1 && *(p - 2) == 0x08 && *(p - 1) == '\\') {
            /* NameOp "\_S5_" */
        } else {
            continue;
        }

        p += 4; /* skip "_S5_" */
        if (p >= end || *p != 0x12) /* PackageOp */
            continue;
        p++;

        /* Skip PkgLength (variable encoding) */
        if (p >= end) continue;
        uint8_t pkg_lead = *p;
        uint8_t pkg_len_bytes = (pkg_lead >> 6) & 3;
        if (pkg_len_bytes == 0)
            p += 1;
        else
            p += 1 + pkg_len_bytes;

        if (p >= end) continue;
        uint8_t num_elements = *p++;  /* NumElements */
        (void)num_elements;

        /* Parse SLP_TYPa */
        if (p >= end) continue;
        if (*p == 0x0A) {       /* BytePrefix */
            p++;
            if (p >= end) continue;
            slp_typa = *p++;
        } else if (*p == 0x0B) { /* WordPrefix */
            p++;
            if (p + 1 >= end) continue;
            slp_typa = *(uint16_t *)p;
            p += 2;
        } else {
            slp_typa = *p++;
        }

        /* Parse SLP_TYPb */
        if (p >= end) { slp_typb = 0; return 1; }
        if (*p == 0x0A) {
            p++;
            if (p >= end) { slp_typb = 0; return 1; }
            slp_typb = *p++;
        } else if (*p == 0x0B) {
            p++;
            if (p + 1 >= end) { slp_typb = 0; return 1; }
            slp_typb = *(uint16_t *)p;
            p += 2;
        } else {
            slp_typb = *p++;
        }

        return 1;
    }
    return 0;
}

/* Enable ACPI mode if not already enabled */
static void acpi_enable_if_needed(void) {
    if (pm1a_cnt_blk == 0) return;
    uint16_t val = inw((uint16_t)pm1a_cnt_blk);
    if (val & 1) return; /* SCI_EN already set */

    if (smi_cmd != 0 && acpi_enable_val != 0) {
        outb((uint16_t)smi_cmd, acpi_enable_val);
        /* Wait for SCI_EN */
        for (int i = 0; i < 300; i++) {
            if (inw((uint16_t)pm1a_cnt_blk) & 1)
                return;
            /* Simple delay */
            for (volatile int j = 0; j < 10000; j++);
        }
    }
}

int acpi_initialize(void) {
    acpi_ready = 0;

    struct rsdp_descriptor *rsdp = acpi_find_rsdp();
    if (!rsdp) {
        printf("ACPI: RSDP not found\n");
        return -1;
    }

    struct acpi_sdt_header *rsdt =
        (struct acpi_sdt_header *)(uintptr_t)rsdp->rsdt_address;
    if (!acpi_checksum(rsdt, rsdt->length)) {
        printf("ACPI: RSDT checksum invalid\n");
        return -1;
    }

    struct acpi_sdt_header *fadt_hdr = acpi_find_table(rsdt, "FACP");
    if (!fadt_hdr) {
        printf("ACPI: FADT not found\n");
        return -1;
    }

    struct acpi_fadt *fadt = (struct acpi_fadt *)fadt_hdr;
    pm1a_cnt_blk = fadt->pm1a_control_block;
    pm1b_cnt_blk = fadt->pm1b_control_block;
    smi_cmd = fadt->smi_command_port;
    acpi_enable_val = fadt->acpi_enable;

    struct acpi_sdt_header *dsdt =
        (struct acpi_sdt_header *)(uintptr_t)fadt->dsdt;
    if (!dsdt || !acpi_checksum(dsdt, dsdt->length)) {
        printf("ACPI: DSDT invalid\n");
        return -1;
    }

    if (!acpi_parse_s5(dsdt)) {
        printf("ACPI: _S5 object not found in DSDT\n");
        return -1;
    }

    acpi_enable_if_needed();
    acpi_ready = 1;
    printf("ACPI: Initialized (PM1a=0x%x SLP_TYPa=%d)\n",
           pm1a_cnt_blk, slp_typa);
    return 0;
}

void acpi_shutdown(void) {
    if (acpi_ready) {
        asm volatile("cli");
        acpi_enable_if_needed();

        uint16_t val = (slp_typa << 10) | ACPI_SLP_EN;
        outw((uint16_t)pm1a_cnt_blk, val);

        if (pm1b_cnt_blk) {
            uint16_t val_b = (slp_typb << 10) | ACPI_SLP_EN;
            outw((uint16_t)pm1b_cnt_blk, val_b);
        }
    } else {
        /* Fallback: try common hardcoded ports */
        asm volatile("cli");
        outw(0x604, 0x2000);   /* QEMU i440fx */
        outw(0xB004, 0x2000);  /* Bochs / older QEMU */
        outw(0x4004, 0x3400);  /* VirtualBox */
    }

    printf("ACPI shutdown failed. System halted.\n");
    while (1) asm volatile("hlt");
}
