#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../common/ext4_structs.h"
#include "../common/ext4_inode.h"
#include "../common/ext4_groupdesc.h"
#include "../common/jbd2_structs.h"

#define MAX_DELETED 4096

typedef struct {
    uint32_t inode_num;
    uint64_t inode_table_block;
    uint32_t offset_in_block;
} deleted_candidate_t;

FILE *f;
ext4_superblock_t sb;
uint32_t block_size;
uint16_t inode_size;
uint32_t inodes_per_group;
ext4_group_desc_t *groups;
uint32_t num_groups;
int is_64bit;

int is_bit_free(uint64_t bitmap_block, uint32_t index) {
    long byte_offset = bitmap_block * block_size + (index / 8);
    uint8_t byte;
    fseek(f, byte_offset, SEEK_SET);
    fread(&byte, 1, 1, f);
    return ((byte >> (index % 8)) & 1) == 0;
}

int find_deleted_inodes(deleted_candidate_t *out) {
    int count = 0;
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

            if (inode.i_dtime != 0 && is_bit_free(inode_bitmap_block, idx)) {
                if (count >= MAX_DELETED) continue;
                uint32_t inodes_per_block = block_size / inode_size;
                uint32_t block_within_table = idx / inodes_per_block;
                out[count].inode_num = inode_num;
                out[count].inode_table_block = inode_table_block + block_within_table;
                out[count].offset_in_block = (idx % inodes_per_block) * inode_size;
                count++;
            }
        }
    }
    return count;
}

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
    uint16_t eh_entries = eh[1];
    if (eh_entries == 0) return 0;

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

typedef void (*tag_callback_t)(uint64_t data_block, uint64_t real_block, uint32_t seq, void *ctx);

void parse_descriptor_tags(uint64_t desc_block, uint32_t seq, tag_callback_t cb, void *ctx) {
    uint8_t *buf = malloc(block_size);
    fseek(f, (long)desc_block * block_size, SEEK_SET);
    fread(buf, block_size, 1, f);

    uint32_t offset = sizeof(journal_header_t);
    uint64_t data_block_cursor = desc_block + 1;

    while (offset + sizeof(journal_block_tag3_t) <= block_size) {
        journal_block_tag3_t *tag = (journal_block_tag3_t *)(buf + offset);
        uint32_t flags = be32(tag->t_flags);
        uint32_t blocknr_lo = be32(tag->t_blocknr);
        uint32_t blocknr_hi = be32(tag->t_blocknr_high);
        uint64_t real_block = blocknr_lo | ((uint64_t)blocknr_hi << 32);

        cb(data_block_cursor, real_block, seq, ctx);

        offset += sizeof(journal_block_tag3_t);
        if (!(flags & JBD2_FLAG_SAME_UUID)) offset += 16;
        data_block_cursor++;

        if (flags & JBD2_FLAG_LAST_TAG) break;
    }
    free(buf);
}

typedef struct {
    deleted_candidate_t *targets;
    int num_targets;
} scan_ctx_t;

