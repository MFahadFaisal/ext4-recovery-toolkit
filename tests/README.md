# Test Images

## test.img — negative case (no sync before delete)

\`\`\`bash
dd if=/dev/zero of=test.img bs=1M count=64
mkfs.ext4 -F test.img
mkdir -p /tmp/mnt
sudo mount -o loop test.img /tmp/mnt
sudo bash -c 'echo "hello forensics world" > /tmp/mnt/keep.txt'
sudo bash -c 'echo "this file will be deleted" > /tmp/mnt/deleted1.txt'
sudo bash -c 'for i in $(seq 1 500); do echo "line $i padding data xxxxxxxx"; done > /tmp/mnt/deleted2.txt'
sudo rm /tmp/mnt/deleted1.txt /tmp/mnt/deleted2.txt
sudo umount /tmp/mnt
\`\`\`

## test2.img — positive case (synced before delete)

\`\`\`bash
dd if=/dev/zero of=test2.img bs=1M count=64
mkfs.ext4 -F test2.img
mkdir -p /tmp/mnt2
sudo mount -o loop test2.img /tmp/mnt2
sudo bash -c 'for i in $(seq 1 500); do echo "line $i padding data xxxxxxxx"; done > /tmp/mnt2/deleted_synced.txt'
sudo sync
sudo rm /tmp/mnt2/deleted_synced.txt
sudo sync
sudo umount /tmp/mnt2
\`\`\`


## test3.img — journal recovery positive case (synced commit boundary before delete)

\`\`\`bash
dd if=/dev/zero of=test3.img bs=1M count=64
mkfs.ext4 -F test3.img
mkdir -p /tmp/mnt3
sudo mount -o loop test3.img /tmp/mnt3
sudo bash -c 'echo "journal recovery target file" > /tmp/mnt3/journal_target.txt'
sudo sync
sleep 1
sudo rm /tmp/mnt3/journal_target.txt
sudo sync
sudo umount /tmp/mnt3
\`\`\`
