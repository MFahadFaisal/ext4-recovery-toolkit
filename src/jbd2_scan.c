#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../common/ext4_structs.h"
#include "../common/jbd2_structs.h"

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

    // Hard-coded from your debugfs output: journal at blocks 16385-20480
    // (In a later refinement we'll read this dynamically from inode 8's extents)
    uint64_t journal_start_block = 16385;
    uint64_t journal_num_blocks  = 4096;

    printf("=== Scanning journal (blocks %lu - %lu, block_size=%u) ===\n\n",
           journal_start_block, journal_start_block + journal_num_blocks - 1, block_size);

    uint8_t *buf = malloc(block_size);

    // First, read block 0 of the journal as the journal superblock
    fseek(f, (long)journal_start_block * block_size, SEEK_SET);
    fread(buf, block_size, 1, f);

    journal_superblock_t *jsb = (journal_superblock_t *)buf;
    uint32_t magic = be32(jsb->s_header.h_magic);
    uint32_t blocktype = be32(jsb->s_header.h_blocktype);

    if (magic != JBD2_MAGIC_NUMBER) {
        printf("[!] Journal superblock magic mismatch: got 0x%x, expected 0x%x\n",
               magic, JBD2_MAGIC_NUMBER);
    } else {
        printf("Journal superblock found. Block type: %u (expect 3 or 4)\n", blocktype);
        printf("  s_blocksize: %u\n", be32(jsb->s_blocksize));
        printf("  s_maxlen:    %u\n", be32(jsb->s_maxlen));
        printf("  s_first:     %u\n", be32(jsb->s_first));
        printf("  s_sequence:  %u (first expected commit ID)\n", be32(jsb->s_sequence));
        printf("  s_start:     %u (0 = journal currently empty/fully checkpointed)\n", be32(jsb->s_start));
    }
    printf("\n");

    // Now scan every block in the journal, classify by type
    int desc_count = 0, commit_count = 0, revoke_count = 0, other = 0;

    for (uint64_t i = 1; i < journal_num_blocks; i++) {
        fseek(f, (long)(journal_start_block + i) * block_size, SEEK_SET);
        fread(buf, block_size, 1, f);

        journal_header_t *h = (journal_header_t *)buf;
        uint32_t m = be32(h->h_magic);

        if (m != JBD2_MAGIC_NUMBER) continue; // not a jbd2 metadata block (could be journaled data)

        uint32_t bt = be32(h->h_blocktype);
        uint32_t seq = be32(h->h_sequence);

        const char *type_str = "unknown";
        switch (bt) {
            case JBD2_DESCRIPTOR_BLOCK: type_str = "DESCRIPTOR"; desc_count++; break;
            case JBD2_COMMIT_BLOCK:     type_str = "COMMIT";     commit_count++; break;
            case JBD2_REVOKE_BLOCK:     type_str = "REVOKE";     revoke_count++; break;
            default: other++; break;
        }

        printf("[journal block %lu / abs block %lu] type=%s seq=%u\n",
               i, journal_start_block + i, type_str, seq);
    }

    printf("\n=== Summary ===\n");
    printf("Descriptor blocks: %d\n", desc_count);
    printf("Commit blocks:     %d\n", commit_count);
    printf("Revoke blocks:     %d\n", revoke_count);
    printf("Other/unrecognized: %d\n", other);

    free(buf);
    fclose(f);
    return 0;
}
