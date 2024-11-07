#!/bin/sh
  
function set_code_seg_huge_page() {
    while [[ $(df -h | grep /var/ceph) != "" ]]; do
        umount /var/ceph
    done

    rm -rf /var/ceph
    mkdir -p /var/ceph
    mount -t tmpfs -o huge=always tmpfs /var/ceph

    rm -f /usr/bin/ceph-osd
    cp /usr/bin/ceph-osd.in /var/ceph/ceph-osd
    setcap 'CAP_DAC_OVERRIDE+eip CAP_SYS_ADMIN+eip' /var/ceph/ceph-osd
    ln -s /var/ceph/ceph-osd /usr/bin/ceph-osd
}

function unset_code_seg_huge_page() {
    while [[ $(df -h | grep /var/ceph) != "" ]]; do
        umount /var/ceph
    done

    rm -rf /var/ceph
    rm -f /usr/bin/ceph-osd
    cp /usr/bin/ceph-osd.in /usr/bin/ceph-osd
    setcap 'CAP_DAC_OVERRIDE+eip CAP_SYS_ADMIN+eip' /usr/bin/ceph-osd
}


if [[ $1 == "start" ]]; then

    /usr/lib/ceph/spdk/scripts/setup.sh

    # configuring the number of huge pages, adjust the number as required
    numa_num=$(lscpu | grep "NUMA node(s)" | awk '{print $3}')
    for ((i=0;i<${numa_num};i++))
    do
        echo 5120 > /sys/devices/system/node/node${i}/hugepages/hugepages-2048kB/nr_hugepages
    done

    echo 1048576 > /proc/sys/net/core/rmem_default
    echo 1048576 > /proc/sys/net/core/rmem_max

    mkdir -p /mnt/hugepages
    chmod -R 700 /mnt/hugepages
    mount -t hugetlbfs nodev /mnt/hugepages -o pagesize=2M

    mkdir -p /var/run/ceph/osd
    mkdir -p /var/log/ceph/osd
    /usr/lib/ceph/spdk/scripts/setup.sh status

    set_code_seg_huge_page
elif [[ $1 == "stop" ]]; then
    /usr/lib/ceph/spdk/scripts/setup.sh reset

    # configuring the number of huge pages, adjust the number as required
    numa_num=$(lscpu | grep "NUMA node(s)" | awk '{print $3}')
    for ((i=0;i<${numa_num};i++))
    do
        echo 0 > /sys/devices/system/node/node${i}/hugepages/hugepages-2048kB/nr_hugepages
    done

    /usr/lib/ceph/spdk/scripts/setup.sh status

    unset_code_seg_huge_page
fi
