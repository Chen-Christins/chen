/**
 * @file socket.h
 * @brief 封装socketAPI
 * @author Christins
 * @date 2024-11-21
 */
#pragma once

#include <memory>

#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <openssl/err.h>
#include <openssl/ssl.h> // SSL_CTX, SSL

#include "../util/noncopyable.h"
#include "address.h"

namespace chen {

class Socket : public std::enable_shared_from_this<Socket>, Noncopyable {
public:
    typedef std::shared_ptr<Socket> ptr;
    typedef std::weak_ptr<Socket> weak_ptr;

    enum Type {
        /// tcp type
        TCP = SOCK_STREAM,
        /// udp type 
        UDP = SOCK_DGRAM
    };

    enum Family {
        /// IPv4 协议族
        IPv4 = AF_INET,
        /// IPv6 协议族
        IPv6 = AF_INET6,
        /// Unix 协议族
        UNIX = AF_UNIX,
    };
    /**
     * @brief 静态方法，创建一个TCP Socket
     * @param[in] address 网络主机地址信息
     */
    static Socket::ptr CreateTCP(Address::ptr address);

    /**
     * @brief 静态方法，创建一个UDP Socket
     * @param[in] address 网络主机地址信息
     */
    static Socket::ptr CreateUDP(Address::ptr address);

    /**
     * @brief 静态方法，创建一个IPv4的TCP Socket
     */
    static Socket::ptr CreateTCPSocket();

    /**
     * @brief 静态方法，创建一个IPv4的UDP Socket
     */
    static Socket::ptr CreateUDPSocket();

    /**
     * @brief 静态方法，创建一个IPv6的TCP Socket
     */
    static Socket::ptr CreateTCPSocket6();

    /**
     * @brief 静态方法，创建一个IPv6的UdP Socket
     */
    static Socket::ptr CreateUDPSocket6();

    /**
     * @brief 静态方法，创建一个Unix的TCP Socket
     */
    static Socket::ptr CreateUnixTCPSocket();

    /**
     * @brief 静态方法，创建一个Unix的UDP Socket
     */
    static Socket::ptr CreateUnixUDPSocket();

    /**
     * @brief 构造函数
     * @param family 协议族
     * @param type 协议类型 UDP，TCP
     * @param protocol 传输协议
     */
    Socket(int family, int type, int protocol = 0);

    /**
     * @brief 析构函数
     */
    virtual ~Socket();

    /**
     * @brief 获取发送的超时时间(ms)
     */
    uint64_t getSendTimeout();

    /**
     * @brief 设置发送的超时时间
     */
    void setSendTimeout(uint64_t v);

    /**
     * @brief 获取接收的超时时间
     */
    uint64_t getRecvTimeout();

    /**
     * @brief 设置接收的超时时间
     */
    void setRecvTimeout(int64_t v);
    
    /**
     * @brief 获取socket句柄相关信息
     * @param level 选项协议所在的协议层
     * @param option 需要访问的协议名
     * @param result 指向返回值的地址
     * @param len 选项的最大长度
     * @return bool 是否成功获取
     */
    bool getOption(int level, int option, void* result, socklen_t* len);
    
    template <class T>
    bool getOption(int level, int option, T& result) {
        size_t length = sizeof(T);
        return getOption(level, option, &result, &length);
    }

    /**
     * @brief 设置socket句柄相关的信息
     * @param level 选项协议所在的协议层
     * @param option 需要访问的协议名
     * @param result 新选项的地址
     * @param len 现在选项的长度
     * @return bool 是否设置成功
     */
    bool setOption(int level, int option, const void* result, socklen_t len);

    template <class T>
    bool setOption(int level, int option, const T& value) {
        return setOption(level, option, &value, sizeof(T));
    }

    /**
     * @brief 接受客户端连接
     */
    virtual Socket::ptr accept();

    /**
     * @brief 绑定addr的信息
     * @param addr 地址相关信息
     */
    virtual bool bind(const Address::ptr addr);

    /**
     * @brief 连接请求端
     * @param addr 请求端的地址相关信息
     * @param timeout_ms 超时时间
     */
    virtual bool connect(const Address::ptr addr, uint64_t timeout_ms = -1);
	
	/**
	 * @brief 重新连接
	 * @param timeout_ms 
	 * @return bool 
	 */
	virtual bool reconnect(uint64_t timeout_ms = -1);

    /**
     * @brief 对socket句柄设置监听
     * @param backlog 监听的最大数量，这里的 SOMAXCONN 是最大连接套接字数量 
     */
    virtual bool listen(int backlog = SOMAXCONN);

    /**
     * @brief 关闭socket相关句柄
     */
    virtual bool close();

    /**
     * @brief 发送buffer里的数据给目标服务器
     * @param buffer 需要发送的内容
     * @param length 发送内容的长度
     * @param flags 操作类型 0是常规操作
     */
    virtual int send(const void* buffer, size_t length, int flags = 0);

