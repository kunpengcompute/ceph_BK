// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab
/*
 * Ceph - scalable distributed file system
 *
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023.
 *
 * Author: Chunsong Feng <fengchunsong@huawei.com>
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1, as published by the Free Software
 * Foundation.  See file COPYING.
 *
 */

#include "UCXEvent.h"
#include "UCXStack.h"
#include "common/Cycles.h"

#define dout_subsys ceph_subsys_ms

#undef dout_prefix
#define dout_prefix *_dout << "UCXEventDriver "

int UCXEventManager::get_eventfd()
{
    std::lock_guard l(lock);
    int fd = dup(base_fd);
    if (fd > max_fd) {
        max_fd = fd;
        fds.resize(max_fd + 1);
    }

    Tub<UserspaceFDImpl> &impl = fds[fd];
    ceph_assert(!impl);
    impl.construct();
    return fd;
}

void UCXEventManager::close(int fd)
{
    std::lock_guard l(lock);
    if ((size_t)fd >= fds.size()) {
        return;
    }

    Tub<UserspaceFDImpl> &impl = fds[fd];
    if (!impl) {
        return;
    }

    if (fd == max_fd) {
        max_fd--;
    }

    if (impl->activating_mask) {
        if (waiting_fds[max_wait_idx] == fd) {
            ceph_assert(impl->waiting_idx == max_wait_idx);
            --max_wait_idx;
        }
        waiting_fds[impl->waiting_idx] = -1;
    }
    impl.destroy();
}

int UCXEventDriver::init(EventCenter *c, int nevent)
{
    ldout(cct, 20) << __func__ << " num of event_masks: " << nevent << dendl;
    Cycles::init();
    return EpollDriver::init(c, nevent);
}

int UCXEventDriver::add_event(int fd, int cur_mask, int add_mask)
{
    ldout(cct, 20) << __func__ << " fd: " << fd << " read ? " << (EVENT_READABLE & add_mask) << dendl;

    int r = manager.listen(fd, add_mask);
    if (r == -ENOENT) {
        return EpollDriver::add_event(fd, cur_mask, add_mask);
    }

    return r;
}

int UCXEventDriver::del_event(int fd, int cur_mask, int delmask)
{
    ldout(cct, 20) << __func__ << " del event fd=" << fd << " cur_mask=" << cur_mask << " delmask=" << delmask << dendl;
    int r = 0;

    if (delmask != EVENT_NONE) {
        r = manager.unlisten(fd, delmask);
        if (r == -ENOENT) {
            return EpollDriver::del_event(fd, cur_mask, delmask);
        }
    }

    return r;
}

int UCXEventDriver::resize_events(int newsize)
{
    return EpollDriver::resize_events(newsize);
}

int UCXEventDriver::event_wait(vector<FiredFileEvent> &fired_events, struct timeval *tvp)
{
    if (nullptr == ucp_worker) {
        ldout(cct, 1) << __func__ << " ucp_worker null " << dendl;
        return EpollDriver::event_wait(fired_events, tvp);
    }

    uint64_t start = Cycles::rdtsc();
    if (!tvp->tv_sec) {
        if (tvp->tv_usec < cct->_conf->ms_async_rdma_polling_us)
            tvp->tv_usec = cct->_conf->ms_async_rdma_polling_us;
    }
    double seconds = tvp->tv_sec + tvp->tv_usec / 1000000.f;
    uint64_t end = Cycles::from_seconds(seconds) + start;
    bool timeout = false;
    while (ucp_worker_progress(ucp_worker)) {
        uint64_t now = Cycles::rdtsc();
        if (now > end) {
            timeout = true;
            break;
        }
    }

    int retval = manager.poll(ucx_events, ucx_masks, num_events, tvp);
    if (retval > 0) {
        fired_events.resize(retval);
        for (int i = 0; i < retval; i++) {
            fired_events[i].fd = ucx_events[i];
            fired_events[i].mask = ucx_masks[i];
        }
        return fired_events.size();
    }

    if (!timeout && UCS_OK == ucp_worker_arm(ucp_worker)) {
        retval = EpollDriver::event_wait(fired_events, tvp);
        if (retval < 0) {
            lderr(cct) << __func__ << " epoll wait failed with err: " << retval << dendl;
            return retval;
        }

        ldout(cct, 20) << __func__ << " waking up with " << fired_events.size() << " events " << dendl;
    }


    return fired_events.size();
}