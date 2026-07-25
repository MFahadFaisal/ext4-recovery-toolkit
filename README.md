# ext4 Forensics Toolkit

Two from-scratch C/Python tools for ext4 forensic analysis, built without
relying on Sleuth Kit or any existing forensics library, and independently
validated against Sleuth Kit at every phase.

- **`src/`** — deleted file recovery (superblock/group-desc/inode parsing,
  directory slack analysis, unallocated block carving, jbd2 journal replay)
- **`super-timeline/`** — super-timeline tool merging ext4 inode timestamps
  and jbd2 journal commit timestamps into one chronologically-sorted,
  cross-source-corroborated timeline (l2tcsv-style output)
- **`common/`** — shared on-disk struct definitions (superblock, inode,
  group descriptor, jbd2) used by both tools

See each subfolder's README for full methodology, findings, and
validation details.

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

## Phase 5 — jbd2 Journal Parsing (Deleted Inode Recovery)

Parses the ext4 journal (inode 8, jbd2 format) directly from raw blocks —
no `jbd2` kernel code or Sleuth Kit journal tools involved. All journal
metadata (headers, superblock, descriptor tags) is **big-endian**, unlike
the rest of ext4, which is a common gotcha when implementing this from
scratch.

### Approach

1. Locate the journal's physical blocks via inode 8's extent tree
   (`debugfs -R "stat <8>"` confirms; the tool currently hard-codes this
   and reads it dynamically as a planned refinement).
2. Read journal block 0 as the journal superblock (`jbd2_scan.c`) —
   confirms block size, max length, and current sequence number.
3. Scan every subsequent block's 12-byte header to classify it as a
   descriptor, commit, or revoke block.
4. Parse descriptor block tags (`jbd2_tags.c`) — each 16-byte
   `journal_checksum_v3`-format tag says "the next journal data block is a
   backup of real filesystem block N."
5. If a descriptor tag points at the real inode-table block containing a
   deleted file's inode, extract and parse the corresponding journal data
   block as an inode table (`jbd2_extract_inode.c`) — if this snapshot
   predates the delete transaction, the inode's extent tree may still be
   intact, even though the live on-disk inode has been zeroed.

### Findings

**Negative case (`test.img`):** create + delete happened within the same
short shell session, with no `sync` in between. Both transactions were
almost certainly coalesced into a single journal commit (only one
descriptor/commit pair, sequence 2, existed for the whole session).
Extracting the journal's copy of the inode table showed the **already
truncated** state — there was no historical "pre-delete" snapshot to
recover, because nothing forced a commit boundary before the delete
happened. Root cause is identical to the Phase 4b negative case: no forced
durability point between creation and deletion.

**Positive case (`test3.img`):** added an explicit `sync` after file
creation and *before* deletion, forcing two separate journal transactions
(sequence 2 = create, sequence 3 = delete). Extracting the inode from
sequence 2's journal data block recovered a **fully intact pre-truncate
inode**:

i_size_lo: 29
i_links_count: 1
i_dtime: 0
i_flags: 0x80000 (EXTENTS)
Extent 0: logical_block=0 len=1 -> physical_block=32769

Reading physical block 32769 directly recovered the original file content
byte-for-byte (`journal recovery target file`), despite the live inode on
disk being fully zeroed (`i_size=0`, `dtime` set, empty extent tree). This
is a genuine recovery of data that no longer exists anywhere in the live
filesystem structures — the only surviving copy was the stale-but-not-yet-
overwritten journal transaction.

### Cross-cutting takeaway across Phases 3-5

All three independent recovery techniques (inode inspection, directory
slack, journal replay) fail identically whenever an entire
create-then-delete sequence happens inside one write-behind/journal-commit
window with no forced sync boundary. Recoverability isn't primarily a
question of parser sophistication — it's a question of **filesystem
transaction timing**. This is a real and useful anti-forensic insight, not
just a limitation of this specific tool: an attacker or user who
creates-and-deletes quickly, without triggering an fsync, leaves
substantially less recoverable trace across every layer of ext4 at once.

### Validation Summary (updated)

| Phase | Technique | Cross-checked against | Result |
|---|---|---|---|
| 1 | Superblock parse | `dumpe2fs -h` | Exact match |
| 2 | Group descriptor parse | `dumpe2fs` | Exact match |
| 3 | Deleted inode detection | `ils -O`, `debugfs stat` | Exact match (both tools hit the same truncation wall) |
| 4a | Directory slack scan | `fls -d` | Both fail identically on `metadata_csum` fs |
| 4b | Block carving | `blkls` + `strings` | Byte-identical content recovered (when data was synced pre-delete) |
| 5 | Journal (jbd2) replay | `debugfs`, manual hex verification | Byte-identical pre-truncate inode + content recovered (when a commit boundary separated create/delete) |

## Phase 5.5 — Automated Recovery Pipeline (`journal_recover.c`)

Combines Phases 3 and 5 into a single tool requiring no manual block
lookups:

1. Scans the live filesystem for deleted inode candidates (Phase 3 logic).
2. Dynamically locates the journal via inode 8's extent tree (no
   hard-coded block numbers).
3. Walks every descriptor block across every transaction in the journal,
   cross-referencing tagged real-block numbers against the inode-table
   blocks of known deleted inodes.
4. For any match, extracts the journal's copy of that inode and checks
   whether it predates truncation (`i_dtime == 0`, `i_links_count > 0`,
   `i_size_lo > 0`). If so, walks its extent tree and writes recovered
   content directly to `recovered_journal/`.

Validated against both test images: correctly reports "already truncated"
for every transaction on `test.img` (matching the manual Phase 5 finding),
and on `test3.img` automatically distinguishes between transaction
sequence 2 (pre-truncate, recoverable — content extracted byte-for-byte)
and sequence 3 (post-truncate, correctly rejected), with zero manual
intervention.







