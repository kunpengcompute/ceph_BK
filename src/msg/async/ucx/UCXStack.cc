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

#include "UCXStack.h"
#include "UCXEvent.h"
#include "common/deleter.h"

#define dout_subsys ceph_subsys_ms
#undef dout_prefix
#define dout_prefix *_dout << "UCXConnectedSocketImpl "

UCXConnectedSocketImpl::UCXConnectedSocketImpl(UCXWorker *w, int fd) : cct(w->cct), worker(w), conn_fd(fd)
{
    UCXEventDriver *driver = dynamic_cast<UCXEventDriver *>(worker->center.get_driver());
    manager = driver->get_manager();
    ucp_worker = w->get_ucp_worker();
    use_zero_copy = cct->_conf.get_val<bool>("ms_async_ucx_zerocopy");
}

void UCXConnectedSocketImpl::shutdown()
{
    ceph_assert(worker->center.in_thread());
    ldout(cct, 20) << __func__ << " conn fd: " << conn_fd << " is shutting down..." << dendl;
}

void UCXConnectedSocketImpl::request_init(void *req)
{
    UCXReqDescr *desc = static_cast<UCXReqDescr *>(req);
    desc->bl = new bufferlist;
}

void UCXConnectedSocketImpl::request_cleanup(void *req)
{
    UCXReqDescr *desc = static_cast<UCXReqDescr *>(req);

    delete desc->bl;
}

void UCXConnectedSocketImpl::ucx_ep_close(bool close_event)
{
    int fd = conn_fd;
    if (ucp_ep != nullptr) {
        ceph_assert(nullptr != ucp_ep);
        ldout(cct, 20) << __func__ << " fd: " << fd << " ep=" << (void *)ucp_ep << dendl;
        for (auto req : send_reqs) {
            req->conn = nullptr;
            ucp_request_cancel(ucp_worker, req);
        }
        send_reqs.clear();
        for (auto req : recv_reqs) {
            req->conn = nullptr;
            ucp_request_cancel(ucp_worker, req);
        }
        recv_reqs.clear();

        ucs_status_ptr_t request = ucp_ep_close_nb(ucp_ep, UCP_EP_CLOSE_MODE_FORCE);
        if (nullptr == request) {
            ldout(cct, 10) << __func__ << " ucp ep fd: " << fd << " closed in place..." << dendl;
        } else if (UCS_PTR_IS_PTR(request)) {
            /*
             * We don't care even the request is still in
             * UCS_INPROGRESS state - it's UCX responsility now
             */
            ldout(cct, 10) << __func__ << " ucp ep fd: " << fd << " closed pending..." << dendl;
            ucp_request_free(request);
        } else if (UCS_PTR_STATUS(request) != UCS_OK) {
            lderr(cct) << __func__ << " fd: " << fd << " ucp_ep_close_nb call failed: err " <<
                ucs_status_string(UCS_PTR_STATUS(request)) << dendl;
        }

        worker->erase_connection(ucp_ep);
        conn_release_recvs();
        ucp_ep = nullptr;
        conn_state = CLOSED;
    }

    ldout(cct, 10) << __func__ << " fd: " << fd << " exit..." << dendl;
}

void UCXConnectedSocketImpl::conn_release_recvs()
{
    size_t total = rx_bl.length();

    /* Free all pending_fd receives */
    rx_bl.clear();
    if (total)
        ldout(cct, 1) << __func__ << " fd: " << conn_fd << " total " << total << dendl;
}

void UCXConnectedSocketImpl::close()
{
    if (conn_fd > 0) {
        ldout(cct, 10) << __func__ << " fd: " << conn_fd << " send " << sn_send << " recv " << sn_recv << dendl;
        ucx_ep_close(false);
        manager->close(conn_fd);
        conn_fd = -1;
    }
}

UCXConnectedSocketImpl::~UCXConnectedSocketImpl()
{
    UCXConnectedSocketImpl::close();
    out_of_order_map.clear();
}

int UCXConnectedSocketImpl::submit(bufferlist &bl, MsgHeader *msg, const void *buffer, size_t count,
    ucp_datatype_t type)
{
    ucp_dt_iov_t *iov_list = nullptr;
    msg->seq = sn_send;
    msg->hton();
    ucp_request_param_t param;
    param.op_attr_mask =
        UCP_OP_ATTR_FIELD_CALLBACK | UCP_OP_ATTR_FIELD_DATATYPE | UCP_OP_ATTR_FIELD_USER_DATA | UCP_OP_ATTR_FIELD_FLAGS;
    param.datatype = type;
    param.cb.send = send_completion_cb;
    param.flags = UCP_AM_SEND_FLAG_REPLY;
    param.user_data = reinterpret_cast<void *>(msg);
    UCXReqDescr *req =
        static_cast<UCXReqDescr *>(ucp_am_send_nbx(ucp_ep, AM_ID, msg, sizeof(*msg), buffer, count, &param));

    if ((type & UCP_DATATYPE_CLASS_MASK) == UCP_DATATYPE_IOV)
        iov_list = (ucp_dt_iov_t *)buffer;
    if (req == nullptr) {
        /* in place completion */
        ldout(cct, 20) << __func__ << " SENT IN PLACE " << dendl;
        bl.splice(0, msg->data_len);
        sn_send += msg->data_len;
        if (iov_list) {
            delete[] iov_list;
        }
        delete msg;
        return 0;
    }

    if (UCS_PTR_IS_ERR(req)) {
        if (iov_list) {
            delete[] iov_list;
        }
        delete msg;
        lderr(cct) << __func__ << " fd: " << conn_fd << " send failure: " << UCS_PTR_STATUS(req) << dendl;
        return -EAGAIN;
    }

    req->conn = this;
    bl.splice(0, msg->data_len, req->bl);
    req->iov_list = iov_list;
    req->sn = sn_send;
    send_reqs.insert(req);
    ldout(cct, 20) << __func__ << " send in progress req " << req << dendl;

    sn_send += msg->data_len;
    return 0;
}

