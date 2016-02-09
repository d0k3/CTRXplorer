#include "fs.hpp"
#include "ui.hpp"

#include <citrus/core.hpp>
#include <citrus/hid.hpp>

#include <sys/dirent.h>
#include <sys/errno.h>
#include <sys/unistd.h>
#include <string.h>

#include <cstdio>
#include <cstdlib>
#include <algorithm>

#include <3ds.h>

using namespace ctr;

#define CTRX_BUFSIZ (1 * 1024 * 1024)

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
        uiDisplayProgress(gpu::SCREEN_TOP, operationStr, uiTruncateString(pathStr, 36, 0) + "\nPress B to cancel.", true, progress);
    }
    
    hid::poll();
    return !hid::pressed(hid::BUTTON_B);
}

u64 fsGetFreeSpace() {
    FS_ArchiveResource resource;
    Result res = FSUSER_GetSdmcArchiveResource(&resource);
    return (res != 0) ? 0 : (u64) resource.clusterSize * (u64) resource.freeClusters;
}

bool fsExists(const std::string path) {
    FILE* fd = fopen(path.c_str(), "r");
    if(fd) {
        fclose(fd);
        return true;
    }

    return fsIsDirectory(path);
}

bool fsIsDirectory(const std::string path) {
    DIR* dir = opendir(path.c_str());
    if(dir) {
        closedir(dir);
        return true;
    }

    return false;
}

std::string fsGetFileName(const std::string path) {
    std::string::size_type slashPos = path.rfind('/');
    if(slashPos == std::string::npos) {
        return path;
    }

    return path.substr(slashPos + 1);
}

std::string fsGetExtension(const std::string path) {
    std::string::size_type dotPos = path.rfind('.');
    if(dotPos == std::string::npos) {
        return "";
    }

    return path.substr(dotPos + 1);
}

bool fsHasExtension(const std::string path, const std::string extension) {
    if(extension.empty()) {
        return true;
    }

    const std::string ext = fsGetExtension(path);
    return strcasecmp(ext.c_str(), extension.c_str()) == 0;
}

bool fsHasExtensions(const std::string path, const std::vector<std::string> extensions) {
    if(extensions.empty()) {
        return true;
    }

    const std::string ext = fsGetExtension(path);
    for(std::vector<std::string>::const_iterator it = extensions.begin(); it != extensions.end(); it++) {
        std::string extension = *it;
        if(strcasecmp(ext.c_str(), extension.c_str()) == 0) {
            return true;
        }
    }

    return false;
}

u32 fsGetFileSize(const std::string path) {
    struct stat st;
    stat(path.c_str(), &st);
    return (u32) st.st_size;
}

bool fsFileResize(const std::string path, u32 offset, u32 oldsize, u32 newsize, bool showProgress) {
    if(newsize == oldsize) return true;
    
    bool ret = true;
    u64 total = fsGetFileSize(path);
    size_t l_bufsiz = (total - (offset + oldsize) < CTRX_BUFSIZ) ?
        total - (offset + oldsize) : CTRX_BUFSIZ;
    u8* buffer = (u8*) malloc( l_bufsiz );
    FILE* fp = fopen(path.c_str(), "rb+");;
    
    if(offset + oldsize > total) {
        errno = ENOTSUP;
        ret = false;
    }
    
    if(ret && (fp != NULL) && (buffer != NULL)) {
        size_t l_size = l_bufsiz; // don't change this
        if(newsize > oldsize) { // increase file size
            ret = (ftruncate(fileno(fp), total + newsize - oldsize) == 0);
            for (u32 rpos = total; ret && (rpos > offset + oldsize); ) {
                if(showProgress && !fsShowProgress("Inflating", path, total - rpos, total)) {
                    errno = ECANCELED;
                    ret = false;
                    break;
                }
                l_size = ((rpos - (offset + oldsize)) > l_bufsiz) ? l_bufsiz : rpos - (offset + oldsize);
                rpos -= l_size;
                u32 wpos = rpos + newsize - oldsize;
                ret = ret && (fseek(fp, rpos, SEEK_SET) == 0);
                ret = ret && (fread(buffer, 1, l_size, fp) == l_size);
                ret = ret && (fseek(fp, wpos, SEEK_SET) == 0);
                ret = ret && (fwrite(buffer, 1, l_size, fp) == l_size);
            }
        } else { // truncate file
            for (u32 rpos = offset + oldsize; ret && (rpos < total); rpos += l_size) {
                u32 wpos = rpos + newsize - oldsize;
                l_size = ((total - rpos) > l_bufsiz) ? l_bufsiz : total - rpos;
                if(showProgress && !fsShowProgress("Deflating", path, rpos, total)) {
                    errno = ECANCELED;
                    ret = false;
                    break;
                }
                ret = ret && (fseek(fp, rpos, SEEK_SET) == 0);
                ret = ret && (fread(buffer, 1, l_size, fp) == l_size);
                ret = ret && (fseek(fp, wpos, SEEK_SET) == 0);
                ret = ret && (fwrite(buffer, 1, l_size, fp) == l_size);
            }
            ret = (ftruncate(fileno(fp), total + newsize - oldsize) == 0);
        }
    } else ret = false;
    
    if(buffer != NULL) free(buffer);
    if(fp != NULL) fclose(fp);
    
    return ret;
}

