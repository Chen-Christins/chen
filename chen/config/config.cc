#include "config.h"

#include <sys/stat.h>

#include <tinyxml2.h>

#include "../util/env.h"

namespace chen {

static Logger::ptr logger = LOG_NAME("system");

ConfigVarBase::ptr Config::LookupBase(const std::string& name) {
    auto it = GetDatas().find(name);
    return it == GetDatas().end() ? nullptr : it->second;
}

static void ListAllMember(const std::string& prefix
        ,const YAML::Node& node
        ,std::list<std::pair<std::string, const YAML::Node>>& output) {
    if (prefix.find_first_not_of("abcdefghijklmnopqrstuvwxyz._0123456789")
            != std::string::npos) {
        ERROR(logger) << "Config invalid name: " << prefix << " : " << node;
        return ;
    }
    output.push_back({prefix, node});
    if (node.IsMap()) {
        for (auto it = node.begin(); it != node.end(); ++it) {
            ListAllMember(prefix.empty() ? it->first.Scalar()
                : prefix + "." + it->first.Scalar(), it->second, output);
        }
    }
}

void Config::LoadFromYaml(const YAML::Node& root) {
    std::list<std::pair<std::string, const YAML::Node>> all_nodes;
    ListAllMember("", root, all_nodes);

    for (auto& [x, y] : all_nodes) {
        std::string key = x;
        if (key.empty()) {
            continue;
        }
        std::transform(key.begin(), key.end(), key.begin(), ::tolower);
        ConfigVarBase::ptr var = LookupBase(key);

        if (var) {
            if (y.IsScalar()) {
                var->fromString(y.Scalar());
            } else {
                std::stringstream ss;
                ss << y;
                var->fromString(ss.str());
            }
        }
    }
}

static YAML::Node XmlToYaml(const tinyxml2::XMLElement* elem) {
    // 按标签名分组子元素
    std::map<std::string, std::vector<const tinyxml2::XMLElement*>> groups;
    for (const tinyxml2::XMLElement* child = elem->FirstChildElement();
            child; child = child->NextSiblingElement()) {
        groups[child->Name()].push_back(child);
    }

    // 无子元素，视为标量
    if (groups.empty()) {
        const char* text = elem->GetText();
        std::string value = text ? text : "";
        auto begin = value.find_first_not_of(" \t\r\n");
        auto end = value.find_last_not_of(" \t\r\n");
        if (begin == std::string::npos) {
            value.clear();
        } else {
            value = value.substr(begin, end - begin + 1);
        }
        return YAML::Node(value);
    }

    // 判定是否为序列：唯一标签名且重复出现(>=2)，或保留的列表标记(item/entry/value)
    bool is_sequence = false;
    if (groups.size() == 1) {
        const auto& [tag, vec] = *groups.begin();
        is_sequence = vec.size() >= 2 || tag == "item" || tag == "entry" || tag == "value";
    }

    YAML::Node node;
    if (is_sequence) {
        for (const auto& child : groups.begin()->second) {
            node.push_back(XmlToYaml(child));
        }
        return node;
    }

    // 否则视为 map，重复出现的同名子元素折叠为序列
    for (const auto& [tag, vec] : groups) {
        if (vec.size() > 1) {
            YAML::Node seq;
            for (const auto& child : vec) {
                seq.push_back(XmlToYaml(child));
            }
            node[tag] = seq;
        } else {
            node[tag] = XmlToYaml(vec[0]);
        }
    }
    return node;
}

void Config::LoadFromXml(const tinyxml2::XMLElement& root) {
    LoadFromYaml(XmlToYaml(&root));
}

static std::map<std::string, uint64_t> s_file2modifytime;
static std::mutex mtx;

/**
 * @brief 获取文件修改时间（纳秒精度）
 * @details 秒级精度会漏掉同一秒内的多次修改，导致热加载读到半截配置
 */
static uint64_t GetModifyTimeNs(const struct stat& st) {
#if defined(__linux__)
    return (uint64_t)st.st_mtim.tv_sec * 1000000000ull + (uint64_t)st.st_mtim.tv_nsec;
#elif defined(__APPLE__)
    return (uint64_t)st.st_mtimespec.tv_sec * 1000000000ull + (uint64_t)st.st_mtimespec.tv_nsec;
#else
    return (uint64_t)st.st_mtime * 1000000000ull;
#endif
}

bool Config::LoadFromFile(const std::string& filepath, bool force) {
    struct stat st;
    if (lstat(filepath.c_str(), &st) != 0) {
        ERROR(logger) << "LoadFromFile stat failed: " << filepath;
        return false;
    }
    uint64_t mtime_ns = GetModifyTimeNs(st);
    {
        std::lock_guard lock(mtx);
        if (!force && s_file2modifytime[filepath] == mtime_ns) {
            return true;
        }
        s_file2modifytime[filepath] = mtime_ns;
    }
    try {
        if (filepath.size() >= 4 && filepath.compare(filepath.size() - 4, 4, ".xml") == 0) {
            tinyxml2::XMLDocument doc;
            if (doc.LoadFile(filepath.c_str()) != tinyxml2::XML_SUCCESS) {
                ERROR(logger) << "LoadFromFile file=" << filepath << " failed";
                return false;
            }
            LoadFromXml(*doc.RootElement());
        } else {
            YAML::Node root = YAML::LoadFile(filepath);
            LoadFromYaml(root);
        }
        INFO(logger) << "LoadFromFile file=" << filepath << " ok";
        return true;
    } catch (...) {
        ERROR(logger) << "LoadFromFile file=" << filepath << " failed";
        return false;
    }
}

void Config::LoadFromConfDir(const std::string& path, bool force) {
    std::string absolute_path = EnvMgr::GetInstance()->getAbsolutePath(path);
    std::vector<std::string> files;
    FSUtil::ListAllFile(files, absolute_path, ".yml");
    FSUtil::ListAllFile(files, absolute_path, ".xml");
    for (auto& i : files) {
        LoadFromFile(i, force);
    }
}

void Config::Visit(std::function<void(ConfigVarBase::ptr)> callback) {
	std::shared_lock lock(GetMutex());
	ConfigVarMap& m = GetDatas();
	for (auto it = m.begin(); it != m.end(); ++it) {
		callback(it->second);
	}
}

}
