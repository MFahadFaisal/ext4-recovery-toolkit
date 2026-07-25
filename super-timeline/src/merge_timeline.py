#!/usr/bin/env python3
import csv
import sys
from datetime import datetime, timezone

def epoch_to_l2t(epoch):
    if not epoch or int(epoch) == 0:
        return None, None
    dt = datetime.fromtimestamp(int(epoch), tz=timezone.utc)
    return dt.strftime("%m/%d/%Y"), dt.strftime("%H:%M:%S")

def load_inode_events(inode_csv):
    rows = []
    with open(inode_csv, newline='') as f:
        reader = csv.DictReader(f)
        for row in reader:
            inode = row['inode']
            fields = [
                ('atime', 'A...', 'Last Access Time'),
                ('mtime', '.M..', 'Content Modification Time'),
                ('ctime', '..C.', 'Inode/Metadata Change Time'),
                ('crtime', '...B', 'Creation (Birth) Time'),
                ('dtime', '..D.', 'Deletion Time'),
            ]
            for key, macb, desc in fields:
                date, time_str = epoch_to_l2t(row[key])
                if date is None:
                    continue
                rows.append({
                    'date': date, 'time': time_str, 'timezone': 'UTC',
                    'macb': macb, 'source': 'FILE', 'sourcetype': 'ext4 inode',
                    'type': desc,
                    'description': f"inode {inode} (size={row['size']}, links={row['links']}, mode={row['mode']})",
                    'inode': inode,
                })
    return rows

def load_journal_events(journal_csv):
    rows = []
    with open(journal_csv, newline='') as f:
        reader = csv.DictReader(f)
        for row in reader:
            date, time_str = epoch_to_l2t(row['commit_sec'])
            if date is None:
                continue
            rows.append({
                'date': date, 'time': time_str, 'timezone': 'UTC',
                'macb': '....', 'source': 'JOURNAL', 'sourcetype': 'jbd2 commit',
                'type': 'Transaction Commit',
                'description': f"journal transaction seq={row['seq']} committed (block {row['journal_block']})",
                'inode': '',
            })
    return rows

def main():
    if len(sys.argv) != 4:
        print(f"Usage: {sys.argv[0]} <inode_csv> <journal_csv> <output_super_timeline.csv>")
        sys.exit(1)

    events = []
    events += load_inode_events(sys.argv[1])
    events += load_journal_events(sys.argv[2])

    events.sort(key=lambda r: (r['date'], r['time']))

    with open(sys.argv[3], 'w', newline='') as f:
        writer = csv.DictWriter(f, fieldnames=[
            'date', 'time', 'timezone', 'macb', 'source',
            'sourcetype', 'type', 'description', 'inode'
        ])
        writer.writeheader()
        writer.writerows(events)

    print(f"Merged {len(events)} events from {len(sys.argv[1:3])} sources into {sys.argv[3]}")

if __name__ == '__main__':
    main()
