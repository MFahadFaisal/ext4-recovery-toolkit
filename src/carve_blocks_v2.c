#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "../common/ext4_structs.h"
#include "../common/ext4_groupdesc.h"
#include "file_signatures.h"

#define MAX_RUN_BLOCKS 4096

int is_block_free(FILE *f, uint64_t bitmap_block, uint32_t block_size, uint32_t index) {
    long byte_offset = bitmap_block * block_size + (index / 8);
    uint8_t byte;
    fseek(f, byte_offset, SEEK_SET);
    fread(&byte, 1, 1, f);
    return ((byte >> (index % 8)) & 1) == 0;
}

int looks_like_text(uint8_t *buf, uint32_t len) {
    // Find where trailing zero-padding begins (if any), and judge the
    // printable ratio only over the actual content before that padding.
    // This avoids penalizing small files that don't fill a whole block.
    uint32_t content_end = len;
    while (content_end > 0 && buf[content_end - 1] == 0) content_end--;

    if (content_end == 0) return 0; // fully empty, nothing to judge

    int printable = 0;
    for (uint32_t i = 0; i < content_end; i++) {
        if (isprint(buf[i]) || buf[i] == '\n' || buf[i] == '\r' || buf[i] == '\t') printable++;
    }
    return printable > (int)(content_end * 0.9);
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
    for (uint32_t i = 0; i < num_groups; i++) fread(&groups[i], desc_size, 1, f);

    system("mkdir -p recovered_v2");

    uint32_t total_blocks = sb.s_blocks_count_lo;
    uint8_t *free_map = calloc(total_blocks, 1);

    for (uint32_t g = 0; g < num_groups; g++) {
        uint64_t bitmap_block = groups[g].bg_block_bitmap_lo |
                                 ((uint64_t)groups[g].bg_block_bitmap_hi << 32);
        for (uint32_t idx = 0; idx < blocks_per_group; idx++) {
            uint32_t block_num = sb.s_first_data_block + g * blocks_per_group + idx;
            if (block_num >= total_blocks) break;
            free_map[block_num] = is_block_free(f, bitmap_block, block_size, idx);
        }
    }

    int carved_count = 0;
    uint8_t *scratch = malloc((size_t)MAX_RUN_BLOCKS * block_size);

    uint32_t b = 0;
    while (b < total_blocks) {
        if (!free_map[b]) { b++; continue; }

        uint32_t run_start = b;
        uint32_t run_len = 0;
        while (b < total_blocks && free_map[b] && run_len < MAX_RUN_BLOCKS) {
            run_len++;
            b++;
        }

        uint32_t offset_in_run = 0;
        while (offset_in_run < run_len) {
            uint32_t block_num = run_start + offset_in_run;

            uint8_t block_buf[65536];
            fseek(f, (long)block_num * block_size, SEEK_SET);
            fread(block_buf, block_size, 1, f);

            int all_zero = 1;
            for (uint32_t i = 0; i < block_size; i++) if (block_buf[i] != 0) { all_zero = 0; break; }
            if (all_zero) { offset_in_run++; continue; }

            int matched_sig = -1;
            for (size_t s = 0; s < NUM_SIGNATURES; s++) {
                if (match_signature(block_buf, block_size, &SIGNATURES[s])) {
                    matched_sig = (int)s;
                    break;
                }
            }

            if (matched_sig >= 0) {
                const file_signature_t *sig = &SIGNATURES[matched_sig];

                uint32_t remaining = run_len - offset_in_run;
                fseek(f, (long)block_num * block_size, SEEK_SET);
                size_t read_len = fread(scratch, block_size, remaining, f) * block_size;

                long end_offset = -1;
                if (sig->footer) {
                    end_offset = find_footer(scratch, read_len, sig->footer, sig->footer_len);
                }
                size_t write_len = (end_offset > 0) ? (size_t)end_offset : read_len;
                uint32_t blocks_consumed = (uint32_t)((write_len + block_size - 1) / block_size);

                char outname[160];
                snprintf(outname, sizeof(outname), "recovered_v2/carved_%s_block_%u.%s",
                          sig->name, block_num, sig->ext);
                FILE *out = fopen(outname, "wb");
                fwrite(scratch, 1, write_len, out);
                fclose(out);

                printf("[CARVED: %s] block %u, wrote %zu bytes -> %s%s\n",
                       sig->name, block_num, write_len, outname,
                       (end_offset > 0) ? " (footer found, trimmed)" : " (no footer, wrote to end of run)");
                carved_count++;

                offset_in_run += (blocks_consumed > 0) ? blocks_consumed : 1;
                continue;
            }

            if (!looks_like_text(block_buf, block_size)) {
                offset_in_run++;
                continue;
            }

            uint32_t text_run_len = 1;
            while (offset_in_run + text_run_len < run_len) {
                uint8_t next_buf[65536];
                fseek(f, (long)(block_num + text_run_len) * block_size, SEEK_SET);
                fread(next_buf, block_size, 1, f);

                int next_all_zero = 1;
                for (uint32_t i = 0; i < block_size; i++) if (next_buf[i] != 0) { next_all_zero = 0; break; }
                if (next_all_zero) break;

                if (!looks_like_text(next_buf, block_size)) break;
                text_run_len++;
            }

            fseek(f, (long)block_num * block_size, SEEK_SET);
            size_t read_len = fread(scratch, block_size, text_run_len, f) * block_size;

            // Trim trailing null-byte padding from the final block before writing,
            // so the output file matches the original content length exactly.
            size_t write_len = read_len;
            while (write_len > 0 && scratch[write_len - 1] == 0) write_len--;

            char outname[160];
            snprintf(outname, sizeof(outname), "recovered_v2/carved_text_block_%u.txt", block_num);
            FILE *out = fopen(outname, "wb");
            fwrite(scratch, 1, write_len, out);
            fclose(out);            printf("[CARVED: TEXT] block %u, length %u blocks -> %s\n",
                   block_num, text_run_len, outname);
            carved_count++;

            offset_in_run += text_run_len;
        }
    }

    printf("\nTotal files carved: %d\n", carved_count);

    free(scratch);
    free(free_map);
    free(groups);
    fclose(f);
    return 0;
}
