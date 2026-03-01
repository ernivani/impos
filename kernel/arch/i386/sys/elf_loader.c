#include <kernel/elf_loader.h>
#include <kernel/fs.h>
#include <kernel/pmm.h>
#include <kernel/vmm.h>
#include <kernel/task.h>
#include <kernel/vma.h>
#include <kernel/frame_ref.h>
#include <kernel/signal.h>
#include <kernel/pipe.h>
#include <kernel/linux_syscall.h>
#include <kernel/crypto.h>
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

/* Maximum ELF binary size we'll attempt to load (16 MB).
 * Must NOT use MAX_FILE_SIZE (0xFFFFFFFF) — overflows 32-bit ALIGN. */
#define ELF_MAX_LOAD_SIZE (16 * 1024 * 1024)

/* ── Interpreter Loader (ET_DYN at fixed base) ───────────────── */

uint32_t elf_load_interp(uint32_t pd, const char *path, uint32_t base,
                         void *task_ptr, void *vt_ptr) {
    task_info_t *task = (task_info_t *)task_ptr;
    vma_table_t *vt = (vma_table_t *)vt_ptr;

    /* Read interpreter file */
    uint8_t *file_data = malloc(ELF_MAX_LOAD_SIZE);
    if (!file_data) return 0;

    size_t file_size = ELF_MAX_LOAD_SIZE;
    if (fs_read_file(path, file_data, &file_size) < 0) {
        free(file_data);
        return 0;
    }

    if (file_size < sizeof(Elf32_Ehdr)) {
        free(file_data);
        return 0;
    }

    Elf32_Ehdr *ehdr = (Elf32_Ehdr *)file_data;

    /* Validate: must be ET_DYN, i386 */
    if (!elf_detect(file_data, file_size) ||
        ehdr->e_ident[4] != ELFCLASS32 ||
        ehdr->e_ident[5] != ELFDATA2LSB ||
        ehdr->e_type != ET_DYN ||
        ehdr->e_machine != EM_386 ||
        ehdr->e_phnum == 0) {
        printf("elf_load_interp: invalid interpreter %s\n", path);
        free(file_data);
        return 0;
    }

    Elf32_Phdr *phdr = (Elf32_Phdr *)(file_data + ehdr->e_phoff);

    /* Load PT_LOAD segments at base + p_vaddr */
    for (int i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_type != PT_LOAD)
            continue;

        uint32_t vaddr = base + phdr[i].p_vaddr;
        uint32_t memsz = phdr[i].p_memsz;
        uint32_t filesz = phdr[i].p_filesz;
        uint32_t offset = phdr[i].p_offset;

        uint32_t seg_start = align_down(vaddr, PAGE_SIZE);
        uint32_t seg_end = align_up(vaddr + memsz, PAGE_SIZE);

        for (uint32_t va = seg_start; va < seg_end; va += PAGE_SIZE) {
            /* Check if page already mapped (overlapping segments) */
            uint32_t existing = vmm_get_pte(pd, va);
            if (existing & PTE_PRESENT) continue;

            uint32_t frame = pmm_alloc_frame();
            if (!frame) {
                printf("elf_load_interp: OOM at 0x%x\n", va);
                free(file_data);
                return 0;
            }
            memset((void *)frame, 0, PAGE_SIZE);

            uint32_t pte_flags = PTE_PRESENT | PTE_USER;
            if (phdr[i].p_flags & PF_W)
                pte_flags |= PTE_WRITABLE;

            if (!vmm_map_user_page(pd, va, frame, pte_flags)) {
                pmm_free_frame(frame);
                free(file_data);
                return 0;
            }

            elf_track_frame(task, frame);

            /* Copy file data into this page */
            uint32_t copy_start = 0, copy_len = 0;
            if (va < vaddr) {
                uint32_t skip = vaddr - va;
                copy_start = skip;
                if (filesz > 0) {
                    copy_len = PAGE_SIZE - skip;
                    if (copy_len > filesz) copy_len = filesz;
                }
            } else {
                uint32_t file_off = va - vaddr;
                if (file_off < filesz) {
                    copy_len = filesz - file_off;
                    if (copy_len > PAGE_SIZE) copy_len = PAGE_SIZE;
                }
            }

            if (copy_len > 0) {
                uint32_t src_off = (va < vaddr) ? offset : offset + (va - vaddr);
                if (src_off + copy_len <= file_size)
                    memcpy((void *)(frame + copy_start), file_data + src_off, copy_len);
            }
        }

        /* Register VMA for interpreter segment */
        if (vt) {
            uint32_t vflags = VMA_READ;
            if (phdr[i].p_flags & PF_W) vflags |= VMA_WRITE;
            if (phdr[i].p_flags & PF_X) vflags |= VMA_EXEC;
            vma_insert(vt, seg_start, seg_end, vflags, VMA_TYPE_ELF);
        }
    }

    uint32_t entry = base + ehdr->e_entry;
    free(file_data);
    return entry;
}

