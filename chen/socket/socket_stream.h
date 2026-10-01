/**
 * @file socket_stream.h
 * @brief 封装socket相关的函数
 * @author Christins
 * @date 2024-11-25
 */
#pragma once

#include <functional>
#include <memory>

#include <boost/any.hpp>

#include "../bytearray/bytearray.h"
#include "../iomanager/iomanager.h"
#include "../schedule/schedule.h"
#include "../timer/timer.h"
#include "../util/mutex.h"
#include "socket.h"

namespace chen {

class Stream {
public:
    typedef std::shared_ptr<Stream> ptr;

    /**
     * @brief 析构函数
     */
    virtual ~Stream() {}
    
    /**
     * @brief 将数据读入缓冲区
     * @param buffer 缓冲区
     * @param length 读入的大小
     * @return int 失败-1|成功0
     */
    virtual int read(void* buffer, size_t length) = 0;

    /**
     * @brief 将数据读入ByteArray序列化实例
     * @param ba ByteArray序列化实例
     * @param length 数据长度
     * @return int 失败-1|成功0
     */
    virtual int read(ByteArray::ptr ba, size_t length) = 0;

    /**
     * @brief 读取固定长度到内存
     * @param buffer 缓冲区
     * @param length 长度
     * @return int 返回读入的长度
     */
    virtual int readFixSize(void* buffer, size_t length);

    /**
     * @brief 读取固定长度到序列化实例中
     * @param buffer 序列化缓冲区
     * @param length 长度
     * @return int 返回读入的长度
     */
    virtual int readFixSize(ByteArray::ptr ba, size_t length);

    /**
     * @brief 写数据到对端
     * @param buffer 缓冲区
     * @param length 数据长度
     * @return int 失败-1|成功0
     */
    virtual int write(const void* buffer, size_t length) = 0;

    /**
     * @brief 写数据到对端
     * @param buffer 序列化缓冲区
     * @param length 数据长度
     * @return int 失败-1|成功0
     */
    virtual int write(ByteArray::ptr ba, size_t length) = 0;

    /**
     * @brief 写入数据到对端
     * @param buffer 缓冲区
     * @param length 长度
     * @return int 返回写入的长度
     */
    virtual int writeFixSize(const void* buffer, size_t length);

    /**
     * @brief 将序列化缓冲区的数据写入到对端
     * @param buffer 缓冲区
     * @param length 长度
     * @return int 返回写入的长度
     */
    virtual int writeFixSize(ByteArray::ptr ba, size_t length);

    /**
     * @brief 关闭socket连接
     */
    virtual void close() = 0;
};

class SocketStream : public Stream {
public:
    typedef std::shared_ptr<SocketStream> ptr;

    /**
     * @brief 构造函数
     * @param sock 传入的socket
     * @param owner 是否是自己控制的socket
     */
    SocketStream(Socket::ptr sock, bool owner = true);

    /**
     * @brief 析构函数
     */
    ~SocketStream();

    /**
     * @brief 将数据读入缓冲区
     * @param buffer 缓冲区
     * @param length 读入的大小
     * @return int 失败-1|成功0
     */
    virtual int read(void* buffer, size_t length) override;

    /**
     * @brief 将数据读入ByteArray序列化实例
     * @param ba ByteArray序列化实例
     * @param length 数据长度
     * @return int 失败-1|成功0
     */
    virtual int read(ByteArray::ptr ba, size_t length) override;

    /**
     * @brief 写数据到对端
     * @param buffer 缓冲区
     * @param length 数据长度
     * @return int 失败-1|成功0
     */
    virtual int write(const void* buffer, size_t length) override;

    /**
     * @brief 写数据到对端
     * @param buffer 序列化缓冲区
     * @param length 数据长度
     * @return int 失败-1|成功0
     */
    virtual int write(ByteArray::ptr ba, size_t length) override;

    /**
     * @brief 关闭socket连接
     */
    virtual void close() override;

    /**
     * @brief 获取socket示例
     * @return Socket::ptr 返回的socket示例
     */
    Socket::ptr getSocket() const { return m_socket; }

