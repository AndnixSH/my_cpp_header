#pragma once
#include <stdio.h>
#include <sys/sysmacros.h>
#include <sys/stat.h>
#include <iostream>
#include <sys/mman.h>
#include <vector>
#include <fcntl.h>
#include <unistd.h>

using namespace std;

#define ADD_SIZE(s,x) \
                if (strstr(line,s) == line) { \
                    map.smap.x = m; \
                    continue; \
                }
                

struct SMapInfo {
    size_t mem = 0;
    size_t kernel_page = 0;
    size_t mmu_page = 0;
    size_t rss = 0;
    size_t pss = 0;
    size_t shared_clean = 0;
    size_t shared_dirty = 0;
    size_t private_clean = 0;
    size_t private_dirty = 0;
    size_t referenced = 0;
    size_t anonymous = 0;
    size_t anonhugepages = 0;
    size_t shared_hugetlb = 0;
    size_t private_hugetlb = 0;
    size_t swap = 0;
    size_t swappss = 0;
    size_t locked = 0;
};

struct MapInfo {
    uintptr_t start;
    uintptr_t end;
    uint8_t perms;
    bool is_private;
    dev_t dev;
    ino_t inode;
    std::string path;
    uintptr_t offset;
    SMapInfo smap;
};

std::vector<MapInfo> Scan(int pid = -1, bool smap = false){
    std::vector<MapInfo> maps;
    MapInfo map;
    FILE *fp;
    char path[128];
    if (pid <= 0) {
        snprintf(path, 128, "/proc/self/%s", (smap)? "smaps" : "maps");
        fp = fopen(path, "re");
    } else {
        snprintf(path, 128, "/proc/%d/%s", pid, (smap)? "smaps" : "maps");
        fp = fopen(path, "re");
    }
    if (fp == nullptr) return maps;
    char line[4098];
    while (fgets(line, sizeof(line), fp)) {
        char path[4098];
        char perms[10];
        char address[50];
        int major_dev, minor_dev, path_start;
        sscanf(line, "%s %s %ld %x:%x %ld %n%*s", address, perms, &map.offset, &major_dev, &minor_dev, &map.inode, &path_start);
        map.dev = makedev(major_dev, minor_dev);
        line [strlen(line) - 1] = '\0';
        map.path = line + path_start;
        map.perms = 0;
        map.is_private = false;
        if (strchr(perms, 'r')) map.perms |= PROT_READ;
        if (strchr(perms, 'w')) map.perms |= PROT_WRITE;
        if (strchr(perms, 'x')) map.perms |= PROT_EXEC;
        if (strchr(perms, 'p')) map.is_private = true;
        char *address2 = strchr(address, '-');
        *address2 = '\0';
        address2++;
        map.start = 0; map.end = 0;
        for (int i=0; address[i]; i++) {
            map.start *= 16;
            int x = 0;
            if (address[i] >= 'a' && address[i] <= 'f') {
                x = address[i] - 'a' + 10;
            } else if (address[i] >= '0' && address[i] <= '9') {
                x = address[i] - '0';
            }
            map.start += x;
        }
        for (int i=0; address2[i]; i++) {
            map.end *= 16;
            int x = 0;
            if (address2[i] >= 'a' && address2[i] <= 'f') {
                x = address[i] - 'a' + 10;
            } else if (address2[i] >= '0' && address2[i] <= '9') {
                x = address2[i] - '0';
            }
            map.end += x;
        }
        printf("%s\n", line);
        printf("%s\n", map.path.data());
        // smaps scan
        if (smap) {
            while (fgets(line, sizeof(line), fp)) {
                if (strstr(line, "VmFlags:") == line) break;
                int m;
                sscanf(line, "%*s %u", &m);
                ADD_SIZE("Size", mem)
                ADD_SIZE("KernelPageSize", kernel_page)
                ADD_SIZE("MMUPageSize", mmu_page)
                ADD_SIZE("Rss", rss)
                ADD_SIZE("Pss", pss)
                ADD_SIZE("Shared_Clean", shared_clean)
                ADD_SIZE("Shared_Dirty", shared_dirty)
                ADD_SIZE("Private_Clean", private_clean)
                ADD_SIZE("Private_Dirty", private_dirty)
                ADD_SIZE("Referenced", referenced)
                ADD_SIZE("Anonymous", anonymous)
                ADD_SIZE("AnonHugePages", anonhugepages)
                ADD_SIZE("Shared_Hugetlb", shared_hugetlb)
                ADD_SIZE("Private_Hugetlb", private_hugetlb)
                ADD_SIZE("Swap", swap)
                ADD_SIZE("SwapPss", swappss)
                ADD_SIZE("Locked", locked)
            }
        }
        maps.emplace_back(map);
    }
    fclose(fp);
    return maps;
}

#undef ADD_SIZE

static vector<MapInfo> find_maps(const char *name) {
    auto maps = Scan();
    for (auto iter = maps.begin(); iter != maps.end();) {
        if (iter->path != name) {
            iter = maps.erase(iter);
        } else {
            ++iter;
        }
    }
    return maps;
}

void remap_all(const char *name) {
    auto maps = find_maps(name);
    for (auto &info : maps) {
        void *addr = reinterpret_cast<void *>(info.start);
        size_t size = info.end - info.start;
        void *copy = mmap(nullptr, size, PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
        if ((info.perms & PROT_READ) == 0) {
            mprotect(addr, size, PROT_READ);
        }
        memcpy(copy, addr, size);
        mremap(copy, size, size, MREMAP_MAYMOVE | MREMAP_FIXED, addr);
        mprotect(addr, size, info.perms);
    }
}
