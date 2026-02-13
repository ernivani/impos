#ifndef _KERNEL_WIN32_SEH_H
#define _KERNEL_WIN32_SEH_H

#include <kernel/win32_types.h>
#include <kernel/idt.h>
#include <kernel/task.h>

/* Dispatch a CPU exception through the SEH chain for a PE task.
 * Returns 1 if handled (regs updated), 0 if not handled (fall through). */
int seh_dispatch_exception(task_info_t *t, registers_t *regs, uint32_t int_no);

/* SetUnhandledExceptionFilter — store/return global top-level filter */
LPTOP_LEVEL_EXCEPTION_FILTER seh_SetUnhandledExceptionFilter(
    LPTOP_LEVEL_EXCEPTION_FILTER filter);

/* UnhandledExceptionFilter — invoke the stored filter */
LONG seh_UnhandledExceptionFilter(EXCEPTION_POINTERS *ep);

/* RaiseException — build record and walk chain from current TEB */
void seh_RaiseException(DWORD code, DWORD flags, DWORD nargs, const DWORD *args);

/* RtlUnwind — walk chain with UNWINDING flag, set head to target */
void seh_RtlUnwind(void *target_frame, void *target_ip,
                   EXCEPTION_RECORD *er, DWORD return_value);

/* ── Vectored Exception Handling ─────────────────────────────── */

typedef LONG (WINAPI *PVECTORED_EXCEPTION_HANDLER)(EXCEPTION_POINTERS *);

/* Add/Remove vectored exception handlers.
 * first_handler: 1 = insert at head, 0 = insert at tail.
 * Returns opaque handle for removal. */
PVOID seh_AddVectoredExceptionHandler(ULONG first_handler,
                                       PVECTORED_EXCEPTION_HANDLER handler);
ULONG seh_RemoveVectoredExceptionHandler(PVOID handle);

/* Add/Remove vectored continue handlers (called after CONTINUE_EXECUTION) */
PVOID seh_AddVectoredContinueHandler(ULONG first_handler,
                                      PVECTORED_EXCEPTION_HANDLER handler);
ULONG seh_RemoveVectoredContinueHandler(PVOID handle);

/* Dispatch through VEH chain before SEH. Returns EXCEPTION_CONTINUE_EXECUTION
 * or EXCEPTION_CONTINUE_SEARCH. */
LONG seh_dispatch_vectored(EXCEPTION_POINTERS *ep);

/* ── C++ Exception Support ───────────────────────────────────── */

/* _set_se_translator: set per-thread SEH→C++ exception translator.
 * Returns previous translator. */
typedef void (*_se_translator_function)(unsigned int, EXCEPTION_POINTERS *);
_se_translator_function seh_set_se_translator(_se_translator_function func);

/* ── Guard Page Support ──────────────────────────────────────── */
#define STATUS_GUARD_PAGE_VIOLATION 0x80000001
#define PAGE_GUARD                  0x100

/* ── Crash Dialog ─────────────────────────────────────────────── */

/* Show a graphical crash dialog with exception info, registers, and stack trace.
 * Called from the ISR handler when a PE task has an unhandled exception. */
void seh_show_crash_dialog(const char *exception_name, uint32_t int_no,
                           registers_t *regs, uint32_t cr2, task_info_t *task);

#endif
