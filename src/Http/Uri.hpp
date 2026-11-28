/**
 * @file Uri.hpp
 * @brief 
 * @author Christins
 * @date 2025-01-19
 * @copyright GPL-3.0
 */
#pragma once

#include <memory>

#include "../Socket/Address.hpp"

/*
    foo://user@chen.com:8042/over/there?name=ferret#nose
      \_/   \______________/\_________/ \_________/ \__/
       |           |            |            |        |
    scheme     authority       path        query   fragment
*/
namespace chen {

class Uri {
public:
    typedef std::shared_ptr<Uri> ptr;

    static Uri::ptr Create(const std::string& uri);
    Uri();

    const std::string& getPath() const;
    const std::string& getScheme() const { return scheme_; }
    const std::string& getHost() const { return host_; }
    const std::string& getUserinfo() const { return userinfo_; }
    const std::string& getQuery() const { return query_; }
    const std::string& getFragment() const { return fragment_; }
    int32_t getPort() const;

    void setScheme(const std::string& v) { scheme_ = v; }
    void setUserinfo(const std::string& v) { userinfo_ = v; }
    void setHost(const std::string& v) { host_ = v; }
    void setPath(const std::string& v) { path_ = v; }
    void setQuery(const std::string& v) { query_ = v; }
    void setFragment(const std::string& v) { fragment_ = v; }
    void setPort(int32_t v) { port_ = v; }

    std::ostream& dump(std::ostream& os) const;
    std::string toString() const;
    Address::ptr createAddress() const;
private:
    bool isDefaultPort() const;
private:
    std::string scheme_;
    std::string host_;
    std::string path_;
    std::string query_;
    std::string fragment_;
    std::string userinfo_;
    int32_t port_;
};

}
