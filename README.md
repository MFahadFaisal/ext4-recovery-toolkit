# ext4 Deleted File Recovery Tool (from scratch, no Sleuth Kit)

A from-scratch C implementation of ext4 filesystem parsing and deleted-file
recovery, built to understand the on-disk format at the byte level rather
than relying on existing forensics libraries. Every phase is independently
validated against Sleuth Kit (`fls`, `ils`, `debugfs`, `blkls`, `dumpe2fs`)
to confirm correctness.

## Why build this instead of just using Sleuth Kit?

The goal wasn't to reinvent Sleuth Kit — it was to understand *why* deleted
file recovery on modern ext4 is hard, by hitting the same walls real tools
hit, and explaining the filesystem-level reasons for each one. Several of
those walls turned into the most interesting findings of this project (see
below).

## Architecture

src/
├── ext4_structs.h # superblock struct
├── ext4_inode.h # inode struct
├── ext4_dirent.h # directory entry struct
├── superblock.c # Phase 1: superblock parser
├── group_desc.c # Phase 2: block group descriptor table parser
├── inode_scan.c # Phase 3: inode table walker + deletion detection
├── dir_scan.c # Phase 4a: directory entry / slack space parser
└── carve_blocks.c # Phase 4b: unallocated block content carver

tests/
├── test.img # negative case: quick create+delete, no sync
└── test2.img # positive case: create, sync, then delete


## Phase 1 — Superblock Parser

Parses the ext4 superblock at fixed byte offset 1024. Validated field-by-field
against `dumpe2fs -h` — magic number, block size, inode/block counts, free
counts, inodes-per-group, blocks-per-group, UUID all matched exactly.

## Phase 2 — Block Group Descriptor Table

Parses the group descriptor array (supports both 32-byte classic and 64-byte
`64bit`-feature formats) to locate each group's block bitmap, inode bitmap,
and inode table. Validated against `dumpe2fs` per-group output — inode table
block numbers matched exactly across all 8 groups on the test image.

## Phase 3 — Inode Table Walker & Deletion Detection

Walks every inode across every group. Flags an inode as a deleted-file
candidate when `dtime != 0` **and** its bit in the inode bitmap is free
(matching the same heuristic Sleuth Kit's `ils -O` uses).

**Finding: ext4 truncates inodes on unlink.** When a file's link count hits
zero, `ext4_evict_inode()` zeros `i_size` and empties the extent tree
*before* setting `dtime`. This means the inode itself no longer points to
any data blocks — confirmed independently via this tool, `debugfs -R "stat <N>"`,
and Sleuth Kit's own `ils -O` (which also returned zero usable rows for the
deleted inodes). This is a well-known but rarely explained reason why ext4
recovery is fundamentally harder than ext3/FAT — recovery has to come from
somewhere *other* than the inode: directory entries, unallocated block
carving, or the journal.

## Phase 4a — Directory Entry Parser (Slack Space)

Parses directory data blocks as a sequence of `rec_len`-strided dentries,
looking for entries whose `rec_len` is larger than their tight-packed size —
normally a sign that a deleted entry's bytes are still sitting in the slack
space after a preceding live entry.

**Finding: this technique fails on `metadata_csum` ext4.** Hex-dumping the
identified slack region showed it was entirely zeroed, not stale bytes.
Because directory blocks carry a tail checksum under the `metadata_csum`
feature, the ext4 driver must rewrite the whole block cleanly on any
modification rather than leaving old bytes in place (which would violate the
checksum). Sleuth Kit's `fls -d` hit the identical wall — it fell back to
reporting `$OrphanFiles/OrphanFile-N` (a different recovery mechanism, ext4's
orphan-file feature) rather than recovering real filenames from the
directory block. Documented as a validated negative result rather than a bug.

## Phase 4b — Unallocated Block Carver

Iterates every block marked free in each group's block bitmap, skips
all-zero blocks, and flags blocks that are >90% printable ASCII as carved
content.

**Finding 1 (negative case, `test.img`):** carving found zero non-zero free
blocks. Root cause: the test file was created and deleted in the same shell
session with no `sync`, so its data likely never left page cache / never hit
disk before being discarded — there was nothing to carve. This is a real,
documentable anti-forensic factor (fast create+delete without an fsync can
leave literally no on-disk trace of file content).

**Finding 2 (positive case, `test2.img`):** after forcing `sync` before
deletion, the carver recovered 15 of 16 non-zero free blocks as text,
reconstructing the original 500-line file **byte-for-byte** for lines 1–499
(verified via `diff` against the original content). The 16th block (final
block of the file, `line 500` + zero padding) was missed because it falls
under the 90% printable-ASCII threshold — a known limitation of naive
carving heuristics with no file-boundary information. Cross-validated
against Sleuth Kit's `blkls` unallocated-space dump, which contained
identical content.

**Takeaway:** naive signature/text-based carving works, but its accuracy is
bounded by (a) whether data was ever flushed to disk, and (b) whether you
know the true byte length of the file (partial final blocks are hard to
detect on printable-ratio heuristics alone).

## Validation Summary

| Phase | Technique | Cross-checked against | Result |
|---|---|---|---|
| 1 | Superblock parse | `dumpe2fs -h` | Exact match |
| 2 | Group descriptor parse | `dumpe2fs` | Exact match |
| 3 | Deleted inode detection | `ils -O`, `debugfs stat` | Exact match (both tools hit the same truncation wall) |
| 4a | Directory slack scan | `fls -d` | Both fail identically on `metadata_csum` fs |
| 4b | Block carving | `blkls` + `strings` | Byte-identical content recovered |

## Building

```bash
gcc -o superblock src/superblock.c
gcc -o group_desc src/group_desc.c
gcc -o inode_scan src/inode_scan.c
gcc -o dir_scan src/dir_scan.c
gcc -o carve_blocks src/carve_blocks.c
```

## Test Images

Both test images and their exact creation steps are documented in
`tests/README.md` for full reproducibility.

## Next Steps (in progress)

- **Phase 5:** jbd2 journal parsing — recovering pre-truncate inode copies
  (with intact extent trees) from committed-but-not-yet-checkpointed
  journal transactions, the "proper" recovery path for cases like `test.img`
  where content never touched non-journal disk space.
- **NTFS support** — same methodology applied to MFT parsing.
- **Super-timeline tool** — merging inode/MFT timestamps, journal entries,
  and system logs into a unified timeline (mini-Plaso).