    /**
     * @brief 是否连接成功
     * @return bool 成功 true|失败 false
     */
    bool isConnected() const;
    
    /**
     * @brief 获取远端地址信息
     */
    Address::ptr getRemoteAddress();

    /**
     * @brief 获取本地地址信息
     */
    Address::ptr getLocalAddress();

    /**
     * @brief 获取远端地址字符串
     */
    std::string getRemoteAddressString();
    
    /**
     * @brief 获取本地地址字符串
     */
    std::string getLocalAddressString();
protected:
    /// socket信息
    Socket::ptr m_socket;
    /// 是否自己控制socket
    bool m_owner;
};

class AsyncSocketStream : public SocketStream
						, public std::enable_shared_from_this<AsyncSocketStream> {
public:
	typedef std::shared_ptr<AsyncSocketStream> ptr;
	typedef std::function<bool(AsyncSocketStream::ptr)> connect_callback;
	typedef std::function<void(AsyncSocketStream::ptr)> disconnect_callback;

    /**
     * @brief 构造函数
     * @param sock socket实例
     * @param owner 是否自己控制socket
     */
	AsyncSocketStream(Socket::ptr sock, bool owner = true);

    /**
     * @brief 启动异步socket流
     * @return bool 
     */
	virtual bool start();

    /**
     * @brief 关闭异步socket流
     */
	virtual void close() override;
public:
    /**
     * @brief 错误类型
     */
    enum Error {
        OK = 0,
        TIMEOUT = -1,
        IO_ERROR = -2,
        NOT_CONNECT = -3,
    };
public:
    /**
     * @brief 发送上下文
     */
	struct SendCtx {
		typedef std::shared_ptr<SendCtx> ptr;
        /// 析构函数
		virtual ~SendCtx() {};
		virtual bool doSend(AsyncSocketStream::ptr stream) = 0;
	};
    /**
     * @brief 上下文信息
     */
	struct Ctx : public SendCtx {
		typedef std::shared_ptr<Ctx> ptr;

		Ctx();
		virtual ~Ctx() {}

        /// 序列号
		uint32_t sn;
        /// 超时时间
		uint32_t timeout;
        /// 结果
		uint32_t result;
        /// 是否超时
		bool timed;
        /// 调度器
		Scheduler* scheduler;
        /// 协程
		Fiber::ptr fiber;
        /// 定时器
		Timer::ptr timer;

        /// 处理响应
		virtual void doRsp();
	};
public:
    /**
     * @brief 设置工作线程
     * @param v 工作线程
     */
	void setWorker(IOManager* v) { m_worker = v; }
	IOManager* getWorker() const { return m_worker; }

    /**
     * @brief 设置IO线程
     * @param v IO线程
     */
	void setIOManager(IOManager* v) { m_iomanager = v; }
	IOManager* getIOManager() const { return m_iomanager; }

    /**
     * @brief 设置自动连接
     * @param v 是否自动连接
     */
	void setAutoConnect(bool v) { m_autoConnect = v; }
	bool isAutoConnect() const { return m_autoConnect; }

    /**
     * @brief 设置连接回调
     * @param cb 连接回调函数
     */
	void setConnectCb(connect_callback cb) { m_connectCb = cb; }
	connect_callback getConnectCb() const { return m_connectCb; }

    /**
     * @brief 设置断开连接回调
     * @param cb 断开连接回调函数
     */
	void setDisConnectCb(disconnect_callback cb) { m_disconnectCb = cb; }
	disconnect_callback getDisConnectCb() const { return m_disconnectCb; }

    /**
     * @brief 设置数据
     * @param v 数据
     */
	template <class T>
	void setData(const T& v) { m_data = v; }

    /**
     * @brief 获取数据
     */
	template <class T>
	T getData() const {
		try {
			return boost::any_cast<T>(m_data);
		} catch (...) {
		}
		return T();
	}
protected:
    /// 读取数据
	virtual void doRead();
    /// 写入数据
	virtual void doWrite();
    /// 开始读取
	virtual void startRead();
    /// 开始写入
	virtual void startWrite();
    /// 超时处理
	virtual void onTimeOut(Ctx::ptr ctx);
	virtual Ctx::ptr doRecv() = 0;
    /// 获取上下文
	Ctx::ptr getCtx(uint32_t sn);
    /// 获取并删除上下文
	Ctx::ptr getAndDelCtx(uint32_t sn);

	template <class T>
	std::shared_ptr<T> getCtxAs(uint32_t sn) {
		auto ctx = getAndDelCtx(sn);
		if (ctx) {
			return std::dynamic_pointer_cast<T>(ctx);
		}
		return nullptr;
	}

    template <class T>
    std::shared_ptr<T> getAndDelCtxAs(uint32_t sn) {
        auto ctx = getAndDelCtx(sn);
        if(ctx) {
            return std::dynamic_pointer_cast<T>(ctx);
        }
        return nullptr;
    }
    /// 添加上下文
	bool addCtx(Ctx::ptr ctx);
    /// 入队列
	bool enqueue(SendCtx::ptr ctx);
    /// 内部关闭
	bool innerClose();
    /// 等待协程
	bool waitFiber();
protected:
	FiberSemaphore m_sem;
	FiberSemaphore m_waitSem;
	/// 消费队列的读写锁和对象的读写锁
	std::list<SendCtx::ptr> m_queue;
	/// 上下文信息
	std::unordered_map<uint32_t, Ctx::ptr> m_ctxs;
    /// 序列号
	uint32_t m_sn;
	/// 是否自动连接
	bool m_autoConnect;
	/// 定时器
	Timer::ptr m_timer;
	/// io线程
	IOManager* m_iomanager;
	/// work线程
	IOManager* m_worker;
	/// 连接时的回调
	connect_callback m_connectCb;
	/// 断开连接时的回调
	disconnect_callback m_disconnectCb;
    /// 数据
	boost::any m_data;
	/// 消费队列的读写锁和对象的读写锁
	std::shared_mutex m_queueMtx, m_mtx;
};

class AsyncSocketStreamManager {
public:
    typedef std::shared_ptr<AsyncSocketStreamManager> ptr;
    typedef std::function<bool(AsyncSocketStream::ptr)> connect_callback;
	typedef std::function<void(AsyncSocketStream::ptr)> disconnect_callback;

