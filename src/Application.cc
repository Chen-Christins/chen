/**
 * @file Application.cc
 * @brief 应用模块实现（进程引导 + 对外门面）
 * @author Christins
 * @date 2025-03-12
 * @copyright GPL-3.0
 */
#include "Application.hpp"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <signal.h>

#include "Config/Config.hpp"
#include "Daemon.hpp"
#include "ProcessSupervisor.hpp"
#include "ServerRegistry.hpp"
#include "Util/Env.hpp"
#include "Util/FSUtil.hpp"
#include "Version.hpp"
#include "WorkerRuntime.hpp"

namespace chen {

static Logger::ptr logger = LOG_NAME("system");

static ConfigVar<std::string>::ptr g_server_work_path =
    Config::Lookup("server.work_path", std::string(), "server work path");

static ConfigVar<std::string>::ptr g_server_pid_file =
    Config::Lookup("server.pid_file", std::string("chen.pid"), "server pid file");

static ConfigVar<std::string>::ptr g_server_process_name = Config::Lookup("server.process_name",
    std::string(),
    "process title base name, default is the executable file name");

Application* Application::kInstance = nullptr;

Application::Application()
{
    kInstance = this;
}

bool Application::init(int argc, char** argv)
{
    argc_ = argc;
    argv_ = argv;

    EnvMgr::GetInstance()->addHelp("s", "start in terminal, or -s reload|stop|quit to signal a running instance");
    EnvMgr::GetInstance()->addHelp("d", "run as daemon");
    EnvMgr::GetInstance()->addHelp("c", "conf path default: ./conf");
    EnvMgr::GetInstance()->addHelp("v", "print version, e.g. -v or --version");
    EnvMgr::GetInstance()->addHelp("p", "print help");

    bool is_print_help = false;
    if (!EnvMgr::GetInstance()->init(argc, argv)) {
        is_print_help = true;
    }

    if (EnvMgr::GetInstance()->has("v") || EnvMgr::GetInstance()->has("version")) {
        std::cout << "chen/" << CHEN_VERSION << std::endl;
        return false;
    }

    if (EnvMgr::GetInstance()->has("p")) {
        is_print_help = true;
    }

    if (is_print_help) {
        EnvMgr::GetInstance()->printHelp();
        return false;
    }

    std::string conf_path = EnvMgr::GetInstance()->getConfigPath();
    if (!FSUtil::Exists(conf_path)) {
        FATAL(logger) << "conf_path is invalid errno=" << errno << ", errstr=" << strerror(errno);
        return false;
    }

    Config::LoadFromConfDir(conf_path);

    EnvMgr::GetInstance()->setProcTitleBase(g_server_process_name->getValue());

    // 信号命令: -s reload / -s stop / -s quit（类似 nginx -s）
    if (EnvMgr::GetInstance()->has("s")) {
        std::string sig_name = EnvMgr::GetInstance()->get("s");
        if (!sig_name.empty()) {
            sendSignal(sig_name);
            return false;
        }
    }

    int run_type = 0;
    if (EnvMgr::GetInstance()->has("s")) {
        run_type = 1;
    }
    if (EnvMgr::GetInstance()->has("d")) {
        run_type = 2;
    }

    if (run_type == 0) {
        EnvMgr::GetInstance()->printHelp();
        return false;
    }

    if (g_server_work_path->getValue().empty()) {
        FATAL(logger) << "server work path is empty";
        return false;
    }

    pidfile_ = g_server_work_path->getValue() + "/" + g_server_pid_file->getValue();
    if (FSUtil::IsRunningPidfile(pidfile_)) {
        ERROR(logger) << "server is running: " << pidfile_;
        return false;
    }
    if (!FSUtil::Mkdir(g_server_work_path->getValue())) {
        FATAL(logger) << "create work path [" << g_server_work_path->getValue() << " errno=" << errno
                      << " errstr=" << std::strerror(errno);
        return false;
    }
    return true;
}

bool Application::run()
{
    bool is_daemon = EnvMgr::GetInstance()->has("d");

    // IOManager 在 WorkerRuntime 里，注册表必须先于它析构（成员声明顺序见头文件）
    server_registry_.reset(new ServerRegistry());
    worker_runtime_.reset(new WorkerRuntime(server_registry_));
    worker_runtime_->setArgs(argc_, argv_);

    ProcessSupervisor supervisor(server_registry_, worker_runtime_);

    bool ret = start_daemon(
        argc_, argv_,
        [&supervisor, pidfile = pidfile_](int, char**) {
            return supervisor.run(pidfile);
        },
        is_daemon);

    return ret == 0;
}

void Application::signalHandler(int sig)
{
    WorkerRuntime::signalHandler(sig);
}

int Application::sendSignal(const std::string& sig_name)
{
    std::string pidfile = g_server_work_path->getValue() + "/" + g_server_pid_file->getValue();

    std::string content = FSUtil::ReadFileToString(pidfile);
    if (content.empty()) {
        ERROR(logger) << "failed to read pid file: " << pidfile;
        return -1;
    }
    pid_t pid = atoi(content.c_str());
    if (pid <= 1) {
        ERROR(logger) << "invalid pid in pid file: " << content;
        return -1;
    }

    if (kill(pid, 0) != 0) {
        ERROR(logger) << "process " << pid << " is not running, cleaning up stale pid file";
        FSUtil::Unlink(pidfile, true);
        return -1;
    }

    int sig;
    if (sig_name == "reload") {
        sig = SIGHUP;
    } else if (sig_name == "stop" || sig_name == "quit") {
        sig = SIGTERM;
    } else {
        ERROR(logger) << "unknown signal: " << sig_name << " (expected: reload, stop, quit)";
        return -1;
    }

    if (kill(pid, sig) != 0) {
        ERROR(logger) << "failed to send signal to pid " << pid << ": " << strerror(errno);
        return -1;
    }

    INFO(logger) << "sent " << sig_name << " signal to process " << pid;
    return 0;
}

bool Application::getServer(const std::string& name, std::vector<TcpServer::ptr>& svrs)
{
    if (!server_registry_) {
        return false;
    }
    return server_registry_->getServer(name, svrs);
}

void Application::getAllServers(std::vector<TcpServer::ptr>& svrs)
{
    if (!server_registry_) {
        return;
    }
    server_registry_->getAllServers(svrs);
}

IServiceDiscovery::ptr Application::getServiceDiscovery() const
{
    std::lock_guard lock(service_discovery_mutex_);
    return service_discovery_;
}

void Application::setServiceDiscovery(IServiceDiscovery::ptr sd)
{
    std::lock_guard lock(service_discovery_mutex_);
    service_discovery_ = std::move(sd);
}

} // namespace chen
