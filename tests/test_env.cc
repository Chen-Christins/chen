#include "chen/util/env.h"
#include <iostream>
#include <fstream>
#include <unistd.h>

struct A {
    A() {
        std::ifstream ifs("/proc/" + std::to_string(getpid()) + "/cmdline", std::ios::binary);
        std::string content;
        content.resize(4096);

        ifs.read(&content[0], content.size());
        content.resize(ifs.gcount());

        for (size_t i = 0; i < content.size(); ++i) {
            std::cout << i << " - " << content[i] << " - " << (int)content[i] << std::endl;
        }
        std::cout << content << std::endl;
    }
};

A a;

int main(int argc, char** argv) {

    chen::EnvMgr::GetInstance()->addHelp("s", "start with the terminal");
    chen::EnvMgr::GetInstance()->addHelp("d", "run as daemon");
    chen::EnvMgr::GetInstance()->addHelp("p", "print help");
    if(!chen::EnvMgr::GetInstance()->init(argc, argv)) {
        chen::EnvMgr::GetInstance()->printHelp();
        return 0;
    }

    std::cout << "exe=" << chen::EnvMgr::GetInstance()->getExe() << std::endl;
    std::cout << "cwd=" << chen::EnvMgr::GetInstance()->getCwd() << std::endl;

    std::cout << "PATH=" << chen::EnvMgr::GetInstance()->getEnv("HADOOP_HOME", "xxxx") << std::endl;

    std::cout << "set Env " << chen::EnvMgr::GetInstance()->setEnv("TEST", "1") << std::endl;
    std::cout << "TEST=" << chen::EnvMgr::GetInstance()->getEnv("TEST", "xxxx") << std::endl;


    if (chen::EnvMgr::GetInstance()->has("p")) {
        chen::EnvMgr::GetInstance()->printHelp();
    }

    return 0;
}