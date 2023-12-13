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

#ifndef CEPH_UCXSTACK_H
#define CEPH_UCXSTACK_H

#include <vector>
#include <thread>
#include <deque>

#include "common/ceph_context.h"
#include "common/debug.h"
#include "common/errno.h"

#include "msg/async/Stack.h"
#include "UCXEvent.h"

extern "C" {
#include <ucp/api/ucp.h>
};

class UCXStack;
class UCXConnectedSocketImpl;
class UCXServerSocketImpl;

struct MsgHeader{
    uint16_t ver = 1;
    uint16_t tag = 0;
    uint32_t data_len;
    uint64_t seq;
    uint32_t align = 0xFFFFFFFF;  
    void hton(void) {
        ver = htole16(ver);
        tag = htole16(tag);
        seq = htole64(seq);        
        data_len = htole64(data_len);        
        align = htole32(align);       
    }
    void ntoh(void) {
        ver = le16toh(ver);
        tag = le16toh(tag);
        seq = le64toh(seq);
        data_len = le64toh(data_len);
        align = le32toh(align);      
    }
} __attribute__((packed));

struct UCXReqDescr {
    UCXConnectedSocketImpl *conn;
    bufferlist *bl;
    ucp_dt_iov_t *iov_list;
    uint64_t sn;
};

struct desc_data {
    void *data;
    size_t length;
    struct MsgHeader msg;
};

class UCXWorker : public Worker {
public:
    explicit UCXWorker(CephContext *c, unsigned i);
    virtual ~UCXWorker();

    int listen(entity_addr_t &addr, unsigned addr_slot, const SocketOptions &opts, ServerSocket *) override;
    int connect(const entity_addr_t &addr, const SocketOptions &opts, ConnectedSocket *socket) override;
    void initialize() override;
    void destroy() override;

    void accept_connection(UCXConnectedSocketImpl *conn, ucp_conn_request_h conn_req);
    static void accept_cb(ucp_conn_request_h conn_req, void *arg);
    static void ep_error_cb(void *arg, ucp_ep_h ep, ucs_status_t status);
    static void ep_error_cb_client(void *arg, ucp_ep_h ep, ucs_status_t status);

    static ucs_status_t am_recv_callback(void *arg, const void *header, size_t header_length, void *data, size_t length,
        const ucp_am_recv_param_t *param);

    ucp_worker_h get_ucp_worker()
    {
        return ucp_worker;
    }

    void set_ucp_context(ucp_context_h ctx)
    {
	ucp_context = ctx;
    }

    UCXEventManager *get_manager()
    {
        return manager;
    }

    void erase_connection(ucp_ep_h ucp_ep)
    {
        conn_map.erase(ucp_ep);
    }

    UCXConnectedSocketImpl *find_connection(ucp_ep_h ucp_ep)
    {
        if (conn_map.count(ucp_ep) == 0)
            return nullptr;
        return conn_map[ucp_ep];
    }
    void erase_listen_fd(int fd)
    {
        listen_map.erase(fd);
    }

    UCXServerSocketImpl *find_listener(int fd)
    {
        if (listen_map.count(fd) == 0)
            return nullptr;
        return listen_map[fd];
    }    

    void handle_poll(uint64_t fd_or_id);

private:
    UCXEventManager *manager;
    ucp_worker_h ucp_worker = nullptr;
    ucp_context_h ucp_context = nullptr;
    EventCallbackRef event_handler;

    std::set<int> accepting;
    std::unordered_map<ucp_ep_h, UCXConnectedSocketImpl *> conn_map;
    std::unordered_map<int, UCXServerSocketImpl *> listen_map;
    int ucp_fd = -1;

    void set_recv_handler(void);
};

class UCXConnectedSocketImpl : public ConnectedSocketImpl {
    static constexpr __u16 PAGE_SIZE_ALIGNMENT = 4096;
    
    UCXEventManager *manager;
    bool use_zero_copy;
public:
    static constexpr unsigned AM_ID = 0x43657068; // Ceph
    enum CONNECT_STATE {
        CONNECTING = 0,
        CONNECTED,
        DISCONNECTED,
        CLOSED,
    };
    ucp_ep_h ucp_ep = nullptr;
    uint64_t sn_send = 0;
    uint64_t sn_recv = 0;
    std::set<UCXReqDescr *> send_reqs;
    std::set<UCXReqDescr *> recv_reqs;
    CephContext *get_cct()
    {
        return cct;
    }
    UCXWorker *get_worker()
    {
        return worker;
    }

