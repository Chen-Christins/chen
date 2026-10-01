/**
 * @file rock_server.h
 * @brief RockServer
 * @author Christins (chen.christins@icloud.com)
 * @date 2026-05-26
 * @copyright GPL-3.0
 */
#pragma once

#include "../tcp/tcp_server.h"

namespace chen {

class RockServer : public TcpServer {
public:
    typedef std::shared_ptr<RockServer> ptr;
    RockServer(const std::string& type = "rock"
               ,IOManager* worker = IOManager::GetThis()
               ,IOManager* io_worker = IOManager::GetThis()
               ,IOManager* accept_worker = IOManager::GetThis());

protected:
    virtual void handleClient(Socket::ptr client) override;
};


} // namespace chen
