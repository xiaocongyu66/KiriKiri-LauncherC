#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200112L
#endif

/* ioapi.c -- IO callbacks for minizip.
   This file uses stdio only so the local minizip target stays platform-neutral. */

#include "ioapi.h"

#include <limits.h>
#include <stdio.h>

#if defined(_WIN32)
#include <io.h>
#else
#include <sys/types.h>
#endif

voidpf call_zopen64(const zlib_filefunc64_32_def *pfilefunc,
                    const void *filename, int mode) {
    if(pfilefunc->zfile_func64.zopen64_file != NULL) {
        return (*(pfilefunc->zfile_func64.zopen64_file))(
            pfilefunc->zfile_func64.opaque, filename, mode);
    }
    return (*(pfilefunc->zopen32_file))(pfilefunc->zfile_func64.opaque,
                                        (const char *)filename, mode);
}

voidpf call_zopendisk64(const zlib_filefunc64_32_def *pfilefunc,
                        voidpf filestream, uint32_t number_disk, int mode) {
    (void)pfilefunc;
    (void)filestream;
    (void)number_disk;
    (void)mode;
    return NULL;
}

long call_zseek64(const zlib_filefunc64_32_def *pfilefunc, voidpf filestream,
                  ZPOS64_T offset, int origin) {
    if(pfilefunc->zfile_func64.zseek64_file != NULL) {
        return (*(pfilefunc->zfile_func64.zseek64_file))(
            pfilefunc->zfile_func64.opaque, filestream, offset, origin);
    }
    if(offset > (ZPOS64_T)ULONG_MAX) {
        return -1;
    }
    return (*(pfilefunc->zseek32_file))(pfilefunc->zfile_func64.opaque,
                                        filestream, (uLong)offset, origin);
}

ZPOS64_T call_ztell64(const zlib_filefunc64_32_def *pfilefunc,
                      voidpf filestream) {
    if(pfilefunc->zfile_func64.ztell64_file != NULL) {
        return (*(pfilefunc->zfile_func64.ztell64_file))(
            pfilefunc->zfile_func64.opaque, filestream);
    }
    {
        uLong tell_uLong = (*(pfilefunc->ztell32_file))(
            pfilefunc->zfile_func64.opaque, filestream);
        if(tell_uLong == (uLong)-1) {
            return (ZPOS64_T)-1;
        }
        return tell_uLong;
    }
}

void fill_zlib_filefunc64_32_def_from_filefunc32(
    zlib_filefunc64_32_def *p_filefunc64_32,
    const zlib_filefunc_def *p_filefunc32) {
    p_filefunc64_32->zfile_func64.zopen64_file = NULL;
    p_filefunc64_32->zopen32_file = p_filefunc32->zopen_file;
    p_filefunc64_32->zfile_func64.zread_file = p_filefunc32->zread_file;
    p_filefunc64_32->zfile_func64.zwrite_file = p_filefunc32->zwrite_file;
    p_filefunc64_32->zfile_func64.ztell64_file = NULL;
    p_filefunc64_32->zfile_func64.zseek64_file = NULL;
    p_filefunc64_32->zfile_func64.zclose_file = p_filefunc32->zclose_file;
    p_filefunc64_32->zfile_func64.zerror_file = p_filefunc32->zerror_file;
    p_filefunc64_32->zfile_func64.opaque = p_filefunc32->opaque;
    p_filefunc64_32->ztell32_file = p_filefunc32->ztell_file;
    p_filefunc64_32->zseek32_file = p_filefunc32->zseek_file;
}

static voidpf ZCALLBACK fopen_file_func(voidpf opaque, const char *filename,
                                        int mode);
static uLong ZCALLBACK fread_file_func(voidpf opaque, voidpf stream, void *buf,
                                       uLong size);
static uLong ZCALLBACK fwrite_file_func(voidpf opaque, voidpf stream,
                                        const void *buf, uLong size);
static long ZCALLBACK ftell_file_func(voidpf opaque, voidpf stream);
static long ZCALLBACK fseek_file_func(voidpf opaque, voidpf stream,
                                      uLong offset, int origin);
static ZPOS64_T ZCALLBACK ftell64_file_func(voidpf opaque, voidpf stream);
static long ZCALLBACK fseek64_file_func(voidpf opaque, voidpf stream,
                                        ZPOS64_T offset, int origin);
static int ZCALLBACK fclose_file_func(voidpf opaque, voidpf stream);
static int ZCALLBACK ferror_file_func(voidpf opaque, voidpf stream);

