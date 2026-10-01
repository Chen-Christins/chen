#!/bin/sh

command_error_exit() {
    $*
    if [ $? -ne 0 ]
    then
        exit 1
    fi
}

INSTALL_DIR="$(pwd)"/3rdparty

command_error_exit mkdir -p $INSTALL_DIR
command_error_exit cd $INSTALL_DIR

# 安装其他相关的依赖库
if command -v apt-get >/dev/null 2>&1; then
    # Ubuntu/Debian 系统
    command_error_exit sudo apt-get update
    command_error_exit sudo apt-get install -y libtool \
        gcc \
        g++ \
        autoconf \
        automake \
        cmake \
        libboost-all-dev \
        libsqlite3-dev \
        libssl-dev \
        libevent-dev \
        libmysqlclient-dev \
        redis \
        libnghttp2-dev \
        git \
        wget \
        zip
elif command -v yum >/dev/null 2>&1; then
    # CentOS/RHEL 系统
    command_error_exit sudo yum install libtool \
        gcc \
        g++ \
        autoconf \
        automake \
        cmake \
        boost-devel \
        sqlite-devel \
        openssl-devel \
        libevent-devel \
        nghttp2-devel \
        git \
        wget \
        zip
else
    echo "Error: Neither apt-get nor yum package manager found"
    exit 1
fi 

# 安装yaml-cpp
command_error_exit wget https://github.com/jbeder/yaml-cpp/archive/refs/tags/yaml-cpp-0.9.0.zip
command_error_exit unzip yaml-cpp-0.9.0.zip
command_error_exit cd yaml-cpp-yaml-cpp-0.9.0
command_error_exit mkdir build && cd build
command_error_exit cmake -D BUILD_SHARED_LIBS=ON ..
command_error_exit make -j4
command_error_exit sudo make install

command_error_exit cd $INSTALL_DIR

# 安装ragel-6.10
command_error_exit curl -O http://www.colm.net/files/ragel/ragel-6.10.tar.gz
command_error_exit tar -zxvf ragel-6.10.tar.gz
command_error_exit cd ragel-6.10
command_error_exit ./configure
command_error_exit make -j4
command_error_exit sudo make install

command_error_exit cd $INSTALL_DIR

# 安装tinyxml2
command_error_exit git clone https://github.com/leethomason/tinyxml2.git
command_error_exit cd tinyxml2
command_error_exit mkdir build
command_error_exit cd build
command_error_exit cmake -D BUILD_SHARED_LIBS=ON ..
command_error_exit make -j4
command_error_exit sudo make install

command_error_exit cd $INSTALL_DIR

# 安装hiredis_vip
command_error_exit git clone https://github.com/Chen-Christins/hiredis-vip.git
command_error_exit cd hiredis-vip
command_error_exit make -j4
command_error_exit sudo make install

command_error_exit cd $INSTALL_DIR

command_error_exit git clone https://github.com/Chen-Christins/jsoncpp.git
command_error_exit cd jsoncpp
command_error_exit mkdir build
command_error_exit cd build
command_error_exit cmake -D BUILD_SHARED_LIBS=ON ..
command_error_exit make -j4
command_error_exit sudo make install

command_error_exit cd $INSTALL_DIR

command_error_exit wget https://github.com/protocolbuffers/protobuf/releases/download/v3.12.4/protobuf-all-3.12.4.tar.gz
command_error_exit tar -zxvf protobuf-all-3.12.4.tar.gz
command_error_exit cd protobuf-3.12.4
command_error_exit ./configure
command_error_exit make -j4
command_error_exit sudo make install

command_error_exit cd $INSTALL_DIR

command_error_exit sudo ldconfig

echo "the dependencies has successfully installed at $INSTALL_DIR." 