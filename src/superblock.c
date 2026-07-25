#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../common/ext4_structs.h"

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <ext4_image>\n", argv[0]);
        return 1;
    }

    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror("fopen"); return 1; }

    ext4_superblock_t sb;
    fseek(f, 1024, SEEK_SET);          // superblock always at byte offset 1024
    if (fread(&sb, sizeof(sb), 1, f) != 1) {
        fprintf(stderr, "Failed to read superblock\n");
        return 1;
    }

    if (sb.s_magic != EXT4_SUPER_MAGIC) {
        printf("Not an ext4 filesystem (magic=0x%04x)\n", sb.s_magic);
        return 1;
    }

    uint32_t block_size = 1024 << sb.s_log_block_size;

    printf("=== ext4 Superblock ===\n");
    printf("Magic:              0x%04x (valid)\n", sb.s_magic);
    printf("Block size:         %u bytes\n", block_size);
    printf("Inodes count:       %u\n", sb.s_inodes_count);
    printf("Free inodes:        %u\n", sb.s_free_inodes_count);
    printf("Blocks count:       %u\n", sb.s_blocks_count_lo);
    printf("Free blocks:        %u\n", sb.s_free_blocks_count_lo);
    printf("Inodes per group:   %u\n", sb.s_inodes_per_group);
    printf("Blocks per group:   %u\n", sb.s_blocks_per_group);
    printf("First data block:   %u\n", sb.s_first_data_block);
    printf("Inode size:         %u bytes\n", sb.s_inode_size);
    printf("First non-res ino:  %u\n", sb.s_first_ino);
    printf("Volume name:        %.16s\n", sb.s_volume_name);

    char uuid_str[37];
    snprintf(uuid_str, sizeof(uuid_str),
        "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        sb.s_uuid[0],sb.s_uuid[1],sb.s_uuid[2],sb.s_uuid[3],
        sb.s_uuid[4],sb.s_uuid[5],sb.s_uuid[6],sb.s_uuid[7],
        sb.s_uuid[8],sb.s_uuid[9],sb.s_uuid[10],sb.s_uuid[11],
        sb.s_uuid[12],sb.s_uuid[13],sb.s_uuid[14],sb.s_uuid[15]);
    printf("UUID:               %s\n", uuid_str);

    fclose(f);
    return 0;
}
