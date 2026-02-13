#include <kernel/win32_seh.h>
#include <kernel/win32_types.h>
#include <kernel/idt.h>
#include <kernel/task.h>
#include <kernel/io.h>
#include <string.h>
#include <stdio.h>

/* ── Global unhandled exception filter ──────────────────────── */

static LPTOP_LEVEL_EXCEPTION_FILTER g_unhandled_filter = NULL;

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