    /**
     * @brief 发送buffers里的数据给目标服务器
     * @param buffers 需要发送的内容
     * @param length 发送buffers，也就是iovec内存块的个数
     * @param flags 操作类型 0是常规操作
     */
    virtual int send(const iovec* buffers, size_t length, int flags = 0);
    
    /**
     * @brief 发送buffer里的数据给目标服务器
     * @param buffer 需要发送的内容
     * @param length 发送内容的长度
     * @param to 目标服务器地址
     * @param flags 操作类型 0是常规操作
     */
    virtual int sendTo(const void* buffer, size_t length, const Address::ptr to, int flags = 0);

    /**
     * @brief 发送buffers里的数据给目标服务器
     * @param buffers 需要发送的内容
     * @param length 发送buffers，也就是iovec内存块的个数
     * @param to 目标服务器地址
     * @param flags 操作类型 0是常规操作
     */
    virtual int sendTo(const iovec* buffers, size_t length, const Address::ptr to, int flags = 0);
    
    /**
     * @brief 接受来自发送端的请求信息
     * @param buffer 存接受信息的内存的起始地址
     * @param length 存储地址可以接受的长度
     * @param flags 操作类型 0是常规操作，和read()一样
     */
    virtual int recv(void* buffer, size_t length, int flags = 0);

    /**
     * @brief 接受来自发送端的请求信息
     * @param buffers iovec 存储地址
     * @param length iovec的个数
     * @param flags 操作类型 0是常规操作，和read()一样
     */
    virtual int recv(iovec* buffers, size_t length, int flags = 0);

    /**
     * @brief 接受来自发送端的请求的信息
     * @param buffer 存接受信息的内存的起始地址
     * @param length 存储地址可以接受的长度
     * @param from 发送端地址
     * @param flags 操作类型 0是常规操作，和read()一样
     */
    virtual int recvFrom(void* buffer, size_t length, Address::ptr from, int flags = 0);

    /**
     * @brief 接受来自发送端的请求
     * @param buffers iovec内存块的信息
     * @param length iovec的个数
     * @param from 发送端地址
     * @param flags 操作类型 0是常规操作，和read()一样
     */
    virtual int recvFrom(iovec* buffers, size_t length, Address::ptr from, int flags = 0);

    /**
     * @brief 获取远端地址
     */
    Address::ptr getRemoteAddress();

    /**
     * @brief 获取本地地址
     */
    Address::ptr getLocalAddress();

    /**
     * @brief 获取socket的字符串表示
     * @return std::string 
     */
    std::string toString() const;

    /**
     * @brief 获取协议族
     */
    int getFamily() const { return m_family; }

    /**
     * @brief 获取协议类型
     */
    int getType() const { return m_type; }
    
    /**
     * @brief 获取传输协议
     */
    int getProtocol() const { return m_protocol; }

    /**
     * @brief 是否连接成功
     */
    bool isConnected() const { return m_isConnected; }

    /**
     * @brief 是否有效
     */
    bool isValid() const;

    /**
     * @brief 获取错误信息
     */
    int getError();
    
    /**
     * @brief 类似于一个打印socket的所有信息的流
     * @param os 输出流
     */
    virtual std::ostream& dump(std::ostream& os) const;
    
    /**
     * @brief 返回socket句柄
     */
    int getSocket() const { return m_sock; }

    /**
     * @brief 取消读事件
     */
    bool cancelRead();

    /**
     * @brief 取消写事件
     */
    bool cancelWrite();

    /**
     * @brief 本质就是取消读事件
     */
    bool cancelAccept();

    /**
     * @brief 取消所有事件
     */
    bool cancelAll();
protected:
    /**
    * @brief 初始化socket
    */
    void initSock();

    /**
     * @brief 创建新的socket
     */
    void newSock();

    /**
     * @brief 通过一个创建好的文件描述符来初始化，设置端口复用，本地协议地址，远端协议地址
     * @param sock 文件描述符
     */
    virtual bool init(int sock);
protected:
    /// socket句柄
    int m_sock;
    /// 协议族
    int m_family;
    /// 协议类型，TCP，UDP
    int m_type;
    /// 传输协议
    int m_protocol;
    /// 是否连接
    bool m_isConnected;

    /// 本地地址
    Address::ptr m_localAddress;
    /// 远端地址
    Address::ptr m_remoteAddress;
};

class SSLSocket : public Socket {
public:
	typedef std::shared_ptr<SSLSocket> ptr;

    /**
     * @brief 创建一个SSL的TCP Socket
     * @param address 目标地址
     * @return SSLSocket::ptr 
     */
	static SSLSocket::ptr CreateTCP(Address::ptr address);
    
    /**
     * @brief 创建一个IPv4的SSL TCP Socket
     * @return SSLSocket::ptr 
     */
	static SSLSocket::ptr CreateTCPSocket();

