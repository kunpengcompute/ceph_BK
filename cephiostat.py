#!/usr/bin/env python3

import sys
import argparse
import time
import os
import json
import signal


SPDK_CPU_STAT = "/proc/stat"
SPDK_UPTIME = "/proc/uptime"

SPDK_CPU_STAT_HEAD = ['cpu_stat:', 'user_stat', 'nice_stat',
                      'system_stat', 'iowait_stat', 'steal_stat', 'idle_stat']
SPDK_BDEV_KB_STAT_HEAD = ['Device', 'tps', 'KB_read/s',
                          'KB_wrtn/s', 'KB_dscd/s', 'KB_read', 'KB_wrtn', 'KB_dscd',
                          'lat_read', 'lat_write', 'lat_dc', 'rd_count', 'wr_count', 'dc_count']
SPDK_BDEV_MB_STAT_HEAD = ['Device', 'tps', 'MB_read/s',
                          'MB_wrtn/s', 'MB_dscd/s', 'MB_read', 'MB_wrtn', 'MB_dscd',
                          'lat_read', 'lat_write', 'lat_dc', 'rd_count', 'wr_count', 'dc_count']

SPDK_MAX_SECTORS = 0xffffffff

class BdevStat:
    def __init__(self, dictionary):
        if dictionary is None:
            return
        for k, value in dictionary.items():
            if k == 'name':
                self.bdev_name = value
            elif k == 'read_bytes':
                self.rd_sectors = value >> 9
            elif k == 'write_bytes':
                self.wr_sectors = value >> 9
            elif k == 'flush_bytes':
                self.dc_sectors = value >> 9
            elif k == 'write_lat':
                self.wr_time = value["sum"]
                self.wr_count = value["avgcount"]
            elif k == 'flush_lat':
                self.dc_time = value["sum"]
                self.dc_count = value["avgcount"]
            elif k == 'read_lat':
                self.rd_time = value["sum"]
                self.rd_count = value["avgcount"]

        self.rd_merges = 0
        self.wr_merges = 0
        self.dc_merges = 0
        self.upt = 0.0

    def __getattr__(self, name):
        return 0

def get_osd_info(path, blkinfo):
    osdList = []
    str = os.popen("ls " + path).read()
    for s in str.split():
        if "osd" in s and "asok" in s:
            osdList.append(s)
    if (not blkinfo):
        return osdList

    newList = []
    for osd in osdList:
        osdperf = os.popen(" ceph daemon " + path + "/" +  osd + " perf dump").read()
        if "NVMEDevice" in osdperf :
            param_json = json.loads(osdperf)
            items = param_json.items()
            for item in items:
                #print(item[0])
                if "NVMEDevice" in item[0] and item[0].split("-")[1] in blkinfo:
                    newList.append(osd)
    return newList;

def get_stat_info(path, osdList):
    dev_stat = {}

    for osd in osdList:
        osdperf = os.popen(" ceph daemon " + path + "/" +  osd + " perf dump").read()
        if "NVMEDevice" in osdperf :
            param_json = json.loads(osdperf)
            items = param_json.items()
            for item in items:
                if "NVMEDevice" in item[0]:
                    #print(item[1])
                    _stat = BdevStat(item[1])
                    _stat.bdev_name = item[0].split("-")[1]

                    dev_stat[_stat.bdev_name] = _stat
    
    return dev_stat


def uptime():
    with open(SPDK_UPTIME, 'r') as f:
        return float(f.readline().split()[0])