static int translate_seek_origin(int origin) {
    switch(origin) {
        case ZLIB_FILEFUNC_SEEK_CUR:
            return SEEK_CUR;
        case ZLIB_FILEFUNC_SEEK_END:
            return SEEK_END;
        case ZLIB_FILEFUNC_SEEK_SET:
            return SEEK_SET;
        default:
            return -1;
    }
}

static voidpf ZCALLBACK fopen_file_func(voidpf opaque, const char *filename,
                                        int mode) {
    const char *mode_fopen = NULL;
    (void)opaque;
    if((mode & ZLIB_FILEFUNC_MODE_READWRITEFILTER) == ZLIB_FILEFUNC_MODE_READ) {
        mode_fopen = "rb";
    } else if(mode & ZLIB_FILEFUNC_MODE_EXISTING) {
        mode_fopen = "rb+";
    } else if(mode & ZLIB_FILEFUNC_MODE_CREATE) {
        mode_fopen = "wb+";
    }
    if(mode_fopen == NULL) {
        return NULL;
    }
    return (voidpf)fopen(filename, mode_fopen);
}

static voidpf ZCALLBACK fopen64_file_func(voidpf opaque, const void *filename,
                                          int mode) {
    return fopen_file_func(opaque, (const char *)filename, mode);
}

static uLong ZCALLBACK fread_file_func(voidpf opaque, voidpf stream, void *buf,
                                       uLong size) {
    (void)opaque;
    return (uLong)fread(buf, 1, size, (FILE *)stream);
}

static uLong ZCALLBACK fwrite_file_func(voidpf opaque, voidpf stream,
                                        const void *buf, uLong size) {
    (void)opaque;
    return (uLong)fwrite(buf, 1, size, (FILE *)stream);
}

static long ZCALLBACK ftell_file_func(voidpf opaque, voidpf stream) {
    ZPOS64_T position = ftell64_file_func(opaque, stream);
    if(position > (ZPOS64_T)LONG_MAX) {
        return -1;
    }
    return (long)position;
}

static long ZCALLBACK fseek_file_func(voidpf opaque, voidpf stream,
                                      uLong offset, int origin) {
    return fseek64_file_func(opaque, stream, offset, origin);
}

static ZPOS64_T ZCALLBACK ftell64_file_func(voidpf opaque, voidpf stream) {
    (void)opaque;
#if defined(_WIN32)
    {
        __int64 position = _ftelli64((FILE *)stream);
        return position < 0 ? (ZPOS64_T)-1 : (ZPOS64_T)position;
    }
#else
    {
        off_t position = ftello((FILE *)stream);
        return position < 0 ? (ZPOS64_T)-1 : (ZPOS64_T)position;
    }
#endif
}

static long ZCALLBACK fseek64_file_func(voidpf opaque, voidpf stream,
                                        ZPOS64_T offset, int origin) {
    int translated_origin = translate_seek_origin(origin);
    (void)opaque;
    if(translated_origin < 0) {
        return -1;
    }
#if defined(_WIN32)
    return _fseeki64((FILE *)stream, (__int64)offset, translated_origin);
#else
    return fseeko((FILE *)stream, (off_t)offset, translated_origin);
#endif
}

static int ZCALLBACK fclose_file_func(voidpf opaque, voidpf stream) {
    (void)opaque;
    return fclose((FILE *)stream);
}

static int ZCALLBACK ferror_file_func(voidpf opaque, voidpf stream) {
    (void)opaque;
    return ferror((FILE *)stream);
}

zlib_filefunc64_def TVPZlibFileFunc = { fopen64_file_func, fread_file_func,
                                        fwrite_file_func, ftell64_file_func,
                                        fseek64_file_func, fclose_file_func,
                                        ferror_file_func, NULL };

void fill_fopen_filefunc(zlib_filefunc_def *pzlib_filefunc_def) {
    pzlib_filefunc_def->zopen_file = fopen_file_func;
    pzlib_filefunc_def->zread_file = fread_file_func;
    pzlib_filefunc_def->zwrite_file = fwrite_file_func;
    pzlib_filefunc_def->ztell_file = ftell_file_func;
    pzlib_filefunc_def->zseek_file = fseek_file_func;
    pzlib_filefunc_def->zclose_file = fclose_file_func;
    pzlib_filefunc_def->zerror_file = ferror_file_func;
    pzlib_filefunc_def->opaque = NULL;
}

void fill_fopen64_filefunc(zlib_filefunc64_def *pzlib_filefunc_def) {
    *pzlib_filefunc_def = TVPZlibFileFunc;
}
