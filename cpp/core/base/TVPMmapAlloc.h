#ifndef TVP_MMAP_ALLOC_H
#define TVP_MMAP_ALLOC_H

#if defined(__APPLE__) || defined(__linux__) || defined(__ANDROID__)
#include <sys/mman.h>
#include <unistd.h>
#include <cstddef>

inline void *TVPMmapAlloc(size_t size) {
    static const size_t pageSize = (size_t)sysconf(_SC_PAGESIZE);
    size_t totalSize = size + sizeof(size_t);
    totalSize = (totalSize + pageSize - 1) & ~(pageSize - 1);
    void *ptr = mmap(nullptr, totalSize, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANON, -1, 0);
    if(ptr == MAP_FAILED) return nullptr;
    *(size_t *)ptr = totalSize;
    return (char *)ptr + sizeof(size_t);
}

inline void TVPMmapFree(void *mem) {
    if(!mem) return;
    char *base = (char *)mem - sizeof(size_t);
    size_t totalSize = *(size_t *)base;
    munmap(base, totalSize);
}

#define TVP_USE_MMAP_TEMP 1
#endif

#endif