    /**
     * @brief 构造函数/析构函数
     */
    AsyncSocketStreamManager();
    virtual ~AsyncSocketStreamManager() {}

    /**
     * @brief 添加异步socket流
     * @param stream 异步socket流
     */
    void add(AsyncSocketStream::ptr stream);

    /**
     * @brief 清除所有异步socket流
     */
    void clear();
    
    /**
     * @brief 设置异步socket流
     * @param streams 异步socket流列表
     */
    void setConnection(const std::vector<AsyncSocketStream::ptr>& streams);

    /**
    * @brief 获取异步socket流
    * @return AsyncSocketStream::ptr 异步socket流
    */
    AsyncSocketStream::ptr get();

    template <class T>
    std::shared_ptr<T> getAs() {
        auto rt = get();
        if (rt) {
            return std::dynamic_pointer_cast<T>(rt);
        }
        return nullptr;
    }

    /**
     * @brief 设置连接回调
     * @param v 连接回调函数
     */
    void setConnectCb(connect_callback v);
    connect_callback getConnectCb() const { return m_connectCb; }
    
    /**
     * @brief 设置断开连接回调
     * @param v 断开连接回调函数
     */
    void setDisConnectCb(disconnect_callback v);
    disconnect_callback getDisConnectCb() const { return m_disconnectCb; }

private:
    /// 轮询索引
    uint32_t m_idx;
    /// 异步socket流数量
    uint32_t m_size;
    /// 所有的数据
    std::vector<AsyncSocketStream::ptr> m_datas;
    /// 开始连接的回调
    connect_callback m_connectCb;
    /// 断开连接的回调
    disconnect_callback m_disconnectCb;
    /// 读写锁
    std::shared_mutex m_mtx;
};

}
