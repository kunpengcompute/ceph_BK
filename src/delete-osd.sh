#!/bin/sh

host_name=$1
x=$2
y=$3

for ((i=$x;i<=$y;i++))
do
ssh -T root@${host_name} << EOF
systemctl stop ceph-osd@$i
systemctl disable ceph-osd@$i
ceph osd down osd.$i
ceph osd out osd.$i
ceph osd crush remove osd.$i
ceph osd rm osd.$i
ceph auth del osd.$i
rm -rf /var/lib/ceph/osd/ceph-$i
exit
EOF
done