def _stat_format(data, header, leave_first=False):
    list_size = len(data)
    header_len = len(header)

    if list_size == 0:
        raise AssertionError
    list_len = len(data[0])

    for ll in data:
        if len(ll) != list_len:
            raise AssertionError
        for i, r in enumerate(ll):
            ll[i] = str(r)

    if (leave_first and list_len + 1 != header_len) or \
            (not leave_first and list_len != header_len):
        raise AssertionError

    item_sizes = [0 for i in range(header_len)]

    for i in range(0, list_len):
        if leave_first and i == 0:
            item_sizes[i] = len(header[i + 1])

        data_len = 0
        for x in data:
            data_len = max(data_len, len(x[i]))
        index = i + 1 if leave_first else i
        item_sizes[index] = max(len(header[index]), data_len)

    _format = '  '.join('%%-%ss' % item_sizes[i] for i in range(0, header_len))
    print(_format % tuple(header))
    if leave_first:
        #print('\n'.join(_format % ('', *tuple(ll)) for ll in data))
        print('\n'.join(_format % tuple(ll) for ll in data))
    else:
        print('\n'.join(_format % tuple(ll) for ll in data))

    #print()
    sys.stdout.flush()

def check_positive(value):
    v = int(value)
    if v <= 0:
        raise argparse.ArgumentTypeError("%s should be positive int value" % v)
    return v

def read_bdev_stat(last_stat, stats, mb, use_upt):

    upt_cur = uptime()
    info_stats = []
    unit = 2048 if mb else 2

    bdev_stats = []
    if last_stat:
        for key in stats.keys():
            _stat = stats[key]
            _stat.upt = upt_cur
            bdev_stats.append(_stat)
            _last_stat = None
            for last_bdev in last_stat:
                if (_stat.bdev_name == last_bdev.bdev_name):
                    _last_stat = last_bdev
                    break

            # get the interval time
            upt = _stat.upt - _last_stat.upt

            rd_sec = _stat.rd_sectors - _last_stat.rd_sectors
            if (_stat.rd_sectors < _last_stat.rd_sectors) and (_last_stat.rd_sectors <= SPDK_MAX_SECTORS):
                rd_sec &= SPDK_MAX_SECTORS

            wr_sec = _stat.wr_sectors - _last_stat.wr_sectors
            if (_stat.wr_sectors < _last_stat.wr_sectors) and (_last_stat.wr_sectors <= SPDK_MAX_SECTORS):
                wr_sec &= SPDK_MAX_SECTORS

            dc_sec = _stat.dc_sectors - _last_stat.dc_sectors
            if (_stat.dc_sectors < _last_stat.dc_sectors) and (_last_stat.dc_sectors <= SPDK_MAX_SECTORS):
                dc_sec &= SPDK_MAX_SECTORS

            tps = ((_stat.rd_count + _stat.dc_count + _stat.wr_count) -
                   (_last_stat.rd_count + _last_stat.dc_count + _last_stat.wr_count)) / upt

            dc_lat = 0
            wr_lat = 0
            rd_lat = 0
            if ((_stat.wr_count - _last_stat.wr_count) > 0):
                wr_lat = (_stat.wr_time - _last_stat.wr_time) / (_stat.wr_count - _last_stat.wr_count)
            if ((_stat.rd_count - _last_stat.rd_count) > 0):
                rd_lat = (_stat.rd_time - _last_stat.rd_time) / (_stat.rd_count - _last_stat.rd_count)
            if ((_stat.dc_count - _last_stat.dc_count) != 0):
                dc_lat = (_stat.dc_time - _last_stat.dc_time) / (_stat.dc_count - _last_stat.dc_count)

            info_stat = [
                key,
                "{:.2f}".format(tps),
                "{:.2f}".format(
                    (_stat.rd_sectors - _last_stat.rd_sectors) / upt / unit),
                "{:.2f}".format(
                    (_stat.wr_sectors - _last_stat.wr_sectors) / upt / unit),
                "{:.2f}".format(
                    (_stat.dc_sectors - _last_stat.dc_sectors) / upt / unit),
                "{:.2f}".format(rd_sec / unit),
                "{:.2f}".format(wr_sec / unit),
                "{:.2f}".format(dc_sec / unit),
                "{:.6f}".format(rd_lat),
                "{:.6f}".format(wr_lat),
                "{:.6f}".format(dc_lat),
                "{:.1f}".format(_stat.rd_count - _last_stat.rd_count),
                "{:.1f}".format(_stat.wr_count - _last_stat.wr_count),
                "{:.1f}".format(_stat.dc_count - _last_stat.dc_count),
            ]
            info_stats.append(info_stat)
    else:
        for key in stats.keys():
            _stat = stats[key]
            _stat.upt = upt_cur
            bdev_stats.append(_stat)

            upt = _stat.upt
            tps = (_stat.rd_count + _stat.dc_count + _stat.wr_count) / upt

            dc_lat = 0
            wr_lat = 0
            rd_lat = 0
            if (_stat.wr_count > 0):
                wr_lat = _stat.wr_time / _stat.wr_count
            if (_stat.rd_count > 0):
                rd_lat = _stat.rd_time / _stat.rd_count
            if (_stat.dc_count != 0):
                dc_lat = _stat.dc_time / _stat.dc_count

            info_stat = [
                key,
                "{:.2f}".format(tps),
                "{:.2f}".format(_stat.rd_sectors / upt / unit),
                "{:.2f}".format(_stat.wr_sectors / upt / unit),
                "{:.2f}".format(_stat.dc_sectors / upt / unit),
                "{:.2f}".format(_stat.rd_sectors / unit),
                "{:.2f}".format(_stat.wr_sectors / unit),
                "{:.2f}".format(_stat.dc_sectors / unit),
                "{:.6f}".format(rd_lat),
                "{:.6f}".format(wr_lat),
                "{:.6f}".format(dc_lat),
                "{:.1f}".format(_stat.rd_count),
                "{:.1f}".format(_stat.wr_count),
                "{:.1f}".format(_stat.dc_count),
            ]
            info_stats.append(info_stat)

    _stat_format(
        info_stats, SPDK_BDEV_MB_STAT_HEAD if mb else SPDK_BDEV_KB_STAT_HEAD)
    return bdev_stats