u32 fsDataSearch(const std::string path, const std::vector<u8> searchTerm, u32 offset, bool showProgress) {
    u64 total = fsGetFileSize(path);
    u64 totalPlus = total + searchTerm.size() - 1;
    u32 offsetFound = (u32) -1;
    size_t l_bufsiz = (total < CTRX_BUFSIZ) ? total : CTRX_BUFSIZ;
    u8* buffer = (u8*) malloc( l_bufsiz );
    FILE* fp = fopen(path.c_str(), "rb");
    if((!searchTerm.empty()) && (fp != NULL) && (buffer != NULL)) {
        size_t size = 0;
        for (u64 i = 0; (i < totalPlus) && (offsetFound == (u32) -1); i += size) {
            u64 pos = (offset + i) % total;
            if(total - pos < l_bufsiz) size = total - pos;
            else if(totalPlus - i < l_bufsiz) size = totalPlus - i;
            else size = l_bufsiz;
            if(size < searchTerm.size()) continue;
            if(showProgress && !fsShowProgress("Searching", path, pos, total)) {
                errno = ECANCELED;
                break;
            }
            fseek(fp, pos, SEEK_SET);
            if(fread(buffer, 1, size, fp) != size)
                break;
            for (u32 p = 0; p <= size - searchTerm.size(); p++) {
                if(memcmp(buffer + p, searchTerm.data(), searchTerm.size()) == 0) {
                    offsetFound = pos + p;
                    break;
                }
            }
        }
    }
    if(buffer != NULL) free(buffer);
    if(fp != NULL) fclose(fp);
    
    return offsetFound;
}

std::vector<u8> fsDataGet(const std::string path, u32 offset, u32 size) { 
    // this is not intended to be used for large chunks of data
    FILE* fp;
    std::vector<u8> data;
    u64 total = fsGetFileSize(path);
    if(offset + size > total) {
        errno = ENOTSUP;
        return data;
    }
    fp = fopen(path.c_str(), "rb");
    if(fp == NULL) return data;
    fseek(fp, offset, SEEK_SET);
    if((unsigned) ftell(fp) != offset) {
        fclose(fp);
        return data;
    }
    data.resize(size);
    if(fread(data.data(), 1, size, fp) != size) {
        data.clear();
        fclose(fp);
        return data;
    }
    fclose(fp);
    return data;
}

bool fsDataReplace(const std::string path, const std::vector<u8> data, u32 offset, u32 size) {
    FILE* fp;
    bool ret = false;
    u64 total = fsGetFileSize(path);
    if(offset + size > total) {
        errno = ENOTSUP;
        return false;
    }
    if((data.size() != size) && !fsFileResize(path, offset, size, data.size(), true))
        return false;
    fp = fopen(path.c_str(), "rb+");
    if(fp == NULL) return false;
    fseek(fp, offset, SEEK_SET);
    if((unsigned) ftell(fp) != offset) {
        fclose(fp);
        return false;
    }
    ret = (fwrite(data.data(), 1, data.size(), fp) == data.size());
    fclose(fp);
    return ret;
}

