#include <kernel/elf_loader.h>
#include <kernel/fs.h>
#include <kernel/pmm.h>
#include <kernel/vmm.h>
#include <kernel/task.h>
#include <kernel/signal.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ── Helpers ─────────────────────────────────────────────────── */

static uint32_t align_up(uint32_t val, uint32_t align) {
    return (val + align - 1) & ~(align - 1);
}

static uint32_t align_down(uint32_t val, uint32_t align) {
    return val & ~(align - 1);
}

/* Track a PMM frame in the task's elf_frames[] for cleanup */
static int elf_track_frame(task_info_t *t, uint32_t frame) {
    if (t->num_elf_frames >= 64)
        return -1;
    t->elf_frames[t->num_elf_frames++] = frame;
    return 0;
}

/* ── ELF Detection ───────────────────────────────────────────── */

int elf_detect(const uint8_t *data, size_t size) {
    if (size < 4) return 0;
    return (data[0] == ELFMAG0 && data[1] == ELFMAG1 &&
            data[2] == ELFMAG2 && data[3] == ELFMAG3);
}

/* ── ELF Loader ──────────────────────────────────────────────── */

int elf_run(const char *filename) {
    /* Read file into buffer */
    uint8_t *file_data = malloc(MAX_FILE_SIZE);
    if (!file_data) {
        printf("elf: out of memory\n");
        return -1;
    }

    size_t file_size = MAX_FILE_SIZE;
    if (fs_read_file(filename, file_data, &file_size) < 0) {
        free(file_data);
        return -2;
    }

    /* Validate ELF header — silent returns for non-ELF files (used by auto-detect) */
    if (file_size < sizeof(Elf32_Ehdr)) {
        free(file_data);
        return -3;
    }

    Elf32_Ehdr *ehdr = (Elf32_Ehdr *)file_data;

    if (!elf_detect(file_data, file_size)) {
        free(file_data);
        return -3;
    }

    if (ehdr->e_ident[4] != ELFCLASS32 || ehdr->e_ident[5] != ELFDATA2LSB) {
        printf("elf: not 32-bit little-endian\n");
        free(file_data);
        return -4;
    }

    if (ehdr->e_type != ET_EXEC || ehdr->e_machine != EM_386) {
        printf("elf: not i386 executable (type=%d machine=%d)\n",
               ehdr->e_type, ehdr->e_machine);
        free(file_data);
        return -5;
    }

    if (ehdr->e_phnum == 0 || ehdr->e_phoff == 0) {
        printf("elf: no program headers\n");
        free(file_data);
        return -6;
    }

    /* Find a free task slot */
    uint32_t flags = 0;
    __asm__ volatile ("pushf; pop %0; cli" : "=r"(flags));

    int tid = -1;
    for (int i = 4; i < TASK_MAX; i++) {
        task_info_t *t = task_get_raw(i);
        if (t && !t->active) {
            tid = i;
            break;
        }
    }
    if (tid < 0) {
        __asm__ volatile ("push %0; popf" : : "r"(flags));
        printf("elf: no free task slots\n");
        free(file_data);
        return -7;
    }

    /* Reserve the slot */
    task_info_t *task = task_get_raw(tid);
    memset(task, 0, sizeof(task_info_t));
    task->active = 1;
    task->state = TASK_STATE_BLOCKED;

    __asm__ volatile ("push %0; popf" : : "r"(flags));

    /* Create per-process page directory */
    uint32_t pd = vmm_create_user_pagedir();
    if (!pd) {
        printf("elf: failed to allocate page directory\n");
        task->active = 0;
        task->state = TASK_STATE_UNUSED;
        free(file_data);
        return -8;
    }

    /* Load PT_LOAD segments */
    Elf32_Phdr *phdr = (Elf32_Phdr *)(file_data + ehdr->e_phoff);
    uint32_t brk_end = 0;

    for (int i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_type != PT_LOAD)
            continue;

        uint32_t vaddr = phdr[i].p_vaddr;
        uint32_t memsz = phdr[i].p_memsz;
        uint32_t filesz = phdr[i].p_filesz;
        uint32_t offset = phdr[i].p_offset;

        uint32_t seg_start = align_down(vaddr, PAGE_SIZE);
        uint32_t seg_end = align_up(vaddr + memsz, PAGE_SIZE);

        /* Allocate and map pages for this segment */
        for (uint32_t va = seg_start; va < seg_end; va += PAGE_SIZE) {
            uint32_t frame = pmm_alloc_frame();
            if (!frame) {
                printf("elf: out of physical memory\n");
                goto fail;
            }
            memset((void *)frame, 0, PAGE_SIZE);

            uint32_t pte_flags = PTE_PRESENT | PTE_USER;
            if (phdr[i].p_flags & PF_W)
                pte_flags |= PTE_WRITABLE;

            if (!vmm_map_user_page(pd, va, frame, pte_flags)) {
                pmm_free_frame(frame);
                printf("elf: failed to map page at 0x%x\n", va);
                goto fail;
            }

            elf_track_frame(task, frame);

            /* Copy file data into this page */
            uint32_t copy_start = 0;
            uint32_t copy_len = 0;

            if (va < vaddr) {
                /* Page starts before segment vaddr */
                uint32_t skip = vaddr - va;
                copy_start = skip;
                if (filesz > 0) {
                    copy_len = PAGE_SIZE - skip;
                    if (copy_len > filesz) copy_len = filesz;
                }
            } else {
                /* How far into the file data are we? */
                uint32_t file_off = (va - vaddr);
                if (file_off < filesz) {
                    copy_len = filesz - file_off;
                    if (copy_len > PAGE_SIZE) copy_len = PAGE_SIZE;
                }
            }

            if (copy_len > 0) {
                uint32_t src_off;
                if (va < vaddr)
                    src_off = offset;
                else
                    src_off = offset + (va - vaddr);

                if (src_off + copy_len <= file_size)
                    memcpy((void *)(frame + copy_start), file_data + src_off, copy_len);
            }
        }

        /* Track highest loaded address for brk */
        uint32_t seg_top = align_up(vaddr + memsz, PAGE_SIZE);
        if (seg_top > brk_end)
            brk_end = seg_top;
    }

    /* Allocate user stack (4KB at USER_SPACE_BASE = 0x40000000) */
    uint32_t ustack = pmm_alloc_frame();
    if (!ustack) {
        printf("elf: failed to allocate user stack\n");
        goto fail;
    }
    memset((void *)ustack, 0, PAGE_SIZE);

    if (!vmm_map_user_page(pd, USER_SPACE_BASE, ustack,
                            PTE_PRESENT | PTE_WRITABLE | PTE_USER)) {
        pmm_free_frame(ustack);
        printf("elf: failed to map user stack\n");
        goto fail;
    }

    /* Allocate kernel stack (4KB) */
    uint32_t kstack = pmm_alloc_frame();
    if (!kstack) {
        printf("elf: failed to allocate kernel stack\n");
        pmm_free_frame(ustack);
        goto fail;
    }
    memset((void *)kstack, 0, PAGE_SIZE);

    /* User stack pointer: top of user stack page minus a small alignment gap.
     * Linux ABI requires 16-byte aligned stack before call. */
    uint32_t user_esp = USER_SPACE_BASE + PAGE_SIZE - 16;

    /* Write argc=0, argv=NULL, envp=NULL on user stack (musl expects these) */
    uint32_t *usp = (uint32_t *)(ustack + PAGE_SIZE - 16);
    usp[0] = 0;  /* argc */
    usp[1] = 0;  /* argv */
    usp[2] = 0;  /* envp */
    usp[3] = 0;  /* aux terminator */

    /* Build ring 3 iret frame on kernel stack */
    uint32_t *ksp = (uint32_t *)(kstack + PAGE_SIZE);

    /* iret frame: SS, UserESP, EFLAGS, CS, EIP */
    *(--ksp) = 0x23;              /* SS: user data segment */
    *(--ksp) = user_esp;          /* UserESP */
    *(--ksp) = 0x202;             /* EFLAGS: IF=1 */
    *(--ksp) = 0x1B;              /* CS: user code segment */
    *(--ksp) = ehdr->e_entry;     /* EIP: ELF entry point */

    /* ISR stub pushes */
    *(--ksp) = 0;                 /* err_code */
    *(--ksp) = 0;                 /* int_no */

    /* pusha block (matches registers_t) */
    *(--ksp) = 0;                 /* EAX */
    *(--ksp) = 0;                 /* ECX */
    *(--ksp) = 0;                 /* EDX */
    *(--ksp) = 0;                 /* EBX */
    *(--ksp) = 0;                 /* ESP (ignored by popa) */
    *(--ksp) = 0;                 /* EBP */
    *(--ksp) = 0;                 /* ESI */
    *(--ksp) = 0;                 /* EDI */

    /* Segment registers: user data selector */
    *(--ksp) = 0x23;             /* DS */
    *(--ksp) = 0x23;             /* ES */
    *(--ksp) = 0x23;             /* FS */
    *(--ksp) = 0x23;             /* GS */

    /* Initialize task */
    flags = 0;
    __asm__ volatile ("pushf; pop %0; cli" : "=r"(flags));

    /* Extract short name from path for task name */
    const char *short_name = filename;
    const char *slash = strrchr(filename, '/');
    if (slash) short_name = slash + 1;

    strncpy(task->name, short_name, 31);
    task->name[31] = '\0';
    task->killable = 1;
    task->wm_id = -1;
    task->is_user = 1;
    task->is_elf = 1;
    task->kernel_stack = kstack;
    task->user_stack = ustack;
    task->kernel_esp = kstack + PAGE_SIZE;
    task->esp = (uint32_t)ksp;
    task->page_dir = pd;
    task->user_page_table = 0;  /* PTs freed via vmm_destroy_user_pagedir() */

    /* ELF-specific fields */
    task->brk_start = brk_end;
    task->brk_current = brk_end;
    task->mmap_next = 0x20000000;  /* 512MB — above ELF load range */
    task->tls_base = 0;

    sig_init(&task->sig);

    task->pid = task_assign_pid(tid);

    task->state = TASK_STATE_READY;

    __asm__ volatile ("push %0; popf" : : "r"(flags));

    free(file_data);

    return tid;

fail:
    /* Clean up on failure */
    for (int f = 0; f < task->num_elf_frames; f++) {
        if (task->elf_frames[f])
            pmm_free_frame(task->elf_frames[f]);
    }
    task->num_elf_frames = 0;
    vmm_destroy_user_pagedir(pd);
    task->active = 0;
    task->state = TASK_STATE_UNUSED;
    free(file_data);
    return -9;
}