ssize_t UCXConnectedSocketImpl::do_send(bufferlist &bl)
{
    unsigned iov_cnt = bl.get_num_buffers();

    auto pb = std::cbegin(bl.buffers());
    int rc;
    size_t length = 0;
    if (iov_cnt == 1) {
        MsgHeader *msg = new MsgHeader;
        length = pb->length();
        msg->data_len = length;
        if (pb->length() >= PAGE_SIZE_ALIGNMENT) {
            msg->align = 0;
        }
        rc = submit(bl, msg, pb->c_str(), pb->length(), ucp_dt_make_contig(1));
        if (rc < 0) {
            return rc;
        }
        return length;
    } else {
        ucp_dt_iov_t *iov_list = new ucp_dt_iov_t[iov_cnt];
        MsgHeader *msg = new MsgHeader;
        int n;
        size_t off = 0;
        const char *pbuf = pb->raw_c_str();
        int buf_count = 0;
        for (n = 0; pb != bl.buffers().end(); ++pb) {
            if (pb->length() == 0) {
                ldout(cct, 10) << __func__ << " skip zero len " << dendl;
                continue;
            }

            iov_list[n].buffer = (void *)(pb->c_str());
            iov_list[n].length = pb->length();

            if (pb->length() >= PAGE_SIZE_ALIGNMENT) {
                msg->align = off;
            }
            off += pb->length();
            n++;
            if (pb->raw_c_str() == pbuf) {
                buf_count++;
                if (buf_count == 3) {
                    buf_count = 0;
                    break;
                }
            }
        }
        length = off;
        msg->data_len = off;
        rc = submit(bl, msg, iov_list, n, ucp_dt_make_iov());
        if (rc < 0) {
            return rc;
        }
    }

    return length;
}

ssize_t UCXConnectedSocketImpl::send(bufferlist &bl, bool more)
{
    unsigned total_len = bl.length();

    if (total_len == 0) {
        ldout(cct, 1) << __func__ << " fd: " << conn_fd << " sending " << total_len << dendl;
        return 0;
    }

    if (conn_state >= DISCONNECTED)
        return -EIO;

    if (nullptr == ucp_ep) {
        ldout(cct, 20) << __func__ << " fd: " << conn_fd << " put " << total_len << " bytes to the pending" << dendl;
        return -EIO;
    }
    
    ssize_t rc = do_send(bl);
    if (rc < 0) {
        return rc;
    }
    if (!more) {
        while (ucp_worker_progress(ucp_worker))
            ;
    }

    return total_len - bl.length();
}

int UCXConnectedSocketImpl::getsockname(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
    ucp_ep_attr_t attr;
    attr.field_mask = UCP_EP_ATTR_FIELD_LOCAL_SOCKADDR;
    ucs_status_t status = ucp_ep_query(ucp_ep, &attr);
    if (status != UCS_OK) {
        return -1;
    }
    if (*addrlen < sizeof(struct sockaddr)) {
        memcpy(addr, (char *)(&attr.local_sockaddr), *addrlen);
        *addrlen = sizeof(struct sockaddr);
    } else {
        memcpy(addr, (char *)(&attr.local_sockaddr), sizeof(struct sockaddr));
    }

    return 0;
};

void UCXConnectedSocketImpl::send_completion_cb(void *req, ucs_status_t status, void *userdata)
{
    UCXReqDescr *descr = static_cast<UCXReqDescr *>(req);
    UCXConnectedSocketImpl *conn = descr->conn;
    MsgHeader *msg = reinterpret_cast<MsgHeader *>(userdata);
    if (conn) {
        conn->send_reqs.erase(descr);
        ldout(conn->cct, 20) << __func__ << " send complete sn_send " << descr->sn << " length " <<
            descr->bl->length() << dendl;
    }
    descr->bl->clear();
    if (descr->iov_list) {
        delete[] descr->iov_list;
    }
    delete msg;

    ucp_request_free(req);
}

void UCXConnectedSocketImpl::insert_recv_buffers(uint64_t sn, bufferlist &bl, size_t length)
{
    if (sn > sn_recv) {
        ldout(cct, 20) << __func__ << " out of oder, expected " << sn_recv << " got " << sn << " length " <<
            bl.length() << dendl;
        insert_out_of_order(sn, bl);
    } else if (sn_recv - sn + bl.length() <= length) {
        sn_recv += bl.length();
        rx_bl.append(std::move(bl));
        merge_out_of_order();
        manager->notify(conn_fd, EVENT_READABLE);
    } else {
        ldout(cct, 0) << __func__ << " out of oder error " << sn_recv << " got " << sn << " length " << bl.length() <<
            " msg length " << length << dendl;
    }
}