    UCXConnectedSocketImpl(UCXWorker *w, int fd);
    virtual ~UCXConnectedSocketImpl();

    int is_connected() override
    {
        return (conn_state == CONNECTED) && (ucp_ep != nullptr);
    };
    ssize_t read(char *, size_t) override;
    ssize_t zero_copy_read(ceph::bufferlist &bl, size_t length) override;
    ssize_t send(bufferlist &bl, bool more) override;
    void shutdown() override;
    void close() override;
    int fd() const override
    {
        return conn_fd;
    }
    int getsockname(int sockfd, struct sockaddr *addr, socklen_t *addrlen) override;                       
    int socket_fd() const override
    {
        return conn_fd;
    }

    
    void set_sockaddr(const struct sockaddr *sa){
        memcpy(&sock_addr, sa, sizeof(sock_addr));
    }
    static void request_init(void *req);
    static void request_cleanup(void *req);

    ssize_t do_send(bufferlist &bl);
    void conn_release_recvs();
    void ucx_ep_close(bool close_event);

    static void send_completion_cb(void *req, ucs_status_t status, void *userdata);

    static void am_recv_data_callback(void *req, ucs_status_t status, size_t length, void *user_data);
    ucs_status_t process_recv_request(ucp_worker_h ucp_worker, void *data, size_t length, MsgHeader &msg);

    ssize_t read_buffers(char *rbuf, size_t bytes);

    bool merge_out_of_order();
    void insert_recv_buffers(uint64_t sn, bufferlist &bl, size_t length);
    void insert_out_of_order(uint64_t seg, bufferlist &bl)
    {
        auto it = out_of_order_map.find(seg);
        if (it == out_of_order_map.end())
        {
            out_of_order_map.emplace(seg, std::move(bl));
        }
        else
        {
            it->second.append(std::move(bl));
        }        
    }
    void set_connect_stat(enum CONNECT_STATE stat)
    {
        conn_state = stat;
    }
    enum CONNECT_STATE get_connect_stat(void)
    {
        return conn_state;
    }
    bufferlist create_bufferlist_with_hint(size_t length, MsgHeader &msg);

private:
    CephContext *cct;
    UCXWorker *worker;
    ucp_worker_h ucp_worker;
    bufferlist rx_bl;
    std::map<uint64_t, bufferlist> out_of_order_map;
    int conn_fd;
    enum CONNECT_STATE conn_state = CONNECTING;
    struct sockaddr sock_addr;
    int submit(bufferlist &bl, MsgHeader *msg, const void *buffer, size_t count, ucp_datatype_t type);  
};

class UCXServerSocketImpl : public ServerSocketImpl {
public:
    UCXServerSocketImpl(UCXWorker *w, int server_socket, entity_addr_t &a, unsigned addr_slot);
    ~UCXServerSocketImpl();

    int accept(ConnectedSocket *sock, const SocketOptions &opt, entity_addr_t *out, Worker *w) override;
    void abort_accept() override;
    int fd() const override
    {
        return server_fd;
    }

    UCXWorker *get_worker()
    {
        return worker;
    }

    CephContext *cct()
    {
        return worker->cct;
    }

    void add_conn_request(ucp_conn_request_h conn_req)
    {
        conn_request_queue.push_back(conn_req);
    }

    void set_ucp_listener(ucp_listener_h listener)
    {
        ucp_listener = listener;
    }
private:
    UCXWorker *worker;
    int server_fd;
    std::deque<ucp_conn_request_h> conn_request_queue;
    ucp_listener_h ucp_listener = nullptr;

    ucp_conn_request_h pop_conn_request()
    {
        if (conn_request_queue.empty()) {
            return nullptr;
        }

        ucp_conn_request_h conn_req = conn_request_queue.front();
        conn_request_queue.pop_front();
        return conn_req;
    }
};

class UCXStack : public NetworkStack {
public:
    explicit UCXStack(CephContext *cct, const string &t);
    virtual ~UCXStack();

    bool support_zero_copy_read() const override
    {
        return true;
    }

    bool nonblock_connect_need_writable_event() const override
    {
        return false;
    }

    void spawn_worker(unsigned i, std::function<void()> &&func) override;
    void join_worker(unsigned i) override;

    bool is_ready() override
    {
        return nullptr != ucp_context;
    };

    ucp_context_h get_ucp_context()
    {
        return ucp_context;
    }

private:
    vector<std::thread> threads;
    ucp_context_h ucp_context = nullptr;

    void ucx_contex_create();
};

#endif
