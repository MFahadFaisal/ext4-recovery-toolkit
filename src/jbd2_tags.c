#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../common/ext4_structs.h"
#include "../common/jbd2_structs.h"

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <ext4_image> <descriptor_abs_block_num>\n", argv[0]);
        return 1;
    }

    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror("fopen"); return 1; }
    uint64_t desc_block = strtoull(argv[2], NULL, 10);

    ext4_superblock_t sb;
    fseek(f, 1024, SEEK_SET);
    fread(&sb, sizeof(sb), 1, f);
    uint32_t block_size = 1024 << sb.s_log_block_size;

    uint8_t *buf = malloc(block_size);
    fseek(f, (long)desc_block * block_size, SEEK_SET);
    fread(buf, block_size, 1, f);

    journal_header_t *h = (journal_header_t *)buf;
    if (be32(h->h_magic) != JBD2_MAGIC_NUMBER || be32(h->h_blocktype) != JBD2_DESCRIPTOR_BLOCK) {
        printf("[!] Block %lu is not a descriptor block\n", desc_block);
        return 1;
    }

    printf("=== Descriptor block %lu (seq %u) tags ===\n\n", desc_block, be32(h->h_sequence));

    uint32_t offset = sizeof(journal_header_t);
    int tag_num = 0;
    uint64_t data_block_cursor = desc_block + 1; // data blocks follow immediately after descriptor

    while (offset + sizeof(journal_block_tag3_t) <= block_size) {
        journal_block_tag3_t *tag = (journal_block_tag3_t *)(buf + offset);

        uint32_t flags = be32(tag->t_flags);
        uint32_t blocknr_lo = be32(tag->t_blocknr);
        uint32_t blocknr_hi = be32(tag->t_blocknr_high);
        uint64_t real_block = blocknr_lo | ((uint64_t)blocknr_hi << 32);

        printf("Tag %d: journal data block (abs %lu) is a backup of REAL filesystem block %lu\n",
               tag_num, data_block_cursor, real_block);
        printf("        flags=0x%x  escaped=%s  same_uuid=%s  last_tag=%s\n",
               flags,
               (flags & JBD2_FLAG_ESCAPE) ? "yes" : "no",
               (flags & JBD2_FLAG_SAME_UUID) ? "yes" : "no",
               (flags & JBD2_FLAG_LAST_TAG) ? "yes" : "no");

        offset += sizeof(journal_block_tag3_t);
        if (!(flags & JBD2_FLAG_SAME_UUID)) {
            offset += 16; // skip embedded UUID if present
        }

        data_block_cursor++;
        tag_num++;

        if (flags & JBD2_FLAG_LAST_TAG) break;
    }

    printf("\nTotal tags: %d\n", tag_num);

    free(buf);
    fclose(f);
    return 0;
}
