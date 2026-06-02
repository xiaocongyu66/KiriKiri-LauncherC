#include <jni.h>

#include <oneapi/tbb/blocked_range.h>
#include <oneapi/tbb/parallel_for.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <sys/stat.h>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

struct FileInfo {
    std::string name;
    std::string lowerName;
    std::string path;
    std::string extension;
    bool isDirectory = false;
    bool isFile      = false;
    long long modifiedMillis = 0;
    long long size          = 0;
};

struct NativeGameEntry {
    std::string title;
    std::string gameDir;
    std::string coverPath;
    std::string backgroundPath;
    std::string launchFile;
    long long lastModified = 0;
};

struct LaunchCandidate {
    std::string path;
    std::string relative;
    std::string nameLower;
    std::string extension;
};

std::string ToLowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char ch) {
                       return static_cast<char>(std::tolower(ch));
                   });
    return value;
}

std::string Trim(std::string value) {
    auto isSpace = [](unsigned char ch) { return std::isspace(ch) != 0; };
    while(!value.empty() && isSpace(static_cast<unsigned char>(value.front())))
        value.erase(value.begin());
    while(!value.empty() && isSpace(static_cast<unsigned char>(value.back())))
        value.pop_back();
    if(value.size() >= 3 &&
       static_cast<unsigned char>(value[0]) == 0xef &&
       static_cast<unsigned char>(value[1]) == 0xbb &&
       static_cast<unsigned char>(value[2]) == 0xbf) {
        value.erase(0, 3);
    }
    return value;
}

std::string JoinPath(const std::string &dir, const std::string &name) {
    if(dir.empty() || dir == "/") return "/" + name;
    if(dir.back() == '/') return dir + name;
    return dir + "/" + name;
}

std::string BaseName(const std::string &path) {
    const size_t pos = path.find_last_of('/');
    if(pos == std::string::npos) return path;
    return path.substr(pos + 1);
}

std::string ExtensionLower(const std::string &name) {
    const size_t pos = name.find_last_of('.');
    if(pos == std::string::npos || pos + 1 >= name.size()) return {};
    return ToLowerAscii(name.substr(pos + 1));
}

std::string NameWithoutExtensionLower(const std::string &name) {
    const size_t pos = name.find_last_of('.');
    const std::string base =
        pos == std::string::npos ? name : name.substr(0, pos);
    return ToLowerAscii(base);
}

bool StartsWithDotName(const std::string &pathOrName) {
    const std::string name = BaseName(pathOrName);
    return !name.empty() && name[0] == '.';
}

long long StatModifiedMillis(const struct stat &st) {
    return static_cast<long long>(st.st_mtime) * 1000LL;
}