void UCXConnectedSocketImpl::am_recv_data_callback(void *req, ucs_status_t status, size_t length, void *user_data)
{
    UCXReqDescr *desc = reinterpret_cast<UCXReqDescr *>(req);
    UCXWorker *myworker = reinterpret_cast<UCXWorker *>(user_data);
    UCXConnectedSocketImpl *conn = desc->conn;

    if (conn == nullptr) {
        lderr(myworker->cct) << " callback after disconnect status " << status << dendl;
        goto free_resource;
    }

    if (status != UCS_OK) {
        lderr(myworker->cct) << " callback with error status " << ucs_status_string(status) << dendl;
        goto remove_req;
    }

    ceph_assert(desc->bl->length() == length);
    conn->insert_recv_buffers(desc->sn, *desc->bl, desc->bl->length());
    ldout(myworker->cct, 20) << " am_recv_data_callback with length " << length << dendl;

remove_req:
    conn->recv_reqs.erase(desc);

free_resource:
    desc->bl->clear();
    if (desc->iov_list) {
        delete[] desc->iov_list;
    }
    ucp_request_free(req);
}

bufferlist UCXConnectedSocketImpl::create_bufferlist_with_hint(size_t length, MsgHeader &msg)
{
    bufferlist bl;
    size_t len = length;
    if (!use_zero_copy) {
        bufferptr ptr(buffer::create(length));
        bl.append(std::move(ptr));
        return bl;
    }
    if (msg.align != 0xFFFFFFFF) {
        len = msg.align;
    }
    if (len) {
        bufferptr ptr(buffer::create(len));
        bl.append(std::move(ptr));
    }
    len = length - len;
    if (len) {
        bufferptr ptr(buffer::create_aligned(len, PAGE_SIZE_ALIGNMENT));
        bl.append(std::move(ptr));
    }
    return bl;
}

ucs_status_t UCXConnectedSocketImpl::process_recv_request(ucp_worker_h ucp_worker, void *data, size_t length,
    MsgHeader &msg)
{
    bufferlist bl = create_bufferlist_with_hint(length, msg);

    unsigned iov_cnt = bl.get_num_buffers();

    ucp_request_param_t param;
    param.op_attr_mask = UCP_OP_ATTR_FIELD_CALLBACK | UCP_OP_ATTR_FIELD_DATATYPE | UCP_OP_ATTR_FIELD_USER_DATA;
    param.datatype = (iov_cnt == 1) ? ucp_dt_make_contig(1) : ucp_dt_make_iov();
    param.cb.recv_am = am_recv_data_callback;
    param.user_data = reinterpret_cast<void *>(worker);
    ucp_dt_iov_t *iov_list;
    UCXReqDescr *req;
    auto pb = std::cbegin(bl.buffers());
    if (iov_cnt == 1) {
        iov_list = nullptr;

        req = static_cast<UCXReqDescr *>(
            ucp_am_recv_data_nbx(ucp_worker, data, (void *)(pb->c_str()), pb->length(), &param));
    } else {
        iov_list = new ucp_dt_iov_t[iov_cnt];
        for (int n = 0; pb != bl.buffers().end(); ++pb, ++n) {
            iov_list[n].buffer = (void *)(pb->c_str());
            iov_list[n].length = pb->length();
        }
        req = static_cast<UCXReqDescr *>(ucp_am_recv_data_nbx(ucp_worker, data, iov_list, iov_cnt, &param));
    }
    if (req == nullptr) {
        if (iov_list) {
            delete[] iov_list;
        }
        insert_recv_buffers(msg.seq, bl, msg.data_len);
        bl.clear();
        lderr(worker->cct) << __func__ << " read inplace length " << length << " total " << sn_recv << dendl;
        return UCS_OK;
    } else if (UCS_PTR_IS_ERR(req)) {
        if (iov_list) {
            delete[] iov_list;
        }
        bl.clear();
        lderr(worker->cct) << __func__ << " fd: " << conn_fd << " send failure: " << UCS_PTR_STATUS(req) << dendl;
        return UCS_PTR_STATUS(req);
    } else {
        req->conn = this;
        req->bl->swap(bl);
        req->iov_list = iov_list;
        req->sn = msg.seq;
        recv_reqs.insert(req);
        ldout(worker->cct, 20) << " recv sn: " << msg.seq << ", pending sn: " << sn_recv << dendl;
    }
    return UCS_INPROGRESS;
}

ssize_t UCXConnectedSocketImpl::read_buffers(char *rbuf, size_t bytes)
{
    int fd = conn_fd;
    size_t read_size = 0;
    size_t left;
    size_t len;

    auto pb = std::cbegin(rx_bl.buffers());
    while (read_size < bytes) {
        left = pb->length();
        len = bytes - read_size;
        ldout(cct, 20) << __func__ << " fd: " << fd << " read to " << (void *)rbuf << " wanted " << len << " left " <<
            left << " pb->>offset= " << pb->offset() << " pb->>length= " << pb->length() << dendl;

        len = len > left ? left : len;
        memcpy(rbuf + read_size, pb->c_str(), len);
        read_size += len;
        pb++;
    }
    return read_size;
}