bool fsDataProvider(const std::string path, u32 offset, u32 buffSize, std::function<bool(u32 &offset, bool &forceRefresh)> onLoop, std::function<bool(u8* data)> onUpdate) {
    if((onLoop == NULL) || (onUpdate == NULL)) {
        errno = ENOTSUP;
        return false;
    }
    if(!fsExists(path)) {
        errno = ENOENT;
        return false;
    }
    
    FILE* fp = fopen(path.c_str(), "rb");
    u8* buffer = (u8*) calloc(buffSize, 1);
    u8* bufferEnd = buffer + buffSize;
    
    u32 fileSize  = fsGetFileSize(path);
    u32 offsetPrev = (u32) -1;
    
    bool forceRefresh = false;
    
    bool result = false;
    
    if((fp == NULL) || (buffer == NULL)) {
        if(fp != NULL) fclose(fp);
        if(buffer != NULL) free(buffer);
        return false;
    }
    
    while(core::running()) {
        if(((offset != offsetPrev) || forceRefresh) && (offset <= fileSize)) {
            if (forceRefresh) {
                fclose(fp);
                fileSize = fsGetFileSize(path);
                fp = fopen(path.c_str(), "rb");
                if(fp == NULL) break;
                if(offset > fileSize) offset = fileSize;
                fseek(fp, offset, SEEK_SET);
                fread(buffer, 1, buffSize, fp);
                forceRefresh = false;
            } else if(offset < offsetPrev) {
                u32 dataEnd = offset + buffSize;
                u32 overlap = (dataEnd > offsetPrev) ? dataEnd - offsetPrev : 0;
                memmove(bufferEnd - overlap, buffer, overlap);
                fseek(fp, offset, SEEK_SET);
                fread(buffer, 1, buffSize - overlap, fp);
            } else {
                u32 dataEnd = offset + buffSize;
                u32 dataEndPrev = offsetPrev + buffSize;
                u32 overlap = (dataEndPrev > offset) ? dataEndPrev - offset : 0;
                memmove(buffer, bufferEnd - overlap, overlap);
                if(dataEnd > fileSize) {
                    memset(buffer + overlap, 0x00, buffSize - overlap);
                }
                fseek(fp, offset + overlap, SEEK_SET);
                fread(buffer + overlap, 1, buffSize - overlap, fp);
            }
            offsetPrev = offset;
            if(onUpdate(buffer)) {
                result = true;
            }
        } else if(offset > fileSize) {
            offset = fileSize;
        } else if(onLoop(offset, forceRefresh)) {
            result = true;
        }
        
        if(result) break;
    }
    
    if(fp != NULL) fclose(fp);
    if(buffer != NULL) free(buffer);
    
    return result;
}

bool fsPathDelete(const std::string path) {
    if(fsIsDirectory(path)) {
        std::vector<FileInfo> contents = fsGetDirectoryContents(path);
        for (std::vector<FileInfo>::iterator it = contents.begin(); it != contents.end(); it++)
            if (!fsPathDelete((*it).path)) return false;
        return (rmdir(path.c_str()) == 0);
    } else return (remove(path.c_str()) == 0);
}

