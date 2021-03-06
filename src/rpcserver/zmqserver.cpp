/*
 * Copyright 2019 Peifeng Yu <peifeng@umich.edu>
 * 
 * This file is part of Salus
 * (see https://github.com/SymbioticLab/Salus).
 * 
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * 
 *    http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "zmqserver.h"

#include "rpcservercore.h"
#include "platform/logging.h"
#include "platform/signals.h"
#include "platform/thread_annotations.h"
#include "utils/protoutils.h"

#include "protos.h"

#include <functional>
#include <chrono>
#include <iostream>

using namespace std::literals::chrono_literals;

namespace {
constexpr const char kBeAddr[] = "inproc://backend";
} // namespace

ZmqServer::ZmqServer()
    : m_zmqCtx(1)
    , m_keepRunning(false)
    , m_pLogic(std::make_unique<RpcServerCore>())
    , m_sendQueue(128)
{
}

ZmqServer::~ZmqServer()
{
    requestStop();
}

void ZmqServer::start(const std::string& address)
{
    if (m_keepRunning) {
        LOG(ERROR) << "ZmqServer already started.";
        return;
    }

    m_keepRunning = true;
    m_recvThread = std::make_unique<std::thread>(std::bind(&ZmqServer::proxyRecvLoop, this, address));

    m_sendThread = std::make_unique<std::thread>(std::bind(&ZmqServer::sendLoop, this));
}

bool ZmqServer::pollWithCheck(const std::vector<zmq::pollitem_t> &items, long timeout)
{
    try {
        zmq::poll(items, timeout);
    } catch (zmq::error_t &err) {
        switch (err.num()) {
        case ETIMEDOUT:
            return true;
        case EINTR:
        case ETERM:
            m_keepRunning = false;
            return false;
        default:
            LOG(ERROR) << "Exiting serving due to error while polling: " << err;
            m_keepRunning = false;
            return false;
        }
    }
    return true;
}

void ZmqServer::proxyRecvLoop(const std::string &feAddr)
{
    VLOG(2) << "Started recving and sending loop";
    zmq::socket_t m_frontend_sock(m_zmqCtx, zmq::socket_type::router);
    zmq::socket_t m_backend_sock(m_zmqCtx, zmq::socket_type::pair);
    m_frontend_sock.setsockopt(ZMQ_ROUTER_MANDATORY, 1);
    m_frontend_sock.setsockopt(ZMQ_ROUTER_HANDOVER, 1);

    try {
        VLOG(2) << "Binding frontend socket to address: " << feAddr;
        m_frontend_sock.bind(feAddr);

        VLOG(2) << "Binding backend socket to address: " << kBeAddr;
        m_backend_sock.bind(kBeAddr);
    } catch (zmq::error_t &err) {
        LOG(FATAL) << "Error while binding sockets: " << err;
        // re-throw to stop the process
        throw;
    }

    // set up pulling.
    // we are interested in POLLIN and POLLOUT on m_frontend_sock, and POLLIN out m_backend_sock.
    // messages received on m_frontend_sock are directly dispatched using m_pLogic,
    // messages received on m_backend_sock are forwarded to m_frontend_sock.

    static std::vector<zmq::pollitem_t> pollin_events {
        {m_frontend_sock, 0, ZMQ_POLLIN, 0},
        {m_backend_sock, 0, ZMQ_POLLIN, 0},
    };
    static std::vector<zmq::pollitem_t> all_events {
        {m_frontend_sock, 0, ZMQ_POLLIN | ZMQ_POLLOUT, 0},
        {m_backend_sock, 0, ZMQ_POLLIN, 0},
    };
    std::vector<zmq::pollitem_t> *wait_events = &pollin_events;

    bool canSendOut = false;
    bool needSendOut = false;
    bool shouldDispatch = false;
    while (m_keepRunning) {
        // first blocking wait on pollin (read) events
        VLOG(2) << "Blocking pool on " << (wait_events == &pollin_events ? "pollin events" : "all events");
        if (!pollWithCheck(*wait_events, -1)) {
            break;
        }
        // something happened, so we poll w/o waiting on all_events
        // to set events in all_events
        VLOG(2) << "Non-blocking poll on all events";
        if (!pollWithCheck(all_events, 0)) {
            break;
        }

        // process events
        for (auto item : all_events) {
            if (item.socket == m_frontend_sock) {
                shouldDispatch = (item.revents & ZMQ_POLLIN) != 0;
                canSendOut = (item.revents & ZMQ_POLLOUT) != 0;
            } else if (item.socket == m_backend_sock) {
                needSendOut = (item.revents & ZMQ_POLLIN) != 0;
            }
        }
        VLOG(3) << "Events summary: shouldDispatch=" << shouldDispatch
                << ", canSendOut=" << canSendOut << ", needSendOut=" << needSendOut;

        // process dispatch if any
        if (shouldDispatch) {
            dispatch(m_frontend_sock);
            shouldDispatch = false;
        }

        // forward any send message
        if (needSendOut && canSendOut) {
            VLOG(2) << "Forwarding message out";
            zmq::message_t msg;
            bool more = false;
            while (true) {
                try {
                    m_backend_sock.recv(&msg);
                    VLOG(2) << "Forwarding message part: " << msg;
                    more = m_backend_sock.getsockopt<int64_t>(ZMQ_RCVMORE);
                    m_frontend_sock.send(msg, more ? ZMQ_SNDMORE : 0);
                    if (!more)
                        break;
                } catch (zmq::error_t &err) {
                    LOG(ERROR) << "Dropping message part while sending out due to error: " << err;
                    if (!more)
                        break;
                }
            }
            needSendOut = canSendOut = false;
            wait_events = &pollin_events;
        } else if (needSendOut) {
            // should also wait for POLLOUT on m_frontend_sock
            wait_events = &all_events;
        } else if (canSendOut) {
            // only wait for POLLIN on sockets
            wait_events = &pollin_events;
        }
    }
}

void ZmqServer::dispatch(zmq::socket_t &sock)
{
    MultiPartMessage identities;
    zmq::message_t evenlop;
    zmq::message_t body;
    try {
        VLOG(2) << "==============================================================";
        // First receive all identity frames added by ZMQ_ROUTER socket
        identities->emplace_back();
        sock.recv(&identities->back());
        VLOG(2) << "Received identity frame " << (identities->size() - 1) << ": " << identities->back();
        // Identity frames stop at an empty message
        // ZMQ_RCVMORE is a int64_t according to doc, not a bool
        while (identities->back().size() != 0 && sock.getsockopt<int64_t>(ZMQ_RCVMORE)) {
            identities->emplace_back();
            sock.recv(&identities->back());
            VLOG(2) << "Received identity frame " << (identities->size() - 1) << ": " << identities->back();
        }
        if (!sock.getsockopt<int64_t>(ZMQ_RCVMORE)) {
            LOG(ERROR) << "Skipped one iteration due to no body message part found after identity frames";
            return;
        }
        // Now receive our message
        sock.recv(&evenlop);
        VLOG(2) << "Received evenlop frame: " << evenlop;
        if (!sock.getsockopt<int64_t>(ZMQ_RCVMORE)) {
            LOG(ERROR) << "Skipped one iteration due to no body message part found after identity frames";
            return;
        }
        // NOTE: we only assume there's only one body part.
        sock.recv(&body);
        VLOG(2) << "Received body frame: " << body;
    } catch (zmq::error_t &err) {
        LOG(ERROR) << "Skipped one iteration due to error while receiving: " << err;
        return;
    }

    m_iopool.post([this, identities{std::move(identities)}, evenlop{std::move(evenlop)}, body{std::move(body)}]() mutable {
        auto pEvenlop = sstl::createMessage<executor::EvenlopDef>("executor.EvenlopDef", evenlop.data(), evenlop.size());
        if (!pEvenlop) {
            LOG(ERROR) << "Skipped one iteration due to malformatted request evenlop received.";
            return;
        }
        VLOG(2) << "Received request evenlop: " << *pEvenlop;

        // step 1. replace the first frame in identity with the requested identity and make a sender
        if (!pEvenlop->recvidentity().empty()) {
            identities->front().rebuild(pEvenlop->recvidentity().data(), pEvenlop->recvidentity().size());
        }
        auto sender = std::make_shared<SenderImpl>(*this, pEvenlop->seq(), std::move(identities));

        // step 2. create request object
        auto pRequest = sstl::createMessage(pEvenlop->type(), body.data(), body.size());
        if (!pRequest) {
            LOG(ERROR) << "Skipped one iteration due to malformatted request received.";
            return;
        }
        VLOG(2) << "Received request body byte array size " << body.size();

        // step 3. dispatch
        m_pLogic->dispatch(std::move(sender), *pEvenlop, *pRequest);
    });
}

ZmqServer::SenderImpl::SenderImpl(ZmqServer &server, uint64_t seq, MultiPartMessage &&identities)
    : m_server(server)
    , m_identities(std::move(identities))
    , m_seq(seq)
{
}

void ZmqServer::SenderImpl::sendMessage(ProtoPtr &&msg)
{
    MultiPartMessage parts;
    parts->emplace_back(msg->ByteSizeLong());
    auto &reply = parts->back();
    msg->SerializeToArray(reply.data(), reply.size());
    sendMessage(msg->GetTypeName(), std::move(parts));
}

void ZmqServer::SenderImpl::sendMessage(const std::string &typeName, MultiPartMessage &&msg)
{
    auto parts = m_identities.clone();
    // step 4.1. unused parts of evenlop is unset to save a few bytes on the wire,
    executor::EvenlopDef evenlop;
    evenlop.set_seq(m_seq);
    evenlop.set_type(typeName);
    parts->emplace_back(evenlop.ByteSizeLong());
    evenlop.SerializeToArray(parts->back().data(), parts->back().size());

    // step 4.2. append actual message
    VLOG(2) << "Response proto object have size " << msg.totalSize() << " with evenlop " << evenlop;
    parts.merge(std::move(msg));

    m_server.sendMessage(std::move(parts));
}

uint64_t ZmqServer::SenderImpl::sequenceNumber() const
{
    return m_seq;
}

void ZmqServer::sendMessage(MultiPartMessage &&parts)
{
    m_sendQueue.push({parts.release()});
}

void ZmqServer::sendLoop()
{
    salus::threading::set_thread_name("ZmqSendLoop");

    zmq::socket_t sock(m_zmqCtx, zmq::socket_type::pair);
    sock.connect(kBeAddr);
    VLOG(2) << "Sending loop started";

    while (m_keepRunning) {
        SendItem item;
        if(!m_sendQueue.pop(item)) {
            std::this_thread::sleep_for(1ms);
            continue;
        }
        // Wrap the address in smart pointer immediately so we won't risk memory leak.
        MultiPartMessage parts(item.p_parts);
        try {
            for (size_t i = 0; i != parts->size() - 1; ++i) {
                auto &msg = parts->at(i);
                sock.send(msg, ZMQ_SNDMORE);
            }
            sock.send(parts->back());
            VLOG(2) << "Response sent on internal socket";
        } catch (zmq::error_t &err) {
            LOG(ERROR) << "Sending error: " << err;
        }
    }
}

void ZmqServer::requestStop()
{
    if (!m_keepRunning) {
        return;
    }

    VLOG(2) << "Stopping ZMQ context";
    m_keepRunning = false;
    m_zmqCtx.close();
}

void ZmqServer::join()
{
    // Handle SIGINT and SIGTERM
    // so the user can stop the server from terminal
    for (auto [signo, action] = signals::waitForTerminate(); action != signals::SignalAction::Exit;) {
        UNUSED(signo);
    }

    LOG(INFO) << "Stopping ZmqServer";
    requestStop();

    if (m_sendThread && m_sendThread->joinable()) {
        m_sendThread->join();
    }

    if (m_recvThread && m_recvThread->joinable()) {
        m_recvThread->join();
    }

    LOG(INFO) << "ZmqServer stopped";
}
