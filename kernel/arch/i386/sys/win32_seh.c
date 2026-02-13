#include <kernel/win32_seh.h>
#include <kernel/win32_types.h>
#include <kernel/idt.h>
#include <kernel/task.h>
#include <kernel/io.h>
#include <kernel/gfx.h>
#include <string.h>
#include <stdio.h>

/* ── Global unhandled exception filter ──────────────────────── */

static LPTOP_LEVEL_EXCEPTION_FILTER g_unhandled_filter = NULL;

/* ── Vectored Exception Handling ───────────────────────────── */

#define VEH_MAX_HANDLERS 16

typedef struct {
    PVECTORED_EXCEPTION_HANDLER handler;
    int active;
} veh_entry_t;

static veh_entry_t veh_exception_handlers[VEH_MAX_HANDLERS];
static int veh_exception_count = 0;

static veh_entry_t veh_continue_handlers[VEH_MAX_HANDLERS];
static int veh_continue_count = 0;

PVOID seh_AddVectoredExceptionHandler(ULONG first_handler,
                                       PVECTORED_EXCEPTION_HANDLER handler)
{
    if (!handler || veh_exception_count >= VEH_MAX_HANDLERS)
        return NULL;

    if (first_handler) {
        /* Insert at head — shift all entries right */
        for (int i = veh_exception_count; i > 0; i--)
            veh_exception_handlers[i] = veh_exception_handlers[i - 1];
        veh_exception_handlers[0].handler = handler;
        veh_exception_handlers[0].active = 1;
    } else {
        veh_exception_handlers[veh_exception_count].handler = handler;
        veh_exception_handlers[veh_exception_count].active = 1;
    }
    veh_exception_count++;
    return (PVOID)handler;
}

ULONG seh_RemoveVectoredExceptionHandler(PVOID handle) {
    for (int i = 0; i < veh_exception_count; i++) {
        if ((PVOID)veh_exception_handlers[i].handler == handle) {
            for (int j = i; j < veh_exception_count - 1; j++)
                veh_exception_handlers[j] = veh_exception_handlers[j + 1];
            veh_exception_count--;
            veh_exception_handlers[veh_exception_count].handler = NULL;
            veh_exception_handlers[veh_exception_count].active = 0;
            return 1;
        }
    }
    return 0;
}

PVOID seh_AddVectoredContinueHandler(ULONG first_handler,
                                      PVECTORED_EXCEPTION_HANDLER handler)
{
    if (!handler || veh_continue_count >= VEH_MAX_HANDLERS)
        return NULL;

    if (first_handler) {
        for (int i = veh_continue_count; i > 0; i--)
            veh_continue_handlers[i] = veh_continue_handlers[i - 1];
        veh_continue_handlers[0].handler = handler;
        veh_continue_handlers[0].active = 1;
    } else {
        veh_continue_handlers[veh_continue_count].handler = handler;
        veh_continue_handlers[veh_continue_count].active = 1;
    }
    veh_continue_count++;
    return (PVOID)handler;
}

ULONG seh_RemoveVectoredContinueHandler(PVOID handle) {
    for (int i = 0; i < veh_continue_count; i++) {
        if ((PVOID)veh_continue_handlers[i].handler == handle) {
            for (int j = i; j < veh_continue_count - 1; j++)
                veh_continue_handlers[j] = veh_continue_handlers[j + 1];
            veh_continue_count--;
            veh_continue_handlers[veh_continue_count].handler = NULL;
            veh_continue_handlers[veh_continue_count].active = 0;
            return 1;
        }
    }
    return 0;
}

