#include <cstdint>
#include <uchardet.h>
#include <zlib.h>
#include <atomic>
#include <optional>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "TextStream.h"

#include <boost/locale.hpp>
#include <oneapi/tbb/blocked_range.h>
#include <oneapi/tbb/parallel_for.h>
#include <opencv2/core/hal/interface.h>
#include <sqlite3.h>
#include <spdlog/spdlog.h>

#include "MsgIntf.h"
#include "UtilStreams.h"
#include "tjsError.h"
#include "CharacterSet.h"
#include "BinaryStream.h"
#include "Platform.h"
#include "StorageIntf.h"

static std::string G_DefaultReadEncoding = "UTF-8";
static std::mutex G_ReadEncodingCacheMutex;
static std::unordered_map<std::string, std::string> G_ReadEncodingCache;
static std::unordered_set<std::string> G_ReadEncodingCacheLoadedGames;
static bool G_ReadEncodingPersistentCachePruned = false;

static std::string canonicalEncodingName(const std::string &encoding);

static std::string trimTrailingPathSeparators(std::string path) {
    while(path.size() > 1 && (path.back() == '/' || path.back() == '\\'))
        path.pop_back();
    return path;
}

static std::string getCurrentGameEncodingCachePath() {
    try {
        const ttstr appPath = TVPGetAppPath();
        const ttstr nativePath = TVPGetLocallyAccessibleName(appPath);
        std::string key = !nativePath.IsEmpty() ? nativePath.AsStdString()
                                                : appPath.AsStdString();
        return trimTrailingPathSeparators(key);
    } catch(...) {
        return "";
    }
}

static std::string getReadEncodingMemoryKey(const std::string &gamePath) {
    return gamePath.empty() ? std::string("<process>") : gamePath;
}

static bool isPrunableLocalGamePath(const std::string &path) {
    return !path.empty() && path.find("://") == std::string::npos &&
        path.find('>') == std::string::npos;
}

static bool localGamePathExists(const std::string &path) {
    if(!isPrunableLocalGamePath(path))
        return true;
    std::error_code ec;
    return std::filesystem::exists(std::filesystem::u8path(path), ec);
}

static std::string getReadEncodingCacheDbPath() {
    try {
        const ttstr prefPath = TVPGetInternalPreferencePath();
        std::filesystem::path dir =
            std::filesystem::u8path(prefPath.AsStdString());
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        return (dir / "krkr2_text_encoding_cache.sqlite3").u8string();
    } catch(...) {
        return "";
    }
}

static bool execEncodingCacheSql(sqlite3 *db, const char *sql) {
    char *err = nullptr;
    const int rc = sqlite3_exec(db, sql, nullptr, nullptr, &err);
    if(err)
        sqlite3_free(err);
    return rc == SQLITE_OK;
}

static sqlite3 *openReadEncodingCacheDb() {
    const std::string dbPath = getReadEncodingCacheDbPath();
    if(dbPath.empty())
        return nullptr;

    sqlite3 *db = nullptr;
    if(sqlite3_open_v2(dbPath.c_str(), &db,
                       SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE |
                           SQLITE_OPEN_FULLMUTEX,
                       nullptr) != SQLITE_OK) {
        if(db)
            sqlite3_close(db);
        return nullptr;
    }

    sqlite3_busy_timeout(db, 100);
    execEncodingCacheSql(db, "PRAGMA journal_mode=WAL");
    execEncodingCacheSql(db, "PRAGMA synchronous=NORMAL");
    if(!execEncodingCacheSql(
           db,
           "CREATE TABLE IF NOT EXISTS text_encoding_cache ("
           "game_path TEXT PRIMARY KEY,"
           "encoding TEXT NOT NULL,"
           "updated_at INTEGER NOT NULL)")) {
        sqlite3_close(db);
        return nullptr;
    }
    return db;
}