/* ── ELF Loader ──────────────────────────────────────────────── */

int elf_run(const char *filename) {
    const char *argv[] = { filename };
    return elf_run_argv(filename, 1, argv);
}

int elf_run_argv(const char *filename, int argc, const char **argv) {
    /* Read file into buffer */
    uint8_t *file_data = malloc(ELF_MAX_LOAD_SIZE);
    if (!file_data) {
        printf("elf: out of memory\n");
        return -1;
    }

    size_t file_size = ELF_MAX_LOAD_SIZE;
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

    if ((ehdr->e_type != ET_EXEC && ehdr->e_type != ET_DYN) ||
        ehdr->e_machine != EM_386) {
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

    /* ET_DYN (PIE) binaries have relative addresses; pick a base */
    uint32_t exec_base = 0;
    if (ehdr->e_type == ET_DYN)
        exec_base = 0x08048000;

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

        uint32_t vaddr = exec_base + phdr[i].p_vaddr;
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

    /* ── Scan for PT_INTERP and PT_PHDR ───────────────────────────── */
    char interp_path[256] = {0};
    uint32_t phdr_vaddr = 0;
    int phdr_vaddr_found = 0;

    for (int i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_type == PT_INTERP) {
            uint32_t len = phdr[i].p_filesz;
            if (len >= sizeof(interp_path)) len = sizeof(interp_path) - 1;
            memcpy(interp_path, file_data + phdr[i].p_offset, len);
            interp_path[len] = '\0';
            /* Strip trailing newline/null if present */
            while (len > 0 && (interp_path[len-1] == '\n' || interp_path[len-1] == '\0'))
                interp_path[--len] = '\0';
        }
        if (phdr[i].p_type == PT_PHDR) {
            phdr_vaddr = exec_base + phdr[i].p_vaddr;
            phdr_vaddr_found = 1;
        }
    }

    /* If no PT_PHDR, compute from first PT_LOAD + e_phoff */
    if (!phdr_vaddr_found) {
        for (int i = 0; i < ehdr->e_phnum; i++) {
            if (phdr[i].p_type == PT_LOAD) {
                /* phdr table is within the first loadable segment typically */
                uint32_t load_vaddr = exec_base + phdr[i].p_vaddr;
                uint32_t load_offset = phdr[i].p_offset;
                if (ehdr->e_phoff >= load_offset &&
                    ehdr->e_phoff < load_offset + phdr[i].p_filesz) {
                    phdr_vaddr = load_vaddr + (ehdr->e_phoff - load_offset);
                    phdr_vaddr_found = 1;
                }
                break;
            }
        }
        /* Last resort: approximate */
        if (!phdr_vaddr_found)
            phdr_vaddr = exec_base + ehdr->e_phoff;
    }

    /* Sanity: verify AT_PHDR falls within a mapped PT_LOAD segment */
    {
        int phdr_in_load = 0;
        uint32_t phdr_end = phdr_vaddr + ehdr->e_phnum * sizeof(Elf32_Phdr);
        for (int i = 0; i < ehdr->e_phnum; i++) {
            if (phdr[i].p_type != PT_LOAD) continue;
            uint32_t seg_s = exec_base + phdr[i].p_vaddr;
            uint32_t seg_e = seg_s + phdr[i].p_memsz;
            if (phdr_vaddr >= seg_s && phdr_end <= seg_e) {
                phdr_in_load = 1;
                break;
            }
        }
        if (!phdr_in_load) {
            printf("elf: warning: AT_PHDR 0x%x not in any PT_LOAD segment\n", phdr_vaddr);
            /* Try exec_base + ehdr->e_phoff as fallback */
            phdr_vaddr = exec_base + ehdr->e_phoff;
        }
    }

    /* ── Create VMA table early (interpreter loading needs it) ────── */
    vma_table_t *vt = vma_init();
    if (vt) {
        /* VMAs for PT_LOAD segments */
        for (int i = 0; i < ehdr->e_phnum; i++) {
            if (phdr[i].p_type != PT_LOAD) continue;
            uint32_t seg_s = align_down(exec_base + phdr[i].p_vaddr, PAGE_SIZE);
            uint32_t seg_e = align_up(exec_base + phdr[i].p_vaddr + phdr[i].p_memsz, PAGE_SIZE);
            uint32_t vflags = VMA_READ;
            if (phdr[i].p_flags & PF_W) vflags |= VMA_WRITE;
            if (phdr[i].p_flags & PF_X) vflags |= VMA_EXEC;
            vma_insert(vt, seg_s, seg_e, vflags, VMA_TYPE_ELF);
        }
        vt->brk_start = brk_end;
        vt->brk_current = brk_end;
        vt->mmap_next = 0x20000000;
    }

    /* ── Load interpreter if PT_INTERP found ──────────────────────── */
    uint32_t interp_base = 0;
    uint32_t entry_point = exec_base + ehdr->e_entry;

    if (interp_path[0]) {
        interp_base = INTERP_BASE_ADDR;
        uint32_t interp_entry = elf_load_interp(pd, interp_path, interp_base, task, vt);
        if (!interp_entry) {
            printf("elf: failed to load interpreter '%s'\n", interp_path);
            if (vt) vma_destroy(vt);
            goto fail;
        }
        entry_point = interp_entry;  /* jump to interpreter, not main binary */
    }

    /* Finalize VMA table */
    if (vt) {
        /* VMA for user stack */
        vma_insert(vt, USER_SPACE_BASE, USER_SPACE_BASE + PAGE_SIZE,
                   VMA_READ | VMA_WRITE | VMA_GROWSDOWN, VMA_TYPE_STACK);
        /* VMA for initial brk (zero-length, grows via brk syscall) */
        vma_insert(vt, brk_end, brk_end, VMA_READ | VMA_WRITE, VMA_TYPE_BRK);
    }

    /* Allocate user stack (4KB at USER_SPACE_BASE = 0x40000000) */
    uint32_t ustack = pmm_alloc_frame();
    if (!ustack) {
        printf("elf: failed to allocate user stack\n");
        if (vt) vma_destroy(vt);
        goto fail;
    }
    memset((void *)ustack, 0, PAGE_SIZE);

    if (!vmm_map_user_page(pd, USER_SPACE_BASE, ustack,
                            PTE_PRESENT | PTE_WRITABLE | PTE_USER)) {
        pmm_free_frame(ustack);
        printf("elf: failed to map user stack\n");
        if (vt) vma_destroy(vt);
        goto fail;
    }

    /* Allocate kernel stack (4KB) */
    uint32_t kstack = pmm_alloc_frame();
    if (!kstack) {
        printf("elf: failed to allocate kernel stack\n");
        pmm_free_frame(ustack);
        if (vt) vma_destroy(vt);
        goto fail;
    }
    memset((void *)kstack, 0, PAGE_SIZE);

    /* ── Build argc/argv/envp/auxv on user stack ───────────────── */
    /* Stack layout (grows downward):
     *   [AT_RANDOM 16 bytes]
     *   [strings: argv[0]\0 argv[1]\0 ...]
     *   [padding for 16-byte alignment]
     *   [auxv entries: type,val pairs + AT_NULL]
     *   [envp terminator: NULL]
     *   [argv terminator: NULL]
     *   [argv[n-1] ptr] ... [argv[0] ptr]
     *   [argc]               ← user_esp points here
     */
    uint8_t *stack_top = (uint8_t *)(ustack + PAGE_SIZE);
    uint32_t vstack_top = USER_SPACE_BASE + PAGE_SIZE;

    /* Default: argv[0] = filename if no argv provided */
    int real_argc = argc;
    const char *default_argv[1];
    const char **real_argv = argv;
    if (!argv || argc <= 0) {
        default_argv[0] = filename;
        real_argv = default_argv;
        real_argc = 1;
    }

    /* Phase 1: Copy strings to top of stack, record virtual addresses */
    uint32_t str_vaddrs[32]; /* max 32 args */
    if (real_argc > 32) real_argc = 32;

    uint8_t *str_ptr = stack_top;
    uint32_t str_vptr = vstack_top;

    /* Push 16 bytes of random data for AT_RANDOM */
    str_ptr -= 16;
    str_vptr -= 16;
    prng_random(str_ptr, 16);
    uint32_t at_random_addr = str_vptr;

    for (int i = 0; i < real_argc; i++) {
        size_t slen = strlen(real_argv[i]) + 1;
        str_ptr -= slen;
        str_vptr -= slen;
        memcpy(str_ptr, real_argv[i], slen);
        str_vaddrs[i] = str_vptr;
    }

    /* Phase 2: Build the pointer table below the strings */
    /* Align down to 4-byte boundary */
    str_ptr = (uint8_t *)((uint32_t)str_ptr & ~3u);
    str_vptr = str_vptr & ~3u;

    /* Count entries: argc + argv ptrs + NULL + envp NULL + auxv (13 entries × 2 words) */
    #define AUXV_COUNT 13
    int table_words = 1 + real_argc + 1 + 1 + (AUXV_COUNT * 2);
    uint32_t *tbl = (uint32_t *)str_ptr - table_words;
    uint32_t vtbl = str_vptr - table_words * 4;

    /* Align to 16 bytes */
    vtbl &= ~15u;
    tbl = (uint32_t *)(ustack + (vtbl - USER_SPACE_BASE));

    int idx = 0;
    tbl[idx++] = real_argc;                  /* argc */
    for (int i = 0; i < real_argc; i++)
        tbl[idx++] = str_vaddrs[i];          /* argv[i] */
    tbl[idx++] = 0;                          /* argv terminator */
    tbl[idx++] = 0;                          /* envp terminator */

    /* Full auxiliary vector */
    tbl[idx++] = AT_PHDR;    tbl[idx++] = phdr_vaddr;
    tbl[idx++] = AT_PHENT;   tbl[idx++] = sizeof(Elf32_Phdr);
    tbl[idx++] = AT_PHNUM;   tbl[idx++] = ehdr->e_phnum;
    tbl[idx++] = AT_PAGESZ;  tbl[idx++] = PAGE_SIZE;
    tbl[idx++] = AT_BASE;    tbl[idx++] = interp_base; /* 0 if static */
    tbl[idx++] = AT_ENTRY;   tbl[idx++] = exec_base + ehdr->e_entry;
    tbl[idx++] = AT_UID;     tbl[idx++] = 0;
    tbl[idx++] = AT_EUID;    tbl[idx++] = 0;
    tbl[idx++] = AT_GID;     tbl[idx++] = 0;
    tbl[idx++] = AT_EGID;    tbl[idx++] = 0;
    tbl[idx++] = AT_CLKTCK;  tbl[idx++] = 120; /* PIT Hz */
    tbl[idx++] = AT_RANDOM;  tbl[idx++] = at_random_addr;
    tbl[idx++] = AT_NULL;    tbl[idx++] = 0;

    uint32_t user_esp = vtbl;

    /* Build ring 3 iret frame on kernel stack */
    uint32_t *ksp = (uint32_t *)(kstack + PAGE_SIZE);

    /* iret frame: SS, UserESP, EFLAGS, CS, EIP */
    *(--ksp) = 0x23;              /* SS: user data segment */
    *(--ksp) = user_esp;          /* UserESP */
    *(--ksp) = 0x202;             /* EFLAGS: IF=1 */
    *(--ksp) = 0x1B;              /* CS: user code segment */
    *(--ksp) = entry_point;       /* EIP: interpreter or main entry */

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
    task->vma = vt;
    task->umask = 0022;

    sig_init(&task->sig);

    /* Allocate FD table and open fd 0 (stdin), 1 (stdout), 2 (stderr) as TTY */
    fd_table_init(tid);
    if (task->fds) {
        task->fds[0] = (fd_entry_t){ .type = FD_TTY, .flags = LINUX_O_RDONLY };
        task->fds[1] = (fd_entry_t){ .type = FD_TTY, .flags = LINUX_O_WRONLY };
        task->fds[2] = (fd_entry_t){ .type = FD_TTY, .flags = LINUX_O_WRONLY };
    }

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

/* ── elf_exec — Replace current task image (execve semantics) ───── */

int elf_exec(int tid, const char *filename, int argc, const char **argv) {
    task_info_t *task = task_get(tid);
    if (!task) return -LINUX_EINVAL;

    /* ── Step 1: Read and validate ELF ─────────────────────────── */

    uint8_t *file_data = malloc(ELF_MAX_LOAD_SIZE);
    if (!file_data) return -LINUX_ENOMEM;

    size_t file_size = ELF_MAX_LOAD_SIZE;
    if (fs_read_file(filename, file_data, &file_size) < 0) {
        free(file_data);
        return -LINUX_ENOENT;
    }

    Elf32_Ehdr *ehdr = (Elf32_Ehdr *)file_data;
    if (!elf_detect(file_data, file_size) ||
        ehdr->e_ident[4] != ELFCLASS32 ||
        (ehdr->e_type != ET_EXEC && ehdr->e_type != ET_DYN) ||
        ehdr->e_machine != EM_386 ||
        ehdr->e_phnum == 0) {
        free(file_data);
        return -LINUX_ENOEXEC;
    }

    /* ET_DYN (PIE) binaries have relative addresses; pick a base */
    uint32_t exec_base = 0;
    if (ehdr->e_type == ET_DYN)
        exec_base = 0x08048000;

    /* ── Step 2: Copy argv into kernel buffer ──────────────────── */
    /* Must copy BEFORE tearing down old address space since argv
     * pointers may live in user memory that's about to be unmapped. */

    #define EXEC_MAX_ARGC   32
    #define EXEC_ARGV_BUFSZ 2048
    static char kargv_buf[EXEC_ARGV_BUFSZ];
    static char *kargv_ptrs[EXEC_MAX_ARGC];

    int real_argc = argc;
    if (real_argc > EXEC_MAX_ARGC) real_argc = EXEC_MAX_ARGC;
    /* TODO: argv limit of 32 — sufficient for most use, but truncates
     * commands like gcc with many flags. Increase if needed. */

    char *bp = kargv_buf;
    int buf_remaining = EXEC_ARGV_BUFSZ;
    for (int i = 0; i < real_argc; i++) {
        const char *src = argv ? argv[i] : NULL;
        if (!src) { real_argc = i; break; }
        size_t slen = strlen(src) + 1;
        if ((int)slen > buf_remaining) { real_argc = i; break; }
        memcpy(bp, src, slen);
        kargv_ptrs[i] = bp;
        bp += slen;
        buf_remaining -= slen;
    }
    if (real_argc == 0) {
        /* Default: argv[0] = filename */
        size_t slen = strlen(filename) + 1;
        memcpy(kargv_buf, filename, slen);
        kargv_ptrs[0] = kargv_buf;
        real_argc = 1;
    }

    /* ── Step 3: Tear down old address space ───────────────────── */

    uint32_t old_pd = task->page_dir;
    uint32_t kernel_pd = vmm_get_kernel_pagedir();

    if (old_pd && old_pd != kernel_pd) {
        /* VMA-based cleanup */
        if (task->vma) {
            for (int v = 0; v < VMA_MAX_PER_TASK; v++) {
                vma_t *vma = &task->vma->vmas[v];
                if (!vma->active) continue;
                for (uint32_t va = vma->vm_start; va < vma->vm_end; va += PAGE_SIZE) {
                    uint32_t pte = vmm_get_pte(old_pd, va);
                    if (!(pte & PTE_PRESENT)) continue;
                    uint32_t frame = pte & PAGE_MASK;
                    vmm_unmap_user_page(old_pd, va);
                    if (frame_ref_dec(frame) == 0)
                        pmm_free_frame(frame);
                }
            }
            vma_destroy(task->vma);
            task->vma = NULL;
        } else {
            /* Legacy elf_frames[] cleanup */
            for (int f = 0; f < task->num_elf_frames; f++) {
                if (task->elf_frames[f])
                    pmm_free_frame(task->elf_frames[f]);
            }
            task->num_elf_frames = 0;
        }

        /* Free old user stack frame */
        if (task->user_stack) {
            pmm_free_frame(task->user_stack);
            task->user_stack = 0;
        }

        vmm_destroy_user_pagedir(old_pd);
        task->page_dir = 0;
    }

    /* ── Step 4: Close CLOEXEC file descriptors ────────────────── */

    if (task->fds) {
        for (int i = 0; i < task->fd_count; i++) {
            if (task->fds[i].type != FD_NONE && task->fds[i].cloexec) {
                if (task->fds[i].type == FD_PIPE_R ||
                    task->fds[i].type == FD_PIPE_W) {
                    pipe_close(i, tid);
                } else {
                    task->fds[i].type = FD_NONE;
                    task->fds[i].inode = 0;
                    task->fds[i].offset = 0;
                    task->fds[i].flags = 0;
                    task->fds[i].pipe_id = 0;
                    task->fds[i].cloexec = 0;
                }
            }
        }
    }

    /* ── Step 5: Create new page directory + load segments ─────── */

    uint32_t pd = vmm_create_user_pagedir();
    if (!pd) {
        free(file_data);
        goto exec_fail;
    }

    /* Reset elf_frames tracking for new image */
    task->num_elf_frames = 0;
    memset(task->elf_frames, 0, sizeof(task->elf_frames));

    Elf32_Phdr *phdr = (Elf32_Phdr *)(file_data + ehdr->e_phoff);
    uint32_t brk_end = 0;

    for (int i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_type != PT_LOAD) continue;

        uint32_t vaddr = exec_base + phdr[i].p_vaddr;
        uint32_t memsz = phdr[i].p_memsz;
        uint32_t filesz = phdr[i].p_filesz;
        uint32_t offset = phdr[i].p_offset;

        uint32_t seg_start = align_down(vaddr, PAGE_SIZE);
        uint32_t seg_end = align_up(vaddr + memsz, PAGE_SIZE);

        for (uint32_t va = seg_start; va < seg_end; va += PAGE_SIZE) {
            uint32_t frame = pmm_alloc_frame();
            if (!frame) goto exec_fail;
            memset((void *)frame, 0, PAGE_SIZE);

            uint32_t pte_flags = PTE_PRESENT | PTE_USER;
            if (phdr[i].p_flags & PF_W) pte_flags |= PTE_WRITABLE;

            if (!vmm_map_user_page(pd, va, frame, pte_flags)) {
                pmm_free_frame(frame);
                goto exec_fail;
            }
            elf_track_frame(task, frame);

            /* Copy file data */
            uint32_t copy_start = 0, copy_len = 0;
            if (va < vaddr) {
                uint32_t skip = vaddr - va;
                copy_start = skip;
                if (filesz > 0) {
                    copy_len = PAGE_SIZE - skip;
                    if (copy_len > filesz) copy_len = filesz;
                }
            } else {
                uint32_t file_off = va - vaddr;
                if (file_off < filesz) {
                    copy_len = filesz - file_off;
                    if (copy_len > PAGE_SIZE) copy_len = PAGE_SIZE;
                }
            }
            if (copy_len > 0) {
                uint32_t src_off = (va < vaddr) ? offset : offset + (va - vaddr);
                if (src_off + copy_len <= file_size)
                    memcpy((void *)(frame + copy_start), file_data + src_off, copy_len);
            }
        }

        uint32_t seg_top = align_up(vaddr + memsz, PAGE_SIZE);
        if (seg_top > brk_end) brk_end = seg_top;
    }

    /* ── Step 5b: Scan for PT_INTERP and PT_PHDR ─────────────── */
    char interp_path[256] = {0};
    uint32_t phdr_vaddr = 0;
    int phdr_vaddr_found = 0;

    for (int i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_type == PT_INTERP) {
            uint32_t len = phdr[i].p_filesz;
            if (len >= sizeof(interp_path)) len = sizeof(interp_path) - 1;
            memcpy(interp_path, file_data + phdr[i].p_offset, len);
            interp_path[len] = '\0';
            while (len > 0 && (interp_path[len-1] == '\n' || interp_path[len-1] == '\0'))
                interp_path[--len] = '\0';
        }
        if (phdr[i].p_type == PT_PHDR) {
            phdr_vaddr = exec_base + phdr[i].p_vaddr;
            phdr_vaddr_found = 1;
        }
    }

    if (!phdr_vaddr_found) {
        for (int i = 0; i < ehdr->e_phnum; i++) {
            if (phdr[i].p_type == PT_LOAD) {
                uint32_t load_vaddr = exec_base + phdr[i].p_vaddr;
                uint32_t load_offset = phdr[i].p_offset;
                if (ehdr->e_phoff >= load_offset &&
                    ehdr->e_phoff < load_offset + phdr[i].p_filesz) {
                    phdr_vaddr = load_vaddr + (ehdr->e_phoff - load_offset);
                    phdr_vaddr_found = 1;
                }
                break;
            }
        }
        if (!phdr_vaddr_found)
            phdr_vaddr = exec_base + ehdr->e_phoff;
    }

    /* Sanity: verify AT_PHDR falls within a mapped PT_LOAD segment */
    {
        int phdr_in_load = 0;
        uint32_t phdr_end_addr = phdr_vaddr + ehdr->e_phnum * sizeof(Elf32_Phdr);
        for (int i = 0; i < ehdr->e_phnum; i++) {
            if (phdr[i].p_type != PT_LOAD) continue;
            uint32_t seg_s = exec_base + phdr[i].p_vaddr;
            uint32_t seg_e = seg_s + phdr[i].p_memsz;
            if (phdr_vaddr >= seg_s && phdr_end_addr <= seg_e) {
                phdr_in_load = 1;
                break;
            }
        }
        if (!phdr_in_load)
            phdr_vaddr = exec_base + ehdr->e_phoff;
    }

    /* ── Step 5c: Create VMA table (needed for interpreter load) ── */

    vma_table_t *vt = vma_init();
    if (vt) {
        for (int i = 0; i < ehdr->e_phnum; i++) {
            if (phdr[i].p_type != PT_LOAD) continue;
            uint32_t seg_s = align_down(exec_base + phdr[i].p_vaddr, PAGE_SIZE);
            uint32_t seg_e = align_up(exec_base + phdr[i].p_vaddr + phdr[i].p_memsz, PAGE_SIZE);
            uint32_t vflags = VMA_READ;
            if (phdr[i].p_flags & PF_W) vflags |= VMA_WRITE;
            if (phdr[i].p_flags & PF_X) vflags |= VMA_EXEC;
            vma_insert(vt, seg_s, seg_e, vflags, VMA_TYPE_ELF);
        }
        vt->brk_start = brk_end;
        vt->brk_current = brk_end;
        vt->mmap_next = 0x20000000;
    }

    /* ── Step 5d: Load interpreter if PT_INTERP found ──────────── */
    uint32_t interp_base = 0;
    uint32_t entry_point = exec_base + ehdr->e_entry;

    if (interp_path[0]) {
        interp_base = INTERP_BASE_ADDR;
        uint32_t interp_entry = elf_load_interp(pd, interp_path, interp_base, task, vt);
        if (!interp_entry) {
            if (vt) vma_destroy(vt);
            goto exec_fail;
        }
        entry_point = interp_entry;
    }

    /* Finalize VMA table */
    if (vt) {
        vma_insert(vt, USER_SPACE_BASE, USER_SPACE_BASE + PAGE_SIZE,
                   VMA_READ | VMA_WRITE | VMA_GROWSDOWN, VMA_TYPE_STACK);
        vma_insert(vt, brk_end, brk_end, VMA_READ | VMA_WRITE, VMA_TYPE_BRK);
    }

    /* ── Step 6: Allocate new user stack ───────────────────────── */

    uint32_t ustack = pmm_alloc_frame();
    if (!ustack) {
        if (vt) vma_destroy(vt);
        goto exec_fail;
    }
    memset((void *)ustack, 0, PAGE_SIZE);

    if (!vmm_map_user_page(pd, USER_SPACE_BASE, ustack,
                            PTE_PRESENT | PTE_WRITABLE | PTE_USER)) {
        pmm_free_frame(ustack);
        if (vt) vma_destroy(vt);
        goto exec_fail;
    }

    /* ── Step 7: Build argc/argv/envp/auxv on new user stack ────── */

    uint8_t *stack_top = (uint8_t *)(ustack + PAGE_SIZE);
    uint32_t vstack_top = USER_SPACE_BASE + PAGE_SIZE;

    uint32_t str_vaddrs[EXEC_MAX_ARGC];
    uint8_t *str_ptr = stack_top;
    uint32_t str_vptr = vstack_top;

    /* Push 16 bytes of random data for AT_RANDOM */
    str_ptr -= 16;
    str_vptr -= 16;
    prng_random(str_ptr, 16);
    uint32_t at_random_addr = str_vptr;

    for (int i = 0; i < real_argc; i++) {
        size_t slen = strlen(kargv_ptrs[i]) + 1;
        str_ptr -= slen;
        str_vptr -= slen;
        memcpy(str_ptr, kargv_ptrs[i], slen);
        str_vaddrs[i] = str_vptr;
    }

    str_ptr = (uint8_t *)((uint32_t)str_ptr & ~3u);
    str_vptr = str_vptr & ~3u;

    int table_words = 1 + real_argc + 1 + 1 + (AUXV_COUNT * 2);
    uint32_t vtbl = str_vptr - table_words * 4;
    vtbl &= ~15u;  /* 16-byte align per ABI */
    uint32_t *tbl = (uint32_t *)(ustack + (vtbl - USER_SPACE_BASE));

    int idx = 0;
    tbl[idx++] = real_argc;
    for (int i = 0; i < real_argc; i++)
        tbl[idx++] = str_vaddrs[i];
    tbl[idx++] = 0;  /* argv NULL */
    tbl[idx++] = 0;  /* envp NULL */

    /* Full auxiliary vector */
    tbl[idx++] = AT_PHDR;    tbl[idx++] = phdr_vaddr;
    tbl[idx++] = AT_PHENT;   tbl[idx++] = sizeof(Elf32_Phdr);
    tbl[idx++] = AT_PHNUM;   tbl[idx++] = ehdr->e_phnum;
    tbl[idx++] = AT_PAGESZ;  tbl[idx++] = PAGE_SIZE;
    tbl[idx++] = AT_BASE;    tbl[idx++] = interp_base;
    tbl[idx++] = AT_ENTRY;   tbl[idx++] = exec_base + ehdr->e_entry;
    tbl[idx++] = AT_UID;     tbl[idx++] = 0;
    tbl[idx++] = AT_EUID;    tbl[idx++] = 0;
    tbl[idx++] = AT_GID;     tbl[idx++] = 0;
    tbl[idx++] = AT_EGID;    tbl[idx++] = 0;
    tbl[idx++] = AT_CLKTCK;  tbl[idx++] = 120;
    tbl[idx++] = AT_RANDOM;  tbl[idx++] = at_random_addr;
    tbl[idx++] = AT_NULL;    tbl[idx++] = 0;

    uint32_t user_esp = vtbl;

    /* ── Step 8: Rebuild kernel stack frame ────────────────────── */

    uint32_t kstack = task->kernel_stack;
    uint32_t *ksp = (uint32_t *)(kstack + PAGE_SIZE);

    /* Ring 3 iret frame */
    *(--ksp) = 0x23;             /* SS */
    *(--ksp) = user_esp;         /* UserESP */
    *(--ksp) = 0x202;            /* EFLAGS: IF=1 */
    *(--ksp) = 0x1B;             /* CS */
    *(--ksp) = entry_point;      /* EIP: interpreter or main entry */

    /* ISR stub fields */
    *(--ksp) = 0;                /* err_code */
    *(--ksp) = 0;                /* int_no */

    /* pusha block — all zeros for fresh start */
    for (int i = 0; i < 8; i++)
        *(--ksp) = 0;

    /* Segment registers */
    *(--ksp) = 0x23;             /* DS */
    *(--ksp) = 0x23;             /* ES */
    *(--ksp) = 0x23;             /* FS */
    *(--ksp) = 0x23;             /* GS */

    /* ── Step 10: Update task fields ───────────────────────────── */

    const char *short_name = filename;
    const char *slash = strrchr(filename, '/');
    if (slash) short_name = slash + 1;
    strncpy(task->name, short_name, 31);
    task->name[31] = '\0';

    task->is_user = 1;
    task->is_elf = 1;
    task->user_stack = ustack;
    task->page_dir = pd;
    task->esp = (uint32_t)ksp;
    task->kernel_esp = kstack + PAGE_SIZE;
    task->brk_start = brk_end;
    task->brk_current = brk_end;
    task->mmap_next = 0x20000000;
    task->tls_base = 0;
    task->vma = vt;

    /* Reset signal handlers to default (POSIX: exec resets caught signals) */
    for (int i = 0; i < NSIG; i++)
        task->sig.handlers[i] = SIG_DFL;
    task->sig.pending = 0;
    task->sig.in_handler = 0;
    /* Keep sig.blocked — POSIX says signal mask is preserved across exec */

    free(file_data);
    return 0;  /* success — caller must NOT return to old user code */

exec_fail:
    /* On failure, task is in a broken state (old image torn down, new failed).
     * Best we can do: kill the task. */
    for (int f = 0; f < task->num_elf_frames; f++) {
        if (task->elf_frames[f])
            pmm_free_frame(task->elf_frames[f]);
    }
    task->num_elf_frames = 0;
    if (pd) vmm_destroy_user_pagedir(pd);
    task->state = TASK_STATE_ZOMBIE;
    task->active = 0;
    task->exit_code = 255;
    free(file_data);
    return -LINUX_ENOMEM;
}