LONG seh_dispatch_vectored(EXCEPTION_POINTERS *ep) {
    for (int i = 0; i < veh_exception_count; i++) {
        if (veh_exception_handlers[i].active && veh_exception_handlers[i].handler) {
            LONG result = veh_exception_handlers[i].handler(ep);
            if (result == EXCEPTION_CONTINUE_EXECUTION)
                return EXCEPTION_CONTINUE_EXECUTION;
        }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

/* ── SE Translator ─────────────────────────────────────────── */

static _se_translator_function g_se_translator = NULL;

_se_translator_function seh_set_se_translator(_se_translator_function func) {
    _se_translator_function prev = g_se_translator;
    g_se_translator = func;
    return prev;
}

/* ── CPU exception → Win32 exception code mapping ───────────── */

static DWORD cpu_exception_to_win32(uint32_t int_no) {
    switch (int_no) {
        case 0:  return EXCEPTION_INT_DIVIDE_BY_ZERO;
        case 1:  return EXCEPTION_SINGLE_STEP;
        case 3:  return EXCEPTION_BREAKPOINT;
        case 4:  return EXCEPTION_INT_OVERFLOW;
        case 5:  return EXCEPTION_ARRAY_BOUNDS_EXCEEDED;
        case 6:  return EXCEPTION_ILLEGAL_INSTRUCTION;
        case 13: return EXCEPTION_ACCESS_VIOLATION;
        case 14: return EXCEPTION_ACCESS_VIOLATION;
        default: return EXCEPTION_NONCONTINUABLE_EXCEPTION;
    }
}

/* ── Register conversion helpers ────────────────────────────── */

static void regs_to_context(const registers_t *regs, CONTEXT *ctx) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->ContextFlags = CONTEXT_FULL;
    ctx->Eax = regs->eax;
    ctx->Ebx = regs->ebx;
    ctx->Ecx = regs->ecx;
    ctx->Edx = regs->edx;
    ctx->Esi = regs->esi;
    ctx->Edi = regs->edi;
    ctx->Ebp = regs->ebp;
    ctx->Esp = regs->useresp;
    ctx->Eip = regs->eip;
    ctx->EFlags = regs->eflags;
    ctx->SegCs = regs->cs;
    ctx->SegDs = regs->ds;
    ctx->SegEs = regs->es;
    ctx->SegFs = regs->fs;
    ctx->SegGs = regs->gs;
    ctx->SegSs = regs->ss;
}

static void context_to_regs(const CONTEXT *ctx, registers_t *regs) {
    regs->eax = ctx->Eax;
    regs->ebx = ctx->Ebx;
    regs->ecx = ctx->Ecx;
    regs->edx = ctx->Edx;
    regs->esi = ctx->Esi;
    regs->edi = ctx->Edi;
    regs->ebp = ctx->Ebp;
    regs->useresp = ctx->Esp;
    regs->eip = ctx->Eip;
    regs->eflags = ctx->EFlags;
}

/* ── SEH chain walker ───────────────────────────────────────── */

int seh_dispatch_exception(task_info_t *t, registers_t *regs, uint32_t int_no) {
    if (!t || !t->tib)
        return 0;

    WIN32_TEB *teb = (WIN32_TEB *)(t->tib);
    EXCEPTION_REGISTRATION_RECORD *reg =
        (EXCEPTION_REGISTRATION_RECORD *)teb->tib.ExceptionList;

    /* Build exception record */
    EXCEPTION_RECORD er;
    memset(&er, 0, sizeof(er));
    er.ExceptionCode = cpu_exception_to_win32(int_no);
    er.ExceptionFlags = 0;
    er.ExceptionRecord = NULL;
    er.ExceptionAddress = (PVOID)regs->eip;
    er.NumberParameters = 0;

    /* For page faults, add access violation info */
    if (int_no == 14) {
        uint32_t cr2;
        __asm__ volatile ("mov %%cr2, %0" : "=r"(cr2));
        er.NumberParameters = 2;
        er.ExceptionInformation[0] = (regs->err_code & 2) ? 1 : 0; /* write=1, read=0 */
        er.ExceptionInformation[1] = cr2;
    }

    /* Build context */
    CONTEXT ctx;
    regs_to_context(regs, &ctx);

    EXCEPTION_POINTERS ep;
    ep.ExceptionRecord = &er;
    ep.ContextRecord = &ctx;

    /* Try Vectored Exception Handlers first (VEH runs before SEH) */
    LONG veh_result = seh_dispatch_vectored(&ep);
    if (veh_result == EXCEPTION_CONTINUE_EXECUTION) {
        context_to_regs(&ctx, regs);
        serial_printf("[SEH] VEH returned ContinueExecution\n");
        return 1;
    }

    /* Try SE translator (C++ bridge) */
    if (g_se_translator) {
        g_se_translator(er.ExceptionCode, &ep);
    }

    /* Walk SEH chain */
    while ((uint32_t)reg != SEH_CHAIN_END && reg != NULL) {
        /* Validate frame pointer is reasonable */
        if ((uint32_t)reg < 0x1000 || (uint32_t)reg > 0x0FFFFFFF)
            break;

        if (reg->Handler) {
            /* Call handler: disposition = handler(er, frame, ctx, dispatcher_ctx) */
            EXCEPTION_DISPOSITION disp;

            /* In our kernel-mode SEH dispatch, we call the handler directly.
             * The handler signature is:
             *   EXCEPTION_DISPOSITION handler(EXCEPTION_RECORD*, void* frame,
             *                                 CONTEXT*, void* dispatcher_ctx) */
            typedef EXCEPTION_DISPOSITION (*seh_handler_t)(
                EXCEPTION_RECORD *, void *, CONTEXT *, void *);
            seh_handler_t handler = (seh_handler_t)reg->Handler;

            serial_printf("[SEH] calling handler at 0x%x for frame 0x%x\n",
                          (unsigned)handler, (unsigned)reg);

            disp = handler(&er, reg, &ctx, NULL);

            if (disp == ExceptionContinueExecution) {
                /* Handler fixed it — update regs and continue */
                context_to_regs(&ctx, regs);
                serial_printf("[SEH] handler returned ContinueExecution\n");
                return 1;
            }
            /* ExceptionContinueSearch — try next handler */
        }

        reg = reg->Next;
    }

    /* No SEH handler handled it — try unhandled exception filter */
    if (g_unhandled_filter) {
        serial_printf("[SEH] calling unhandled exception filter\n");
        LONG result = g_unhandled_filter(&ep);
        if (result == EXCEPTION_CONTINUE_EXECUTION) {
            context_to_regs(&ctx, regs);
            return 1;
        }
    }

    /* Not handled */
    serial_printf("[SEH] exception 0x%x not handled, falling through\n",
                  er.ExceptionCode);
    return 0;
}

/* ── Public API ─────────────────────────────────────────────── */

LPTOP_LEVEL_EXCEPTION_FILTER seh_SetUnhandledExceptionFilter(
    LPTOP_LEVEL_EXCEPTION_FILTER filter)
{
    LPTOP_LEVEL_EXCEPTION_FILTER prev = g_unhandled_filter;
    g_unhandled_filter = filter;
    return prev;
}

LONG seh_UnhandledExceptionFilter(EXCEPTION_POINTERS *ep) {
    if (g_unhandled_filter)
        return g_unhandled_filter(ep);
    return EXCEPTION_CONTINUE_SEARCH;
}

void seh_RaiseException(DWORD code, DWORD flags, DWORD nargs, const DWORD *args) {
    /* Build exception record */
    EXCEPTION_RECORD er;
    memset(&er, 0, sizeof(er));
    er.ExceptionCode = code;
    er.ExceptionFlags = flags;
    er.ExceptionRecord = NULL;
    er.ExceptionAddress = NULL; /* could capture EIP via inline asm */
    er.NumberParameters = (nargs > EXCEPTION_MAXIMUM_PARAMETERS) ?
                           EXCEPTION_MAXIMUM_PARAMETERS : nargs;
    if (args) {
        for (DWORD i = 0; i < er.NumberParameters; i++)
            er.ExceptionInformation[i] = args[i];
    }

    /* Get current task's TEB */
    int tid = task_get_current();
    task_info_t *t = task_get(tid);
    if (!t || !t->tib) {
        serial_printf("[SEH] RaiseException: no TEB, code=0x%x\n", code);
        return;
    }

    WIN32_TEB *teb = (WIN32_TEB *)(t->tib);
    EXCEPTION_REGISTRATION_RECORD *reg =
        (EXCEPTION_REGISTRATION_RECORD *)teb->tib.ExceptionList;

    /* Build a context from current state (approximate) */
    CONTEXT ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.ContextFlags = CONTEXT_FULL;

    EXCEPTION_POINTERS ep;
    ep.ExceptionRecord = &er;
    ep.ContextRecord = &ctx;

    /* Walk SEH chain */
    while ((uint32_t)reg != SEH_CHAIN_END && reg != NULL) {
        if ((uint32_t)reg < 0x1000 || (uint32_t)reg > 0x0FFFFFFF)
            break;

        if (reg->Handler) {
            typedef EXCEPTION_DISPOSITION (*seh_handler_t)(
                EXCEPTION_RECORD *, void *, CONTEXT *, void *);
            seh_handler_t handler = (seh_handler_t)reg->Handler;
            EXCEPTION_DISPOSITION disp = handler(&er, reg, &ctx, NULL);
            if (disp == ExceptionContinueExecution)
                return;
        }
        reg = reg->Next;
    }

    /* Try unhandled filter */
    if (g_unhandled_filter) {
        LONG result = g_unhandled_filter(&ep);
        if (result == EXCEPTION_CONTINUE_EXECUTION)
            return;
    }

    serial_printf("[SEH] RaiseException: unhandled code=0x%x\n", code);
}

void seh_RtlUnwind(void *target_frame, void *target_ip,
                   EXCEPTION_RECORD *er, DWORD return_value)
{
    (void)target_ip;
    (void)return_value;

    int tid = task_get_current();
    task_info_t *t = task_get(tid);
    if (!t || !t->tib)
        return;

    WIN32_TEB *teb = (WIN32_TEB *)(t->tib);
    EXCEPTION_REGISTRATION_RECORD *reg =
        (EXCEPTION_REGISTRATION_RECORD *)teb->tib.ExceptionList;

    /* Build unwind record if none provided */
    EXCEPTION_RECORD local_er;
    if (!er) {
        memset(&local_er, 0, sizeof(local_er));
        local_er.ExceptionCode = STATUS_UNWIND;
        local_er.ExceptionFlags = EXCEPTION_UNWINDING;
        er = &local_er;
    } else {
        er->ExceptionFlags |= EXCEPTION_UNWINDING;
    }
    if (!target_frame)
        er->ExceptionFlags |= EXCEPTION_EXIT_UNWIND;

    CONTEXT ctx;
    memset(&ctx, 0, sizeof(ctx));

    /* Walk frames, calling handlers with UNWINDING flag */
    while ((uint32_t)reg != SEH_CHAIN_END && reg != NULL) {
        if ((uint32_t)reg < 0x1000 || (uint32_t)reg > 0x0FFFFFFF)
            break;

        /* If we've reached the target frame, stop */
        if (target_frame && reg == target_frame) {
            teb->tib.ExceptionList = (uint32_t)reg;
            return;
        }

        if (reg->Handler) {
            typedef EXCEPTION_DISPOSITION (*seh_handler_t)(
                EXCEPTION_RECORD *, void *, CONTEXT *, void *);
            seh_handler_t handler = (seh_handler_t)reg->Handler;
            handler(er, reg, &ctx, NULL);
        }

        reg = reg->Next;
    }

    /* Reached end of chain */
    teb->tib.ExceptionList = SEH_CHAIN_END;
}

/* ── Crash Dialog ────────────────────────────────────────────── */

static const char *exception_code_name(uint32_t code) {
    switch (code) {
        case 0xC0000005: return "EXCEPTION_ACCESS_VIOLATION";
        case 0xC0000006: return "EXCEPTION_IN_PAGE_ERROR";
        case 0xC000001D: return "EXCEPTION_ILLEGAL_INSTRUCTION";
        case 0xC0000094: return "EXCEPTION_INT_DIVIDE_BY_ZERO";
        case 0xC00000FD: return "EXCEPTION_STACK_OVERFLOW";
        case 0x80000003: return "EXCEPTION_BREAKPOINT";
        case 0x80000004: return "EXCEPTION_SINGLE_STEP";
        case 0xC000008C: return "EXCEPTION_ARRAY_BOUNDS_EXCEEDED";
        case 0xC0000025: return "EXCEPTION_NONCONTINUABLE";
        case 0xC0000026: return "EXCEPTION_INVALID_DISPOSITION";
        case 0x80000001: return "STATUS_GUARD_PAGE_VIOLATION";
        case 0xC00000FE: return "STATUS_STACK_BUFFER_OVERRUN";
        case 0xE06D7363: return "C++ Exception (0xE06D7363)";
        default:         return "Unknown Exception";
    }
}

void seh_show_crash_dialog(const char *exception_name, uint32_t int_no,
                           registers_t *regs, uint32_t cr2, task_info_t *task)
{
    if (!gfx_is_active())
        return;

    /* Dialog dimensions and position (centered) */
    int dw = 620, dh = 340;
    int dx = ((int)gfx_width() - dw) / 2;
    int dy = ((int)gfx_height() - dh) / 2;

    /* Colors */
    uint32_t bg      = 0x2D2D2D;  /* dark gray background */
    uint32_t border  = 0xCC0000;  /* red border */
    uint32_t title_bg= 0xCC0000;  /* red title bar */
    uint32_t fg      = 0xFFFFFF;  /* white text */
    uint32_t fg_dim  = 0xBBBBBB;  /* dimmer text */
    uint32_t fg_val  = 0x55FF55;  /* green for values */

    /* Draw dark overlay behind dialog */
    gfx_fill_rect_alpha(0, 0, (int)gfx_width(), (int)gfx_height(), 0x80000000);

    /* Dialog background with border */
    gfx_fill_rect(dx - 2, dy - 2, dw + 4, dh + 4, border);
    gfx_fill_rect(dx, dy, dw, dh, bg);

    /* Title bar */
    gfx_fill_rect(dx, dy, dw, 28, title_bg);
    gfx_draw_string(dx + 10, dy + 6, "Unhandled Exception", fg, title_bg);

    int y = dy + 38;
    char line[128];

    /* Exception info */
    DWORD exc_code = 0;
    switch (int_no) {
        case 0:  exc_code = 0xC0000094; break;  /* div by zero */
        case 6:  exc_code = 0xC000001D; break;  /* illegal instruction */
        case 13: exc_code = 0xC0000005; break;  /* GPF → access violation */
        case 14: exc_code = 0xC0000005; break;  /* page fault → access violation */
        default: exc_code = 0xC0000025; break;  /* noncontinuable */
    }

    snprintf(line, sizeof(line), "Exception:  %s (INT %u)", exception_name, int_no);
    gfx_draw_string(dx + 14, y, line, fg, bg);
    y += 18;

    snprintf(line, sizeof(line), "Code:       0x%08X  %s", exc_code, exception_code_name(exc_code));
    gfx_draw_string(dx + 14, y, line, fg_val, bg);
    y += 18;

    snprintf(line, sizeof(line), "Task:       '%s'  (PID %d, TID %d)",
             task->name[0] ? task->name : "?", task->pid, task_get_current());
    gfx_draw_string(dx + 14, y, line, fg, bg);
    y += 18;

    snprintf(line, sizeof(line), "Address:    0x%08X", regs->eip);
    gfx_draw_string(dx + 14, y, line, fg_val, bg);
    y += 18;

    if (int_no == 14) {
        snprintf(line, sizeof(line), "Fault Addr: 0x%08X  (%s)",
                 cr2, (regs->err_code & 2) ? "write" : "read");
        gfx_draw_string(dx + 14, y, line, fg_val, bg);
        y += 18;
    }

    /* Separator line */
    y += 4;
    gfx_fill_rect(dx + 10, y, dw - 20, 1, fg_dim);
    y += 8;

    /* Register dump */
    gfx_draw_string(dx + 14, y, "Register State:", fg, bg);
    y += 18;

    snprintf(line, sizeof(line), "EAX=%08X  EBX=%08X  ECX=%08X  EDX=%08X",
             regs->eax, regs->ebx, regs->ecx, regs->edx);
    gfx_draw_string(dx + 14, y, line, fg_dim, bg);
    y += 16;

    snprintf(line, sizeof(line), "ESI=%08X  EDI=%08X  EBP=%08X  ESP=%08X",
             regs->esi, regs->edi, regs->ebp, regs->useresp);
    gfx_draw_string(dx + 14, y, line, fg_dim, bg);
    y += 16;

    snprintf(line, sizeof(line), "EIP=%08X  EFL=%08X  CS=%04X  SS=%04X",
             regs->eip, regs->eflags, regs->cs, regs->ss);
    gfx_draw_string(dx + 14, y, line, fg_dim, bg);
    y += 22;

    /* Stack trace (walk EBP chain) */
    gfx_draw_string(dx + 14, y, "Stack Trace:", fg, bg);
    y += 18;

    uint32_t ebp = regs->ebp;
    for (int frame = 0; frame < 6 && ebp > 0x1000 && ebp < 0x10000000; frame++) {
        uint32_t ret_addr = *(uint32_t *)(ebp + 4);
        uint32_t prev_ebp = *(uint32_t *)ebp;
        snprintf(line, sizeof(line), "  #%d  0x%08X  (frame 0x%08X)", frame, ret_addr, ebp);
        gfx_draw_string(dx + 14, y, line, fg_dim, bg);
        y += 16;
        if (prev_ebp <= ebp) break;  /* prevent infinite loop */
        ebp = prev_ebp;
    }

    /* Footer */
    y = dy + dh - 22;
    gfx_draw_string(dx + 14, y, "The application will be terminated.", fg_dim, bg);

    /* Flush to screen */
    gfx_flip_rect(dx - 2, dy - 2, dw + 4, dh + 4);

    serial_printf("[CRASH DIALOG] Displayed for task '%s' (PID %d)\n",
                  task->name, task->pid);
}
