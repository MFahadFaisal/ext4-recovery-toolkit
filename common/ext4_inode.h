#ifndef EXT4_INODE_H
#define EXT4_INODE_H

#include <stdint.h>

typedef struct {
    uint16_t i_mode;
    uint16_t i_uid;
    uint32_t i_size_lo;
    uint32_t i_atime;
    uint32_t i_ctime;
    uint32_t i_mtime;
    uint32_t i_dtime;        // deletion time — our key signal
    uint16_t i_gid;
    uint16_t i_links_count;  // 0 when deleted
    uint32_t i_blocks_lo;
    uint32_t i_flags;        // bit 0x80000 = uses extents
    uint32_t i_osd1;
    uint32_t i_block[15];    // extent tree or indirect pointers live here
    uint32_t i_generation;
    uint32_t i_file_acl_lo;
    uint32_t i_size_high;
    uint32_t i_obso_faddr;
    uint16_t i_blocks_high;
    uint16_t i_file_acl_high;
    uint16_t i_uid_high;
    uint16_t i_gid_high;
    uint16_t i_checksum_lo;
    uint16_t i_reserved;
    uint16_t i_extra_isize;
    uint16_t i_checksum_hi;
    uint32_t i_ctime_extra;
    uint32_t i_mtime_extra;
    uint32_t i_atime_extra;
    uint32_t i_crtime;
    uint32_t i_crtime_extra;
} __attribute__((packed)) ext4_inode_t;

#endif
