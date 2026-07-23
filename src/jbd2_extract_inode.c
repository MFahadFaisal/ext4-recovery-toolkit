#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "ext4_structs.h"
#include "ext4_inode.h"

int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <ext4_image> <journal_data_abs_block> <inode_num>\n", argv[0]);
        fprintf(stderr, "  inode_num is the real inode number you're looking for (e.g. 14)\n");
        return 1;
    }

    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror("fopen"); return 1; }
    uint64_t data_block = strtoull(argv[2], NULL, 10);
    uint32_t target_inode = strtoul(argv[3], NULL, 10);

    ext4_superblock_t sb;
    fseek(f, 1024, SEEK_SET);
    fread(&sb, sizeof(sb), 1, f);
    uint32_t block_size = 1024 << sb.s_log_block_size;
    uint16_t inode_size = sb.s_inode_size;
    uint32_t inodes_per_group = sb.s_inodes_per_group;

    // We already know from Phase 3 that inode table for group 0 starts at block 275
    // and this journal block is a backup of real block 278 (the 4th inode-table block).
    // Compute which inode indices (within the group) this real block covers:
    uint64_t inode_table_start = 275; // TODO: read dynamically from group desc in a later refinement
    uint64_t real_block = 278;        // from jbd2_tags output for this specific tag
    uint32_t inodes_per_block = block_size / inode_size;
    uint32_t first_index_in_group = (real_block - inode_table_start) * inodes_per_block;

    uint32_t target_index_in_group = target_inode - 1; // inode numbers are 1-based
    uint32_t offset_in_block = (target_index_in_group - first_index_in_group) * inode_size;

    printf("Journal data block %lu covers group-relative inode indices %u-%u (inode numbers %u-%u)\n",
           data_block, first_index_in_group, first_index_in_group + inodes_per_block - 1,
           first_index_in_group + 1, first_index_in_group + inodes_per_block);
    printf("Target inode %u -> offset %u within this block\n\n", target_inode, offset_in_block);

    uint8_t *buf = malloc(block_size);
    fseek(f, (long)data_block * block_size, SEEK_SET);
    fread(buf, block_size, 1, f);

    ext4_inode_t *inode = (ext4_inode_t *)(buf + offset_in_block);

    printf("=== Inode %u as found in JOURNAL (pre-checkpoint copy) ===\n", target_inode);
    printf("i_size_lo:    %u\n", inode->i_size_lo);
    printf("i_links_count:%u\n", inode->i_links_count);
    printf("i_dtime:      %u", inode->i_dtime);
    if (inode->i_dtime) {
        time_t dt = inode->i_dtime;
        printf(" (%s)", ctime(&dt));
    } else {
        printf(" (0 = not yet marked deleted at this point in time!)\n");
    }
    printf("i_flags:      0x%x %s\n", inode->i_flags,
           (inode->i_flags & 0x80000) ? "(EXTENTS)" : "(indirect)");
    printf("i_block[0..3]: %u %u %u %u\n",
           inode->i_block[0], inode->i_block[1], inode->i_block[2], inode->i_block[3]);

    // Decode extent header if present
    if (inode->i_flags & 0x80000) {
        uint16_t *eh = (uint16_t *)inode->i_block;
        uint16_t eh_magic = eh[0];
        uint16_t eh_entries = eh[1];
        uint16_t eh_max = eh[2];
        uint16_t eh_depth = eh[3];
        printf("\nExtent header: magic=0x%x entries=%u max=%u depth=%u\n",
               eh_magic, eh_entries, eh_max, eh_depth);

        if (eh_magic == 0xF30A && eh_entries > 0 && eh_depth == 0) {
            // leaf extents follow the 12-byte header, each extent is 12 bytes
            uint8_t *extent_ptr = (uint8_t *)inode->i_block + 12;
            for (int e = 0; e < eh_entries; e++) {
                uint32_t ee_block = *(uint32_t *)(extent_ptr);
                uint16_t ee_len   = *(uint16_t *)(extent_ptr + 4);
                uint16_t ee_start_hi = *(uint16_t *)(extent_ptr + 6);
                uint32_t ee_start_lo = *(uint32_t *)(extent_ptr + 8);
                uint64_t physical_block = ee_start_lo | ((uint64_t)ee_start_hi << 32);

                printf("  Extent %d: logical_block=%u len=%u -> physical_block=%lu\n",
                       e, ee_block, ee_len, physical_block);

                extent_ptr += 12;
            }
        }
    }

    free(buf);
    fclose(f);
    return 0;
}
