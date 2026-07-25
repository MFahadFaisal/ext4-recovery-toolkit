#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../common/ext4_structs.h"

// 64-byte version (32-byte classic + 32-byte hi fields if 64bit feature is set)
typedef struct {
    uint32_t bg_block_bitmap_lo;
    uint32_t bg_inode_bitmap_lo;
    uint32_t bg_inode_table_lo;
    uint16_t bg_free_blocks_count_lo;
    uint16_t bg_free_inodes_count_lo;
    uint16_t bg_used_dirs_count_lo;
    uint16_t bg_flags;
    uint32_t bg_exclude_bitmap_lo;
    uint16_t bg_block_bitmap_csum_lo;
    uint16_t bg_inode_bitmap_csum_lo;
    uint16_t bg_itable_unused_lo;
    uint16_t bg_checksum;
    // --- hi 32 bytes, only present if 64bit feature flag set ---
    uint32_t bg_block_bitmap_hi;
    uint32_t bg_inode_bitmap_hi;
    uint32_t bg_inode_table_hi;
    uint16_t bg_free_blocks_count_hi;
    uint16_t bg_free_inodes_count_hi;
    uint16_t bg_used_dirs_count_hi;
    uint16_t bg_itable_unused_hi;
    uint32_t bg_exclude_bitmap_hi;
    uint16_t bg_block_bitmap_csum_hi;
    uint16_t bg_inode_bitmap_csum_hi;
    uint32_t bg_reserved;
} __attribute__((packed)) ext4_group_desc_t;

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <ext4_image>\n", argv[0]);
        return 1;
    }

    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror("fopen"); return 1; }

    ext4_superblock_t sb;
    fseek(f, 1024, SEEK_SET);
    fread(&sb, sizeof(sb), 1, f);

    if (sb.s_magic != EXT4_SUPER_MAGIC) {
        fprintf(stderr, "Not ext4\n");
        return 1;
    }

    uint32_t block_size = 1024 << sb.s_log_block_size;
    int is_64bit = (sb.s_feature_incompat & 0x80) != 0;  // INCOMPAT_64BIT
    size_t desc_size = is_64bit ? sizeof(ext4_group_desc_t) : 32;

    uint32_t num_groups = (sb.s_blocks_count_lo + sb.s_blocks_per_group - 1)
                          / sb.s_blocks_per_group;

    long gdt_offset = (long)(sb.s_first_data_block + 1) * block_size;

    printf("Block size: %u | 64bit: %s | Groups: %u\n\n",
           block_size, is_64bit ? "yes" : "no", num_groups);

    fseek(f, gdt_offset, SEEK_SET);

    for (uint32_t i = 0; i < num_groups; i++) {
        ext4_group_desc_t gd;
        memset(&gd, 0, sizeof(gd));
        fread(&gd, desc_size, 1, f);

        uint64_t block_bitmap = gd.bg_block_bitmap_lo |
                                 ((uint64_t)gd.bg_block_bitmap_hi << 32);
        uint64_t inode_bitmap = gd.bg_inode_bitmap_lo |
                                 ((uint64_t)gd.bg_inode_bitmap_hi << 32);
        uint64_t inode_table  = gd.bg_inode_table_lo |
                                 ((uint64_t)gd.bg_inode_table_hi << 32);

        printf("Group %u:\n", i);
        printf("  Block bitmap:  block %lu\n", block_bitmap);
        printf("  Inode bitmap:  block %lu\n", inode_bitmap);
        printf("  Inode table:   block %lu\n", inode_table);
        printf("  Free blocks:   %u\n", gd.bg_free_blocks_count_lo);
        printf("  Free inodes:   %u\n", gd.bg_free_inodes_count_lo);
        printf("  Used dirs:     %u\n\n", gd.bg_used_dirs_count_lo);
    }

    fclose(f);
    return 0;
}
