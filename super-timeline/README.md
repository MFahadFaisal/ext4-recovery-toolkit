# Super-Timeline Tool (mini-Plaso, from scratch)

A from-scratch C + Python tool that extracts timestamps from two
independent on-disk ext4 sources (inode metadata and jbd2 journal commits)
and merges them into a single chronologically-sorted super-timeline, in
an l2tcsv-style format compatible with the conventions used by Plaso /
log2timeline and Sleuth Kit's `mactime`.

## Why two sources instead of one?

Inode timestamps (`atime`/`mtime`/`ctime`/`crtime`) are the traditional
MACB data every timeline tool starts from, but they're also the easiest
thing for an attacker to falsify directly (`touch -d`, `debugfs`, or
purpose-built timestomping tools all just call `utimes()`/edit the
inode). Journal transaction commit timestamps are a second, largely
independent source: they record when jbd2 actually wrote a transaction
to disk, and forging them convincingly requires understanding and
rewriting jbd2 internals rather than a single syscall. Cross-referencing
both sources in one timeline lets an analyst spot inconsistencies (e.g.
an inode's `mtime` claiming a time that no journal commit corroborates)
that a single-source timeline would miss entirely.

## Architecture

src/
├── ext4_timestamps.c # Extracts atime/mtime/ctime/crtime/dtime per inode
├── journal_timeline.c # Extracts commit timestamps from jbd2 commit blocks
├── csv_to_l2t.py # Converts raw inode CSV into l2tcsv-style rows
└── merge_timeline.py # Merges inode + journal event rows, sorts chronologically


## Phase T1 — Inode Timestamp Extraction

Walks every inode across every block group, extracting all five ext4
timestamp fields (`atime`, `mtime`, `ctime`, `crtime`, and `dtime` —
deletion time, which most timeline tools don't surface as its own event
type, but which is directly useful forensic signal). Validated against
Sleuth Kit's `fls -r -m` (body-file format) — epoch values matched
exactly for both live and deleted (orphaned) inodes.

## Phase T2 — Journal Commit Timestamp Extraction

Parses jbd2 commit blocks (`h_blocktype == 2`) directly from the raw
journal, extracting the `commit_header`'s big-endian `h_commit_sec` /
`h_commit_nsec` fields.

**Bug found and fixed during development:** the real on-disk
`commit_header` struct has an 8-entry `uint32_t` checksum array
(`JBD2_CHECKSUM_BYTES = 8`, 32 bytes), not 4 as initially assumed. The
16-byte discrepancy silently shifted every read 16 bytes short of the
real timestamp field, producing `commit_sec=0` for every commit. Found
by hex-dumping a raw commit block and locating a plausible Unix-epoch
byte pattern (`6a61d99c` = 1784797596 = a sane July 2026 timestamp)
manually, then recomputing the correct struct offset from it — a good
example of why validating against raw bytes, not just "the code
compiles and doesn't crash," is essential when reverse-engineering an
on-disk format from documentation/memory rather than official headers.

## Phase T3 — Merge & Correlate

Merges both event streams into one super-timeline, sorted by
date/time, in l2tcsv-style columns (`date, time, timezone, macb,
source, sourcetype, type, description, inode`).

### Validated result (test3.img: create file → sync → sleep 1s → delete → sync)

The merged timeline correctly reconstructs the full sequence with
independent corroboration at each step:

| Time (UTC) | Event | Source |
|---|---|---|
| 09:06:35 | Filesystem created (root, lost+found, journal inodes born) | inode |
| 09:06:36 | File created (`atime`/`crtime` set) | inode |
| 09:06:36 | Journal transaction seq=2 committed | journal |
| 09:06:37 | File `mtime`/`ctime`/`dtime` all update (delete/truncate) | inode |
| 09:06:37 | Parent directory (inode 2) `mtime`/`ctime` update (dentry removed) | inode |
| 09:06:37 | Journal transaction seq=3 committed | journal |

The 1-second gap between the two journal commits (09:06:36 → 09:06:37)
matches the `sleep 1` inserted between file creation and deletion in the
test script exactly — an independently verifiable ground-truth check.

## Validation Summary

| Phase | Technique | Cross-checked against | Result |
|---|---|---|---|
| T1 | Inode timestamp extraction | `fls -r -m` (TSK body-file format) | Exact epoch match |
| T2 | Journal commit timestamp extraction | Manual hex inspection + known test timing (`sleep 1`) | Exact match, 1-second gap confirmed |
| T3 | Chronological merge | Manual narrative reconstruction of test3.img's known creation script | Correctly ordered, cross-source corroborated |

## Building

```bash
gcc -o ext4_timestamps src/ext4_timestamps.c
gcc -o journal_timeline src/journal_timeline.c
```

## Usage

```bash
./ext4_timestamps <image.img> inode_times.csv
./journal_timeline <image.img> journal_times.csv
python3 src/merge_timeline.py inode_times.csv journal_times.csv super_timeline.csv
```

## Next Steps

- Log normalization (syslog/auth.log) as a third event source
- Extend inode extraction to include journal-recovered pre-truncate
  inodes from the ext4-recovery-toolkit project, so deleted files whose
  content was recovered also appear in the timeline with their true
  original timestamps
- NTFS: $STANDARD_INFORMATION / $FILE_NAME MFT attribute timestamps,
  $LogFile / $UsnJrnl as the journal-equivalent source