void on_tag_found(uint64_t data_block, uint64_t real_block, uint32_t seq, void *ctx_v) {
    scan_ctx_t *ctx = (scan_ctx_t *)ctx_v;

    for (int i = 0; i < ctx->num_targets; i++) {
        if (ctx->targets[i].inode_table_block != real_block) continue;

        uint8_t *buf = malloc(block_size);
        fseek(f, (long)data_block * block_size, SEEK_SET);
        fread(buf, block_size, 1, f);

        ext4_inode_t inode_copy;
        memcpy(&inode_copy, buf + ctx->targets[i].offset_in_block, sizeof(ext4_inode_t));
        ext4_inode_t *inode = &inode_copy;

        int looks_pre_truncate = (inode->i_dtime == 0 && inode->i_links_count > 0 && inode->i_size_lo > 0);

        printf("Inode %u | journal seq %u | data block %lu (real block %lu): ",
               ctx->targets[i].inode_num, seq, data_block, real_block);

        if (looks_pre_truncate) {
            printf("PRE-TRUNCATE COPY FOUND\n");
            printf("  size=%u links=%u dtime=%u flags=0x%x\n",
                   inode->i_size_lo, inode->i_links_count, inode->i_dtime, inode->i_flags);

            if (inode->i_flags & 0x80000) {
                uint16_t eh[4];
                memcpy(eh, inode->i_block, sizeof(eh));
                uint16_t eh_entries = eh[1];
                if (eh_entries > 0) {
                    uint8_t extent_raw[12];
                    memcpy(extent_raw, (uint8_t *)inode->i_block + 12, 12);
                    for (int e = 0; e < eh_entries; e++) {
                        uint32_t ee_block;
                        uint16_t ee_len, ee_start_hi;
                        uint32_t ee_start_lo;
                        memcpy(&ee_block, extent_raw, 4);
                        memcpy(&ee_len, extent_raw + 4, 2);
                        memcpy(&ee_start_hi, extent_raw + 6, 2);
                        memcpy(&ee_start_lo, extent_raw + 8, 4);
                        uint64_t physical_block = ee_start_lo | ((uint64_t)ee_start_hi << 32);

                        printf("  Extent %d: logical=%u len=%u -> physical_block=%lu\n",
                               e, ee_block, ee_len, physical_block);

                        char outname[128];
                        snprintf(outname, sizeof(outname), "recovered_journal/inode_%u_extent_%d.bin",
                                 ctx->targets[i].inode_num, e);
                        uint8_t *content = malloc((size_t)ee_len * block_size);
                        fseek(f, (long)physical_block * block_size, SEEK_SET);
                        fread(content, block_size, ee_len, f);
                        FILE *out = fopen(outname, "wb");
                        uint32_t write_size = inode->i_size_lo < (uint32_t)(ee_len * block_size) ?
                                               inode->i_size_lo : (uint32_t)(ee_len * block_size);
                        fwrite(content, 1, write_size, out);
                        fclose(out);
                        free(content);
                        printf("  -> recovered content written to %s\n", outname);

                        extent_raw[0] = extent_raw[0]; /* no-op, keep structure simple */
                    }
                }
            }
        } else {
            printf("already truncated at this point (size=%u links=%u dtime=%u)\n",
                   inode->i_size_lo, inode->i_links_count, inode->i_dtime);
        }
        free(buf);
    }
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <ext4_image>\n", argv[0]);
        return 1;
    }

    f = fopen(argv[1], "rb");
    if (!f) { perror("fopen"); return 1; }

    fseek(f, 1024, SEEK_SET);
    fread(&sb, sizeof(sb), 1, f);
    if (sb.s_magic != EXT4_SUPER_MAGIC) {
        fprintf(stderr, "Not ext4\n");
        return 1;
    }

    block_size = 1024 << sb.s_log_block_size;
    inode_size = sb.s_inode_size;
    inodes_per_group = sb.s_inodes_per_group;
    is_64bit = (sb.s_feature_incompat & 0x80) != 0;
    size_t desc_size = is_64bit ? sizeof(ext4_group_desc_t) : 32;
    num_groups = (sb.s_blocks_count_lo + sb.s_blocks_per_group - 1) / sb.s_blocks_per_group;

    long gdt_offset = (long)(sb.s_first_data_block + 1) * block_size;
    groups = calloc(num_groups, sizeof(ext4_group_desc_t));
    fseek(f, gdt_offset, SEEK_SET);
    for (uint32_t i = 0; i < num_groups; i++) fread(&groups[i], desc_size, 1, f);

    printf("=== Step 1: Finding deleted inode candidates (Phase 3 logic) ===\n");
    deleted_candidate_t *candidates = malloc(sizeof(deleted_candidate_t) * MAX_DELETED);
    int num_candidates = find_deleted_inodes(candidates);
    for (int i = 0; i < num_candidates; i++) {
        printf("  Deleted inode %u lives in real block %lu, offset %u\n",
               candidates[i].inode_num, candidates[i].inode_table_block, candidates[i].offset_in_block);
    }
    if (num_candidates == 0) {
        printf("  No deleted inode candidates found on the live filesystem.\n");
        fclose(f);
        return 0;
    }

    printf("\n=== Step 2: Locating journal (inode 8) ===\n");
    uint64_t journal_start, journal_blocks;
    if (!find_journal_extent(&journal_start, &journal_blocks)) {
        fprintf(stderr, "Could not locate journal extent\n");
        fclose(f);
        return 1;
    }
    printf("  Journal at blocks %lu - %lu\n\n", journal_start, journal_start + journal_blocks - 1);

    printf("=== Step 3: Scanning all journal transactions for recoverable copies ===\n");
    system("mkdir -p recovered_journal");

    scan_ctx_t ctx = { candidates, num_candidates };
    uint8_t *buf = malloc(block_size);

    for (uint64_t i = 1; i < journal_blocks; i++) {
        fseek(f, (long)(journal_start + i) * block_size, SEEK_SET);
        fread(buf, block_size, 1, f);

        journal_header_t *h = (journal_header_t *)buf;
        if (be32(h->h_magic) != JBD2_MAGIC_NUMBER) continue;
        if (be32(h->h_blocktype) != JBD2_DESCRIPTOR_BLOCK) continue;

        uint32_t seq = be32(h->h_sequence);
        parse_descriptor_tags(journal_start + i, seq, on_tag_found, &ctx);
    }

    printf("\nDone. Check recovered_journal/ for any extracted content.\n");

    free(buf);
    free(candidates);
    free(groups);
    fclose(f);
    return 0;
}