ssize_t UCXConnectedSocketImpl::zero_copy_read(ceph::bufferlist &bl, size_t length)
{
    ceph_assert(worker->center.in_thread());
    if (rx_bl.length() == 0) {
	while (ucp_worker_progress(ucp_worker));
    }
    size_t bytes = length - bl.length();
    size_t len = bytes > rx_bl.length() ? rx_bl.length() : bytes;
    if (len == 0) {
        if (conn_state >= DISCONNECTED) {
            return 0; // peer close
        }
        return -EAGAIN;
    }
    ldout(cct, 20) << __func__ << " fd: " << conn_fd << " rx_bl.length() " << rx_bl.length() << " wanted " << bytes <<
        " len = " << len << dendl;
    ceph::bufferlist rbl;
    rx_bl.splice(0, len, &rbl);
    bl.append(std::move(rbl));
    if (rx_bl.length()) {
        manager->notify(conn_fd, EVENT_READABLE);
    }

    return len;
}

ssize_t UCXConnectedSocketImpl::read(char *rbuf, size_t bytes)
{
    ceph_assert(worker->center.in_thread());
    if (rx_bl.length() == 0) {
	while (ucp_worker_progress(ucp_worker));
    }
    size_t len = bytes > rx_bl.length() ? rx_bl.length() : bytes;
    if (len == 0) {
        if (conn_state >= DISCONNECTED) {
            return 0; // peer close
        }
        return -EAGAIN;
    }

    read_buffers(rbuf, len);
    rx_bl.splice(0, len);

    if (rx_bl.length()) {
        manager->notify(conn_fd, EVENT_READABLE);
    }

    return len;
}

bool UCXConnectedSocketImpl::merge_out_of_order()
{
    bool merged = false;
    if (out_of_order_map.empty()) {
        return merged;
    }

    for (auto it = out_of_order_map.begin(); it != out_of_order_map.end();) {
        auto &bl = it->second;
        auto seg_beg = it->first;
        auto seg_len = bl.length();
        auto seg_end = seg_beg + seg_len;

        if (seg_beg <= sn_recv && seg_end > sn_recv) {
            // This segment has been received out of order and its previous
            // segment has been received now
            auto trim = sn_recv - seg_beg;
            if (trim) {
                bl.splice(0, trim);
                seg_len -= trim;
            }
            sn_recv += seg_len;
            rx_bl.append(std::move(bl));
            ldout(cct, 20) << __func__ << " merge out of oder sn " << seg_beg << " length" << seg_len <<
                " current sn: " << sn_recv << " trim " << trim << dendl;
            // Since c++11, erase() always returns the value of the following element
            it = out_of_order_map.erase(it);
            merged = true;
        } else if (sn_recv >= seg_end) {
            ldout(cct, 20) << __func__ << " drop merge out of oder seg_beg " << seg_beg << " seg_len: " << seg_len <<
                " current sn: " << sn_recv << dendl;

            // This segment has been receive already, drop it
            it = out_of_order_map.erase(it);
        } else {
            // seg_beg > sn_recv, can not merge. Note, seg_beg can grow only,
            // so we can stop looking here.
            it++;
            break;
        }
    }
    return merged;
}

#undef dout_prefix
#define dout_prefix *_dout << "UCXServerSocketImpl "

UCXServerSocketImpl::UCXServerSocketImpl(UCXWorker *w, int server_socket, entity_addr_t &a, unsigned addr_slot)
    : ServerSocketImpl(a.get_type(), addr_slot), worker(w), server_fd(server_socket)
{}

UCXServerSocketImpl::~UCXServerSocketImpl()
{
    if (ucp_listener) {
        ucp_listener_destroy(ucp_listener);
        ucp_listener = nullptr;
    }
    worker->erase_listen_fd(server_fd);
}

int UCXServerSocketImpl::accept(ConnectedSocket *sock, const SocketOptions &opt, entity_addr_t *peer_addr, Worker *w)
{
    ceph_assert(worker->center.in_thread());
    ucp_conn_request_h conn_req = pop_conn_request();
    if (conn_req == nullptr) {
        return -EAGAIN;
    }
    ldout(worker->cct, 20) << __func__ << " server_fd: " << server_fd << " conn_req: " << (void *)conn_req << dendl;

    ucp_conn_request_attr_t attr;
    attr.field_mask = UCP_CONN_REQUEST_ATTR_FIELD_CLIENT_ADDR;
    ucs_status_t status = ucp_conn_request_query(conn_req, &attr);
    if (status != UCS_OK) {
        if (status != UCS_ERR_UNSUPPORTED) {
            ucp_listener_reject(ucp_listener, conn_req);
        }
        return -EIO;
    };
    peer_addr->set_sockaddr((sockaddr *)(&attr.client_address));
    peer_addr->set_type(addr_type);
    if (!conn_request_queue.empty()) {
        worker->get_manager()->notify(server_fd, EVENT_READABLE);
    }

    UCXWorker *dest_worker = static_cast<UCXWorker *>(w);
    int fd = dest_worker->get_manager()->get_eventfd();
    UCXConnectedSocketImpl *p = new UCXConnectedSocketImpl(dest_worker, fd);
    p->set_sockaddr((sockaddr *)(&attr.client_address));
    if (w->center.in_thread()) {
        dest_worker->accept_connection(p, conn_req);
    } else {
        w->center.submit_to(
            w->center.get_id(), [dest_worker, p, conn_req]() { dest_worker->accept_connection(p, conn_req); }, true);
    }

    std::unique_ptr<UCXConnectedSocketImpl> csi(p);
    *sock = ConnectedSocket(std::move(csi));

    return 0;
}

