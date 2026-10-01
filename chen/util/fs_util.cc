#include "fs_util.h"

#include <csignal>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unistd.h>

#include <sys/stat.h>

namespace chen {

static int __lstat(const char* file, struct stat* st = nullptr) {
    struct stat lst;
    int ret = lstat(file, &lst);
    if (st) {
        *st = lst;
    }
    return ret;
}

static int __mkdir(const char* dirname) {
    if (access(dirname, F_OK) == 0) {
        return 0;
    }
    return mkdir(dirname, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
}

void FSUtil::ListAllFile(std::vector<std::string>& files, const std::string& path, const std::string& subfix) {
    if (access(path.c_str(), 0) != 0) {
        return ;
    }
    DIR* dir = opendir(path.c_str());
    if (dir == nullptr) {
        return ;
    }
    struct dirent* dp = nullptr;
    while ((dp = readdir(dir)) != nullptr) {
        if (dp->d_type == DT_DIR) {
            if (!strcmp(dp->d_name, ".") || !strcmp(dp->d_name, "..")) {
                continue ;
            }
            ListAllFile(files, path + "/" + dp->d_name, subfix);
        } else if (dp->d_type == DT_REG) {
            std::string filename(dp->d_name);
            if (subfix.empty()) {
                files.push_back(path + "/" + filename);
            } else {
                if (filename.size() < subfix.size()) {
                    continue ;
                }
                if (filename.substr(filename.length() - subfix.size()) == subfix) {
                    files.push_back(path + "/" + filename);
                }
            }
        }
    }
    closedir(dir);
}

bool FSUtil::IsRunningPidfile(const std::string& pidfile) {
    if (__lstat(pidfile.c_str()) != 0) {
        return false;
    }
    std::ifstream ifs(pidfile);
    std::string line;
    if (!ifs || !std::getline(ifs, line)) {
        return false;
    }
    if (line.empty()) {
        return false;
    }
    pid_t pid = atoi(line.c_str());
    if (pid <= 1) {
        return false;
    }
    if (kill(pid, 0) != 0) {
        return false;
    }
    return true;
}

bool FSUtil::Mkdir(const std::string& dirname) {
    if (__lstat(dirname.c_str()) == 0) {
        return true;
    }
    char* path = strdup(dirname.c_str());
    char* ptr = strchr(path + 1, '/');
    do {
        for (; ptr; *ptr = '/', ptr = strchr(ptr + 1, '/')) {
            *ptr = '\0';
            if (__mkdir(path) != 0) {
                break;
            }
        }
        if (ptr != nullptr) {
            break;
        } else if (__mkdir(path) != 0) {
            break;
        }
        free(path);
        return true;
    } while (0);

    free(path);
    return false;
}

std::string FSUtil::Dirname(const std::string& filename) {
    if (filename.empty()) {
        return ".";
    }
    auto pos = filename.rfind('/');
    if (pos == 0) {
        return "/";
    } else if (pos == std::string::npos) {
        return ".";
    } else {
        return filename.substr(0, pos);
    }
}

std::string FSUtil::Basename(const std::string& path) {
    if (path.empty()) {
        return path;
    }
    auto pos = path.rfind('/');
    if (pos == std::string::npos) {
        return path;
    } else {
        return path.substr(pos + 1);
    }
}

bool FSUtil::OpenForWrite(std::ofstream& ofs, const std::string& filename, std::ios_base::openmode mode) {
    ofs.open(filename.c_str(), mode);
    if (!ofs.is_open()) {
        std::string dir = Dirname(filename);
        Mkdir(dir);
        ofs.open(filename.c_str(), mode);
    }
    return ofs.is_open();
}

bool FSUtil::Unlink(const std::string& filename, bool exist) {
    if (!exist && __lstat(filename.c_str())) {
        return true;
    }
    return ::unlink(filename.c_str()) == 0;
}

std::string FSUtil::ReadFileToString(const std::string& filename) {
    std::ifstream ifs(filename);
    if (!ifs.is_open()) {
        return "";
    }
    std::stringstream ss;
    ss << ifs.rdbuf();
    return ss.str();
}

std::pair<int32_t, uint64_t> FSUtil::DirSize(const std::string& path) {
    uint64_t total_size = 0;
    if (access(path.c_str(), 0) != 0) {
        return {0, total_size};
    }
    int32_t count = 0;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(path)) {
        if (entry.is_regular_file()) {
            total_size += entry.file_size();
            count += 1;
        }
    }
    return {count, total_size};
}

bool FSUtil::Exists(const std::string& path) {
    return std::filesystem::exists(path);
}

bool FSUtil::IsDirectory(const std::string& path) {
    return std::filesystem::is_directory(path);
}

int64_t FSUtil::FileSize(const std::string& filename) {
    std::error_code ec;
    auto size = std::filesystem::file_size(filename, ec);
    if (ec) { return -1; }
    return static_cast<int64_t>(size);
}

std::string FSUtil::GetCwd() {
    return std::filesystem::current_path().string();
}

} // namespace chen
