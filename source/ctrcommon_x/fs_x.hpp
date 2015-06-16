#ifndef __CTRCOMMON_FS_X_HPP__
#define __CTRCOMMON_FS_X_HPP__

#include <ctrcommon/types.hpp>

#include <functional>
#include <string>
#include <vector>

typedef struct {
	std::string path;
	std::string name;
	bool isDirectory;
} FileInfoEx;

u32 fsGetFileSize(const std::string path);
std::string fsGetName(const std::string path);
bool fsProvideData(const std::string path, u32 offset, u32 buffSize, std::function<bool(u32 &offset)> onLoop, std::function<bool(u8* data)> onUpdate);
bool fsPathDelete(const std::string path);
bool fsPathCopy(const std::string path, const std::string dest, bool showProgress = false);
bool fsPathRename(const std::string path, const std::string dest);
bool fsCreateDir(const std::string path);
bool fsCreateDummyFile(const std::string path, u64 size = 0, u16 content = 0x0000, bool showProgress = false);
std::vector<FileInfoEx> fsGetDirectoryContentsEx(const std::string directory);

#endif
