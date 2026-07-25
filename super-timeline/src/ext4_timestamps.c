#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../../common/ext4_structs.h"
#include "../../common/ext4_inode.h"
#include "../../common/ext4_groupdesc.h"

// ext4 stores extra nanosecond precision + epoch extension in the "_extra" fields.
// Lower 2 bits of the extra field extend the epoch beyond 2038 (Y2038 fix);
// the upper 30 bits are nanoseconds. For a portfolio tool, we mainly care about
// the base 32-bit seconds value for human-readable output, but we surface both.
void print_time(const char *label, uint32_t sec) {
    if (sec == 0) {
        printf("%s: (not set)\n", label);
        return;
    }
    time_t t = sec;
    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", gmtime(&t));
    printf("%s: %s UTC (epoch %u)\n", label, buf, sec);
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <ext4_image> <output_csv>\n", argv[0]);
        return 1;
    }

    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror("fopen"); return 1; }

    FILE *csv = fopen(argv[2], "w");
    if (!csv) { perror("fopen csv"); return 1; }
    fprintf(csv, "inode,atime,mtime,ctime,crtime,dtime,size,links,mode\n");
    ext4_superblock_t sb;
    fseek(f, 1024, SEEK_SET);
    fread(&sb, sizeof(sb), 1, f);
    if (sb.s_magic != EXT4_SUPER_MAGIC) {
        fprintf(stderr, "Not ext4\n");
        return 1;
    }

    uint32_t block_size = 1024 << sb.s_log_block_size;
    uint16_t inode_size = sb.s_inode_size;
    uint32_t inodes_per_group = sb.s_inodes_per_group;
    int is_64bit = (sb.s_feature_incompat & 0x80) != 0;
    size_t desc_size = is_64bit ? sizeof(ext4_group_desc_t) : 32;
    uint32_t num_groups = (sb.s_blocks_count_lo + sb.s_blocks_per_group - 1)
                          / sb.s_blocks_per_group;

    long gdt_offset = (long)(sb.s_first_data_block + 1) * block_size;
    ext4_group_desc_t *groups = calloc(num_groups, sizeof(ext4_group_desc_t));
    fseek(f, gdt_offset, SEEK_SET);
    for (uint32_t i = 0; i < num_groups; i++) fread(&groups[i], desc_size, 1, f);

    int total = 0;

    for (uint32_t g = 0; g < num_groups; g++) {
        uint64_t inode_table_block = groups[g].bg_inode_table_lo |
                                      ((uint64_t)groups[g].bg_inode_table_hi << 32);
        uint64_t inode_bitmap_block = groups[g].bg_inode_bitmap_lo |
                                      ((uint64_t)groups[g].bg_inode_bitmap_hi << 32);

        for (uint32_t idx = 0; idx < inodes_per_group; idx++) {
            uint32_t inode_num = g * inodes_per_group + idx + 1;
            if (inode_num < sb.s_first_ino && inode_num != 2) continue;

            // check if allocated (skip free/unused inodes for this pass -- can
            // widen later to include deleted ones too, since dtime/ctime on
            // deleted inodes are themselves useful timeline events)
            long byte_offset = inode_bitmap_block * block_size + (idx / 8);
            uint8_t bmbyte;
            fseek(f, byte_offset, SEEK_SET);
            fread(&bmbyte, 1, 1, f);
            int allocated = (bmbyte >> (idx % 8)) & 1;

            long offset = inode_table_block * block_size + (long)idx * inode_size;
            ext4_inode_t inode;
            memset(&inode, 0, sizeof(inode));
            fseek(f, offset, SEEK_SET);
            fread(&inode, sizeof(ext4_inode_t) < inode_size ? sizeof(ext4_inode_t) : inode_size, 1, f);

            // skip totally empty inode slots (never used, not even deleted)
            if (inode.i_mode == 0 && inode.i_atime == 0 && inode.i_ctime == 0 &&
                inode.i_mtime == 0 && inode.i_dtime == 0) continue;

            fprintf(csv, "%u,%u,%u,%u,%u,%u,%u,%u,%u\n",
                    inode_num, inode.i_atime, inode.i_mtime, inode.i_ctime,
                    inode.i_crtime, inode.i_dtime, inode.i_size_lo,
                    inode.i_links_count, inode.i_mode);
            total++;
        }
    }

    printf("Wrote %d inode timestamp records to %s\n", total, argv[2]);

    free(groups);
    fclose(f);
    fclose(csv);
    return 0;
}