def io_stat_display(args, stat):

    _stats = get_stat_info(args.path, args.osdList)
    bdev_stats = read_bdev_stat(
        stat, _stats, args.mb_display, True)
    return bdev_stats

def io_stat_display_loop(args):
    interval = args.interval
    time_in_second = args.time_in_second

    last_cpu_stat = None
    bdev_stats = None
    args.osdList = get_osd_info(args.path, args.name)

    cur = 0
    while True:
        bdev_stats = io_stat_display(
            args, bdev_stats)

        time.sleep(interval)
        cur += interval
        if cur >= time_in_second:
            break

def signal_handler(signal,frame):
    print('You pressed Ctrl + C!')
    sys.exit(0)

if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description='Ceph SPDK iostats command line interface')

    parser.add_argument('-k', '--kb-display', dest='kb_display',
                        action='store_true', help="Display drive stats in KiB",
                        required=False, default=False)

    parser.add_argument('-m', '--mb-display', dest='mb_display',
                        action='store_true', help="Display drive stats in MiB",
                        required=False, default=False)

    parser.add_argument('-i', '--interval', dest='interval',
                        type=check_positive, help='Time interval (in seconds) on which \
                        to poll I/O stats. Used in conjunction with -t',
                        required=False, default=0)

    parser.add_argument('-t', '--time', dest='time_in_second',
                        type=check_positive, help='The number of second to display stats \
                        before returning. Used in conjunction with -i',
                        required=False, default=0)

    parser.add_argument('-b', '--name', dest='name',
                        help="Name of the Blockdev. Example: 0000:81:00.0", required=False, default = None)
    
    parser.add_argument('-p', '--path', dest='path',
            help="Ceph osd asok file path. Example: /var/run/ceph", required=False, default = "/var/run/ceph")

    args = parser.parse_args()
    if ((args.interval == 0 and args.time_in_second != 0) or
            (args.interval != 0 and args.time_in_second == 0)):
        raise argparse.ArgumentTypeError(
            "interval and time_in_second should be greater than 0 at the same time")
    if args.kb_display and args.mb_display:
        parser.print_help()
        exit()

    signal.signal(signal.SIGINT,signal_handler)

    io_stat_display_loop(args)