    /**
     * @brief 创建一个IPv6的SSL TCP Socket
     * @return SSLSocket::ptr 
     */
	static SSLSocket::ptr CreateTCPSocket6();

    /**
     * @brief 创建一个Unix的SSL TCP Socket
     * @return SSLSocket::ptr 
     */
	SSLSocket(int family, int type, int protocol = 0);

    /**
     * @brief 接受连接
     * @return Socket::ptr 
     */
	virtual Socket::ptr accept() override;

    /**
     * @brief 绑定地址
     * @param addr 地址信息
     * @return bool 是否成功
     */
	virtual bool bind(const Address::ptr addr) override;

    /**
     * @brief 连接地址
     * @param addr 地址信息
     * @param timeout_ms 超时时间
     * @return bool 是否成功
     */
	virtual bool connect(const Address::ptr addr, uint64_t timeout_ms = -1) override;
	
    /**
     * @brief 监听socket
     * @param backlog 最大监听数量
     * @return bool 是否成功
     */
    virtual bool listen(int backlog = SOMAXCONN) override;
	
    /**
     * @brief 关闭socket
     * @return bool 是否成功
     */
    virtual bool close() override;
	
    /**
     * @brief 发送数据
     * @param buffer 发送缓冲区
     * @param length 缓冲区长度
     * @param flags 操作类型 0是常规操作
     * @return int 发送的字节数
     */
    virtual int send(const void* buffer, size_t length, int flags = 0) override;
	
    /**
     * @brief 发送多个数据块
     * @param buffers iovec内存块的信息
     * @param length iovec的个数
     * @param flags 操作类型 0是常规操作
     * @return int 发送的字节数
     */
    virtual int send(const iovec* buffers, size_t length, int flags = 0) override;
	
    /**
     * @brief 发送数据到指定地址
     * @param buffer 发送缓冲区
     * @param length 缓冲区长度
     * @param to 目标服务器地址
     * @param flags 操作类型 0是常规操作
     * @return int 发送的字节数
     */
    virtual int sendTo(const void* buffer, size_t length, const Address::ptr to, int flags = 0) override;
	
    /**
     * @brief 发送多个数据块到指定地址
     * @param buffers iovec内存块的信息
     * @param length iovec的个数
     * @param to 目标服务器地址
     * @param flags 操作类型 0是常规操作
     * @return int 发送的字节数
     */
    virtual int sendTo(const iovec* buffers, size_t length, const Address::ptr to, int flags = 0) override;
	
    /**
     * @brief 接收数据
     * @param buffer 接收缓冲区
     * @param length 缓冲区长度
     * @param flags 操作类型 0是常规操作，和read()一样
     * @return int 接收的字节数
     */
    virtual int recv(void* buffer, size_t length, int flags = 0) override;
	
    /**
     * @brief 接收多个数据块
     * @param buffers iovec内存块的信息
     * @param length iovec的个数
     * @param flags 操作类型 0是常规操作，和read()一样
     * @return int 接收的字节数
     */
    virtual int recv(iovec* buffers, size_t length, int flags = 0) override;
	
    /**
     * @brief 接收数据并获取发送地址
     * @param buffer 接收缓冲区
     * @param length 缓冲区长度
     * @param from 发送端地址
     * @param flags 操作类型 0是常规操作，和read()一样
     * @return int 接收的字节数
     */
    virtual int recvFrom(void* buffer, size_t length, Address::ptr from, int flags = 0) override;
	
    /**
     * @brief 接收多个数据块并获取发送地址
     * @param buffers iovec内存块的信息
     * @param length iovec的个数
     * @param from 发送端地址
     * @param flags 操作类型 0是常规操作，和read()一样
     * @return int 接收的字节数
     */
    virtual int recvFrom(iovec* buffers, size_t length, Address::ptr from, int flags = 0) override;

    /**
     * @brief 加载证书
     * @param cert_file 证书文件
     * @param key_file 私钥文件
     * @return bool 是否成功
     */
	bool loadCertificates(const std::string& cert_file, const std::string& key_file);
	
    /**
     * @brief 打印Socket信息
     * @param os 写入流
     * @return std::ostream& 
     */
    virtual std::ostream& dump(std::ostream& os) const override;
protected:
    /**
     * @brief 通过文件描述符初始化SSL Socket
     * @param sock 文件描述符
     * @return bool 是否成功
     */
	virtual bool init(int sock) override;
private:
    /// SSL上下文
	std::shared_ptr<SSL_CTX> m_ctx;
    /// SSL实例
	std::shared_ptr<SSL> m_ssl;
};

/**
 * @brief 为了方便打印日志
 * @param os 输出流
 * @param sock Socket信息
 */
std::ostream& operator<<(std::ostream& os, const Socket& sock);

}
