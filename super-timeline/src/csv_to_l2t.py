#!/usr/bin/env python3
import csv
import sys
from datetime import datetime, timezone

def epoch_to_l2t(epoch):
    if epoch == 0:
        return None, None
    dt = datetime.fromtimestamp(int(epoch), tz=timezone.utc)
    return dt.strftime("%m/%d/%Y"), dt.strftime("%H:%M:%S")

def main():
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} <inode_csv> <output_l2t_csv>")
        sys.exit(1)

    rows = []
    with open(sys.argv[1], newline='') as f:
        reader = csv.DictReader(f)
        for row in reader:
            inode = row['inode']
            fields = [
                ('atime', 'A...', 'Last Access Time'),
                ('mtime', '.M..', 'Content Modification Time'),
                ('ctime', '..C.', 'Inode/Metadata Change Time'),
                ('crtime', '...B', 'Creation (Birth) Time'),
            ]
            for key, macb, desc in fields:
                date, time_str = epoch_to_l2t(row[key])
                if date is None:
                    continue
                rows.append({
                    'date': date,
                    'time': time_str,
                    'timezone': 'UTC',
                    'macb': macb,
                    'source': 'FILE',
                    'sourcetype': 'ext4 inode',
                    'type': desc,
                    'description': f"inode {inode} (size={row['size']}, links={row['links']}, mode={row['mode']})",
                    'inode': inode,
                })

    rows.sort(key=lambda r: (r['date'], r['time']))

    with open(sys.argv[2], 'w', newline='') as f:
        writer = csv.DictWriter(f, fieldnames=[
            'date', 'time', 'timezone', 'macb', 'source',
            'sourcetype', 'type', 'description', 'inode'
        ])
        writer.writeheader()
        writer.writerows(rows)

    print(f"Wrote {len(rows)} timeline events to {sys.argv[2]}")

if __name__ == '__main__':
    main()
