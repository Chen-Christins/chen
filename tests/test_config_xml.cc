/**
 * @file test_config_xml.cc
 * @brief 配置模块 XML 兼容性测试
 * @author Christins
 * @date 2026-08-24
 */
#include "chen/config/config.h"

#include <tinyxml2.h>

#include <unistd.h>

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <list>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {

chen::ConfigVar<int>::ptr g_port = chen::Config::Lookup("xml_test.port", 0, "xml test port");
chen::ConfigVar<std::string>::ptr g_name = chen::Config::Lookup("xml_test.name", std::string(), "xml test name");
chen::ConfigVar<bool>::ptr g_enable = chen::Config::Lookup("xml_test.enable", false, "xml test enable");
chen::ConfigVar<std::vector<int>>::ptr g_vec =
    chen::Config::Lookup("xml_test.vec", std::vector<int>(), "xml test vec");
chen::ConfigVar<std::list<int>>::ptr g_list =
    chen::Config::Lookup("xml_test.list", std::list<int>(), "xml test list");
chen::ConfigVar<std::set<int>>::ptr g_set =
    chen::Config::Lookup("xml_test.set", std::set<int>(), "xml test set");
chen::ConfigVar<std::map<std::string, int>>::ptr g_map =
    chen::Config::Lookup("xml_test.map", std::map<std::string, int>(), "xml test map");
chen::ConfigVar<std::map<std::string, std::map<std::string, std::string>>>::ptr g_nested_map =
    chen::Config::Lookup("xml_test.nested", std::map<std::string, std::map<std::string, std::string>>(), "xml test nested map");
chen::ConfigVar<std::vector<std::map<std::string, std::string>>>::ptr g_servers =
    chen::Config::Lookup("xml_test.servers", std::vector<std::map<std::string, std::string>>(), "xml test servers");
chen::ConfigVar<std::vector<std::string>>::ptr g_tokens =
    chen::Config::Lookup("xml_test.tokens", std::vector<std::string>(), "xml test single item list");

chen::ConfigVar<int>::ptr g_dir_port = chen::Config::Lookup("dir_test.port", 0, "dir test port");
chen::ConfigVar<std::string>::ptr g_dir_host = chen::Config::Lookup("dir_test.host", std::string(), "dir test host");

void test_load_from_xml() {
    std::cout << "=== test_load_from_xml ===" << std::endl;

    const char* xml = R"(<config>
    <server>
        <work_path>/tmp/chen_test</work_path>
    </server>
    <xml_test>
        <port>8080</port>
        <name>hello</name>
        <enable>true</enable>
        <vec>
            <item>1</item>
            <item>2</item>
            <item>3</item>
        </vec>
        <list>
            <item>4</item>
            <item>5</item>
        </list>
        <set>
            <item>7</item>
            <item>8</item>
        </set>
        <map>
            <a>10</a>
            <b>20</b>
        </map>
        <nested>
            <blog>
                <host>127.0.0.1</host>
                <port>6379</port>
            </blog>
        </nested>
        <servers>
            <server>
                <id>1</id>
                <type>http</type>
            </server>
            <server>
                <id>2</id>
                <type>rpc</type>
            </server>
        </servers>
        <tokens>
            <item>only-one</item>
        </tokens>
    </xml_test>
</config>)";

    tinyxml2::XMLDocument doc;
    assert(doc.Parse(xml) == tinyxml2::XML_SUCCESS);
    assert(doc.RootElement() != nullptr);
    chen::Config::LoadFromXml(*doc.RootElement());

    // 标量
    assert(g_port->getValue() == 8080);
    assert(g_name->getValue() == "hello");
    assert(g_enable->getValue() == true);

    // 序列
    assert(g_vec->getValue() == (std::vector<int>{1, 2, 3}));
    assert(g_list->getValue() == (std::list<int>{4, 5}));
    assert(g_set->getValue() == (std::set<int>{7, 8}));

    // map
    auto m = g_map->getValue();
    assert(m.size() == 2);
    assert(m["a"] == 10);
    assert(m["b"] == 20);

    // 嵌套 map
    auto nm = g_nested_map->getValue();
    assert(nm.size() == 1);
    assert(nm["blog"]["host"] == "127.0.0.1");
    assert(nm["blog"]["port"] == "6379");

    // 对象序列（重复同名标签）
    auto servers = g_servers->getValue();
    assert(servers.size() == 2);
    assert(servers[0]["id"] == "1");
    assert(servers[0]["type"] == "http");
    assert(servers[1]["id"] == "2");
    assert(servers[1]["type"] == "rpc");

    // 单元素序列（保留标记 item）
    auto tokens = g_tokens->getValue();
    assert(tokens.size() == 1);
    assert(tokens[0] == "only-one");

    std::cout << "PASS" << std::endl;
}

void test_load_from_conf_dir() {
    std::cout << "=== test_load_from_conf_dir ===" << std::endl;

    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / ("chen_test_xml_" + std::to_string(::getpid()));
    fs::create_directories(dir);

    const char* xml = R"(<config>
    <dir_test>
        <port>9090</port>
        <host>example.com</host>
    </dir_test>
</config>)";
    {
        std::ofstream ofs(dir / "test.xml");
        ofs << xml;
    }

    chen::Config::LoadFromConfDir(dir.string());

    assert(g_dir_port->getValue() == 9090);
    assert(g_dir_host->getValue() == "example.com");

    fs::remove_all(dir);
    std::cout << "PASS" << std::endl;
}

} // namespace

int main() {
    test_load_from_xml();
    test_load_from_conf_dir();

    std::cout << "\nAll tests passed!" << std::endl;
    return 0;
}
