#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "ext4_structs.h"
#include "ext4_inode.h"

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

int is_inode_free_in_bitmap(FILE *f, uint64_t bitmap_block, uint32_t block_size,
                             uint32_t index_in_group) {
    long byte_offset = bitmap_block * block_size + (index_in_group / 8);
    uint8_t byte;
    fseek(f, byte_offset, SEEK_SET);
    fread(&byte, 1, 1, f);
    int bit = index_in_group % 8;
    return ((byte >> bit) & 1) == 0;   // 0 = free, 1 = allocated
}

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
    int is_64bit = (sb.s_feature_incompat & 0x80) != 0;
    size_t desc_size = is_64bit ? sizeof(ext4_group_desc_t) : 32;
    uint32_t num_groups = (sb.s_blocks_count_lo + sb.s_blocks_per_group - 1)
                          / sb.s_blocks_per_group;
    uint16_t inode_size = sb.s_inode_size;
    uint32_t inodes_per_group = sb.s_inodes_per_group;

    long gdt_offset = (long)(sb.s_first_data_block + 1) * block_size;

    ext4_group_desc_t *groups = calloc(num_groups, sizeof(ext4_group_desc_t));
    fseek(f, gdt_offset, SEEK_SET);
    for (uint32_t i = 0; i < num_groups; i++) {
        fread(&groups[i], desc_size, 1, f);
    }

    printf("=== Scanning %u inodes across %u groups for deletion signatures ===\n\n",
           sb.s_inodes_count, num_groups);

    int deleted_found = 0;

    for (uint32_t g = 0; g < num_groups; g++) {
        uint64_t inode_table_block = groups[g].bg_inode_table_lo |
                                      ((uint64_t)groups[g].bg_inode_table_hi << 32);
        uint64_t inode_bitmap_block = groups[g].bg_inode_bitmap_lo |
                                      ((uint64_t)groups[g].bg_inode_bitmap_hi << 32);

        for (uint32_t idx = 0; idx < inodes_per_group; idx++) {
            uint32_t inode_num = g * inodes_per_group + idx + 1;

            if (inode_num < sb.s_first_ino && inode_num != 2) continue;

            long offset = inode_table_block * block_size + (long)idx * inode_size;
            ext4_inode_t inode;
            memset(&inode, 0, sizeof(inode));
            fseek(f, offset, SEEK_SET);
            fread(&inode, sizeof(ext4_inode_t) < inode_size ? sizeof(ext4_inode_t) : inode_size, 1, f);

            int free_in_bitmap = is_inode_free_in_bitmap(f, inode_bitmap_block, block_size, idx);

            if (inode.i_dtime != 0 && free_in_bitmap) {
                deleted_found++;
                time_t dtime = inode.i_dtime;
                printf("[DELETED] Inode %u | size=%u bytes | links=%u | dtime=%s",
                       inode_num, inode.i_size_lo, inode.i_links_count,
                       ctime(&dtime));
                printf("           i_block[0..3] = %u %u %u %u (0=extent header if flag set)\n",
                       inode.i_block[0], inode.i_block[1], inode.i_block[2], inode.i_block[3]);
                printf("           i_flags = 0x%x %s\n\n", inode.i_flags,
                       (inode.i_flags & 0x80000) ? "(EXTENTS)" : "(indirect blocks)");
            }
        }
    }

    printf("Total deleted inode candidates found: %d\n", deleted_found);

    free(groups);
    fclose(f);
    return 0;
}
