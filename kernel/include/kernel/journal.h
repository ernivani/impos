#ifndef _KERNEL_JOURNAL_H
#define _KERNEL_JOURNAL_H

#include <stdint.h>

/* ── Journal Configuration ──────────────────────────────────────── */

#define JOURNAL_MAGIC           0x4A524E4C  /* "JRNL" */
#define JOURNAL_BLOCKS          1024        /* 4MB journal area */
#define JOURNAL_BLOCK_START     68          /* starts after inode table */
#define JOURNAL_MAX_ENTRIES     256         /* max log entries per transaction */

/* After journal, data blocks start at 68 + 1024 = 1092 */
#define JOURNAL_DATA_START      (JOURNAL_BLOCK_START + JOURNAL_BLOCKS)  /* 1092 */

/* ── Journal Entry Types ────────────────────────────────────────── */

#define JLOG_INODE_UPDATE       1   /* inode metadata changed */
#define JLOG_BLOCK_ALLOC        2   /* block allocated */
#define JLOG_BLOCK_FREE         3   /* block freed */
#define JLOG_INODE_ALLOC        4   /* inode allocated */
#define JLOG_INODE_FREE         5   /* inode freed */
#define JLOG_DIR_ADD            6   /* directory entry added */
#define JLOG_DIR_REMOVE         7   /* directory entry removed */

/* ── Transaction States ─────────────────────────────────────────── */

#define TXN_NONE                0
#define TXN_ACTIVE              1
#define TXN_COMMITTED           2

/* ── On-disk Structures ─────────────────────────────────────────── */

/* Journal superblock: occupies the first block of the journal area */
typedef struct {
    uint32_t magic;
    uint32_t head;              /* next write position (block offset within journal) */
    uint32_t tail;              /* oldest un-applied entry (block offset) */
    uint32_t sequence;          /* monotonic transaction counter */
    uint32_t num_transactions;  /* count of committed but unapplied transactions */
    uint8_t  _pad[4096 - 20];
} journal_super_t;

/* Transaction header: marks the start of a transaction's log entries */
typedef struct {
    uint32_t magic;             /* JOURNAL_MAGIC */
    uint32_t sequence;          /* transaction sequence number */
    uint32_t num_entries;       /* number of log entries in this transaction */
    uint32_t state;             /* TXN_ACTIVE / TXN_COMMITTED */
    uint8_t  _pad[16];         /* alignment padding */
} txn_header_t;

/* Individual log entry (32 bytes each, 128 entries per block) */
typedef struct {
    uint8_t  type;              /* JLOG_* */
    uint8_t  _pad1[3];
    uint32_t arg0;              /* inode number or block number */
    uint32_t arg1;              /* secondary argument (parent inode, etc.) */
    uint32_t arg2;              /* tertiary argument */
    uint8_t  name[16];         /* short name for dir operations */
} journal_entry_t;

/* ── API ────────────────────────────────────────────────────────── */

void journal_init(void);
int  journal_begin(void);
void journal_log_inode_update(uint32_t inode_num);
void journal_log_block_alloc(uint32_t block_num);
void journal_log_block_free(uint32_t block_num);
void journal_log_inode_alloc(uint32_t inode_num);
void journal_log_inode_free(uint32_t inode_num);
void journal_log_dir_add(uint32_t parent_inode, uint32_t child_inode, const char *name);
void journal_log_dir_remove(uint32_t parent_inode, uint32_t child_inode, const char *name);
int  journal_commit(void);
int  journal_replay(void);

#endif