void UCXServerSocketImpl::abort_accept()
{
    ceph_assert(worker->center.in_thread());
    if (server_fd == -1)
        return;
    if (ucp_listener) {
        for (auto it = conn_request_queue.cbegin(); it != conn_request_queue.cend(); ++it) {
            ucp_listener_reject(ucp_listener, *it);
        }
        conn_request_queue.clear();

        worker->get_manager()->close(server_fd);

        // ucp_listener_destroy(ucp_listener);

        // ucp_listener = nullptr;
    }
    server_fd = -1;
}

#undef dout_prefix
#define dout_prefix *_dout << "UCXWorker "

class C_handle_event : public EventCallback {
    UCXWorker *worker;

public:
    explicit C_handle_event(UCXWorker *w) : worker(w) {}
    void do_request(uint64_t fd_or_id) override
    {
        worker->handle_poll(fd_or_id);
    }
};

void UCXWorker::handle_poll(uint64_t fd_or_id) {}

UCXWorker::UCXWorker(CephContext *c, unsigned i) : Worker(c, i)
{
    event_handler = new C_handle_event(this);
}

UCXWorker::~UCXWorker()
{
    for (auto it : conn_map) {
        UCXConnectedSocketImpl *conn = it.second;
        conn->close();
    }
    conn_map.clear();
}

void UCXWorker::ep_error_cb_client(void *arg, ucp_ep_h ep, ucs_status_t status)
{
    UCXWorker *worker = reinterpret_cast<UCXWorker *>(arg);
    UCXConnectedSocketImpl *conn = worker->find_connection(ep);
    if (!conn) {
        ldout(worker->cct, 0) << "Can not find ep in conn_map" << dendl;
        return;
    }
    CephContext *cct = conn->get_cct();
    if (conn->is_connected()) {
        lderr(cct) << "ep_error_cb_client " << ucs_status_string(status) << " fd " << conn->fd() << " ep " << ep <<
            " connected " << dendl;
        conn->set_connect_stat(UCXConnectedSocketImpl::DISCONNECTED);
        worker->manager->notify(conn->fd(), EVENT_READABLE);
    } else {
        lderr(cct) << "ep_error_cb_client " << ucs_status_string(status) << " fd " << conn->fd() << " ep " << ep <<
            " unconnected" << dendl;
        ucs_status_ptr_t request = ucp_ep_close_nb(ep, UCP_EP_CLOSE_MODE_FORCE);
        if (nullptr == request) {
            ldout(cct, 10) << __func__ << " ucp ep fd: " << conn->fd() << " closed in place..." << dendl;
        } else if (UCS_PTR_IS_PTR(request)) {
            /*
             * We don't care even the request is still in
             * UCS_INPROGRESS state - it's UCX responsility now
             */
            ucp_request_free(request);
        } else if (UCS_PTR_STATUS(request) != UCS_OK) {
            lderr(cct) << __func__ << " ep: " << ep << " ucp_ep_close_nb call failed: err " <<
                ucs_status_string(UCS_PTR_STATUS(request)) << dendl;
        }
        ceph_assert(conn->send_reqs.size() == 0);
        ceph_assert(conn->recv_reqs.size() == 0);
    }
}

void UCXWorker::ep_error_cb(void *arg, ucp_ep_h ep, ucs_status_t status)
{
    UCXWorker *worker = reinterpret_cast<UCXWorker *>(arg);
    UCXConnectedSocketImpl *conn = worker->find_connection(ep);
    if (!conn) {
        ldout(worker->cct, 0) << "Can not find ep in conn_map" << dendl;
        return;
    }
    CephContext *cct = conn->get_cct();
    if (conn->is_connected()) {
        lderr(cct) << "ep_error_cb " << ucs_status_string(status) << " fd " << conn->fd() << " ep " << ep <<
            " connected " << dendl;
        conn->set_connect_stat(UCXConnectedSocketImpl::DISCONNECTED);
        worker->manager->notify(conn->fd(), EVENT_READABLE);
    } else {
        lderr(cct) << "ep_error_cb " << ucs_status_string(status) << " fd " << conn->fd() << " ep " << ep <<
            " unconnected" << dendl;
        ucs_status_ptr_t request = ucp_ep_close_nb(ep, UCP_EP_CLOSE_MODE_FORCE);
        if (nullptr == request) {
            ldout(cct, 10) << __func__ << " ucp ep fd: " << conn->fd() << " closed in place..." << dendl;
        } else if (UCS_PTR_IS_PTR(request)) {
            /*
             * We don't care even the request is still in
             * UCS_INPROGRESS state - it's UCX responsility now
             */
            ucp_request_free(request);
        } else if (UCS_PTR_STATUS(request) != UCS_OK) {
            lderr(cct) << __func__ << " ep: " << ep << " ucp_ep_close_nb call failed: err " <<
                ucs_status_string(UCS_PTR_STATUS(request)) << dendl;
        }
        ceph_assert(conn->send_reqs.size() == 0);
        ceph_assert(conn->recv_reqs.size() == 0);
    }
}