static void pruneMissingReadEncodingCacheRowsLocked(sqlite3 *db) {
    if(G_ReadEncodingPersistentCachePruned)
        return;
    G_ReadEncodingPersistentCachePruned = true;

    sqlite3_stmt *stmt = nullptr;
    if(sqlite3_prepare_v2(db, "SELECT game_path FROM text_encoding_cache", -1,
                          &stmt, nullptr) != SQLITE_OK) {
        return;
    }

    std::vector<std::string> missingPaths;
    while(sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char *raw = sqlite3_column_text(stmt, 0);
        if(!raw)
            continue;
        std::string path(reinterpret_cast<const char *>(raw));
        if(isPrunableLocalGamePath(path) && !localGamePathExists(path))
            missingPaths.push_back(path);
    }
    sqlite3_finalize(stmt);

    if(missingPaths.empty())
        return;

    sqlite3_stmt *del = nullptr;
    if(sqlite3_prepare_v2(
           db, "DELETE FROM text_encoding_cache WHERE game_path = ?", -1, &del,
           nullptr) != SQLITE_OK) {
        return;
    }
    for(const auto &path : missingPaths) {
        sqlite3_reset(del);
        sqlite3_clear_bindings(del);
        sqlite3_bind_text(del, 1, path.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(del);
    }
    sqlite3_finalize(del);
}

static void deleteReadEncodingCacheRowLocked(sqlite3 *db,
                                             const std::string &gamePath) {
    sqlite3_stmt *stmt = nullptr;
    if(sqlite3_prepare_v2(
           db, "DELETE FROM text_encoding_cache WHERE game_path = ?", -1, &stmt,
           nullptr) != SQLITE_OK) {
        return;
    }
    sqlite3_bind_text(stmt, 1, gamePath.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

static std::string loadPersistentReadEncodingLocked(
    const std::string &gamePath) {
    if(gamePath.empty())
        return "";

    sqlite3 *db = openReadEncodingCacheDb();
    if(!db)
        return "";

    pruneMissingReadEncodingCacheRowsLocked(db);
    if(isPrunableLocalGamePath(gamePath) && !localGamePathExists(gamePath)) {
        deleteReadEncodingCacheRowLocked(db, gamePath);
        sqlite3_close(db);
        return "";
    }

    sqlite3_stmt *stmt = nullptr;
    std::string encoding;
    if(sqlite3_prepare_v2(
           db,
           "SELECT encoding FROM text_encoding_cache WHERE game_path = ?", -1,
           &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, gamePath.c_str(), -1, SQLITE_TRANSIENT);
        if(sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char *raw = sqlite3_column_text(stmt, 0);
            if(raw)
                encoding = canonicalEncodingName(
                    reinterpret_cast<const char *>(raw));
        }
        sqlite3_finalize(stmt);
    }

    sqlite3_close(db);
    return encoding;
}

static void storePersistentReadEncodingLocked(const std::string &gamePath,
                                              const std::string &encoding) {
    if(gamePath.empty() || encoding.empty())
        return;
    if(isPrunableLocalGamePath(gamePath) && !localGamePathExists(gamePath)) {
        if(sqlite3 *db = openReadEncodingCacheDb()) {
            deleteReadEncodingCacheRowLocked(db, gamePath);
            sqlite3_close(db);
        }
        return;
    }

    sqlite3 *db = openReadEncodingCacheDb();
    if(!db)
        return;
    pruneMissingReadEncodingCacheRowsLocked(db);

    sqlite3_stmt *stmt = nullptr;
    if(sqlite3_prepare_v2(
           db,
           "INSERT OR REPLACE INTO text_encoding_cache "
           "(game_path, encoding, updated_at) VALUES (?, ?, ?)",
           -1, &stmt, nullptr) == SQLITE_OK) {
        const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::system_clock::now().time_since_epoch())
                             .count();
        sqlite3_bind_text(stmt, 1, gamePath.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, encoding.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 3, static_cast<sqlite3_int64>(now));
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    sqlite3_close(db);
}

static bool hasNonAsciiBytes(const unsigned char *raw, size_t size) {
    for(size_t i = 0; i < size; ++i) {
        if(raw[i] >= 0x80)
            return true;
    }
    return false;
}

static int clampConfidence(int confidence) {
    return std::max(0, std::min(100, confidence));
}

static int adjustUTF16Confidence(std::uint16_t codeUnit, int confidence) {
    if(codeUnit == 0) {
        confidence -= 10;
    } else if((codeUnit >= 0x20 && codeUnit <= 0xff) || codeUnit == 0x0a) {
        confidence += 10;
    }
    return clampConfidence(confidence);
}

static int utf16Confidence(const unsigned char *raw, size_t size,
                           bool bigEndian) {
    if(size < 4)
        return 0;

    int confidence = 10;
    const size_t bytesToCheck = std::min<size_t>(size, 30);

    for(size_t i = 0; i + 1 < bytesToCheck; i += 2) {
        const std::uint16_t codeUnit = bigEndian
            ? static_cast<std::uint16_t>((raw[i] << 8) | raw[i + 1])
            : static_cast<std::uint16_t>(raw[i] | (raw[i + 1] << 8));

        if(i == 0 && codeUnit == 0xfeff) {
            confidence = 100;
            if(!bigEndian && size >= 4 && raw[2] == 0 && raw[3] == 0)
                confidence = 0;
            break;
        }

        confidence = adjustUTF16Confidence(codeUnit, confidence);
        if(confidence == 0 || confidence == 100)
            break;
    }

    return confidence;
}

static bool looksLikeUTF16Endian(const unsigned char *raw, size_t size,
                                 bool bigEndian) {
    return utf16Confidence(raw, size, bigEndian) >= 80;
}

static bool isUtf8Continuation(unsigned char ch) {
    return (ch & 0xc0) == 0x80;
}

static bool isValidUtf8(const unsigned char *raw, size_t size,
                        bool &hasMultibyte) {
    hasMultibyte = false;
    for(size_t i = 0; i < size;) {
        const unsigned char ch = raw[i];
        if(ch < 0x80) {
            ++i;
            continue;
        }

        hasMultibyte = true;
        if(ch >= 0xc2 && ch <= 0xdf) {
            if(i + 1 >= size || !isUtf8Continuation(raw[i + 1]))
                return false;
            i += 2;
        } else if(ch == 0xe0) {
            if(i + 2 >= size || raw[i + 1] < 0xa0 ||
               raw[i + 1] > 0xbf || !isUtf8Continuation(raw[i + 2]))
                return false;
            i += 3;
        } else if((ch >= 0xe1 && ch <= 0xec) ||
                  (ch >= 0xee && ch <= 0xef)) {
            if(i + 2 >= size || !isUtf8Continuation(raw[i + 1]) ||
               !isUtf8Continuation(raw[i + 2]))
                return false;
            i += 3;
        } else if(ch == 0xed) {
            if(i + 2 >= size || raw[i + 1] < 0x80 ||
               raw[i + 1] > 0x9f || !isUtf8Continuation(raw[i + 2]))
                return false;
            i += 3;
        } else if(ch == 0xf0) {
            if(i + 3 >= size || raw[i + 1] < 0x90 ||
               raw[i + 1] > 0xbf || !isUtf8Continuation(raw[i + 2]) ||
               !isUtf8Continuation(raw[i + 3]))
                return false;
            i += 4;
        } else if(ch >= 0xf1 && ch <= 0xf3) {
            if(i + 3 >= size || !isUtf8Continuation(raw[i + 1]) ||
               !isUtf8Continuation(raw[i + 2]) ||
               !isUtf8Continuation(raw[i + 3]))
                return false;
            i += 4;
        } else if(ch == 0xf4) {
            if(i + 3 >= size || raw[i + 1] < 0x80 ||
               raw[i + 1] > 0x8f || !isUtf8Continuation(raw[i + 2]) ||
               !isUtf8Continuation(raw[i + 3]))
                return false;
            i += 4;
        } else {
            return false;
        }
    }
    return true;
}

static bool isCP932Lead(unsigned char ch) {
    return (ch >= 0x81 && ch <= 0x9f) || (ch >= 0xe0 && ch <= 0xfc);
}

static bool isCP932Trail(unsigned char ch) {
    return ch != 0x7f && ((ch >= 0x40 && ch <= 0x7e) ||
                          (ch >= 0x80 && ch <= 0xfc));
}

static bool isCP932HalfwidthKana(unsigned char ch) {
    return ch >= 0xa1 && ch <= 0xdf;
}

static int cp932Confidence(const unsigned char *raw, size_t size) {
    int doubleByteChars = 0;
    int halfwidthKanaChars = 0;
    int badChars = 0;

    for(size_t i = 0; i < size; ++i) {
        const unsigned char ch = raw[i];
        if(ch <= 0x7f)
            continue;

        if(isCP932HalfwidthKana(ch)) {
            ++halfwidthKanaChars;
            continue;
        }

        if(isCP932Lead(ch)) {
            if(i + 1 < size) {
                const unsigned char trail = raw[++i];
                if(isCP932Trail(trail)) {
                    ++doubleByteChars;
                    continue;
                }
            }
            ++badChars;
        } else {
            ++badChars;
        }

        if(badChars >= 2 && badChars * 5 >= doubleByteChars)
            return 0;
    }

    if(doubleByteChars == 0)
        return 0;

    if(doubleByteChars <= 10 && badChars == 0) {
        // KiriKiri scripts often contain only one or two Japanese names in an
        // otherwise ASCII control file. ICU assigns low confidence to such
        // short samples; keep a moderate score so these files still decode.
        return 30 + std::min(40, doubleByteChars * 10 + halfwidthKanaChars * 2);
    }

    if(doubleByteChars < 20 * badChars)
        return 0;

    return clampConfidence(30 + doubleByteChars - 20 * badChars);
}

static int mbcsConfidenceFromCounts(int multibyteChars, int badChars) {
    if(multibyteChars == 0)
        return 0;

    if(multibyteChars <= 10 && badChars == 0)
        return 30 + std::min(40, multibyteChars * 10);

    if(multibyteChars < 20 * badChars)
        return 0;

    return clampConfidence(30 + multibyteChars - 20 * badChars);
}

static int gb18030Confidence(const unsigned char *raw, size_t size) {
    int multibyteChars = 0;
    int badChars = 0;

    for(size_t i = 0; i < size; ++i) {
        const unsigned char first = raw[i];
        if(first <= 0x80)
            continue;

        if(first >= 0x81 && first <= 0xfe && i + 1 < size) {
            const unsigned char second = raw[i + 1];
            if((second >= 0x40 && second <= 0x7e) ||
               (second >= 0x80 && second <= 0xfe)) {
                ++multibyteChars;
                ++i;
                continue;
            }

            if(second >= 0x30 && second <= 0x39 && i + 3 < size &&
               raw[i + 2] >= 0x81 && raw[i + 2] <= 0xfe &&
               raw[i + 3] >= 0x30 && raw[i + 3] <= 0x39) {
                ++multibyteChars;
                i += 3;
                continue;
            }
        }

        ++badChars;
        if(badChars >= 2 && badChars * 5 >= multibyteChars)
            return 0;
    }

    return mbcsConfidenceFromCounts(multibyteChars, badChars);
}

static int big5Confidence(const unsigned char *raw, size_t size) {
    int multibyteChars = 0;
    int badChars = 0;

    for(size_t i = 0; i < size; ++i) {
        const unsigned char first = raw[i];
        if(first <= 0x7f || first == 0xff)
            continue;

        if(i + 1 < size) {
            const unsigned char second = raw[i + 1];
            if(second >= 0x40 && second <= 0xfe && second != 0x7f &&
               second != 0xff) {
                ++multibyteChars;
                ++i;
                continue;
            }
        }

        ++badChars;
        if(badChars >= 2 && badChars * 5 >= multibyteChars)
            return 0;
    }

    return mbcsConfidenceFromCounts(multibyteChars, badChars);
}

static int eucConfidence(const unsigned char *raw, size_t size,
                         bool allowJIS0212) {
    int multibyteChars = 0;
    int badChars = 0;

    for(size_t i = 0; i < size; ++i) {
        const unsigned char first = raw[i];
        if(first <= 0x7f)
            continue;

        if(first >= 0xa1 && first <= 0xfe && i + 1 < size &&
           raw[i + 1] >= 0xa1 && raw[i + 1] <= 0xfe) {
            ++multibyteChars;
            ++i;
            continue;
        }

        if(allowJIS0212 && first == 0x8e && i + 1 < size &&
           raw[i + 1] >= 0xa1 && raw[i + 1] <= 0xdf) {
            ++multibyteChars;
            ++i;
            continue;
        }

        if(allowJIS0212 && first == 0x8f && i + 2 < size &&
           raw[i + 1] >= 0xa1 && raw[i + 1] <= 0xfe &&
           raw[i + 2] >= 0xa1 && raw[i + 2] <= 0xfe) {
            ++multibyteChars;
            i += 2;
            continue;
        }

        ++badChars;
        if(badChars >= 2 && badChars * 5 >= multibyteChars)
            return 0;
    }

    return mbcsConfidenceFromCounts(multibyteChars, badChars);
}

static std::string detectLegacyCJKEncoding(const unsigned char *raw,
                                           size_t size) {
    struct Candidate {
        const char *encoding;
        int confidence;
    };
    const Candidate candidates[] = {
        {"CP932", cp932Confidence(raw, size)},
        {"GB18030", gb18030Confidence(raw, size)},
        {"BIG5", big5Confidence(raw, size)},
        {"EUC-JP", eucConfidence(raw, size, true)},
        {"EUC-KR", eucConfidence(raw, size, false)},
    };

    const Candidate *best = nullptr;
    for(const auto &candidate : candidates) {
        if(candidate.confidence < 30)
            continue;
        if(!best || candidate.confidence > best->confidence)
            best = &candidate;
    }

    return best ? best->encoding : "";
}

static size_t iso2022SequenceLength(const unsigned char seq[5]) {
    size_t len = 0;
    while(len < 5 && seq[len] != 0)
        ++len;
    return len;
}

static int iso2022Confidence(const unsigned char *raw, size_t size,
                             const unsigned char sequences[][5],
                             size_t sequenceCount) {
    int hits = 0;
    int misses = 0;
    int shifts = 0;

    for(size_t i = 0; i < size; ++i) {
        if(raw[i] == 0x1b) {
            bool matched = false;
            for(size_t seqIndex = 0; seqIndex < sequenceCount; ++seqIndex) {
                const size_t seqLen = iso2022SequenceLength(sequences[seqIndex]);
                if(seqLen > 0 && size - i >= seqLen &&
                   std::memcmp(raw + i, sequences[seqIndex], seqLen) == 0) {
                    ++hits;
                    i += seqLen - 1;
                    matched = true;
                    break;
                }
            }
            if(matched)
                continue;
            ++misses;
        }

        if(raw[i] == 0x0e || raw[i] == 0x0f)
            ++shifts;
    }

    if(hits == 0)
        return 0;

    int quality = (100 * hits - 100 * misses) / (hits + misses);
    if(hits + shifts < 5)
        quality -= (5 - (hits + shifts)) * 10;

    return clampConfidence(quality);
}

static std::string detectISO2022Encoding(const unsigned char *raw,
                                         size_t size) {
    static const unsigned char kISO2022JP[][5] = {
        {0x1b, 0x24, 0x28, 0x43, 0},
        {0x1b, 0x24, 0x28, 0x44, 0},
        {0x1b, 0x24, 0x40, 0, 0},
        {0x1b, 0x24, 0x41, 0, 0},
        {0x1b, 0x24, 0x42, 0, 0},
        {0x1b, 0x26, 0x40, 0, 0},
        {0x1b, 0x28, 0x42, 0, 0},
        {0x1b, 0x28, 0x48, 0, 0},
        {0x1b, 0x28, 0x49, 0, 0},
        {0x1b, 0x28, 0x4a, 0, 0},
        {0x1b, 0x2e, 0x41, 0, 0},
        {0x1b, 0x2e, 0x46, 0, 0},
    };
    static const unsigned char kISO2022KR[][5] = {
        {0x1b, 0x24, 0x29, 0x43, 0},
    };
    static const unsigned char kISO2022CN[][5] = {
        {0x1b, 0x24, 0x29, 0x41, 0},
        {0x1b, 0x24, 0x29, 0x47, 0},
        {0x1b, 0x24, 0x2a, 0x48, 0},
        {0x1b, 0x24, 0x29, 0x45, 0},
        {0x1b, 0x24, 0x2b, 0x49, 0},
        {0x1b, 0x24, 0x2b, 0x4a, 0},
        {0x1b, 0x24, 0x2b, 0x4b, 0},
        {0x1b, 0x24, 0x2b, 0x4c, 0},
        {0x1b, 0x24, 0x2b, 0x4d, 0},
        {0x1b, 0x4e, 0, 0, 0},
        {0x1b, 0x4f, 0, 0, 0},
    };

    struct Candidate {
        const char *encoding;
        int confidence;
    };
    const Candidate candidates[] = {
        {"ISO-2022-JP", iso2022Confidence(raw, size, kISO2022JP,
                                           sizeof(kISO2022JP) /
                                               sizeof(kISO2022JP[0]))},
        {"ISO-2022-KR", iso2022Confidence(raw, size, kISO2022KR,
                                           sizeof(kISO2022KR) /
                                               sizeof(kISO2022KR[0]))},
        {"ISO-2022-CN", iso2022Confidence(raw, size, kISO2022CN,
                                           sizeof(kISO2022CN) /
                                               sizeof(kISO2022CN[0]))},
    };

    const Candidate *best = nullptr;
    for(const auto &candidate : candidates) {
        if(candidate.confidence <= 0)
            continue;
        if(!best || candidate.confidence > best->confidence)
            best = &candidate;
    }

    return best ? best->encoding : "";
}

static bool sameEncodingName(const std::string &encoding,
                             const char *expected) {
    size_t i = 0;
    for(; i < encoding.size() && expected[i]; ++i) {
        char a = encoding[i];
        char b = expected[i];
        if(a >= 'a' && a <= 'z')
            a = static_cast<char>(a - ('a' - 'A'));
        if(b >= 'a' && b <= 'z')
            b = static_cast<char>(b - ('a' - 'A'));
        if(a != b)
            return false;
    }
    return i == encoding.size() && expected[i] == 0;
}

struct EncodingAlias {
    const char *alias;
    const char *canonical;
};

static const char *findCanonicalEncodingAlias(const std::string &encoding) {
    static const EncodingAlias kAliases[] = {
        {"CP1250", "WINDOWS-1250"},
        {"WINDOWS-1250", "WINDOWS-1250"},
        {"CP1251", "WINDOWS-1251"},
        {"WINDOWS-1251", "WINDOWS-1251"},
        {"CP1252", "WINDOWS-1252"},
        {"WINDOWS-1252", "WINDOWS-1252"},
        {"CP1253", "WINDOWS-1253"},
        {"WINDOWS-1253", "WINDOWS-1253"},
        {"CP1254", "WINDOWS-1254"},
        {"WINDOWS-1254", "WINDOWS-1254"},
        {"CP1255", "WINDOWS-1255"},
        {"WINDOWS-1255", "WINDOWS-1255"},
        {"CP1256", "WINDOWS-1256"},
        {"WINDOWS-1256", "WINDOWS-1256"},
        {"CP1257", "WINDOWS-1257"},
        {"WINDOWS-1257", "WINDOWS-1257"},
        {"CP1258", "WINDOWS-1258"},
        {"WINDOWS-1258", "WINDOWS-1258"},
        {"CP874", "WINDOWS-874"},
        {"WINDOWS-874", "WINDOWS-874"},
        {"ISO-8859-1", "ISO-8859-1"},
        {"ISO8859-1", "ISO-8859-1"},
        {"ISO-8859-2", "ISO-8859-2"},
        {"ISO8859-2", "ISO-8859-2"},
        {"ISO-8859-3", "ISO-8859-3"},
        {"ISO8859-3", "ISO-8859-3"},
        {"ISO-8859-4", "ISO-8859-4"},
        {"ISO8859-4", "ISO-8859-4"},
        {"ISO-8859-5", "ISO-8859-5"},
        {"ISO8859-5", "ISO-8859-5"},
        {"ISO-8859-6", "ISO-8859-6"},
        {"ISO8859-6", "ISO-8859-6"},
        {"ISO-8859-7", "ISO-8859-7"},
        {"ISO8859-7", "ISO-8859-7"},
        {"ISO-8859-8", "ISO-8859-8"},
        {"ISO8859-8", "ISO-8859-8"},
        {"ISO-8859-8-I", "ISO-8859-8-I"},
        {"ISO-8859-9", "ISO-8859-9"},
        {"ISO8859-9", "ISO-8859-9"},
        {"ISO-8859-10", "ISO-8859-10"},
        {"ISO8859-10", "ISO-8859-10"},
        {"ISO-8859-11", "ISO-8859-11"},
        {"ISO8859-11", "ISO-8859-11"},
        {"ISO-8859-13", "ISO-8859-13"},
        {"ISO8859-13", "ISO-8859-13"},
        {"ISO-8859-14", "ISO-8859-14"},
        {"ISO8859-14", "ISO-8859-14"},
        {"ISO-8859-15", "ISO-8859-15"},
        {"ISO8859-15", "ISO-8859-15"},
        {"ISO-8859-16", "ISO-8859-16"},
        {"ISO8859-16", "ISO-8859-16"},
        {"KOI8-R", "KOI8-R"},
        {"KOI8-U", "KOI8-U"},
        {"KOI8-RU", "KOI8-RU"},
        {"CP866", "CP866"},
        {"IBM866", "CP866"},
        {"CP852", "CP852"},
        {"IBM852", "CP852"},
        {"CP850", "CP850"},
        {"IBM850", "CP850"},
        {"CP858", "CP858"},
        {"IBM858", "CP858"},
        {"CP437", "CP437"},
        {"IBM437", "CP437"},
        {"CP855", "CP855"},
        {"IBM855", "CP855"},
        {"CP857", "CP857"},
        {"IBM857", "CP857"},
        {"CP860", "CP860"},
        {"IBM860", "CP860"},
        {"CP861", "CP861"},
        {"IBM861", "CP861"},
        {"CP862", "CP862"},
        {"IBM862", "CP862"},
        {"CP863", "CP863"},
        {"IBM863", "CP863"},
        {"CP864", "CP864"},
        {"IBM864", "CP864"},
        {"CP865", "CP865"},
        {"IBM865", "CP865"},
        {"CP869", "CP869"},
        {"IBM869", "CP869"},
        {"CP1125", "CP1125"},
        {"IBM1125", "CP1125"},
        {"ISO-IR-111", "ISO-IR-111"},
        {"HP-ROMAN8", "HP-ROMAN8"},
        {"ROMAN8", "HP-ROMAN8"},
        {"CP1133", "CP1133"},
        {"IBM1133", "CP1133"},
        {"MACINTOSH", "MACINTOSH"},
        {"MAC", "MACINTOSH"},
        {"MAC-CYRILLIC", "MAC-CYRILLIC"},
        {"MACCENTRALEUROPE", "MAC-CENTRALEUROPE"},
        {"MAC-CENTRALEUROPE", "MAC-CENTRALEUROPE"},
        {"TIS-620", "TIS-620"},
        {"TIS620", "TIS-620"},
        {"VISCII", "VISCII"},
        {"TCVN", "TCVN"},
        {"TCVN5712-1", "TCVN"},
        {"ARMSCII-8", "ARMSCII-8"},
        {"PT154", "PT154"},
        {"RK1048", "RK1048"},
        {"GEORGIAN-ACADEMY", "GEORGIAN-ACADEMY"},
        {"GEORGIAN-PS", "GEORGIAN-PS"},
        {"MS_KANJI", "CP932"},
        {"X-SJIS", "CP932"},
        {"EUCJP", "EUC-JP"},
        {"ISO-2022-JP-1", "ISO-2022-JP-1"},
        {"ISO-2022-JP-2", "ISO-2022-JP-2"},
        {"CP936", "GBK"},
        {"WINDOWS-936", "GBK"},
        {"MS936", "GBK"},
        {"EUC-CN", "GB2312"},
        {"HZ", "HZ-GB-2312"},
        {"HZ-GB-2312", "HZ-GB-2312"},
        {"CP950", "CP950"},
        {"WINDOWS-950", "CP950"},
        {"BIG5-HKSCS", "BIG5-HKSCS"},
        {"BIG5HKSCS", "BIG5-HKSCS"},
        {"EUC-TW", "EUC-TW"},
        {"ISO-2022-CN-EXT", "ISO-2022-CN-EXT"},
        {"CP949", "CP949"},
        {"UHC", "CP949"},
        {"WINDOWS-949", "CP949"},
        {"JOHAB", "JOHAB"},
    };

    for(const auto &alias : kAliases) {
        if(sameEncodingName(encoding, alias.alias))
            return alias.canonical;
    }
    return nullptr;
}

static bool isLegacyCJKEncodingGuess(const std::string &encoding) {
    return sameEncodingName(encoding, "SHIFT_JIS") ||
           sameEncodingName(encoding, "SHIFT-JIS") ||
           sameEncodingName(encoding, "SJIS") ||
           sameEncodingName(encoding, "CP932") ||
           sameEncodingName(encoding, "WINDOWS-31J") ||
           sameEncodingName(encoding, "CP936") ||
           sameEncodingName(encoding, "GBK") ||
           sameEncodingName(encoding, "GB2312") ||
           sameEncodingName(encoding, "GB18030") ||
           sameEncodingName(encoding, "EUC-CN") ||
           sameEncodingName(encoding, "HZ-GB-2312") ||
           sameEncodingName(encoding, "BIG5") ||
           sameEncodingName(encoding, "CP950") ||
           sameEncodingName(encoding, "BIG5-HKSCS") ||
           sameEncodingName(encoding, "EUC-TW") ||
           sameEncodingName(encoding, "EUC-JP") ||
           sameEncodingName(encoding, "ISO-2022-JP") ||
           sameEncodingName(encoding, "ISO-2022-JP-1") ||
           sameEncodingName(encoding, "ISO-2022-JP-2") ||
           sameEncodingName(encoding, "ISO-2022-KR") ||
           sameEncodingName(encoding, "ISO-2022-CN") ||
           sameEncodingName(encoding, "ISO-2022-CN-EXT") ||
           sameEncodingName(encoding, "EUC-KR") ||
           sameEncodingName(encoding, "CP949") ||
           sameEncodingName(encoding, "UHC") ||
           sameEncodingName(encoding, "JOHAB");
}

static std::string canonicalEncodingName(const std::string &encoding) {
    if(sameEncodingName(encoding, "SHIFT_JIS") ||
       sameEncodingName(encoding, "SHIFT-JIS") ||
       sameEncodingName(encoding, "SJIS") ||
       sameEncodingName(encoding, "CP932") ||
       sameEncodingName(encoding, "WINDOWS-31J")) {
        return "CP932";
    }
    if(sameEncodingName(encoding, "CP936") ||
       sameEncodingName(encoding, "GBK") ||
       sameEncodingName(encoding, "GB2312")) {
        return "GBK";
    }
    if(sameEncodingName(encoding, "GB18030"))
        return "GB18030";
    if(sameEncodingName(encoding, "BIG5") ||
       sameEncodingName(encoding, "BIG-5")) {
        return "BIG5";
    }
    if(sameEncodingName(encoding, "EUC-JP") ||
       sameEncodingName(encoding, "EUCJP")) {
        return "EUC-JP";
    }
    if(sameEncodingName(encoding, "ISO-2022-JP") ||
       sameEncodingName(encoding, "ISO2022JP")) {
        return "ISO-2022-JP";
    }
    if(sameEncodingName(encoding, "ISO-2022-KR") ||
       sameEncodingName(encoding, "ISO2022KR")) {
        return "ISO-2022-KR";
    }
    if(sameEncodingName(encoding, "ISO-2022-CN") ||
       sameEncodingName(encoding, "ISO2022CN")) {
        return "ISO-2022-CN";
    }
    if(sameEncodingName(encoding, "EUC-KR") ||
       sameEncodingName(encoding, "EUCKR")) {
        return "EUC-KR";
    }
    if(sameEncodingName(encoding, "UTF-16LE") ||
       sameEncodingName(encoding, "UTF16LE")) {
        return "UTF-16LE";
    }
    if(sameEncodingName(encoding, "UTF-16BE") ||
       sameEncodingName(encoding, "UTF16BE")) {
        return "UTF-16BE";
    }
    if(sameEncodingName(encoding, "UTF-16") ||
       sameEncodingName(encoding, "UTF16")) {
        return "UTF-16";
    }
    if(sameEncodingName(encoding, "UTF-32LE") ||
       sameEncodingName(encoding, "UTF32LE")) {
        return "UTF-32LE";
    }
    if(sameEncodingName(encoding, "UTF-32BE") ||
       sameEncodingName(encoding, "UTF32BE")) {
        return "UTF-32BE";
    }
    if(sameEncodingName(encoding, "UTF-32") ||
       sameEncodingName(encoding, "UTF32")) {
        return "UTF-32";
    }
    if(sameEncodingName(encoding, "UTF-8") ||
       sameEncodingName(encoding, "UTF8")) {
        return "UTF-8";
    }
    if(sameEncodingName(encoding, "ASCII") ||
       sameEncodingName(encoding, "US-ASCII")) {
        return "ASCII";
    }
    if(sameEncodingName(encoding, "WINDOWS-1252") ||
       sameEncodingName(encoding, "ISO-8859-1")) {
        return "WINDOWS-1252";
    }
    if(const char *alias = findCanonicalEncodingAlias(encoding))
        return alias;
    return encoding;
}

static const char *const kFallbackCharsets[] = {
    // Japanese
    "CP932", "SHIFT_JIS", "WINDOWS-31J", "EUC-JP", "ISO-2022-JP",
    "ISO-2022-JP-1", "ISO-2022-JP-2",
    // Simplified / traditional Chinese
    "GB18030", "GBK", "CP936", "GB2312", "EUC-CN", "HZ-GB-2312",
    "BIG5", "CP950", "BIG5-HKSCS", "EUC-TW", "ISO-2022-CN",
    "ISO-2022-CN-EXT",
    // Korean
    "EUC-KR", "CP949", "UHC", "JOHAB", "ISO-2022-KR",
    // Western / Central European / Turkish / Baltic
    "WINDOWS-1252", "ISO-8859-1", "ISO-8859-15", "WINDOWS-1250",
    "ISO-8859-2", "CP852", "MAC-CENTRALEUROPE", "ISO-8859-3",
    "ISO-8859-4", "WINDOWS-1254", "ISO-8859-9", "ISO-8859-10",
    "WINDOWS-1257", "ISO-8859-13", "ISO-8859-14", "ISO-8859-16",
    // Cyrillic / Greek / Hebrew / Arabic
    "WINDOWS-1251", "ISO-8859-5", "KOI8-R", "KOI8-U", "KOI8-RU",
    "CP866", "MAC-CYRILLIC", "WINDOWS-1253", "ISO-8859-7",
    "WINDOWS-1255", "ISO-8859-8", "ISO-8859-8-I", "WINDOWS-1256",
    "ISO-8859-6",
    // Thai / Vietnamese / other legacy code pages
    "WINDOWS-874", "TIS-620", "ISO-8859-11", "WINDOWS-1258", "VISCII",
    "TCVN", "CP437", "CP850", "CP858", "MACINTOSH", "ARMSCII-8",
    "PT154", "RK1048", "GEORGIAN-ACADEMY", "GEORGIAN-PS",
    // Additional DOS / regional encodings kept late to avoid false positives.
    "CP855", "CP857", "CP860", "CP861", "CP862", "CP863", "CP864",
    "CP865", "CP869", "CP1125", "ISO-IR-111", "HP-ROMAN8", "CP1133",
    nullptr,
};

static size_t fallbackCharsetCount() {
    size_t count = 0;
    while(kFallbackCharsets[count])
        ++count;
    return count;
}

static void addEncodingCandidate(std::vector<std::string> &candidates,
                                 const std::string &encoding) {
    if(encoding.empty() || sameEncodingName(encoding, "ASCII"))
        return;

    const std::string canonical = canonicalEncodingName(encoding);
    for(const auto &candidate : candidates) {
        if(sameEncodingName(candidate, canonical))
            return;
    }
    candidates.push_back(canonical);
}

static bool canDecodeWithCharset(const char *src_begin, const char *src_end,
                                 const std::string &encoding) {
    if(encoding.empty() || sameEncodingName(encoding, "ASCII"))
        return false;

    try {
        std::wstring wide =
            boost::locale::conv::to_utf<wchar_t>(
                src_begin, src_end, encoding, boost::locale::conv::stop);
        return !wide.empty();
    } catch(...) {
        return false;
    }
}

static std::string getCachedReadEncoding() {
    const std::string gamePath = getCurrentGameEncodingCachePath();
    const std::string cacheKey = getReadEncodingMemoryKey(gamePath);

    std::lock_guard<std::mutex> lock(G_ReadEncodingCacheMutex);
    auto it = G_ReadEncodingCache.find(cacheKey);
    if(it != G_ReadEncodingCache.end())
        return it->second;

    if(!gamePath.empty() &&
       G_ReadEncodingCacheLoadedGames.find(gamePath) ==
           G_ReadEncodingCacheLoadedGames.end()) {
        G_ReadEncodingCacheLoadedGames.insert(gamePath);
        std::string persistent = loadPersistentReadEncodingLocked(gamePath);
        if(!persistent.empty()) {
            G_ReadEncodingCache[cacheKey] = persistent;
            return persistent;
        }
    }

    return "";
}

static void setCachedReadEncoding(const std::string &encoding) {
    if(encoding.empty())
        return;
    const std::string canonical = canonicalEncodingName(encoding);
    const std::string gamePath = getCurrentGameEncodingCachePath();
    const std::string cacheKey = getReadEncodingMemoryKey(gamePath);

    std::lock_guard<std::mutex> lock(G_ReadEncodingCacheMutex);
    G_ReadEncodingCache[cacheKey] = canonical;
    if(!gamePath.empty()) {
        G_ReadEncodingCacheLoadedGames.insert(gamePath);
        storePersistentReadEncodingLocked(gamePath, canonical);
    }
}

static std::string findFirstWorkingTextEncoding(const char *src_begin,
                                                const char *src_end,
                                                const std::string &preferred) {
    std::vector<std::string> candidates;
    candidates.reserve(fallbackCharsetCount() + 1);
    addEncodingCandidate(candidates, preferred);
    for(const char *const *cs = kFallbackCharsets; *cs; ++cs)
        addEncodingCandidate(candidates, *cs);

    const size_t count = candidates.size();
    if(count == 0)
        return "";

    std::vector<unsigned char> valid(count, 0);
    std::atomic<size_t> bestIndex{ count };

    oneapi::tbb::parallel_for(
        oneapi::tbb::blocked_range<size_t>(0, count),
        [&](const oneapi::tbb::blocked_range<size_t> &range) {
            for(size_t i = range.begin(); i != range.end(); ++i) {
                if(i >= bestIndex.load(std::memory_order_relaxed))
                    continue;

                try {
                    if(!canDecodeWithCharset(src_begin, src_end, candidates[i]))
                        continue;

                    valid[i] = 1;
                    size_t current = bestIndex.load(std::memory_order_relaxed);
                    while(i < current &&
                          !bestIndex.compare_exchange_weak(
                              current, i, std::memory_order_relaxed,
                              std::memory_order_relaxed)) {
                    }
                } catch(...) {
                    // try next codec
                }
            }
        });

    for(size_t i = 0; i < count; ++i) {
        if(valid[i])
            return candidates[i];
    }
    return "";
}

std::string checkTextEncoding(const void *buf, size_t size,
                              std::uint8_t &bomSize) {
    auto raw = static_cast<const unsigned char *>(buf);
    std::string encoding;
    bomSize = 0;
    // --- 检查 BOM ---
    if(size >= 4 && raw[0] == 0xFF && raw[1] == 0xFE && raw[2] == 0x00 &&
       raw[3] == 0x00) {
        // UTF-32LE BOM
        bomSize = 4;
        encoding = "UTF-32LE";
    } else if(size >= 4 && raw[0] == 0x00 && raw[1] == 0x00 &&
              raw[2] == 0xFE && raw[3] == 0xFF) {
        // UTF-32BE BOM
        bomSize = 4;
        encoding = "UTF-32BE";
    } else if(size >= 2 && raw[0] == 0xFF && raw[1] == 0xFE) {
        // UTF-16LE BOM
        bomSize = 2;
        encoding = "UTF-16LE";
    } else if(size >= 2 && raw[0] == 0xFE && raw[1] == 0xFF) {
        // UTF-16BE BOM
        bomSize = 2;
        encoding = "UTF-16BE";
    } else if(size >= 3 && raw[0] == 0xEF && raw[1] == 0xBB && raw[2] == 0xBF) {
        // UTF-8 BOM
        bomSize = 3;
        encoding = "UTF-8";
    } else {
        const bool hasNonAscii = hasNonAsciiBytes(raw, size);
        encoding = detectISO2022Encoding(raw, size);
        if(!encoding.empty())
            return encoding;

        if(looksLikeUTF16Endian(raw, size, false))
            return "UTF-16LE";
        if(looksLikeUTF16Endian(raw, size, true))
            return "UTF-16BE";

        bool hasUtf8Multibyte = false;
        if(hasNonAscii && isValidUtf8(raw, size, hasUtf8Multibyte) &&
           hasUtf8Multibyte) {
            return "UTF-8";
        }

        // ---------- 普通文本：用 uchardet 检测编码 ----------
        uchardet_t ud = uchardet_new();
        uchardet_handle_data(ud, reinterpret_cast<const char *>(raw), size);
        uchardet_data_end(ud);
        encoding = uchardet_get_charset(ud);
        uchardet_delete(ud);
        encoding = canonicalEncodingName(encoding);

        if(hasNonAscii) {
            const char *src_begin =
                reinterpret_cast<const char *>(raw);
            const char *src_end =
                reinterpret_cast<const char *>(raw + size);
            const std::string legacyCJKEncoding =
                !isLegacyCJKEncodingGuess(encoding)
                    ? detectLegacyCJKEncoding(raw, size)
                    : std::string();
            const std::string preferredEncoding =
                !legacyCJKEncoding.empty() ? legacyCJKEncoding : encoding;
            const std::string cachedEncoding = getCachedReadEncoding();
            if(!cachedEncoding.empty() &&
               (sameEncodingName(cachedEncoding, preferredEncoding) ||
                (!isLegacyCJKEncodingGuess(encoding) &&
                 isLegacyCJKEncodingGuess(cachedEncoding))) &&
               canDecodeWithCharset(src_begin, src_end, cachedEncoding)) {
                return cachedEncoding;
            }

            if(!legacyCJKEncoding.empty()) {
                // Short KiriKiri scripts often contain only a few CJK names in
                // otherwise ASCII control text. uchardet may report those as a
                // western single-byte charset; prefer a structurally valid CJK
                // stream in that case so storage names do not become mojibake.
                encoding = legacyCJKEncoding;
            }

            const std::string validated =
                findFirstWorkingTextEncoding(src_begin, src_end, encoding);
            if(!validated.empty()) {
                encoding = validated;
                setCachedReadEncoding(encoding);
            }
        } else if(!hasNonAscii && sameEncodingName(encoding, "WINDOWS-1252")) {
            encoding = "ASCII";
        }
    }

    return encoding;
}

/*
 *  note: encryption of mode 0 or 1 ( simple crypt ) does never
 *  intend data pretection security.
 */
class tTVPTextReadStream : public iTJSTextReadStream {
    std::unique_ptr<tTJSBinaryStream> _stream{};
    std::u16string _buffer; // 全部文本，UTF-16
    size_t _pos = 0; // 当前读取位置

public:
    tTVPTextReadStream(const ttstr &name, const ttstr &mode) {
        _stream.reset(TVPCreateStream(name, TJS_BS_READ));
        size_t ofs = parseModeNumber(mode.c_str(), TJS_W('o'), 255, 0).value();
        _stream->SetPosition(ofs);

        auto size = static_cast<size_t>(_stream->GetSize() - ofs);
        std::vector<std::uint8_t> raw(size);
        _stream->ReadBuffer(raw.data(), size);

        // ---------- 检查是否加密/压缩 ----------
        if(size >= 3 && raw[0] == 0xFE && raw[1] == 0xFE) {
            std::uint8_t m = raw[2];
            if(m == 0 || m == 1) {
                // 解密 UTF-16 数据
                if(size < 5)
                    TVPThrowExceptionMessage(TVPUnsupportedCipherMode, name);
                size_t len = (size - 5) / 2;
                _buffer.resize(len);
                for(size_t i = 0; i < len; i++) {
                    const size_t pos = 5 + i * 2;
                    char16_t ch = static_cast<char16_t>(
                        raw[pos] | (static_cast<std::uint16_t>(raw[pos + 1])
                                    << 8));
                    if(m == 0) {
                        if(ch >= 0x20)
                            ch ^= (((ch & 0xfe) << 8) ^ 1);
                    } else if(m == 1) {
                        ch = ((ch & 0xaaaa) >> 1) | ((ch & 0x5555) << 1);
                    }
                    _buffer[i] = ch;
                }
                return;
            }
            if(m == 2) {
                // 压缩流
                if(size < 3 + 2 + 16)
                    TVPThrowExceptionMessage(TVPUnsupportedCipherMode, name);

                // 读压缩大小和解压大小
                std::uint8_t *ptr = raw.data() + 5;
                std::uint64_t compressed =
                    *reinterpret_cast<std::uint64_t *>(ptr);
                ptr += 8;
                std::uint64_t uncompressed =
                    *reinterpret_cast<std::uint64_t *>(ptr);
                ptr += 8;

                std::vector<std::uint8_t> compBuf(compressed);
                memcpy(compBuf.data(), ptr, compressed);

                std::vector<std::uint8_t> uncompBuf(uncompressed);
                auto destLen = static_cast<unsigned long>(uncompressed);
                int ret = uncompress(uncompBuf.data(), &destLen, compBuf.data(),
                                     static_cast<unsigned long>(compressed));
                if(ret != Z_OK || destLen != uncompressed)
                    TVPThrowExceptionMessage(TVPUnsupportedCipherMode, name);

                // 解压得到 UTF-16 数据
                _buffer.assign(reinterpret_cast<char16_t *>(uncompBuf.data()),
                               reinterpret_cast<char16_t *>(uncompBuf.data() +
                                                            uncompressed));
                return;
            }
            TVPThrowExceptionMessage(TVPUnsupportedCipherMode, name);
        }
        std::uint8_t bomSize = 0;
        std::string encoding = checkTextEncoding(raw.data(), size, bomSize);
        raw.erase(raw.begin(), raw.begin() + bomSize);
        const size_t rawSize = raw.size();

        if(encoding.empty())
            encoding = G_DefaultReadEncoding; // 默认回退

        if(encoding == "ASCII") {
            _buffer.assign(raw.data(), raw.data() + rawSize);
            return;
        }

        if(encoding == "UTF-8") {
            try {
                _buffer = boost::locale::conv::utf_to_utf<char16_t>(
                    reinterpret_cast<const char *>(raw.data()),
                    reinterpret_cast<const char *>(raw.data() + rawSize),
                    boost::locale::conv::stop);
                return;
            } catch(const std::exception &e) {
                // uchardet sometimes misidentifies CP932 / GBK as UTF-8 when
                // the file happens to start with mostly-ASCII bytes. Fall
                // through to the legacy CJK fallback chain below.
                spdlog::warn(
                    "UTF-8 decode failed (likely misidentified): {}",
                    e.what());
                encoding = "CP932"; // hint for the fallback path
            }
        }

        if(encoding == "UTF-16" || encoding == "UTF-16LE" ||
           encoding == "UTF-16BE") {
            const bool bigEndian = encoding == "UTF-16BE";
            const size_t len = rawSize / 2;
            _buffer.resize(len);
            for(size_t i = 0; i < len; ++i) {
                const size_t j = i * 2;
                if(bigEndian) {
                    _buffer[i] =
                        static_cast<char16_t>((raw[j] << 8) | raw[j + 1]);
                } else {
                    _buffer[i] =
                        static_cast<char16_t>(raw[j] | (raw[j + 1] << 8));
                }
            }

            return;
        }

        if(encoding == "UTF-32" || encoding == "UTF-32LE" ||
           encoding == "UTF-32BE") {
            const bool bigEndian = encoding == "UTF-32BE";
            const size_t len = rawSize / 4;
            std::u32string utf32;
            utf32.resize(len);
            for(size_t i = 0; i < len; ++i) {
                const size_t j = i * 4;
                if(bigEndian) {
                    utf32[i] = (static_cast<char32_t>(raw[j]) << 24) |
                               (static_cast<char32_t>(raw[j + 1]) << 16) |
                               (static_cast<char32_t>(raw[j + 2]) << 8) |
                               static_cast<char32_t>(raw[j + 3]);
                } else {
                    utf32[i] = static_cast<char32_t>(raw[j]) |
                               (static_cast<char32_t>(raw[j + 1]) << 8) |
                               (static_cast<char32_t>(raw[j + 2]) << 16) |
                               (static_cast<char32_t>(raw[j + 3]) << 24);
                }
            }
            _buffer = boost::locale::conv::utf_to_utf<char16_t>(
                utf32.data(), utf32.data() + utf32.size());
            return;
        }

        // 其他文本字符
        try {
            std::wstring wide = boost::locale::conv::to_utf<wchar_t>(
                reinterpret_cast<const char *>(raw.data()),
                reinterpret_cast<const char *>(raw.data() + raw.size()),
                encoding);
            _buffer = boost::locale::conv::utf_to_utf<char16_t>(wide);
        } catch(const std::exception &e) {
            // Primary codec failed. krkr2 was originally a Shift_JIS engine
            // and many .ks scenarios / .tjs sources from older Japanese
            // visual novels are not UTF-8 even when uchardet guesses
            // otherwise. Walk through a list of likely CJK codecs in skip
            // mode (which silently drops bytes the codec cannot map)
            // before giving up and throwing.
            //
            // NOTE: boost::locale::conv ships explicit template
            // instantiations of the charset-aware to_utf<>() only for
            // {char,wchar_t,std::string} -- NOT for char16_t. Even though
            // boost-locale[iconv] is statically linked (libiconv-1.18 from
            // vcpkg), to_utf<char16_t>(..., "CP932", ...) leaves an
            // undefined symbol at link time. Two-step convert: legacy-cp
            // -> wchar_t (uses iconv backend) -> char16_t via utf_to_utf.
            spdlog::warn("primary text decode failed (encoding={}): {}",
                         encoding, e.what());
            const char *src_begin =
                reinterpret_cast<const char *>(raw.data());
            const char *src_end =
                reinterpret_cast<const char *>(raw.data() + raw.size());
            bool decoded = false;

            const std::string fallback =
                findFirstWorkingTextEncoding(src_begin, src_end, "");
            if(!fallback.empty()) {
                try {
                    std::wstring wide =
                        boost::locale::conv::to_utf<wchar_t>(
                            src_begin, src_end, fallback,
                            boost::locale::conv::stop);
                    if(!wide.empty()) {
                        _buffer =
                            boost::locale::conv::utf_to_utf<char16_t>(wide);
                        spdlog::info("text decoded as fallback {}", fallback);
                        decoded = true;
                    }
                } catch(...) {
                    // The parallel validation succeeded, but keep the
                    // existing hard fallback path if the final conversion
                    // somehow fails here.
                }
            }

            if(!decoded) {
                // skip-mode CP932 -- never throws on bad bytes, just
                // drops them. Better to render a partially garbled
                // scenario than to abort.
                try {
                    std::wstring wide =
                        boost::locale::conv::to_utf<wchar_t>(
                            src_begin, src_end, "CP932",
                            boost::locale::conv::skip);
                    if(!wide.empty()) {
                        _buffer =
                            boost::locale::conv::utf_to_utf<char16_t>(wide);
                        spdlog::warn("text decoded with CP932 skip mode "
                                     "(some bytes dropped)");
                        decoded = true;
                    }
                } catch(...) {
                }
            }
            if(!decoded) {
                // Absolute last resort: byte-by-byte Latin-1 pass-through.
                // Cannot throw. Will render mojibake but never aborts the
                // engine over a single malformed file.
                _buffer.clear();
                _buffer.reserve(raw.size());
                for(auto b : raw)
                    _buffer.push_back((char16_t)(unsigned char)b);
                spdlog::warn(
                    "text decoded with Latin-1 byte pass-through "
                    "(no codec matched, mojibake expected)");
                decoded = true;
            }
        }
    }

    ~tTVPTextReadStream() override = default;

    tjs_uint Read(tTJSString &targ, tjs_uint size) override {
        static_assert(sizeof(tjs_char) == sizeof(char16_t),
                      "Char size mismatch");
        if(_pos >= _buffer.size()) {
            targ.Clear();
            return 0;
        }
        size_t remain = _buffer.size() - _pos;
        size_t n = size ? std::min<size_t>(size, remain) : remain;
        tjs_char *buf = targ.AllocBuffer(n);
        std::copy_n(_buffer.data() + _pos, n, buf);
        buf[n] = 0;
        _pos += n;
        targ.FixLen();
        return n;
    }

    void Destruct() override { delete this; }
};


class tTVPTextWriteStream : public iTJSTextWriteStream {
    // TODO: 32bit wchar_t support

    static constexpr size_t COMPRESSION_BUFFER_SIZE = 1024 * 1024;

    std::unique_ptr<tTJSBinaryStream> _stream{};
    tjs_int _cryptMode{};
    // -1 for no-crypt
    // 0: (unused)	(old buggy crypt mode)
    // 1: simple crypt
    // 2: complessed
    int _compressionLevel{}; // compression level of zlib

    std::unique_ptr<z_stream_s> _zStream{};
    tjs_uint _compressionSizePosition{ 0 };
    std::vector<Bytef> _compressionBuffer =
        std::vector<Bytef>(COMPRESSION_BUFFER_SIZE);
    bool _compressionFailed{ false };

public:
    tTVPTextWriteStream(const ttstr &name, const ttstr &mode) {
        // mode supports following modes:
        // dN: deflate(compress) at mode N ( currently not implemented
        // ) cN: write in cipher at mode N ( currently n is ignored )
        // zN: write with compress at mode N ( N is compression level
        // ) oN: write from binary offset N (in bytes)

        // check c/z mode
        _cryptMode =
            parseModeNumber(mode.c_str(), TJS_W('c'), 1, -1).value_or(1);

        if(auto z = parseModeNumber(mode.c_str(), TJS_W('z'), 1,
                                    Z_DEFAULT_COMPRESSION)) {
            _compressionLevel = z.value();
        } else {
            _cryptMode = 2;
        }

        if(_cryptMode != -1 && _cryptMode != 1 && _cryptMode != 2)
            TVPThrowExceptionMessage(TVPUnsupportedModeString,
                                     TJS_W("unsupported cipher mode"));

        // check o mode
        int ofs = parseModeNumber(mode.c_str(), TJS_W('o'), 255, 0).value();
        if(ofs != 0) {
            _stream.reset(TVPCreateStream(name, TJS_BS_UPDATE));
            _stream->SetPosition(ofs);
        } else {
            _stream.reset(TVPCreateStream(name, TJS_BS_WRITE));
        }

        if(_cryptMode == 1 || _cryptMode == 2) {
            // simple crypt or compressed
            tjs_uint8 crypt_mode_sig[4];
            crypt_mode_sig[0] = crypt_mode_sig[1] = 0xfe;
            crypt_mode_sig[2] = static_cast<tjs_uint8>(_cryptMode);
            crypt_mode_sig[3] = 0;
            _stream->WriteBuffer(crypt_mode_sig, 3);
        }

        // now output text stream will write unicode texts
        static tjs_uint8 bommark[2] = { 0xff, 0xfe };
        _stream->WriteBuffer(bommark, 2);

        if(_cryptMode == 2) {
            // allocate and initialize zlib straem
            _zStream.reset(new z_stream_s());
            _zStream->zalloc = Z_NULL;
            _zStream->zfree = Z_NULL;
            _zStream->opaque = Z_NULL;
            if(deflateInit(_zStream.get(), _compressionLevel) != Z_OK) {
                _compressionFailed = true;
                TVPThrowExceptionMessage(TVPCompressionFailed);
            }

            _zStream->next_in = nullptr;
            _zStream->avail_in = 0;
            _zStream->next_out = _compressionBuffer.data();
            _zStream->avail_out = COMPRESSION_BUFFER_SIZE;

            // Compression Size (write dummy)
            _compressionSizePosition =
                static_cast<tjs_uint>(_stream->GetPosition());
            WriteI64LE(0);
            WriteI64LE(0);
        }
    }

    ~tTVPTextWriteStream() override {
        if(_cryptMode == 2) {

            if(!_compressionFailed) {
                try {
                    // close zlib stream
                    int result = 0;
                    do {
                        result = deflate(_zStream.get(), Z_FINISH);
                        if(result != Z_OK && result != Z_STREAM_END) {
                            TVPThrowExceptionMessage(TVPCompressionFailed);
                        }
                        _stream->WriteBuffer(_compressionBuffer.data(),
                                             COMPRESSION_BUFFER_SIZE -
                                                 _zStream->avail_out);
                        _zStream->next_out = _compressionBuffer.data();
                        _zStream->avail_out = COMPRESSION_BUFFER_SIZE;
                    } while(result != Z_STREAM_END);

                    // rollback and write compression size.
                    _stream->SetPosition(_compressionSizePosition);
                    WriteI64LE(_zStream->total_out);
                    WriteI64LE(_zStream->total_in);
                } catch(...) {
                    // delete zlib compress stream
                    if(_zStream) {
                        deflateEnd(_zStream.get());
                    }
                    throw;
                }
            }
            // delete zlib compress stream
            if(_zStream) {
                deflateEnd(_zStream.get());
            }
        }
    }

    void WriteI64LE(tjs_uint64 v) {
        // write 64bit little endian value to the file.
        tjs_uint8 buf[8];
        for(int i = 0; i < 8; i++) {
            buf[i] = static_cast<tjs_uint8>(v >> (i * 8));
        }
        _stream->WriteBuffer(buf, 8);
    }

    void Write(const ttstr &targ) override {
        tjs_int len = targ.GetLen();
        auto buf = std::make_unique<tjs_uint16[]>(len + 1);
        const tjs_char *src = targ.c_str();
        tjs_int i;
        for(i = 0; i < len; i++) {
            buf[i] = src[i];
        }
        buf[i] = 0;

        if(_cryptMode == 1) {
            // simple crypt
            if(tjs_uint16 *p = buf.get()) {
                while(*p) {
                    tjs_char ch = *p;
                    ch = (ch & 0xaaaaaaaa) >> 1 | (ch & 0x55555555) << 1;
                    *p = ch;
                    p++;
                }
            }

            WriteRawData(buf.get(), len * sizeof(tjs_uint16));
        } else {
            WriteRawData(buf.get(), len * sizeof(tjs_uint16));
        }
    }

    void WriteRawData(void *ptr, size_t size) {
        if(_cryptMode == 2) {
            // compressed with zlib stream.
            _zStream->next_in = static_cast<Bytef *>(ptr);
            _zStream->avail_in = size;

            while(_zStream->avail_in > 0) {
                int result = deflate(_zStream.get(), Z_NO_FLUSH);
                if(result != Z_OK) {
                    _compressionFailed = true;
                    TVPThrowExceptionMessage(TVPCompressionFailed);
                }
                if(_zStream->avail_out == 0) {
                    _stream->WriteBuffer(_compressionBuffer.data(),
                                         COMPRESSION_BUFFER_SIZE);
                    _zStream->next_out = _compressionBuffer.data();
                    _zStream->avail_out = COMPRESSION_BUFFER_SIZE;
                }
            }
        } else {
            _stream->WriteBuffer(ptr, size); // write directly
        }
    }

    void Destruct() override { delete this; }
};

iTJSTextReadStream *TVPCreateTextStreamForRead(const ttstr &name,
                                               const ttstr &mode) {
    return new tTVPTextReadStream(name, mode);
}

iTJSTextWriteStream *TVPCreateTextStreamForWrite(const ttstr &name,
                                                 const ttstr &mode) {
    return new tTVPTextWriteStream(name, mode);
}

//---------------------------------------------------------------------------
void TVPSetDefaultReadEncoding(const ttstr &encoding) {
    ttstr codestr = encoding;
    codestr.ToLowerCase();
    if(codestr == TJS_W("sjis") || codestr == TJS_W("shiftjis") ||
       codestr == TJS_W("shift_jis") || codestr == TJS_W("shift-jis")) {
        G_DefaultReadEncoding = "cp932";
    } else if(codestr == TJS_W("utf8") || codestr == TJS_W("utf-8")) {
        G_DefaultReadEncoding = "UTF-8";
    } else {
        G_DefaultReadEncoding = encoding.AsStdString();
    }
}

//---------------------------------------------------------------------------
const tjs_char *TVPGetDefaultReadEncoding() {
    return ttstr{ G_DefaultReadEncoding }.c_str();
}