bool fsPathCopy(const std::string path, const std::string dest, bool overwrite, bool showProgress) {
    if(fsExists(dest)) {
       if(!overwrite) {
            errno = EEXIST;
            return false;
        } else if(path.compare(dest) == 0) {
            errno = EACCES;
            return false;
        } else if(fsIsDirectory(path) != fsIsDirectory(dest)) {
            if (!fsPathDelete(dest)) return false;
        }
    }
    if(showProgress && !fsShowProgress("Copying", path, 0, 1)) {
        errno = ECANCELED;
        return false;
    }
    if(fsIsDirectory(path)) {
        if(dest.find(path + "/") != std::string::npos) {
            errno = ENOTSUP;
            return false;
        }
        if(overwrite && fsIsDirectory(dest));
        else if(mkdir(dest.c_str(), 0777) != 0) return false;
        if(showProgress && !fsShowProgress("Copying", path, 1, 2)) {
            errno = ECANCELED;
            return false;
        }
        std::vector<FileInfo> contents = fsGetDirectoryContents(path);
        for (std::vector<FileInfo>::iterator it = contents.begin(); it != contents.end(); it++)
            if (!fsPathCopy((*it).path, dest + "/" + (*it).name, overwrite, showProgress)) return false;
        return true;
    } else {
        bool ret = true;
        u64 total = fsGetFileSize(path);
        size_t l_bufsiz = (total < CTRX_BUFSIZ) ? total : CTRX_BUFSIZ;
        u8* buffer = (u8*) malloc( l_bufsiz );
        FILE* fp = fopen(path.c_str(), "rb");
        FILE* fd = fopen(dest.c_str(), "wb");
        if((fp != NULL) && (fd != NULL) && (buffer != NULL)) {
            u64 pos = 0;
            size_t size;
            while ((size = fread(buffer, 1, l_bufsiz, fp)) > 0) {
                pos += fwrite(buffer, 1, size, fd);
                if(showProgress && !fsShowProgress("Copying", path, pos, total)) {
                    errno = ECANCELED;
                    ret = false;
                    break;
                }
            }
            ret = ret && (pos == total);
        } else ret = false;
        if(buffer != NULL) free(buffer);
        if(fp != NULL) fclose(fp);
        if(fd != NULL) fclose(fd);
        return ret;
    }
}

bool fsPathRename(const std::string path, const std::string dest, bool overwrite) {
    if(dest.find(path + "/") != std::string::npos) {
        errno = ENOTSUP;
        return false;
    }
    if(fsExists(dest)) {
        if(!overwrite) {
            errno = EEXIST;
            return false;
        } else if(path.compare(dest) == 0) {
            errno = EACCES;
            return false;
        } else if(fsIsDirectory(path) && fsIsDirectory(dest)) {
            std::vector<FileInfo> contents = fsGetDirectoryContents(path);
            for (std::vector<FileInfo>::iterator it = contents.begin(); it != contents.end(); it++)
                if (!fsPathRename((*it).path, dest + "/" + (*it).name, overwrite)) return false;
            return (rmdir(path.c_str()) == 0);
        } else if (!fsPathDelete(dest)) return false;
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

bool fsCreateDummyFile(const std::string path, u64 size, u16 content, bool overwrite, bool showProgress) {
    if(!overwrite && fsExists(path)) {
        errno = EEXIST;
        return false;
    }
    if(size < CTRX_BUFSIZ) showProgress = false;
    if(showProgress) fsShowProgress("Generating", path, 0, 1);
    bool ret = false;
    size_t l_bufsiz = (size < CTRX_BUFSIZ) ? size : CTRX_BUFSIZ;
    u8* buffer = (u8*) malloc( l_bufsiz );
    FILE* fp = fopen(path.c_str(), "wb");
    if((fp != NULL) && (buffer != NULL)) {
        if(content & 0xFF00) {
            u8 byte = content & 0xFF;
            u8 inc = (content >> 8) & 0xFF;
            for(u64 count = 0; count < l_bufsiz; count++, byte += inc)
                buffer[count] = byte;
        } else memset(buffer, content, l_bufsiz);
        u64 pos = 0;
        for(u64 count = 0; count < size; count += l_bufsiz) {
            if(size - count < l_bufsiz) l_bufsiz = size - count;
            pos += fwrite(buffer, 1, l_bufsiz, fp);
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

std::vector<FileInfo> fsGetDirectoryContents(const std::string directory) {
    std::vector<FileInfo> result;
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
        result.push_back({dirWithSlash + std::string(ent->d_name), std::string(ent->d_name)});
    }

    closedir(dir);
    return result;
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
