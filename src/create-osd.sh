#!/bin/sh

host_name=$1
x=$2
y=$3
for ((i=$x;i<=$y;i++))
do
ssh -T root@${host_name} << EOF

# 创建osd配置文件目录
mkdir -p /var/lib/ceph/osd/ceph-$i
chown root:ceph /var/lib/ceph/osd/ceph-$i

su - ceph -c "ceph-osd -i $i --mkfs --mkkey --no-mon-config --cluster ceph --setuser ceph --setgroup ceph"
ceph -i /var/lib/ceph/osd/ceph-$i/keyring auth add osd.$i osd 'allow *' mon 'allow profile osd' mgr 'allow profile osd'

systemctl start ceph-osd@$i
systemctl enable ceph-osd@$i

exit
EOF

done
