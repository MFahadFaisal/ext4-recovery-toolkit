#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "ext4_structs.h"

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

int is_block_free(FILE *f, uint64_t bitmap_block, uint32_t block_size, uint32_t index) {
    long byte_offset = bitmap_block * block_size + (index / 8);
    uint8_t byte;
    fseek(f, byte_offset, SEEK_SET);
    fread(&byte, 1, 1, f);
    int bit = index % 8;
    return ((byte >> bit) & 1) == 0;
}

int looks_like_text(uint8_t *buf, uint32_t len) {
    int printable = 0;
    for (uint32_t i = 0; i < len; i++) {
        if (isprint(buf[i]) || buf[i] == '\n' || buf[i] == '\r' || buf[i] == '\t') printable++;
    }
    return printable > (int)(len * 0.9);
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
    uint32_t blocks_per_group = sb.s_blocks_per_group;

    long gdt_offset = (long)(sb.s_first_data_block + 1) * block_size;
    ext4_group_desc_t *groups = calloc(num_groups, sizeof(ext4_group_desc_t));
    fseek(f, gdt_offset, SEEK_SET);
    for (uint32_t i = 0; i < num_groups; i++) {
        fread(&groups[i], desc_size, 1, f);
    }

    system("mkdir -p recovered");
    uint8_t *buf = malloc(block_size);

    long total_free = 0, zero_blocks = 0, nonzero_blocks = 0, carved = 0;
    int shown_sample = 0;

    for (uint32_t g = 0; g < num_groups; g++) {
        uint64_t bitmap_block = groups[g].bg_block_bitmap_lo |
                                 ((uint64_t)groups[g].bg_block_bitmap_hi << 32);

        for (uint32_t idx = 0; idx < blocks_per_group; idx++) {
            uint32_t block_num = sb.s_first_data_block + g * blocks_per_group + idx;
            if (block_num >= sb.s_blocks_count_lo) break;

            if (!is_block_free(f, bitmap_block, block_size, idx)) continue;

            total_free++;

            fseek(f, (long)block_num * block_size, SEEK_SET);
            fread(buf, block_size, 1, f);

            int all_zero = 1;
            for (uint32_t i = 0; i < block_size; i++) if (buf[i] != 0) { all_zero = 0; break; }

            if (all_zero) { zero_blocks++; continue; }
            nonzero_blocks++;

            if (!shown_sample) {
                printf("[SAMPLE non-zero free block %u, first 64 bytes]:\n", block_num);
                for (int i = 0; i < 64; i++) {
                    printf("%02x ", buf[i]);
                    if ((i + 1) % 16 == 0) printf("\n");
                }
                printf("\n\n");
                shown_sample = 1;
            }

            if (looks_like_text(buf, block_size)) {
                char filename[128];
                snprintf(filename, sizeof(filename), "recovered/carved_block_%u.txt", block_num);
                FILE *out = fopen(filename, "wb");
                fwrite(buf, block_size, 1, out);
                fclose(out);
                printf("[CARVED] Free block %u looks like text -> %s\n", block_num, filename);
                carved++;
            }
        }
    }

    printf("\n=== Stats ===\n");
    printf("Total free blocks scanned: %ld\n", total_free);
    printf("All-zero free blocks:      %ld\n", zero_blocks);
    printf("Non-zero free blocks:      %ld\n", nonzero_blocks);
    printf("Carved as text:            %ld\n", carved);

    free(buf);
    free(groups);
    fclose(f);
    return 0;
}
