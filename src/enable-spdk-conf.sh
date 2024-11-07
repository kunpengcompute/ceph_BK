#!/bin/sh
  
host_name_list=$@

for host_name in ${host_name_list}
do
echo "${host_name} enable spdk conf start"
ssh -T root@${host_name}  << EOF
/usr/lib/ceph/ceph-osd-spdk.sh start
systemctl enable spdk-conf.service
systemctl enable spdk-conf.target
exit
EOF
echo "${host_name} enable spdk conf end"
done
