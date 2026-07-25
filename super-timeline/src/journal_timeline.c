#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../../common/ext4_structs.h"
#include "../../common/ext4_inode.h"
#include "../../common/ext4_groupdesc.h"
#include "../../common/jbd2_structs.h"

FILE *f;
ext4_superblock_t sb;
uint32_t block_size;
uint16_t inode_size;
uint32_t inodes_per_group;
ext4_group_desc_t *groups;
uint32_t num_groups;

int find_journal_extent(uint64_t *start_block, uint64_t *num_blocks) {
    uint32_t inode_num = 8;
    uint32_t g = (inode_num - 1) / inodes_per_group;
    uint32_t idx = (inode_num - 1) % inodes_per_group;
    uint64_t inode_table_block = groups[g].bg_inode_table_lo |
                                  ((uint64_t)groups[g].bg_inode_table_hi << 32);

    long offset = inode_table_block * block_size + (long)idx * inode_size;
    ext4_inode_t inode;
    memset(&inode, 0, sizeof(inode));
    fseek(f, offset, SEEK_SET);
    fread(&inode, sizeof(ext4_inode_t) < inode_size ? sizeof(ext4_inode_t) : inode_size, 1, f);

    if (!(inode.i_flags & 0x80000)) return 0;

    uint16_t eh[4];
    memcpy(eh, inode.i_block, sizeof(eh));
    if (eh[1] == 0) return 0;

    uint8_t extent_raw[12];
    memcpy(extent_raw, (uint8_t *)inode.i_block + 12, 12);
    uint32_t ee_block;
    uint16_t ee_len, ee_start_hi;
    uint32_t ee_start_lo;
    memcpy(&ee_block, extent_raw, 4);
    memcpy(&ee_len, extent_raw + 4, 2);
    memcpy(&ee_start_hi, extent_raw + 6, 2);
    memcpy(&ee_start_lo, extent_raw + 8, 4);
    uint64_t physical_block = ee_start_lo | ((uint64_t)ee_start_hi << 32);

    *start_block = physical_block;
    *num_blocks = inode.i_size_lo / block_size;
    return 1;
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <ext4_image> <output_csv>\n", argv[0]);
        return 1;
    }

    f = fopen(argv[1], "rb");
    if (!f) { perror("fopen"); return 1; }

    FILE *csv = fopen(argv[2], "w");
    fprintf(csv, "seq,commit_sec,commit_nsec,journal_block\n");

    fseek(f, 1024, SEEK_SET);
    fread(&sb, sizeof(sb), 1, f);
    if (sb.s_magic != EXT4_SUPER_MAGIC) {
        fprintf(stderr, "Not ext4\n");
        return 1;
    }

    block_size = 1024 << sb.s_log_block_size;
    inode_size = sb.s_inode_size;
    inodes_per_group = sb.s_inodes_per_group;
    int is_64bit = (sb.s_feature_incompat & 0x80) != 0;
    size_t desc_size = is_64bit ? sizeof(ext4_group_desc_t) : 32;
    num_groups = (sb.s_blocks_count_lo + sb.s_blocks_per_group - 1) / sb.s_blocks_per_group;

    long gdt_offset = (long)(sb.s_first_data_block + 1) * block_size;
    groups = calloc(num_groups, sizeof(ext4_group_desc_t));
    fseek(f, gdt_offset, SEEK_SET);
    for (uint32_t i = 0; i < num_groups; i++) fread(&groups[i], desc_size, 1, f);

    uint64_t journal_start, journal_blocks;
    if (!find_journal_extent(&journal_start, &journal_blocks)) {
        fprintf(stderr, "Could not locate journal extent\n");
        return 1;
    }
    printf("Journal at blocks %lu - %lu\n", journal_start, journal_start + journal_blocks - 1);

    uint8_t *buf = malloc(block_size);
    int count = 0;

    for (uint64_t i = 1; i < journal_blocks; i++) {
        fseek(f, (long)(journal_start + i) * block_size, SEEK_SET);
        fread(buf, block_size, 1, f);

        journal_header_t *h = (journal_header_t *)buf;
        if (be32(h->h_magic) != JBD2_MAGIC_NUMBER) continue;
        if (be32(h->h_blocktype) != JBD2_COMMIT_BLOCK) continue;

        commit_header_t ch;
        memcpy(&ch, buf, sizeof(ch));

        uint32_t seq = be32(ch.h_header.h_sequence);
        uint64_t commit_sec = be64(ch.h_commit_sec);
        uint32_t commit_nsec = be32(ch.h_commit_nsec);

        printf("[COMMIT] journal block %lu | seq=%u | commit_sec=%lu",
               journal_start + i, seq, commit_sec);

        if (commit_sec > 0 && commit_sec < 4000000000UL) {
            time_t t = (time_t)commit_sec;
            char timebuf[64];
            strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", gmtime(&t));
            printf(" (%s UTC)\n", timebuf);
        } else {
            printf(" (out of range / suspicious)\n");
        }

        fprintf(csv, "%u,%lu,%u,%lu\n", seq, commit_sec, commit_nsec, journal_start + i);
        count++;
    }

    printf("\nWrote %d commit timestamps to %s\n", count, argv[2]);

    free(buf);
    free(groups);
    fclose(f);
    fclose(csv);
    return 0;
}
