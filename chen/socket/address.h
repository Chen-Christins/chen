/**
 * @file address.h
 * @brief 网络地址模块
 * @author Christins
 * @date 2024-11-18
 */
#pragma once

#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <arpa/inet.h>
#include <sys/un.h>

namespace chen {

class IPAddress;
/**
 * @brief 抽象类，主要就是提供接口
 */
class Address {
public:
    typedef std::shared_ptr<Address> ptr;
    /**
     * @brief 通过sockaddr创建地址
     * @param[in] addr sockaddr类型的一个
     * @param[in] addrlen sockaddr的长度
     * @return Address::ptr 返回一个具体[ipv4, ipv6, unknow]的类型
     */
    static Address::ptr Create(const sockaddr* addr, socklen_t addrlen);
    
    /**
     * @brief 通过host地址返回所有Address
     * @param[out] result 返回所有的网络地址信息
     * @param[in] host 传入的主机信息
     * @param[in] family 传入的主机信息
     * @param[in] type 协议类型，比如udp，tcp
     * @param[in] protocol 传输协议，一般给个0就行
     * @return bool 是否成功
     */
    static bool Lookup(std::vector<Address::ptr>& result, const std::string& host
        ,int family = AF_INET, int type = 0, int protocol = 0);

    /**
     * @brief 通过host返回任意地址Address
     * @param[in] host 传入的主机信息
     * @param[in] family 传入的主机信息
     * @param[in] type 协议类型，比如udp，tcp
     * @param[in] protocol 传输协议，一般给个0就行
     * @return Address::ptr 返回任意Address，默认是序列第一个
     */
    static Address::ptr LookupAny(const std::string& host, int family = AF_INET
        ,int type = 0, int protocol = 0);
    
    /**
     * @brief 通过host返回任意IPAddress
     * @param[in] host 主机地址
     * @param[in] family 协议族
     * @param[in] type 协议类型，比如udp，tcp
     * @param[in] protocol 传输协议
     * @return std::shared_ptr<IPAddress> 返回任意IPAddress
     */
    static std::shared_ptr<IPAddress> LookupAnyIPAddress(const std::string &host, int family = AF_INET
        ,int type = 0, int protocol = 0);

    /**
     * @brief 返回本机的所有网卡的<网卡名，地址，子网掩码位数>
     * @param[out] result 返回本机的网卡信息，ipv4地址，ipv6地址等
     * @param[in] family 协议族
     * @return bool 是否获取成功
     */
    static bool GetInterfaceAddresses(std::multimap<std::string, std::pair<Address::ptr
        ,uint32_t>>& result, int family = AF_UNSPEC);

    /**
     * @brief 返回指定网卡的<网卡地址，子网掩码位数>
     * @param[out] result 返回指定网卡的<网卡地址，子网掩码位数>
     * @param[in] iface *
     * @param[in] family 协议族
     * @return bool 是否获取成功
     */
    static bool GetInterfaceAddresses(std::vector<std::pair<Address::ptr, uint32_t>>& result
        ,const std::string& iface, int family = AF_INET);
        
    /**
     * @brief 获取协议族
     */
    int getFamily() const;

    /**
     * @brief 获取sockaddr指针，只读，纯虚函数
     */
    virtual const sockaddr* getAddr() const = 0;
    
    /**
     * @brief 获取sockaddr指针，纯虚函数
     */
    virtual sockaddr* getAddr() = 0;
    
    /**
     * @brief 获取sockaddr指针的字节数，只读，纯虚函数
     */
    virtual socklen_t getAddrLen() const = 0;

    /**
     * @brief 将其变成转换可读的字符串
     */
    std::string toString() const;

    /**
     * @brief 主要将其转换为字符串，纯虚函数
     * @param[in] os 输入流
     * @return std::ostream& 
     */
    virtual std::ostream& insert(std::ostream& os) const = 0;

    /**
     * @brief 重载Address的 == 判断
     */
    bool operator==(const Address& rhs) const;

    /**
     * @brief 重载Address的 != 判断
     */
    bool operator!=(const Address& rhs) const;

    /**
     * @brief 重载Address的 < 判断
     */
    bool operator<(const Address& rhs) const;
};

class IPAddress : public Address {
public:
    typedef std::shared_ptr<IPAddress> ptr;

    /**
     * @brief 构建一个IPAddress
     * @param[in] address 点分十进制主机地址
     * @param[in] port 端口号
     * @return IPAddress::ptr 
     */
    static IPAddress::ptr Create(const char* address, uint16_t port = 0);

    /**
     * @brief 创建广播地址
     * @param[in] prefix_len 前缀1的长度，子网掩码的位数
     * @return IPAddress::ptr 返回IP地址的指针
     */
    virtual IPAddress::ptr broadcastAddress(uint32_t prefix_len) = 0;

