#!/bin/sh
  
host_name_list=$@

for host_name in ${host_name_list}
do
echo "${host_name} disable spdk conf start"
ssh -T root@${host_name}  << EOF
/usr/lib/ceph/ceph-osd-spdk.sh stop
systemctl disable spdk-conf.service
systemctl disable spdk-conf.target
exit
EOF
echo "${host_name} disable spdk conf end"
done