bool IsDirectoryPath(const std::string &path) {
    struct stat st {};
    return ::stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

std::vector<FileInfo> ListDirectory(const std::string &dir) {
    std::vector<FileInfo> result;
    DIR *handle = ::opendir(dir.c_str());
    if(!handle) return result;

    while(dirent *entry = ::readdir(handle)) {
        const char *name = entry->d_name;
        if(!name || std::strcmp(name, ".") == 0 ||
           std::strcmp(name, "..") == 0) {
            continue;
        }

        FileInfo info;
        info.name      = name;
        info.lowerName = ToLowerAscii(info.name);
        info.path      = JoinPath(dir, info.name);
        info.extension = ExtensionLower(info.name);

        struct stat st {};
        if(::stat(info.path.c_str(), &st) != 0) continue;
        info.isDirectory    = S_ISDIR(st.st_mode);
        info.isFile         = S_ISREG(st.st_mode);
        info.modifiedMillis = StatModifiedMillis(st);
        info.size           = static_cast<long long>(st.st_size);
        result.push_back(std::move(info));
    }

    ::closedir(handle);
    return result;
}

bool IsImageExtension(const std::string &extension) {
    return extension == "jpg" || extension == "jpeg" ||
           extension == "png" || extension == "webp";
}

bool IsLaunchExtension(const std::string &extension) {
    return extension == "xp3" || extension == "tjs" || extension == "ks";
}

bool IsGameDirectory(const std::vector<FileInfo> &children) {
    static const std::unordered_set<std::string> gameMarkers = {
        "startup.tjs", "start.tjs", "data.xp3",   "patch.xp3",
        "scenario.ks", "first.ks", "config.tjs",
    };

    bool hasDataDir     = false;
    bool hasScenarioDir = false;
    for(const FileInfo &child : children) {
        if(gameMarkers.find(child.lowerName) != gameMarkers.end())
            return true;
        if(child.isFile && (child.extension == "xp3" ||
                            child.extension == "ks")) {
            return true;
        }
        if(child.isDirectory && child.lowerName == "data") hasDataDir = true;
        if(child.isDirectory && child.lowerName == "scenario")
            hasScenarioDir = true;
    }
    return hasDataDir && hasScenarioDir;
}

std::optional<std::string> ReadFirstNonBlankLine(const std::string &path) {
    FILE *file = std::fopen(path.c_str(), "rb");
    if(!file) return std::nullopt;

    std::array<char, 4096> buffer {};
    for(int i = 0; i < 16 && std::fgets(buffer.data(),
                                        static_cast<int>(buffer.size()),
                                        file);
        ++i) {
        std::string line = Trim(buffer.data());
        if(!line.empty()) {
            std::fclose(file);
            return line;
        }
    }

    std::fclose(file);
    return std::nullopt;
}

std::string ReadSmallFile(const std::string &path, size_t limit = 65536) {
    FILE *file = std::fopen(path.c_str(), "rb");
    if(!file) return {};

    std::string data;
    data.resize(limit);
    const size_t read = std::fread(data.data(), 1, limit, file);
    data.resize(read);
    std::fclose(file);
    return data;
}

std::optional<std::string> JsonStringValue(const std::string &json,
                                           const char *key) {
    const std::string needle = std::string("\"") + key + "\"";
    size_t pos              = json.find(needle);
    while(pos != std::string::npos) {
        size_t colon = json.find(':', pos + needle.size());
        if(colon == std::string::npos) return std::nullopt;
        size_t quote = json.find('"', colon + 1);
        if(quote == std::string::npos) return std::nullopt;

        std::string value;
        bool escaped = false;
        for(size_t i = quote + 1; i < json.size(); ++i) {
            const char ch = json[i];
            if(escaped) {
                switch(ch) {
                case 'n': value.push_back('\n'); break;
                case 'r': value.push_back('\r'); break;
                case 't': value.push_back('\t'); break;
                default: value.push_back(ch); break;
                }
                escaped = false;
                continue;
            }
            if(ch == '\\') {
                escaped = true;
                continue;
            }
            if(ch == '"') {
                value = Trim(value);
                if(!value.empty()) return value;
                break;
            }
            value.push_back(ch);
        }
        pos = json.find(needle, pos + needle.size());
    }
    return std::nullopt;
}

std::string ReadTitle(const std::string &dir,
                      const std::vector<FileInfo> &children) {
    for(const FileInfo &child : children) {
        if(child.isFile &&
           (child.lowerName == "title.txt" ||
            child.lowerName == "game.txt")) {
            if(auto title = ReadFirstNonBlankLine(child.path)) return *title;
        }
    }

    for(const FileInfo &child : children) {
        if(child.isFile &&
           (child.lowerName == "package.json" ||
            child.lowerName == "info.json")) {
            const std::string json = ReadSmallFile(child.path);
            if(auto title = JsonStringValue(json, "title")) return *title;
            if(auto name = JsonStringValue(json, "name")) return *name;
        }
    }

    std::string title = BaseName(dir);
    std::replace(title.begin(), title.end(), '_', ' ');
    std::replace(title.begin(), title.end(), '-', ' ');
    title = Trim(title);
    return title.empty() ? dir : title;
}

std::vector<FileInfo> CollectImages(const std::string &dir,
                                    const std::vector<FileInfo> &children) {
    static const std::array<const char *, 8> commonFolders = {
        "image", "images", "bg",    "bgimage",
        "background", "system", "title", "ui",
    };

    std::vector<FileInfo> images;
    std::set<std::string> seen;
    auto addImage = [&](const FileInfo &file) {
        if(file.isFile && IsImageExtension(file.extension) &&
           seen.insert(file.path).second) {
            images.push_back(file);
        }
    };

    for(const FileInfo &child : children) addImage(child);

    for(const char *folder : commonFolders) {
        const std::string folderPath = JoinPath(dir, folder);
        for(const FileInfo &file : ListDirectory(folderPath)) addImage(file);
    }

    return images;
}

std::string LargestImagePath(const std::vector<FileInfo> &images) {
    if(images.empty()) return {};
    const auto it = std::max_element(images.begin(), images.end(),
                                     [](const FileInfo &lhs,
                                        const FileInfo &rhs) {
                                         return lhs.size < rhs.size;
                                     });
    return it == images.end() ? std::string() : it->path;
}

std::string ChooseCoverPath(const std::vector<FileInfo> &images) {
    static const std::array<const char *, 9> coverNames = {
        "cover", "icon", "title", "thumb", "thumbnail",
        "package", "bg", "background", "main",
    };
    for(const FileInfo &image : images) {
        const std::string base = NameWithoutExtensionLower(image.name);
        for(const char *coverName : coverNames) {
            if(base == coverName || base.find(coverName) != std::string::npos)
                return image.path;
        }
    }
    return LargestImagePath(images);
}

std::string ChooseBackgroundPath(const std::vector<FileInfo> &images) {
    for(const FileInfo &image : images) {
        const std::string base = NameWithoutExtensionLower(image.name);
        if(base.find("bg") != std::string::npos ||
           base.find("back") != std::string::npos ||
           base.find("title") != std::string::npos) {
            return image.path;
        }
    }
    return LargestImagePath(images);
}

bool StartsWith(const std::string &value, const char *prefix) {
    return value.rfind(prefix, 0) == 0;
}

int PreferredLaunchIndex(const std::string &nameLower) {
    static const std::array<const char *, 9> preferred = {
        "startup.tjs", "start.tjs", "data.xp3", "startup.xp3",
        "start.xp3", "main.xp3", "game.xp3", "first.ks",
        "scenario.ks",
    };
    for(size_t i = 0; i < preferred.size(); ++i) {
        if(nameLower == preferred[i]) return static_cast<int>(i);
    }
    return -1;
}

bool IsAssetArchiveBase(const std::string &base) {
    return base == "patch" || StartsWith(base, "patch") ||
           base == "bg" || StartsWith(base, "bg") ||
           base.find("image") != std::string::npos ||
           base.find("voice") != std::string::npos ||
           base.find("sound") != std::string::npos ||
           base.find("audio") != std::string::npos ||
           base.find("music") != std::string::npos ||
           base.find("movie") != std::string::npos ||
           base.find("video") != std::string::npos ||
           base.find("effect") != std::string::npos;
}

int LaunchNamePriority(const std::string &nameLower,
                       const std::string &extension) {
    const int preferred = PreferredLaunchIndex(nameLower);
    if(preferred >= 0) return preferred;

    const std::string base = NameWithoutExtensionLower(nameLower);
    if(extension == "xp3") {
        if(base == "boot") return 20;
        if(base == "main" || base == "game" || base == "scenario" ||
           base == "script") {
            return 30;
        }
        if(StartsWith(base, "data")) return 40;
        if(IsAssetArchiveBase(base)) return 300;
        return 80;
    }
    if(extension == "tjs") {
        if(base == "main" || base == "boot" || base == "game") return 60;
        return 90;
    }
    if(extension == "ks") {
        if(base == "first" || base == "scenario") return 70;
        return 100;
    }
    return 500;
}

std::string ChooseLaunchFile(const std::vector<FileInfo> &children) {
    const FileInfo *best = nullptr;
    int bestRank = 0;
    for(const FileInfo &child : children) {
        if(!child.isFile || !IsLaunchExtension(child.extension)) continue;
        const int rank = LaunchNamePriority(child.lowerName, child.extension);
        if(!best || rank < bestRank ||
           (rank == bestRank && child.lowerName < best->lowerName)) {
            best = &child;
            bestRank = rank;
        }
    }
    return best ? best->path : std::string();
}

NativeGameEntry BuildGameEntry(const std::string &dir,
                               const std::vector<FileInfo> &children) {
    NativeGameEntry entry;
    entry.title      = ReadTitle(dir, children);
    entry.gameDir    = dir;
    const auto images = CollectImages(dir, children);
    entry.coverPath  = ChooseCoverPath(images);
    entry.backgroundPath = ChooseBackgroundPath(images);
    entry.launchFile = ChooseLaunchFile(children);

    struct stat dirStat {};
    entry.lastModified =
        ::stat(dir.c_str(), &dirStat) == 0 ? StatModifiedMillis(dirStat) : 0;
    for(const FileInfo &child : children)
        entry.lastModified = std::max(entry.lastModified,
                                      child.modifiedMillis);
    return entry;
}

using ProgressCallback = std::function<void(const std::string &)>;

std::vector<NativeGameEntry> ScanGames(const std::string &root, int maxDepth,
                                       const ProgressCallback &progress) {
    maxDepth = std::max(0, std::min(maxDepth, 32));
    if(root.empty() || !IsDirectoryPath(root)) return {};

    std::vector<NativeGameEntry> games;
    std::vector<std::string> current {root};

    for(int depth = 0; depth <= maxDepth && !current.empty(); ++depth) {
        for(const std::string &dir : current) {
            if(progress) progress(dir);
        }

        std::vector<std::optional<NativeGameEntry>> found(current.size());
        std::vector<std::vector<std::string>> nextByDir(current.size());

        oneapi::tbb::parallel_for(
            oneapi::tbb::blocked_range<size_t>(0, current.size()),
            [&](const oneapi::tbb::blocked_range<size_t> &range) {
                for(size_t i = range.begin(); i != range.end(); ++i) {
                    const std::string &dir = current[i];
                    if(StartsWithDotName(dir)) continue;

                    const auto children = ListDirectory(dir);
                    if(children.empty()) continue;

                    if(IsGameDirectory(children)) {
                        found[i] = BuildGameEntry(dir, children);
                        continue;
                    }

                    if(depth >= maxDepth) continue;
                    for(const FileInfo &child : children) {
                        if(child.isDirectory && !StartsWithDotName(child.name))
                            nextByDir[i].push_back(child.path);
                    }
                }
            });

        std::vector<std::string> next;
        for(size_t i = 0; i < current.size(); ++i) {
            if(found[i]) games.push_back(std::move(*found[i]));
            for(std::string &path : nextByDir[i]) next.push_back(std::move(path));
        }
        current = std::move(next);
    }

    std::sort(games.begin(), games.end(),
              [](const NativeGameEntry &lhs, const NativeGameEntry &rhs) {
                  if(lhs.lastModified != rhs.lastModified)
                      return lhs.lastModified > rhs.lastModified;
                  return ToLowerAscii(lhs.title) < ToLowerAscii(rhs.title);
              });
    return games;
}

int LaunchRank(const LaunchCandidate &candidate) {
    int depthPenalty = 0;
    for(char ch : candidate.relative) {
        if(ch == '/') depthPenalty += 20;
    }
    return LaunchNamePriority(candidate.nameLower, candidate.extension) +
           depthPenalty;
}

std::string RelativePath(const std::string &root, const std::string &path) {
    if(path.size() <= root.size()) return BaseName(path);
    if(path.compare(0, root.size(), root) == 0) {
        size_t offset = root.size();
        if(offset < path.size() && path[offset] == '/') ++offset;
        return path.substr(offset);
    }
    return BaseName(path);
}

std::vector<std::string> ListLaunchCandidates(const std::string &root) {
    if(root.empty() || !IsDirectoryPath(root)) return {};

    std::vector<LaunchCandidate> candidates;
    std::vector<std::string> current {root};
    constexpr int maxDepth = 3;

    for(int depth = 0; depth <= maxDepth && !current.empty(); ++depth) {
        std::vector<std::vector<LaunchCandidate>> candidatesByDir(
            current.size());
        std::vector<std::vector<std::string>> nextByDir(current.size());

        oneapi::tbb::parallel_for(
            oneapi::tbb::blocked_range<size_t>(0, current.size()),
            [&](const oneapi::tbb::blocked_range<size_t> &range) {
                for(size_t i = range.begin(); i != range.end(); ++i) {
                    for(const FileInfo &child : ListDirectory(current[i])) {
                        if(child.isFile && IsLaunchExtension(child.extension)) {
                            candidatesByDir[i].push_back(LaunchCandidate {
                                child.path,
                                ToLowerAscii(RelativePath(root, child.path)),
                                child.lowerName,
                                child.extension,
                            });
                        } else if(depth < maxDepth && child.isDirectory &&
                                  !StartsWithDotName(child.name)) {
                            nextByDir[i].push_back(child.path);
                        }
                    }
                }
            });

        std::vector<std::string> next;
        for(size_t i = 0; i < current.size(); ++i) {
            for(auto &candidate : candidatesByDir[i])
                candidates.push_back(std::move(candidate));
            for(auto &dir : nextByDir[i]) next.push_back(std::move(dir));
        }
        current = std::move(next);
    }

    std::sort(candidates.begin(), candidates.end(),
              [](const LaunchCandidate &lhs, const LaunchCandidate &rhs) {
                  const int lhsRank = LaunchRank(lhs);
                  const int rhsRank = LaunchRank(rhs);
                  if(lhsRank != rhsRank) return lhsRank < rhsRank;
                  return lhs.relative < rhs.relative;
              });

    std::vector<std::string> result;
    const size_t limit = std::min<size_t>(80, candidates.size());
    result.reserve(limit);
    for(size_t i = 0; i < limit; ++i) result.push_back(candidates[i].path);
    return result;
}

std::string JStringToString(JNIEnv *env, jstring value) {
    if(!value) return {};
    const char *chars = env->GetStringUTFChars(value, nullptr);
    if(!chars) return {};
    std::string result(chars);
    env->ReleaseStringUTFChars(value, chars);
    return result;
}

jstring NewNullableString(JNIEnv *env, const std::string &value) {
    return value.empty() ? nullptr : env->NewStringUTF(value.c_str());
}

jobjectArray ToJavaGameEntries(JNIEnv *env,
                               const std::vector<NativeGameEntry> &entries) {
    jclass entryClass = env->FindClass("org/github/krkr2/GameEntry");
    if(!entryClass) return nullptr;
    jmethodID ctor = env->GetMethodID(
        entryClass, "<init>",
        "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;"
        "Ljava/lang/String;Ljava/lang/String;J)V");
    if(!ctor) return nullptr;

    jobjectArray array = env->NewObjectArray(
        static_cast<jsize>(entries.size()), entryClass, nullptr);
    if(!array) return nullptr;

    for(size_t i = 0; i < entries.size(); ++i) {
        const NativeGameEntry &entry = entries[i];
        jstring title = env->NewStringUTF(entry.title.c_str());
        jstring gameDir = env->NewStringUTF(entry.gameDir.c_str());
        jstring cover = NewNullableString(env, entry.coverPath);
        jstring background = NewNullableString(env, entry.backgroundPath);
        jstring launch = NewNullableString(env, entry.launchFile);
        jobject item = env->NewObject(
            entryClass, ctor, title, gameDir, cover, background, launch,
            static_cast<jlong>(entry.lastModified));
        if(env->ExceptionCheck()) return array;
        env->SetObjectArrayElement(array, static_cast<jsize>(i), item);

        if(title) env->DeleteLocalRef(title);
        if(gameDir) env->DeleteLocalRef(gameDir);
        if(cover) env->DeleteLocalRef(cover);
        if(background) env->DeleteLocalRef(background);
        if(launch) env->DeleteLocalRef(launch);
        if(item) env->DeleteLocalRef(item);
    }
    return array;
}

jobjectArray ToJavaStringArray(JNIEnv *env,
                               const std::vector<std::string> &values) {
    jclass stringClass = env->FindClass("java/lang/String");
    if(!stringClass) return nullptr;
    jobjectArray array = env->NewObjectArray(
        static_cast<jsize>(values.size()), stringClass, nullptr);
    if(!array) return nullptr;

    for(size_t i = 0; i < values.size(); ++i) {
        jstring value = env->NewStringUTF(values[i].c_str());
        env->SetObjectArrayElement(array, static_cast<jsize>(i), value);
        if(value) env->DeleteLocalRef(value);
    }
    return array;
}

ProgressCallback MakeProgressCallback(JNIEnv *env, jobject progress) {
    if(!progress) return {};
    jclass progressClass = env->GetObjectClass(progress);
    if(!progressClass) return {};
    jmethodID onPath =
        env->GetMethodID(progressClass, "onPath", "(Ljava/lang/String;)V");
    if(!onPath) return {};

    return [env, progress, onPath](const std::string &path) {
        if(env->ExceptionCheck()) return;
        jstring value = env->NewStringUTF(path.c_str());
        if(!value) return;
        env->CallVoidMethod(progress, onPath, value);
        env->DeleteLocalRef(value);
    };
}

} // namespace

extern "C" JNIEXPORT jobjectArray JNICALL
Java_org_github_krkr2_GameScanner_nativeScan(JNIEnv *env, jobject,
                                             jstring rootPath, jint maxDepth,
                                             jobject progress) {
    const std::string root = JStringToString(env, rootPath);
    auto entries = ScanGames(root, static_cast<int>(maxDepth),
                             MakeProgressCallback(env, progress));
    return ToJavaGameEntries(env, entries);
}

extern "C" JNIEXPORT jobjectArray JNICALL
Java_org_github_krkr2_GameScanner_nativeListLaunchCandidates(JNIEnv *env,
                                                             jobject,
                                                             jstring rootPath) {
    const std::string root = JStringToString(env, rootPath);
    return ToJavaStringArray(env, ListLaunchCandidates(root));
}
