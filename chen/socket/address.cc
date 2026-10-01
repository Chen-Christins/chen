#include "address.h"

#include <cstring>
#include <ifaddrs.h>
#include <netdb.h>

#include <sys/types.h>

#include "../log/log.h"
#include "../util/endian.h"

namespace chen {

static Logger::ptr logger = LOG_NAME("system");

/**
 * @brief 创造子网掩码
 * @details 这里将前bits位置为0，后面置为1，比如一个
 *          正常的子网掩码是 255.255.255.0，在没有取反之前是
 *          0.0.0.255 然后通过~将其变为了 255.255.255.0
 * @tparam T 当前类型的字节数
 * @param bits 二进制中1的个数
 */
template <class T>
static T CreateMask(uint32_t bits) {
    return ~((1 << (sizeof(T) * 8 - bits)) - 1);
}

/**
 * @brief 计算value的二进制下1的个数
 * @tparam T 类型
 * @param value 值
 */
template <class T>
static uint32_t CountBytes(T value) {
    return __builtin_popcount(value);
}

// 通过sockaddr创建地址
Address::ptr Address::Create(const sockaddr* addr, socklen_t addrlen) {
    if (addr == nullptr) {
        return nullptr;
    }
    Address::ptr result;
    switch (addr->sa_family) {
    case AF_INET:
        result.reset(new IPv4Address(*(const sockaddr_in*)addr));
        break;
    case AF_INET6:
        result.reset(new IPv6Address(*(const sockaddr_in6*)addr));
        break;
    default:
        result.reset(new UnknownAddress(*addr));
        break;
    }
    return result;
}

// 通过host地址返回所有Address
bool Address::Lookup(std::vector<Address::ptr>& result, const std::string& host, int family, int type, int protocol) {
    addrinfo hints, *results, *next;
    hints.ai_flags = 0;
    hints.ai_family = family;
    hints.ai_socktype = type;
    hints.ai_protocol = protocol;
    hints.ai_addrlen = 0;
    hints.ai_canonname = NULL;
    hints.ai_next = NULL;
    hints.ai_addr = NULL;

    // 主机地址
    // ipv4: 127.0.0.1
    // ipv6: [fe80:0000:0001:0000:0440:44ff:1233:5678]
    std::string node;
    const char* service = NULL;

    // 检查ipv6 address service [address]:
    if (!host.empty() && host[0] == '[') {
        // 返回 ] 字符之后的整个字符串，否则返回 NULL
        const char* endipv6 = (const char*)memchr(host.c_str() + 1, ']', host.size() - 1);
        if (endipv6) {
            if (*(endipv6 + 1) == ':') { // 拿到端口号
                service = endipv6 + 2;
            }// 然后把 ipv6 的host地址拿到
            node = host.substr(1, endipv6 - host.c_str() - 1);
        }
    }

    // 检查 node service ，如果不是ipv6，那么就是ipv4
    if (node.empty()) {
        // 找:后面的端口号，或者协议名称ftp，http等
        service = (const char*)memchr(host.c_str(), ':', host.size());
        // 拿到了:就把host地址取出来
        if (service) {
            if (!memchr(service + 1, ':', host.c_str() + host.size() - service - 1)) {
                // 取地址 
                node = host.substr(0, service - host.c_str());
                ++service;
            }
        }
    }
    // 如果都不是
    if (node.empty()) {
        node = host;
    }
    int error = getaddrinfo(node.c_str(), service, &hints, &results);
    if (error) {
        ERROR(logger) << "Address::Lookup getaddress(" << host
            << ", " << family << ", " << type << ") error = " << error
            << ", errstr" << gai_strerror(error);
        return false;
    }
    // results指向头节点
    next = results;
    while (next) {
        result.push_back(Create(next->ai_addr, next->ai_addrlen));
        next = next->ai_next;
    }

    freeaddrinfo(results);
    return !result.empty(); 
}

// 通过host返回任意地址Address，默认返回第一个
Address::ptr Address::LookupAny(const std::string& host, int family, int type, int protocol) {
    std::vector<Address::ptr> result;
    if (Lookup(result, host, type, protocol)) {
        return result[0];
    }
    return nullptr;
}

// 通过host返回任意IPAddress
std::shared_ptr<IPAddress> Address::LookupAnyIPAddress(const std::string &host, int family, int type, int protocol) {
    std::vector<Address::ptr> result;
    if (Lookup(result, host, type, protocol)) {
        for (auto& i : result) {
            // 将基类转换为派生类, 智能指针用dynamic_pointer_cast，指针就用dynamic_cast
            IPAddress::ptr v = std::dynamic_pointer_cast<IPAddress>(i);
            if (v) {
                return v;
            }
        }
    }
    return nullptr;
}

/* 
    struct ifaddrs {
        struct ifaddrs  *ifa_next;    指向链表中下一个struct ifaddr结构 
        char            *ifa_name;    网络接口名 
        unsigned int     ifa_flags;   网络接口标志
        struct sockaddr *ifa_addr;    指向一个包含网络地址的sockaddr结构
        struct sockaddr *ifa_netmask; 指向一个包含网络掩码的结构
        union {
            struct sockaddr *ifu_broadaddr;
                            如果(ifa_flags&IFF_BROADCAST)有效，ifu_broadaddr指向一个包含广播地址的结构
            struct sockaddr *ifu_dstaddr;
                            如果(ifa_flags&IFF_POINTOPOINT)有效，ifu_dstaddr指向一个包含p2p目的地址的结构
        } ifa_ifu;
    #define              ifa_broadaddr ifa_ifu.ifu_broadaddr
    #define              ifa_dstaddr   ifa_ifu.ifu_dstaddr
        void            *ifa_data;    指向一个缓冲区，其中包含地址族私有数据。没有私有数据则为NULL
    };
 */
// 返回本机的所有网卡的<网卡名，地址，子网掩码位数>
bool Address::GetInterfaceAddresses(std::multimap<std::string, std::pair<Address::ptr, uint32_t>>& result, int family) {
    /* 这个ifaddrs也是一个链表结构 */
    struct ifaddrs *next, *results;
    /* 创建一个链表，链表的每个节点都是一个ifaddrs */
    /* 返回0成功，其他失败 */
    if (getifaddrs(&results) != 0) {
        ERROR(logger) << "Address::GetInterfaceAddress getifaddrs "
            << " err=" << errno << " errstr=" << strerror(errno);
        return false;
    }

    try {
        for (next = results; next; next = next->ifa_next) {
            Address::ptr addr;
            uint32_t prefix_len = ~0u;
            if (family != AF_UNSPEC && family != next->ifa_addr->sa_family) {
                continue ;
            }
            switch (next->ifa_addr->sa_family) {
            case AF_INET:
                {
                    addr = Create(next->ifa_addr, sizeof(sockaddr_in));
                    uint32_t netmask = ((sockaddr_in*)next->ifa_netmask)->sin_addr.s_addr;
                    prefix_len = CountBytes(netmask);
                }
                break;
            case AF_INET6:
                {
                    addr = Create(next->ifa_addr, sizeof(sockaddr_in6));
                    in6_addr& netmask = ((sockaddr_in6*)next->ifa_netmask)->sin6_addr;
                    prefix_len = 0;
                    for (int i = 0; i < 16; ++i) {
                        prefix_len += CountBytes(netmask.s6_addr[i]);
                    }
                }
                break;
            default:
                break;
            }
            if (addr) {
                result.insert(std::pair(next->ifa_name, std::pair(addr, prefix_len)));
            }
        }
    } catch (...) {
        ERROR(logger) << "Address::GetInterfaceAddress exception";
        freeifaddrs(results);
        return false;
    }
    freeifaddrs(results);
    return !result.empty();
}

// 返回指定网卡的<网卡地址，子网掩码位数>
bool Address::GetInterfaceAddresses(std::vector<std::pair<Address::ptr, uint32_t>>& result, const std::string& iface, int family) {
    if (iface.empty() || iface == "*") {
        if (family == AF_INET || family == AF_UNSPEC) {
            result.push_back(std::pair(Address::ptr(new IPv4Address()), 0u));
        }
        if (family == AF_INET6 || family == AF_UNSPEC) {
            result.push_back(std::pair(Address::ptr(new IPv6Address()), 0u));
        }
        return true;
    }

    std::multimap<std::string, std::pair<Address::ptr, uint32_t>> results;
    if (!GetInterfaceAddresses(results, family)) {
        return false;
    }

    auto its = results.equal_range(iface);
    while (its.first != its.second) {
        result.push_back(its.first->second);
        ++its.first;
    }
    return !result.empty();
}

// 获取协议族
int Address::getFamily() const {
    return getAddr()->sa_family;
}

std::string Address::toString() const {
    std::stringstream ss;
    insert(ss);
    return ss.str();
}

bool Address::operator==(const Address& rhs) const {
    return getAddrLen() == rhs.getAddrLen()
        && memcmp(getAddr(), rhs.getAddr(), getAddrLen()) == 0;
}

bool Address::operator!=(const Address& rhs) const {
    return !(*this == rhs);
}

bool Address::operator<(const Address& rhs) const {
    socklen_t minlen = std::min(getAddrLen(), rhs.getAddrLen());
    int result = memcmp(getAddr(), rhs.getAddr(), minlen);
    if (result < 0) {
        return true;
    } else if (result > 0) {
        return false;
    } else if (getAddrLen() < rhs.getAddrLen()) {
        return true;
    }
    return false;
}

IPAddress::ptr IPAddress::Create(const char* address, uint16_t port) {
    addrinfo hints, *results;
    memset(&hints, 0, sizeof(addrinfo));
    // 此标志表示调用中的节点名必须是一个数字地址字符串
    hints.ai_flags = AI_NUMERICHOST;
    // 不指定协议类型
    hints.ai_family = AF_UNSPEC;

    int error = getaddrinfo(address, NULL, &hints, &results);
    if (error) {
        DEBUG(logger) << "IPAddress::Create(" << address
            << ", " << port << ") error=" << error
            << " errno=" << errno << " errstr=" << strerror(errno);
        return nullptr;
    }

    try {
        IPAddress::ptr result = std::dynamic_pointer_cast<IPAddress>(
            Address::Create(results->ai_addr, (socklen_t)results->ai_addrlen));
        if (result) {
            result->setPort(port);
        }
        freeaddrinfo(results);
        return result;
    } catch (...) {
        freeaddrinfo(results);
        return nullptr;
    }
}

IPv4Address::ptr IPv4Address::Create(const char* address, uint16_t port) {
    IPv4Address::ptr rt(new IPv4Address);
    rt->m_addr.sin_port = byteswapOnLittleEndian(port);
    // 将ip转换为二进制的形式
    int result = inet_pton(AF_INET, address, &rt->m_addr.sin_addr);
    if (result <= 0) {
        ERROR(logger) << "IPv4Address::Create(" << address << ", "
            << port << ") rt=" << result << " errno=" << errno 
            << " errstr=" << strerror(errno);
        return nullptr;
    }
    return rt;
}

IPv4Address::IPv4Address(const sockaddr_in& address) {
    m_addr = address;
}

IPv4Address::IPv4Address(uint32_t address, uint16_t port) {
    memset(&m_addr, 0, sizeof(m_addr));
    m_addr.sin_family = AF_INET;
    m_addr.sin_addr.s_addr = byteswapOnLittleEndian(address);
    m_addr.sin_port = byteswapOnLittleEndian(port);
}

const sockaddr* IPv4Address::getAddr() const {
    return (sockaddr*)&m_addr;
}

sockaddr* IPv4Address::getAddr() {
    return (sockaddr*)&m_addr;
}

socklen_t IPv4Address::getAddrLen() const {
    return sizeof(m_addr);
}

std::ostream& IPv4Address::insert(std::ostream& os) const {
    uint32_t addr = byteswapOnLittleEndian(m_addr.sin_addr.s_addr);
    os << ((addr >> 24) & 0xff) << "."
       << ((addr >> 16) & 0xff) << "."
       << ((addr >> 8) & 0xff) << "."
       << (addr & 0xff);
    os << ": " << byteswapOnLittleEndian(m_addr.sin_port);
    return os;
}

/* 在创建广播地址这里就要将 255.255.255.0 变成 0.0.0.255
 * 然后将最后一个网段给全部按位或 | ，然后就得到了广播地址，例如 
 * 192.168.0.255
 */
IPAddress::ptr IPv4Address::broadcastAddress(uint32_t prefix_len) {
    if (prefix_len > 32) {
        return nullptr;
    }

    sockaddr_in baddr(m_addr);
    baddr.sin_addr.s_addr |= ~byteswapOnLittleEndian(
        CreateMask<uint32_t>(prefix_len));
    
    return IPv4Address::ptr(new IPv4Address(baddr));
}


/* 例如一个ip地址是 192.168.0.0，它的 子网掩码是 255.255.255.0
 * 那么它的网络地址就是每一位互相进行按位与&，然后得到网络地址
 * 192.168.0.0
 */
IPAddress::ptr IPv4Address::networkAddress(uint32_t prefix_len) {
    if (prefix_len > 32) {
        return nullptr;
    }

    sockaddr_in naddr(m_addr);
    naddr.sin_addr.s_addr &= byteswapOnLittleEndian(
        CreateMask<uint32_t>(prefix_len));
    
    return IPv4Address::ptr(new IPv4Address(naddr));
}

/* 
 * 这里其实在创建的时候就已经把子网掩码给做出来了
 */
IPAddress::ptr IPv4Address::subnetMask(uint32_t prefix_len) {
    sockaddr_in subnet;
    memset(&subnet, 0, sizeof(subnet));
    subnet.sin_family = AF_INET;
    subnet.sin_addr.s_addr = byteswapOnLittleEndian(CreateMask<uint32_t>(prefix_len));
    return IPv4Address::ptr(new IPv4Address(subnet));
}

uint32_t IPv4Address::getPort() const {
    return byteswapOnLittleEndian(m_addr.sin_port);
}

void IPv4Address::setPort(uint16_t v) {
    m_addr.sin_port = byteswapOnLittleEndian(v);
}

IPv6Address::ptr IPv6Address::Create(const char* address, uint16_t port) {
    IPv6Address::ptr rt(new IPv6Address);
    rt->m_addr.sin6_port = byteswapOnLittleEndian(port);
    // 将ip转换为二进制的形式
    int result = inet_pton(AF_INET6, address, &rt->m_addr.sin6_addr);
    if (result <= 0) {
        ERROR(logger) << "IPv6Address::Create(" << address << ", "
            << port << ") rt=" << result << " errno=" << errno 
            << " errstr=" << strerror(errno);
        return nullptr;
    }
    return rt;
}

IPv6Address::IPv6Address() {
    memset(&m_addr, 0, sizeof(m_addr));
    m_addr.sin6_family = AF_INET6;
}

IPv6Address::IPv6Address(const sockaddr_in6& address) {
    m_addr = address;
}

IPv6Address::IPv6Address(const uint8_t address[16], uint16_t port) {
    memset(&m_addr, 0, sizeof(m_addr));
    m_addr.sin6_family = AF_INET6;
    m_addr.sin6_port = byteswapOnLittleEndian(port);
    memcpy(&m_addr.sin6_addr.s6_addr, address, 16);
}

const sockaddr* IPv6Address::getAddr() const {
    return (sockaddr*)&m_addr;
}

sockaddr* IPv6Address::getAddr() {
    return (sockaddr*)&m_addr;
}

socklen_t IPv6Address::getAddrLen() const {
    return sizeof(m_addr);
}

/* ipv6的中多个块连续为0可以直接省略，所有如果最后一个 addr[7] 为0，可以省略为 ::
 *
 */
std::ostream& IPv6Address::insert(std::ostream& os) const {
    os << "[";
    uint16_t* addr = (uint16_t*)m_addr.sin6_addr.s6_addr;
    bool used_zeros = false;
    for (size_t i = 0; i < 8; ++i) {
        if (addr[i] == 0 && !used_zeros) {
            continue ;
        }
        // 0:
        if (i && addr[i - 1] == 0 && !used_zeros) {
            os << ":";
            used_zeros = true;
        }
        if (i) {
            os << ":";
        }
        os << std::hex << (int)byteswapOnLittleEndian(addr[i]) << std::dec;
    }
    // 说明这里出现了多块为0，直接加 :: 省略
    if (!used_zeros && addr[7] == 0) {
        os << "::";
    }

    os << "]:" << byteswapOnLittleEndian(m_addr.sin6_port);
    return os;
}

IPAddress::ptr IPv6Address::broadcastAddress(uint32_t prefix_len) {
    sockaddr_in6 baddr(m_addr);
    /*	找到前缀长度结尾在第几个字节，在该字节在哪个位置。
     *	将该字节前剩余位置全部置为1	*/
    baddr.sin6_addr.s6_addr[prefix_len / 8] |= ~CreateMask<uint8_t>(prefix_len % 8);

    for (int i = prefix_len / 8 + 1; i < 16; ++i) {
        baddr.sin6_addr.s6_addr[i] = 0xff;
    }
    return IPv6Address::ptr(new IPv6Address(baddr));
}

IPAddress::ptr IPv6Address::networkAddress(uint32_t prefix_len) {
    sockaddr_in6 naddr(m_addr);
    naddr.sin6_addr.s6_addr[prefix_len / 8] &= CreateMask<uint8_t>(prefix_len % 8);
    for (int i = prefix_len / 8 + 1; i < 16; ++i) {
        naddr.sin6_addr.s6_addr[i] = 0x00;
    }
    return IPv6Address::ptr(new IPv6Address(naddr));
}

IPAddress::ptr IPv6Address::subnetMask(uint32_t prefix_len) {
    sockaddr_in6 subnet;
    memset(&subnet, 0, sizeof(subnet));
    subnet.sin6_family = AF_INET6;
    subnet.sin6_addr.s6_addr[prefix_len / 8] = CreateMask<uint8_t>(prefix_len % 8);

    for (uint32_t i = 0; i < prefix_len / 8; ++i) {
        subnet.sin6_addr.s6_addr[i] = 0xff;
    }
    return IPv6Address::ptr(new IPv6Address(subnet));
}

uint32_t IPv6Address::getPort() const {
    return byteswapOnLittleEndian(m_addr.sin6_port);
}

void IPv6Address::setPort(uint16_t v) {
    m_addr.sin6_port = byteswapOnLittleEndian(v);
}

// Unix域套接字路径名的最大长度。-1是减去'\0'
static const size_t MAX_PATH_LEN = sizeof(((sockaddr_un*)0)->sun_path) - 1;

UnixAddress::UnixAddress() {
    memset(&m_addr, 0, sizeof(m_addr));
    m_addr.sun_family = AF_UNIX;
    // sun_path 的偏移量+最大路径
    m_length = offsetof(sockaddr_un, sun_path) + MAX_PATH_LEN;
}

UnixAddress::UnixAddress(const std::string& path) {
    memset(&m_addr, 0, sizeof(m_addr));
    m_addr.sun_family = AF_UNIX;
    // 加上 '\0'的长度
    m_length = path.size() + 1;
    if (!path.empty() && path[0] == '\0') {
        --m_length;
    }

    if (m_length > sizeof(m_addr.sun_path)) {
        throw std::logic_error("path too long");
    }

    memcpy(m_addr.sun_path, path.c_str(), m_length);

    m_length += offsetof(sockaddr_un, sun_path);
}

const sockaddr* UnixAddress::getAddr() const {
    return (sockaddr*)&m_addr;
}

sockaddr* UnixAddress::getAddr() {
    return (sockaddr*)&m_addr;
}

socklen_t UnixAddress::getAddrLen() const {
    return m_length;
}

void UnixAddress::setAddrLen(uint32_t v) {
    m_length = v;
}

// offsetof 就是根据内存对齐来返回的值，
std::string UnixAddress::getPath() const {
    std::stringstream ss;
    if (m_length > offsetof(sockaddr_un, sun_path) && m_addr.sun_path[0] == '\0') {
        ss << "\\0" << std::string(m_addr.sun_path + 1, m_length - offsetof(sockaddr_un, sun_path) - 1);
    } else {
        ss << m_addr.sun_path;
    }
    return ss.str();
}

std::ostream& UnixAddress::insert(std::ostream& os) const {
    if (m_length > offsetof(sockaddr_un, sun_path) && m_addr.sun_path[0] == '\0') {
        return os << "\\0" << std::string(m_addr.sun_path + 1, m_length - offsetof(sockaddr_un, sun_path) - 1);
    }
    return os << m_addr.sun_path;
}

UnknownAddress::UnknownAddress(int family) {
    memset(&m_addr, 0, sizeof(m_addr));
    m_addr.sa_family = family;
}

UnknownAddress::UnknownAddress(const sockaddr& addr) {
    m_addr = addr;
}

const sockaddr* UnknownAddress::getAddr() const {
    return (sockaddr*)&m_addr;
}

std::ostream& UnknownAddress::insert(std::ostream& os) const {
    os << "UnknownAddress family=" << m_addr.sa_family;
    return os;
}

sockaddr* UnknownAddress::getAddr() {
    return (sockaddr*)&m_addr;
}

socklen_t UnknownAddress::getAddrLen() const {
    return sizeof(m_addr);
}

std::ostream& operator<<(std::ostream& os, const Address& addr) {
    return addr.insert(os);
}

}
