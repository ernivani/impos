#ifndef _KERNEL_IOCTL_H
#define _KERNEL_IOCTL_H

#include <stdint.h>

/*
 * Generic ioctl encoding, compatible with Linux convention.
 *
 * Layout of a 32-bit ioctl command number:
 *   bits 31-30: direction  (00=none, 01=write, 10=read, 11=read|write)
 *   bits 29-16: size of argument struct
 *   bits 15-8:  type (magic character identifying subsystem)
 *   bits 7-0:   number (command within the type)
 */

#define _IOC_NRBITS     8
#define _IOC_TYPEBITS   8
#define _IOC_SIZEBITS   14
#define _IOC_DIRBITS    2

#define _IOC_NRSHIFT    0
#define _IOC_TYPESHIFT  (_IOC_NRSHIFT + _IOC_NRBITS)       /* 8 */
#define _IOC_SIZESHIFT  (_IOC_TYPESHIFT + _IOC_TYPEBITS)   /* 16 */
#define _IOC_DIRSHIFT   (_IOC_SIZESHIFT + _IOC_SIZEBITS)   /* 30 */

#define _IOC_NONE       0U
#define _IOC_WRITE      1U
#define _IOC_READ       2U

#define _IOC(dir, type, nr, size) \
    (((dir)  << _IOC_DIRSHIFT)  | \
     ((type) << _IOC_TYPESHIFT) | \
     ((nr)   << _IOC_NRSHIFT)   | \
     ((size) << _IOC_SIZESHIFT))

/* Convenience macros */
#define _IO(type, nr)           _IOC(_IOC_NONE,  (type), (nr), 0)
#define _IOR(type, nr, sz)      _IOC(_IOC_READ,  (type), (nr), (sz))
#define _IOW(type, nr, sz)      _IOC(_IOC_WRITE, (type), (nr), (sz))
#define _IOWR(type, nr, sz)     _IOC(_IOC_READ | _IOC_WRITE, (type), (nr), (sz))

/* Extract fields from an ioctl command */
#define _IOC_DIR(cmd)   (((cmd) >> _IOC_DIRSHIFT)  & ((1U << _IOC_DIRBITS)  - 1))
#define _IOC_TYPE(cmd)  (((cmd) >> _IOC_TYPESHIFT) & ((1U << _IOC_TYPEBITS) - 1))
#define _IOC_NR(cmd)    (((cmd) >> _IOC_NRSHIFT)   & ((1U << _IOC_NRBITS)   - 1))
#define _IOC_SIZE(cmd)  (((cmd) >> _IOC_SIZESHIFT) & ((1U << _IOC_SIZEBITS) - 1))

/* Generic ioctl dispatcher: called from SYS_IOCTL.
 * Returns 0 on success, negative errno on failure. */
int ioctl_dispatch(int fd, uint32_t cmd, void *arg);

#endif
