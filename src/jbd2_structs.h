#ifndef JBD2_STRUCTS_H
#define JBD2_STRUCTS_H

#include <stdint.h>

#define JBD2_MAGIC_NUMBER 0xc03b3998

// Block types
#define JBD2_DESCRIPTOR_BLOCK   1
#define JBD2_COMMIT_BLOCK       2
#define JBD2_SUPERBLOCK_V1      3
#define JBD2_SUPERBLOCK_V2      4
#define JBD2_REVOKE_BLOCK       5

// All multi-byte fields in jbd2 are BIG-ENDIAN (unlike the rest of ext4!)
typedef struct {
    uint32_t h_magic;
    uint32_t h_blocktype;
    uint32_t h_sequence;
} __attribute__((packed)) journal_header_t;

// Journal superblock (common part of v1/v2)
typedef struct {
    journal_header_t s_header;
    uint32_t s_blocksize;
    uint32_t s_maxlen;       // total journal blocks
    uint32_t s_first;        // first block of log info (usually 1)
    uint32_t s_sequence;     // first commit ID expected in log
    uint32_t s_start;        // block of start of log, 0 = no log (journal empty)
    uint32_t s_errno;
    // v2 fields follow but we don't need them for basic parsing yet
} __attribute__((packed)) journal_superblock_t;

// Helper: jbd2 is big-endian, host is likely little-endian (x86/x86_64)
static inline uint32_t be32(uint32_t val) {
    return ((val & 0xFF) << 24) | ((val & 0xFF00) << 8) |
           ((val & 0xFF0000) >> 8) | ((val & 0xFF000000) >> 24);
}
// Tag flags
#define JBD2_FLAG_ESCAPE     1
#define JBD2_FLAG_SAME_UUID  2
#define JBD2_FLAG_DELETED    4
#define JBD2_FLAG_LAST_TAG   8

// journal_checksum_v3 tag format (16 bytes, big-endian fields)
typedef struct {
    uint32_t t_blocknr;       // low 32 bits: which real FS block this data block backs up
    uint32_t t_flags;
    uint32_t t_blocknr_high;  // high 32 bits (for 64bit feature)
    uint32_t t_checksum;
} __attribute__((packed)) journal_block_tag3_t;
#endif

