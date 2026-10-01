#pragma once

#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "../config/config.h"
#include "../iomanager/iomanager.h"
#include "../socket/address.h"
#include "../socket/socket.h"
#include "../util/noncopyable.h"

namespace chen {

struct UdpServerConf {
    typedef std::shared_ptr<UdpServerConf> ptr;

    std::vector<std::string> address;
    int timeout = 1000 * 2 * 60;
    std::string id;
    std::string type = "udp";
    std::string name;
    std::string io_worker;
    std::string process_worker;
    uint64_t recv_buf_size = 64 * 1024;
    std::map<std::string, std::string> args;

    bool isValid() const {
        return !address.empty();
    }

    bool operator==(const UdpServerConf& oth) const {
        return address == oth.address
            && timeout == oth.timeout
            && id == oth.id
            && type == oth.type
            && name == oth.name
            && io_worker == oth.io_worker
            && process_worker == oth.process_worker
            && recv_buf_size == oth.recv_buf_size
            && args == oth.args;
    }
};

template <>
class LexicalCast<std::string, UdpServerConf> {
public:
    UdpServerConf operator()(const std::string& v) {
        YAML::Node node = YAML::Load(v);
        UdpServerConf conf;
        conf.id = node["id"].as<std::string>(conf.id);
        conf.type = node["type"].as<std::string>(conf.type);
        conf.timeout = node["timeout"].as<int>(conf.timeout);
        conf.name = node["name"].as<std::string>(conf.name);
        conf.io_worker = node["io_worker"].as<std::string>();
        conf.process_worker = node["process_worker"].as<std::string>();
        conf.recv_buf_size = node["recv_buf_size"].as<uint64_t>(conf.recv_buf_size);
        conf.args = LexicalCast<std::string, std::map<std::string, std::string>>()(node["args"].as<std::string>(""));
        if (node["address"].IsDefined()) {
            for (size_t i = 0; i < node["address"].size(); ++i) {
                conf.address.push_back(node["address"][i].as<std::string>());
            }
        }
        return conf;
    }
};

template <>
class LexicalCast<UdpServerConf, std::string> {
public:
    std::string operator()(const UdpServerConf& conf) {
        YAML::Node node;
        node["id"] = conf.id;
        node["type"] = conf.type;
        node["timeout"] = conf.timeout;
        node["name"] = conf.name;
        node["io_worker"] = conf.io_worker;
        node["process_worker"] = conf.process_worker;
        node["recv_buf_size"] = conf.recv_buf_size;
        node["args"] = YAML::Load(LexicalCast<std::map<std::string, std::string>, std::string>()(conf.args));
        for (auto& i : conf.address) {
            node["address"].push_back(i);
        }
        std::stringstream ss;
        ss << node;
        return ss.str();
    }
};

class UdpServer : public std::enable_shared_from_this<UdpServer>, Noncopyable {
public:
    typedef std::shared_ptr<UdpServer> ptr;

    UdpServer(IOManager* worker = IOManager::GetThis(), IOManager* io_worker = IOManager::GetThis());

    virtual ~UdpServer();

    virtual bool bind(Address::ptr addr);
    virtual bool bind(const std::vector<Address::ptr>& addrs, std::vector<Address::ptr>& fails);

    virtual bool start();
    virtual void stop();

    virtual void clearRegistrations() {}

    virtual void handleRecv(Socket::ptr sock, const char* data, size_t len, Address::ptr from);

    IOManager* getWorker() const { return m_worker; }
    IOManager* getIOWorker() const { return m_ioWorker; }

    std::string getName() const { return m_name; }
    virtual void setName(const std::string& v) { m_name = v; }

    const std::string& getType() const { return m_type; }
    void setType(const std::string& v) { m_type = v; }

    bool isStop() const { return m_isStop; }

    size_t getRecvBufSize() const { return m_recvBufSize; }
    void setRecvBufSize(size_t v) { m_recvBufSize = v; }

    UdpServerConf::ptr getConf() const { return m_conf; }
    void setConf(UdpServerConf::ptr v) { m_conf = v; }
    void setConf(const UdpServerConf& v);

    std::string toString(const std::string& prefix);

protected:
    virtual void startRecv(Socket::ptr sock);

    IOManager* m_worker;
    IOManager* m_ioWorker;
    std::vector<Socket::ptr> m_socks;
    std::string m_name;
    std::string m_type = "udp";
    bool m_isStop;
    size_t m_recvBufSize = 64 * 1024;
    UdpServerConf::ptr m_conf;
};

class UdpServerFactory {
public:
    using Creator = std::function<UdpServer::ptr(const UdpServerConf& conf, IOManager* process_worker, IOManager* io_worker)>;

    static bool Register(const std::string& type, Creator cb);
    static bool Unregister(const std::string& type);
    static UdpServer::ptr Create(const UdpServerConf& conf, IOManager* process_worker, IOManager* io_worker);
    static std::vector<std::string> ListTypes();
};

}
