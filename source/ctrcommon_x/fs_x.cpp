#include "fs_x.hpp"
#include "ui_x.hpp"

#include <ctrcommon/fs.hpp>
#include <ctrcommon/input.hpp>
#include <ctrcommon/ui.hpp>

#include <sys/dirent.h>
#include <sys/errno.h>
#include <sys/unistd.h>
#include <string.h>

#include <cstdio>
#include <cstdlib>
#include <algorithm>

#define CTRX_BUFSIZ (128 * 1024)

struct fsAlphabetizeFoldersFiles {
	inline bool operator()(FileInfoEx a, FileInfoEx b) {
		if(a.isDirectory == b.isDirectory)
			return strcasecmp(a.name.c_str(), b.name.c_str()) < 0;
		else return a.isDirectory;
	}
};

bool fsShowProgress(const std::string operationStr, const std::string pathStr, u64 pos, u64 totalSize) {
	static u32 prevProgress = -1;
	u32 progress = (u32) ((pos * 100) / totalSize);
	if(prevProgress != progress) {
		prevProgress = progress;
		uiDisplayProgress(TOP_SCREEN, operationStr, uiTruncateString(pathStr, 36, 0) + "\nPress B to cancel.", true, progress);
	}
	
	inputPoll();
	return !inputIsPressed(BUTTON_B);
}
	
u32 fsGetFileSize(const std::string path) {
	struct stat st;
	stat(path.c_str(), &st);
	return (u32) st.st_size;
}

std::string fsGetName(const std::string path) {
	std::string::size_type slashPos = path.rfind('/');
	return (slashPos != std::string::npos) ? path.substr(slashPos + 1) : path;
}

std::string fsGetExtension(const std::string path) {
	std::string::size_type dotPos = path.rfind('.');
	return (dotPos != std::string::npos) ? path.substr(dotPos + 1) : "";
}

bool fsPathDelete(const std::string path) {
	if(fsIsDirectory(path)) {
		std::vector<FileInfo> contents = fsGetDirectoryContents(path);
		for (std::vector<FileInfo>::iterator it = contents.begin(); it != contents.end(); it++)
			if (!fsPathDelete((*it).path)) return false;
		return (rmdir(path.c_str()) == 0);
	} else return (remove(path.c_str()) == 0);
}

bool fsPathCopy(const std::string path, const std::string dest, bool showProgress) {
	if(fsExists(dest)) {
		errno = EEXIST;
		return false;
	}
	if(fsIsDirectory(path)) {
		if(dest.find(path + "/") != std::string::npos) {
			errno = ENOTSUP;
			return false;
		}
		if(mkdir(dest.c_str(), 0777) != 0) return false;
		std::vector<FileInfo> contents = fsGetDirectoryContents(path);
		for (std::vector<FileInfo>::iterator it = contents.begin(); it != contents.end(); it++)
			if (!fsPathCopy((*it).path, dest + "/" + (*it).name, showProgress)) return false;
		return true;
	} else {
		if(showProgress) fsShowProgress("Copying", path, 0, 1);
		bool ret = false;
		u8* buffer = (u8*) malloc( CTRX_BUFSIZ );
		u64 total = fsGetFileSize(path);
		u64 pos = 0;
		size_t size;
		FILE* fp = fopen(path.c_str(), "rb");
		FILE* fd = fopen(dest.c_str(), "wb");
		if ((fp != NULL) && (fd != NULL) && (buffer != NULL)) {
			while ((size = fread(buffer, 1, CTRX_BUFSIZ, fp)) > 0) {
				pos += fwrite(buffer, 1, size, fd);
				if(showProgress && !fsShowProgress("Copying", path, pos, total)) {
					errno = ECANCELED;
					break;
				}
			}
			ret = (pos == total);
		}
		if(buffer != NULL) free(buffer);
		if(fp != NULL) fclose(fp);
		if(fd != NULL) fclose(fd);
		return ret;
	}
}

bool fsPathRename(const std::string path, const std::string dest) {
	if(dest.find(path + "/") != std::string::npos) {
		errno = ENOTSUP;
		return false;
	}
	if(fsExists(dest)) {
		errno = EEXIST;
		return false;
	}
	return (rename(path.c_str(), dest.c_str()) == 0);
}

bool fsCreateDir(const std::string path) {
	if(fsExists(path)) {
		errno = EEXIST;
		return false;
	}
	return (mkdir(path.c_str(), 0777) == 0);
}

bool fsCreateDummyFile(const std::string path, u64 size, u16 content, bool showProgress) {
	if(fsExists(path)) {
		errno = EEXIST;
		return false;
	}
	if(size < CTRX_BUFSIZ) showProgress = false;
	if(showProgress) fsShowProgress("Generating", path, 0, 1);
	bool ret = false;
	u8* buffer = (u8*) malloc( CTRX_BUFSIZ );
	FILE* fp = fopen(path.c_str(), "wb");
	if((fp != NULL) && (buffer != NULL)) {
		u8 byte = content & 0xFF;
		u8 inc = (content >> 8) & 0xFF;
		for(u64 count = 0; count < CTRX_BUFSIZ; count++, byte += inc)
			buffer[count] = byte;
		u64 pos = 0;
		for(u64 count = 0; count < size; count += CTRX_BUFSIZ) {
			pos += fwrite(buffer, 1, (size - count < CTRX_BUFSIZ) ? size - count : CTRX_BUFSIZ, fp);
			if(showProgress && !fsShowProgress("Generating", path, pos, size)) {
				errno = ECANCELED;
				break;
			}
		}
		ret = (pos == size);
	}
	if(buffer != NULL) free(buffer);
	if(fp != NULL) fclose(fp);
	return ret;
}

std::vector<FileInfoEx> fsGetDirectoryContentsEx(const std::string directory) {
	std::vector<FileInfoEx> result;
	bool hasSlash = directory.size() != 0 && directory[directory.size() - 1] == '/';
	const std::string dirWithSlash = hasSlash ? directory : directory + "/";

	DIR* dir = opendir(dirWithSlash.c_str());
	if(dir == NULL) {
		return result;
	}

	while(true) {
		struct dirent* ent = readdir(dir);
		if(ent == NULL) {
			break;
		}
		const std::string name = std::string(ent->d_name);
		if((name.compare(".") != 0) && (name.compare("..") != 0)) {
			const std::string path = dirWithSlash + std::string(ent->d_name);
			bool isDirectory = fsIsDirectory(path);
			result.push_back({path, std::string(ent->d_name), isDirectory});
		}
	}

	closedir(dir);
	std::sort(result.begin(), result.end(), fsAlphabetizeFoldersFiles());
	return result;
}
