#ifndef EXT4_DIRENT_H
#define EXT4_DIRENT_H

#include <stdint.h>

// ext4 directory entry (dir_entry_2, with file_type byte)
// NOT fixed-size — name[] is variable length, entry length is rec_len
typedef struct {
    uint32_t inode;
    uint16_t rec_len;
    uint8_t  name_len;
    uint8_t  file_type;
    // name follows immediately, name_len bytes, not null-terminated
} __attribute__((packed)) ext4_dirent_header_t;

#endif