void UCXWorker::accept_connection(UCXConnectedSocketImpl *conn, ucp_conn_request_h conn_req)
{
    ceph_assert(center.in_thread());
    int fd = conn->fd();
    ucp_ep_h ucp_ep;
    ucp_ep_params_t ep_params;

    ceph_assert(fd > 0);

    ldout(cct, 20) << __func__ << " fd: " << fd << " ep addr: " << conn_req << dendl;

    ep_params.conn_request = conn_req;

    ep_params.field_mask = UCP_EP_PARAM_FIELD_ERR_HANDLER | UCP_EP_PARAM_FIELD_CONN_REQUEST;

    // ep_params.err_mode = UCP_ERR_HANDLING_MODE_PEER;| UCP_EP_PARAM_FIELD_ERR_HANDLING_MODE
    ep_params.err_handler.cb = ep_error_cb;
    ep_params.err_handler.arg = reinterpret_cast<void *>(this);

    ucs_status_t status = ucp_ep_create(ucp_worker, &ep_params, &ucp_ep);
    if (status == UCS_OK) {
        ldout(cct, 1) << __func__ << " fd: " << fd << " UCP ep: " << (void *)ucp_ep << " successfully created" <<
            dendl;
        conn->ucp_ep = ucp_ep;
        conn->set_connect_stat(UCXConnectedSocketImpl::CONNECTED);
        conn_map[ucp_ep] = conn;
        manager->notify(fd, EVENT_READABLE | EVENT_WRITABLE);
    } else {
        lderr(cct) << __func__ << " failed to create UCP endpoint fd: " << fd << " " << ucs_status_string(status) <<
            dendl;
        conn->set_connect_stat(UCXConnectedSocketImpl::DISCONNECTED);
        manager->notify(fd, EVENT_READABLE);
    }
}

ucs_status_t UCXWorker::am_recv_callback(void *arg, const void *header, size_t header_length, void *data, size_t length,
    const ucp_am_recv_param_t *param)
{
    UCXWorker *worker = reinterpret_cast<UCXWorker *>(arg);
    ceph_assert(worker->center.in_thread());
    if (!(param->recv_attr & UCP_AM_RECV_ATTR_FIELD_REPLY_EP)) {
        ldout(worker->cct, 0) << "UCP_AM_RECV_ATTR_FIELD_REPLY_EP not set" << dendl;
        return UCS_OK;
    }

    UCXConnectedSocketImpl *conn = worker->find_connection(param->reply_ep);
    if (!conn) {
        ldout(worker->cct, 0) << "Can not find ep in conn_map" << dendl;
        return UCS_OK;
    }
    if (!(param->recv_attr & (UCP_AM_RECV_ATTR_FLAG_DATA | UCP_AM_RECV_ATTR_FLAG_RNDV))) {
        ldout(worker->cct, 0) <<
            "Neither UCP_AM_RECV_ATTR_FIELD_DATA nor UCP_AM_RECV_ATTR_FLAG_RNDV is set, just wakeup fd " <<
            conn->fd() << dendl;
        return UCS_OK;
    }

    if (!conn->is_connected()) {
        ldout(worker->cct, 0) << "Received am from after connection closed " << conn->get_connect_stat() << " fd " <<
            conn->fd() << dendl;
        return UCS_OK;
    }
    MsgHeader msg = *reinterpret_cast<const MsgHeader *>(header);
    msg.ntoh();
    ldout(worker->cct, 20) << "got io (AM) message "
                           << "ver " << msg.ver << ", "
                           << "sn " << msg.seq << ", "
                           << "data len " << msg.data_len << ", "
                           << " length " << length << dendl;

    // ceph_assert(msg.data_len == length);
    if (param->recv_attr & UCP_AM_RECV_ATTR_FLAG_RNDV) {
        return conn->process_recv_request(worker->ucp_worker, data, length, msg);
    }

    if (param->recv_attr & UCP_AM_RECV_ATTR_FLAG_DATA) {
        bufferptr bptr(buffer::claim_buffer(length, (char *)data,
            make_deleter([worker, conn, data, length] { ucp_am_data_release(worker->ucp_worker, data); })));
        bufferlist bl;
        bl.append(std::move(bptr));
        // bufferlist bl = conn->create_bufferlist_with_hint(length, msg, true);

        // bl.append((char *)data, length);
        conn->insert_recv_buffers(msg.seq, bl, msg.data_len);
        ldout(worker->cct, 20) << " recv sn: " << msg.seq << ", current sn: " << conn->sn_recv << dendl;
        return UCS_INPROGRESS;
    }
    return UCS_OK;
}

void UCXWorker::accept_cb(ucp_conn_request_h conn_req, void *arg)
{
    UCXServerSocketImpl *server_socket = reinterpret_cast<UCXServerSocketImpl *>(arg);
    int server_fd = server_socket->fd();
    UCXWorker *worker = server_socket->get_worker();
    UCXServerSocketImpl *listern = worker->find_listener(server_fd);

    ceph_assert(listern == server_socket);
    server_socket->add_conn_request(conn_req);
    worker->manager->notify(server_fd, EVENT_READABLE);

    ldout(worker->cct, 20) << __func__ << " server_fd: " << server_fd << " UCP conn_req: " << (void *)conn_req << dendl;
}