    /**
     * @brief 创建网络地址
     * @param[in] prefix_len 前缀1的长度，子网掩码的位数
     * @return IPAddress::ptr 返回IP地址的指针
     */
    virtual IPAddress::ptr networkAddress(uint32_t prefix_len) = 0;

    /**
     * @brief 返回子网掩码
     * @param[in] prefix_len 前缀1的长度，子网掩码的位数
     * @return IPAddress::ptr 返回IP地址的指针
     */
    virtual IPAddress::ptr subnetMask(uint32_t prefix_len) = 0;

    /**
     * @brief 获取端口号
     */
    virtual uint32_t getPort() const = 0;

    /**
     * @brief 设置端口号
     */
    virtual void setPort(uint16_t v) = 0;
};

class IPv4Address : public IPAddress {
public:
    typedef std::shared_ptr<IPv4Address> ptr;
    /**
     * @brief 通过IPv4地址字符串创建一个IPv4地址
     * @param[in] address 点分十进制主机地址
     * @param[in] port 端口号
     */
    static IPv4Address::ptr Create(const char* address, uint16_t port = 0);
    
    /**
     * @brief 构造函数
     * @param[in] address 一个IPv4的结构体
     */
    IPv4Address(const sockaddr_in& address);

    /**
     * @brief 构造函数
     * @param[in] address 通过二进制地址构造IPv4Address
     * @param[in] port 端口号
     */
    IPv4Address(uint32_t address = INADDR_ANY, uint16_t port = 0);
    
    const sockaddr* getAddr() const override;
    sockaddr* getAddr() override;
    socklen_t getAddrLen() const override;
    std::ostream& insert(std::ostream& os) const override;
    IPAddress::ptr broadcastAddress(uint32_t prefix_len) override;
    IPAddress::ptr networkAddress(uint32_t prefix_len) override;
    IPAddress::ptr subnetMask(uint32_t prefix_len) override;
    uint32_t getPort() const override;
    void setPort(uint16_t v) override;
private:
    sockaddr_in m_addr;
};

class IPv6Address : public IPAddress {
public:
    typedef std::shared_ptr<IPv6Address> ptr;

    /**
     * @brief 通过IPv6的地址字符串构建IPv6Address
     * @param[in] address IPv6地址字符串
     * @param[in] port 端口号
     */
    static IPv6Address::ptr Create(const char* address, uint16_t port = 0);

    /**
     * @brief 无参构造函数
     */
    IPv6Address();

    /**
     * @brief 通过sockaddr_in6构造IPv6Address
     * @param[in] address sockaddr_in6结构体
     */
    IPv6Address(const sockaddr_in6& address);

    /**
     * @brief 通过IPv6二进制地址构造IPv6Address
     * @param[in] address IPv6二进制地址
     */
    IPv6Address(const uint8_t address[16], uint16_t port = 0);
    
    const sockaddr* getAddr() const override;
    sockaddr* getAddr() override;
    socklen_t getAddrLen() const override;

    std::ostream& insert(std::ostream& os) const override;
    IPAddress::ptr broadcastAddress(uint32_t prefix_len) override;
    IPAddress::ptr networkAddress(uint32_t prefix_len) override;
    IPAddress::ptr subnetMask(uint32_t prefix_len) override;
    uint32_t getPort() const override;
    void setPort(uint16_t v) override;
private:
    sockaddr_in6 m_addr;
};

class UnixAddress : public Address {
public:
    typedef std::shared_ptr<UnixAddress> ptr;

    /**
     * @brief 无参构造函数
     */
    UnixAddress();

    /**
     * @brief 根据路径构造UnixAddress
     * @param path UnixSocket路径(小于MAX_PATH_LEN)
     */
    UnixAddress(const std::string& path);
    
    const sockaddr* getAddr() const override;
    sockaddr* getAddr() override;
    socklen_t getAddrLen() const override;
    void setAddrLen(uint32_t v);
    std::string getPath() const;
    std::ostream& insert(std::ostream& os) const override;
private:
    sockaddr_un m_addr;
    socklen_t m_length;
};

class UnknownAddress : public Address {
public:
    typedef std::shared_ptr<UnknownAddress> ptr;
    UnknownAddress(int family);
    UnknownAddress(const sockaddr& addr);
    const sockaddr* getAddr() const override;
    std::ostream& insert(std::ostream& os) const override;
    sockaddr* getAddr() override;
    socklen_t getAddrLen() const override;
private:
    sockaddr m_addr;
};

std::ostream& operator<<(std::ostream& os, const Address& addr);

}
