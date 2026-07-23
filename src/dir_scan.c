#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ext4_structs.h"
#include "ext4_dirent.h"

const char* filetype_str(uint8_t t) {
    switch (t) {
        case 1: return "regular file";
        case 2: return "directory";
        case 7: return "symlink";
        default: return "unknown";
    }
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <ext4_image> <physical_block_num>\n", argv[0]);
        return 1;
    }

    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror("fopen"); return 1; }
    uint64_t dir_block = strtoull(argv[2], NULL, 10);

    ext4_superblock_t sb;
    fseek(f, 1024, SEEK_SET);
    fread(&sb, sizeof(sb), 1, f);
    if (sb.s_magic != EXT4_SUPER_MAGIC) {
        fprintf(stderr, "Not ext4\n");
        return 1;
    }
    uint32_t block_size = 1024 << sb.s_log_block_size;

    uint8_t *buf = malloc(block_size);
    fseek(f, (long)dir_block * block_size, SEEK_SET);
    fread(buf, block_size, 1, f);

    printf("=== Directory entries in block %lu (size %u) ===\n\n", dir_block, block_size);

    uint32_t offset = 0;
    int entry_num = 0;

    while (offset < block_size) {
        ext4_dirent_header_t *de = (ext4_dirent_header_t *)(buf + offset);

        if (de->rec_len == 0) {
            printf("[!] rec_len=0 at offset %u — corrupt or end of valid entries, stopping\n", offset);
            break;
        }

        char name[256] = {0};
        if (de->name_len > 0 && de->name_len < 256) {
            memcpy(name, buf + offset + sizeof(ext4_dirent_header_t), de->name_len);
        }

        // "expected" rec_len if this were a tightly packed entry (rounded to 4 bytes)
        uint16_t tight_len = ((sizeof(ext4_dirent_header_t) + de->name_len + 3) / 4) * 4;

        int looks_deleted = (de->rec_len > tight_len) && (de->inode != 0);
        // inode==0 entries are also dead slots but with no name info usually

        printf("Entry %d @offset %u:\n", entry_num, offset);
        printf("  inode=%u  rec_len=%u  name_len=%u  type=%s  name=\"%s\"\n",
               de->inode, de->rec_len, de->name_len, filetype_str(de->file_type), name);

        if (de->inode == 0) {
            printf("  --> [EMPTY SLOT] inode=0, no active entry, but slot exists\n");
        }
        if (looks_deleted) {
            printf("  --> [POSSIBLE GHOST ENTRY] rec_len (%u) > tight-packed size (%u): "
                   "slack space may hide a deleted entry after this one\n",
                   de->rec_len, tight_len);
        }
        printf("\n");

        offset += de->rec_len;
        entry_num++;
    }

    free(buf);
    fclose(f);
    return 0;
}