int UCXWorker::listen(entity_addr_t &sa, unsigned addr_slot, const SocketOptions &opt, ServerSocket *sock)
{
    int server_fd;
    ucp_listener_params_t params;
    ceph_assert(center.in_thread());
    server_fd = manager->get_eventfd();
    UCXServerSocketImpl *p = new UCXServerSocketImpl(this, server_fd, sa, addr_slot);

    params.field_mask = UCP_LISTENER_PARAM_FIELD_SOCK_ADDR | UCP_LISTENER_PARAM_FIELD_CONN_HANDLER;

    params.sockaddr.addr = sa.get_sockaddr();
    params.sockaddr.addrlen = sizeof(struct sockaddr);

    params.conn_handler.cb = accept_cb;
    params.conn_handler.arg = reinterpret_cast<void *>(p);

    ucp_listener_h ucp_listener;

    /* Create a listener on the server side to listen on the given address. */
    ucs_status_t status = ucp_listener_create(ucp_worker, &params, &ucp_listener);
    if (UCS_OK != status) {
        delete p;
        manager->close(server_fd);
        lderr(cct) << __func__ << " Failed to create UCP listener with status: " << ucs_status_string(status) << dendl;
        return -EBUSY;
    }

    ucp_listener_attr_t listen_attr;
    listen_attr.field_mask = UCP_LISTENER_ATTR_FIELD_SOCKADDR;

    status = ucp_listener_query(ucp_listener, &listen_attr);
    if (status != UCS_OK) {
        delete p;
        manager->close(server_fd);
        lderr(cct) << __func__ << "<Failed to query UCP listener> to addr: " << sa.get_sockaddr() << " status: " <<
            ucs_status_string(status) << dendl;
        ucp_listener_destroy(ucp_listener);
        return -EIO;
    }

    ldout(cct, 0) << __func__ << " Created listner to addr: " << sa.get_sockaddr() << " server_fd: " << server_fd <<
        dendl;

    p->set_ucp_listener(ucp_listener);
    listen_map[server_fd] = p;
    *sock = ServerSocket(std::unique_ptr<ServerSocketImpl>(p));
    return 0;
}

int UCXWorker::connect(const entity_addr_t &peer_addr, const SocketOptions &opts, ConnectedSocket *sock)
{
    ceph_assert(center.in_thread());
    int fd = manager->get_eventfd();

    ucp_ep_params_t ep_params;
    ep_params.flags = UCP_EP_PARAMS_FLAGS_CLIENT_SERVER;
    ep_params.field_mask = UCP_EP_PARAM_FIELD_FLAGS | UCP_EP_PARAM_FIELD_SOCK_ADDR | UCP_EP_PARAM_FIELD_ERR_HANDLER |
        UCP_EP_PARAM_FIELD_ERR_HANDLING_MODE;
    ep_params.err_mode = UCP_ERR_HANDLING_MODE_PEER;
    ep_params.err_handler.cb = ep_error_cb_client;
    ep_params.err_handler.arg = reinterpret_cast<void *>(this);

    ep_params.sockaddr.addr = peer_addr.get_sockaddr();
    ep_params.sockaddr.addrlen = sizeof(struct sockaddr);
    entity_addr_t addr = opts.connect_bind_addr;
    if (cct->_conf->ms_bind_before_connect && (!addr.is_blank_ip())) {
        addr.set_port(0);
        ep_params.field_mask |= UCP_EP_PARAM_FIELD_LOCAL_SOCK_ADDR;
        ep_params.local_sockaddr.addr = addr.get_sockaddr();
        ep_params.local_sockaddr.addrlen = addr.get_sockaddr_len();
    }
    ucp_ep_h ucp_ep;
    ucs_status_t status = ucp_ep_create(ucp_worker, &ep_params, &ucp_ep);
    if (UCS_OK != status) {
        manager->close(fd);
        lderr(cct) << __func__ << " creating UCP ep to addr failed addr: " << peer_addr.get_sockaddr() << " status: " <<
            ucs_status_string(status) << dendl;
        return -EIO;
    }
    UCXConnectedSocketImpl *conn = new UCXConnectedSocketImpl(this, fd);
    conn->ucp_ep = ucp_ep;
    conn->set_connect_stat(UCXConnectedSocketImpl::CONNECTED);
    conn_map[ucp_ep] = conn;
    ldout(cct, 0) << __func__ << " fd: " << conn->fd() << " UCP ep: " << (void *)ucp_ep << " to addr: " <<
        peer_addr.get_sockaddr() << " successfully created" << dendl;

    std::unique_ptr<UCXConnectedSocketImpl> csi(conn);
    *sock = ConnectedSocket(std::move(csi));

    return 0;
}

void UCXWorker::set_recv_handler(void)
{
    ucp_am_handler_param_t param;
    param.field_mask = UCP_AM_HANDLER_PARAM_FIELD_ID | UCP_AM_HANDLER_PARAM_FIELD_CB | UCP_AM_HANDLER_PARAM_FIELD_ARG |
        UCP_AM_HANDLER_PARAM_FIELD_FLAGS;
    param.id = UCXConnectedSocketImpl::AM_ID;
    param.cb = UCXWorker::am_recv_callback;
    param.arg = this;
    param.flags = UCP_AM_FLAG_PERSISTENT_DATA;
    ceph_assert(UCS_OK == ucp_worker_set_am_recv_handler(ucp_worker, &param));
}

