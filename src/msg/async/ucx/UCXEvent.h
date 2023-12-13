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

#ifndef CEPH_UCXEVENT_H
#define CEPH_UCXEVENT_H

#include <vector>

#include "msg/async/Event.h"
#include "msg/async/EventEpoll.h"
#include "common/ceph_mutex.h"
#include "msg/async/dpdk/UserspaceEvent.h"

extern "C" {
#include <ucp/api/ucp.h>
};

class UCXEventManager : public UserspaceEventManager {
    CephContext *cct;
    ceph::mutex lock = ceph::make_mutex("UCXEventManager::lock");
    int base_fd;

public:
    explicit UCXEventManager(CephContext *c) : UserspaceEventManager(c), cct(c) {};
    virtual ~UCXEventManager() {};

    int get_eventfd();
    void close(int fd);

    void init(int fd)
    {
        base_fd = fd;
        max_fd = fd;
    }
};

class UCXEventDriver : public EpollDriver {
    UCXEventManager manager;

public:
    explicit UCXEventDriver(CephContext *c) : EpollDriver(c), manager(c), cct(c) {};
    virtual ~UCXEventDriver() {};

    int init(EventCenter *c, int nevent) override;
    int add_event(int fd, int cur_mask, int add_mask) override;
    int del_event(int fd, int cur_mask, int del_mask) override;
    int resize_events(int newsize) override;
    int event_wait(vector<FiredFileEvent> &fired_events, struct timeval *tp) override;
    void driver_init(ucp_worker_h ucp_w, int fd)
    {
        ucp_worker = ucp_w;
        manager.init(fd);
    }

    UCXEventManager *get_manager() {
        return &manager;
    }

private:
    CephContext *cct;
    ucp_worker_h ucp_worker;
    const static int num_events = 512;
    int ucx_events[num_events];
    int ucx_masks[num_events];
};

#endif // CEPH_UCXEVENT_H
