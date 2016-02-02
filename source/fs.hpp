#ifndef __CTRX_FS_HPP__
#define __CTRX_FS_HPP__

#include <citrus/types.hpp>

#include <functional>
#include <string>
#include <vector>

typedef struct {
    const std::string path;
    const std::string name;
} FileInfo;

typedef struct {
    std::string path;
    std::string name;
    bool isDirectory;
} FileInfoEx;

u64 fsGetFreeSpace();
bool fsExists(const std::string path);
bool fsIsDirectory(const std::string path);
std::string fsGetFileName(const std::string path);
std::string fsGetExtension(const std::string path);
bool fsHasExtension(const std::string path, const std::string extension);
bool fsHasExtensions(const std::string path, const std::vector<std::string> extensions);
u32 fsGetFileSize(const std::string path);
u32 fsDataSearch(const std::string path, const std::vector<u8> searchTerm, const u32 offset = 0, bool showProgress = false);
std::vector<u8> fsDataGet(const std::string path, u32 offset, u32 size);
bool fsDataReplace(const std::string path, const std::vector<u8> data, u32 offset, u32 size);
bool fsDataProvider(const std::string path, u32 offset, u32 buffSize, std::function<bool(u32 &offset, bool &forceRefresh)> onLoop, std::function<bool(u8* data)> onUpdate);
bool fsPathDelete(const std::string path);
bool fsPathCopy(const std::string path, const std::string dest, bool showProgress = false);
bool fsPathRename(const std::string path, const std::string dest);
bool fsCreateDir(const std::string path);
bool fsCreateDummyFile(const std::string path, u64 size = 0, u16 content = 0x0000, bool showProgress = false);
std::vector<FileInfo> fsGetDirectoryContents(const std::string directory);
std::vector<FileInfoEx> fsGetDirectoryContentsEx(const std::string directory);

#endif