void UCXWorker::initialize()
{
    ucs_status_t status;
    ucp_worker_params_t params;
    params.thread_mode = UCS_THREAD_MODE_MULTI;
    params.field_mask = UCP_WORKER_PARAM_FIELD_THREAD_MODE;
    UCXEventDriver *driver = dynamic_cast<UCXEventDriver *>(center.get_driver());
    manager = driver->get_manager();
    status = ucp_worker_create(ucp_context, &params, &ucp_worker);
    if (UCS_OK != status) {
        lderr(cct) << __func__ << " failed to create UCP worker " << dendl;
        ceph_abort();
    }

    status = ucp_worker_get_efd(ucp_worker, &ucp_fd);
    if (UCS_OK != status) {
        lderr(cct) << __func__ << " failed to obtain UCP worker event fd " << dendl;
        ceph_abort();
    }

    driver->driver_init(ucp_worker, ucp_fd);

    set_recv_handler();
    center.create_file_event(ucp_fd, EVENT_READABLE, event_handler);
}

void UCXWorker::destroy()
{
    if (ucp_fd > 0) {
        ldout(cct, 20) << __func__ << dendl;

        ceph_assert(nullptr != ucp_worker);

        center.delete_file_event(ucp_fd, EVENT_READABLE);
        delete event_handler;
        event_handler = nullptr;
        ucp_worker_destroy(ucp_worker);
        ucp_worker = nullptr;
        ucp_fd = -1;
    }
}

#undef dout_prefix
#define dout_prefix *_dout << "UCXStack "

void UCXStack::ucx_contex_create()
{
    ucp_params_t params;
    ucs_status_t status;

    ucp_config_t *ucp_config;

    if (nullptr != ucp_context) {
        return;
    }

    ldout(cct, 10) << __func__ << " UCX contex is going to be created..." << dendl;

    int rc = setenv("UCX_NET_DEVICES", cct->_conf->ms_async_ucx_device.c_str(), 1);
    if (rc) {
        lderr(cct) << __func__ << " failed to export UCX_CEPH_NET_DEVICES. Application aborts." << dendl;
        ceph_abort();
    }

    rc = setenv("UCX_TLS", cct->_conf->ms_async_ucx_tls.c_str(), 1);
    if (rc) {
        lderr(cct) << __func__ << " failed to export UCX_CEPH_TLS. Application aborts." << dendl;
        ceph_abort();
    }

    std::string addr = cct->_conf.get_val<std::string>("rdma_cm_source_addr");
    if (!addr.empty()) {
        rc = setenv("UCX_RDMA_CM_SOURCE_ADDRESS", addr.c_str(), 1);
        if (rc) {
            lderr(cct) << __func__ << " failed to export UCX_RDMA_CM_SOURCE_ADDRESS. Application aborts." << dendl;
            ceph_abort();
        }
        lderr(cct) << __func__ << " export UCX_RDMA_CM_SOURCE_ADDRESS = " << addr << dendl;
    }

    status = ucp_config_read("", nullptr, &ucp_config);
    if (UCS_OK != status) {
        lderr(cct) << __func__ << "failed to read UCP config" << dendl;
        ceph_abort();
    }

    memset(&params, 0, sizeof(params));
    params.field_mask = UCP_PARAM_FIELD_FEATURES | UCP_PARAM_FIELD_REQUEST_SIZE | UCP_PARAM_FIELD_REQUEST_INIT |
        UCP_PARAM_FIELD_REQUEST_CLEANUP | UCP_PARAM_FIELD_TAG_SENDER_MASK | UCP_PARAM_FIELD_MT_WORKERS_SHARED;

    params.features = UCP_FEATURE_WAKEUP | UCP_FEATURE_AM;

    params.mt_workers_shared = 1;
    params.tag_sender_mask = -1;
    params.request_size = sizeof(UCXReqDescr);

    params.request_init = UCXConnectedSocketImpl::request_init;
    params.request_cleanup = UCXConnectedSocketImpl::request_cleanup;

    status = ucp_init(&params, ucp_config, &ucp_context);
    ucp_config_release(ucp_config);

    if (UCS_OK != status) {
        lderr(cct) << __func__ << "failed to init UCP context" << dendl;
        ceph_abort();
    }

    ucp_context_print_info(ucp_context, stdout);
}

UCXStack::UCXStack(CephContext *cct, const string &t) : NetworkStack(cct, t)
{
    ldout(cct, 10) << __func__ << " constructing UCX stack "
                   << " with " << get_num_worker() << " workers " << dendl;
}

UCXStack::~UCXStack()
{
    if (nullptr != ucp_context) {
        ucp_cleanup(ucp_context);
    }
}

void UCXStack::spawn_worker(unsigned i, std::function<void()> &&func)
{
    ucx_contex_create();
    UCXWorker *w = dynamic_cast<UCXWorker *>(get_worker(i));
    w->set_ucp_context(ucp_context);
    threads.resize(i + 1);
    threads[i] = std::thread(func);
}

void UCXStack::join_worker(unsigned i)
{
    ceph_assert(threads.size() > i && threads[i].joinable());
    threads[i].join();
}

