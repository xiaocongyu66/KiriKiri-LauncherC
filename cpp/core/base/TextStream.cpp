#include <cstdint>
#include <uchardet.h>
#include <zlib.h>
#include <optional>
#include <algorithm>
#include <cstring>
#include <string>

#include "TextStream.h"

#include <opencv2/core/hal/interface.h>
#include <spdlog/spdlog.h>

#include "MsgIntf.h"
#include "UtilStreams.h"
#include "tjsError.h"
#include "CharacterSet.h"
#include "BinaryStream.h"

static std::string G_DefaultReadEncoding = "UTF-8";

static bool hasNonAsciiBytes(const unsigned char *raw, size_t size) {
    for(size_t i = 0; i < size; ++i) {
        if(raw[i] >= 0x80)
            return true;
    }
    return false;
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

static bool looksLikeCP932(const unsigned char *raw, size_t size) {
    size_t pairs = 0;
    size_t highBytes = 0;

    for(size_t i = 0; i < size; ++i) {
        const unsigned char ch = raw[i];
        if(ch < 0x80)
            continue;

        ++highBytes;
        if(isCP932Lead(ch) && i + 1 < size && isCP932Trail(raw[i + 1])) {
            ++pairs;
            ++i;
            continue;
        }
        if(isCP932HalfwidthKana(ch))
            continue;

        return false;
    }

    return highBytes > 0 && pairs > 0;
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

static bool isLegacyCJKEncodingGuess(const std::string &encoding) {
    return sameEncodingName(encoding, "SHIFT_JIS") ||
           sameEncodingName(encoding, "SHIFT-JIS") ||
           sameEncodingName(encoding, "SJIS") ||
           sameEncodingName(encoding, "CP932") ||
           sameEncodingName(encoding, "CP936") ||
           sameEncodingName(encoding, "GBK") ||
           sameEncodingName(encoding, "GB18030") ||
           sameEncodingName(encoding, "BIG5") ||
           sameEncodingName(encoding, "EUC-JP") ||
           sameEncodingName(encoding, "EUC-KR");
}

std::string checkTextEncoding(const void *buf, size_t size,
                              std::uint8_t &bomSize) {
    auto raw = static_cast<const unsigned char *>(buf);
    std::string encoding;
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

        if(sameEncodingName(encoding, "SHIFT_JIS") ||
           sameEncodingName(encoding, "SHIFT-JIS") ||
           sameEncodingName(encoding, "SJIS") ||
           sameEncodingName(encoding, "CP932")) {
            encoding = "CP932";
        } else if(hasNonAscii && looksLikeCP932(raw, size) &&
                  !isLegacyCJKEncodingGuess(encoding)) {
            // Short KiriKiri scripts are often CP932 and can be misdetected
            // as a western single-byte charset. Accepting that guess turns
            // bytes like CP932 "和" (98 61) into mojibake and breaks storage
            // names, so prefer CP932 when the byte stream is structurally
            // valid Shift_JIS/CP932 and not valid UTF-8.
            encoding = "CP932";
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
            static const char *const kFallbackCharsets[] = {
                "CP932", "SHIFT_JIS", "CP936",   "GBK",
                "BIG5",  "EUC-JP",    "EUC-KR",  nullptr,
            };
            const char *src_begin =
                reinterpret_cast<const char *>(raw.data());
            const char *src_end =
                reinterpret_cast<const char *>(raw.data() + raw.size());
            bool decoded = false;
            for(const char *const *cs = kFallbackCharsets; *cs && !decoded;
                ++cs) {
                try {
                    std::wstring wide =
                        boost::locale::conv::to_utf<wchar_t>(
                            src_begin, src_end, *cs,
                            boost::locale::conv::stop);
                    if(!wide.empty()) {
                        _buffer =
                            boost::locale::conv::utf_to_utf<char16_t>(wide);
                        spdlog::info("text decoded as fallback {}", *cs);
                        decoded = true;
                    }
                } catch(...) {
                    // try next codec
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
