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

#endif
