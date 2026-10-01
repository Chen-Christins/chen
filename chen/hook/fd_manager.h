/**
 * @file fd_manager.h
 * @brief 文件描述符管理模块，辅助方法
 *        判断socket传入是否是一个句柄
 * @author Christins
 * @date 2024-11-17
 */
#pragma once

#include <memory>
#include <shared_mutex>
#include <vector>

#include "../util/singleton.h"

namespace chen {

class IOManager;

class FdCtx : public std::enable_shared_from_this<FdCtx> {
public:
    typedef std::shared_ptr<FdCtx> ptr;
    /**
     * @brief 构造函数
     * @param fd 文件描述符
     */
    FdCtx(int fd);

    /**
     * @brief 析构函数
     */
    ~FdCtx();

    /**
     * @brief 初始化操作
     * @details 主要是判断文件描述符所对应的信息，他是否是socket系列的句柄
     *          是否是在hook模块设置的非阻塞
     * @return bool 
     */
    bool init();

    /**
     * @brief 判断是否初始化成功
     */
    bool isInit() const { return m_isInit; }

    /**
     * @brief 返回这个文件描述符是否是socket系列的
     */
    bool isSocket() const { return m_isSocket; }

    /**
     * @brief 文件描述符是否关闭
     */
    bool isClose() const { return m_isClosed; }

    /**
     * @brief 设置文件描述符关闭状态
     */
    void setClose(bool v) { m_isClosed = v; }

    /**
     * @brief 获取注册了此 fd 事件的 IOManager
     */
    IOManager* getIOManager() const { return m_iom; }

    /**
     * @brief 设置注册了此 fd 事件的 IOManager
     */
    void setIOManager(IOManager* iom) { m_iom = iom; }

    /**
     * @brief 设置是否是用户设置的非阻塞
     * @param v 新的状态
     */
    void setUserNonblock(bool v) { m_userNonblock = v; }

    /**
     * @brief 获取是否是用户设置的非阻塞
     */
    bool getUserNonblock() const { return m_userNonblock; }

    /**
     * @brief 设置是否是hook模块里设置的非阻塞
     * @param v 新的状态
     */
    void setSysNonblock(bool v) { m_sysNonblock = v; }

    /**
     * @brief 获取是否是hook模块设置的状态
     */
    bool getSysNonblock() const { return m_sysNonblock; }

    /**
     * @brief 设置对应类型[read/send]的超时时间
     * @param type 对应的类型 [read/write]
     * @param v 时间戳
     */
    void setTimeout(int type, uint64_t v);

    /**
     * @brief Get the Timeout object
     * @param type 对应的类型 [read/write]
     * @return uint64_t 时间戳
     */
    uint64_t getTimeout(int type);
private:
    /// 是否初始化，这里的这个 :1 是位域的意思，一个
    bool m_isInit: 1;
    /// 是不是socket系列的fd
    bool m_isSocket: 1;
    /// 是否hook非阻塞
    bool m_sysNonblock: 1;
    /// 是否用户主动设置非阻塞
    bool m_userNonblock: 1;
    /// 是否关闭
    bool m_isClosed: 1;
    /// 文件描述符
    int m_fd;
    /// 读超时时间
    uint64_t m_recvTimout;
    /// 写超时时间
    uint64_t m_sendTimeout;
    /// 注册了此 fd 事件的 IOManager（非拥有关系）
    IOManager* m_iom = nullptr;
};

class FdManager {
public:
    /**
    * @brief 构造函数
    */
    FdManager();
    
    /**
    * @brief 获取一个FdCtx
    * @param fd 文件描述符
    * @param auto_create 如果不存在是否创建一个
    */
    FdCtx::ptr get(int fd, bool auto_create = false);

    /**
     * @brief 删除一个与fd关联的FdCtx
     * @param fd 文件描述符
     */
    void del(int fd);
private:
    /// 写锁
    std::shared_mutex m_mutex;
    /// 管理的信息集合
    std::vector<FdCtx::ptr> m_datas;
};

typedef Singleton<FdManager> FdMgr;

}
