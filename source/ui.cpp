#include "ui.hpp"
#include "fs.hpp"

#include <3ds.h>

#include <citrus/core.hpp>
#include <citrus/gpu.hpp>
#include <citrus/gput.hpp>
#include <citrus/hid.hpp>

#include <sys/errno.h>
#include <string.h>

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <stack>

using namespace ctr;

// #define CTRX_EXTRA_SAFE // additional safety checks, not needed by the responsible programmer

struct uiAlphabetize {
    inline bool operator()(SelectableElement a, SelectableElement b) {
        return strcasecmp(a.name.c_str(), b.name.c_str()) < 0;
    }
};

u32 selectorTexture;
u32 selectorVbo;

void uiInit() {
    gpu::createTexture(&selectorTexture);
    gpu::setTextureInfo(selectorTexture, 64, 64, gpu::PIXEL_RGBA8, gpu::textureMinFilter(gpu::FILTER_NEAREST) | gpu::textureMagFilter(gpu::FILTER_NEAREST));

    void* textureData;
    gpu::getTextureData(selectorTexture, &textureData);
    memset(textureData, 0xFF, 64 * 64 * 4);

    gpu::createVbo(&selectorVbo);
    gpu::setVboAttributes(selectorVbo, gpu::vboAttribute(0, 3, gpu::ATTR_FLOAT) | gpu::vboAttribute(1, 2, gpu::ATTR_FLOAT) | gpu::vboAttribute(2, 4, gpu::ATTR_FLOAT), 3);

    const float vboData[] = {
            0.0f, 0.0f, -0.1f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f,
            320.0f, 0.0f, -0.1f, 1.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f,
            320.0f, 12.0f, -0.1f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
            320.0f, 12.0f, -0.1f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
            0.0f, 12.0f, -0.1f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
            0.0f, 0.0f, -0.1f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f,
    };

    gpu::setVboData(selectorVbo, vboData, 6 * 9, gpu::PRIM_TRIANGLES);
}

void uiCleanup() {
    if(selectorTexture != 0) {
        gpu::freeTexture(selectorTexture);
        selectorTexture = 0;
    }

    if(selectorVbo != 0) {
        gpu::freeVbo(selectorVbo);
        selectorVbo = 0;
    }
}

void uiDrawRectangle(int x, int y, u32 width, u32 height, u8 red, u8 green, u8 blue, u8 alpha) {
    gput::drawString(std::string(1, 0xDB), x, y, width, height, red, green, blue, alpha);
}

void uiDrawPositionBar(u32 pos, u32 nShown, u32 total, bool use_bottom) {
    const u32 barMinHeight = 32;
    const u32 barWidth = 2;
    const u8 gr = 0x4F;
    
    if ((pos >= total) || (nShown >= total)) return;
    
    u32 screenWidth;
    u32 screenHeight;
    gpu::getViewportWidth(&screenWidth);
    gpu::getViewportHeight(&screenHeight);
    
    if(!use_bottom) {
        u32 barHeight = (nShown * screenHeight) / total;
        if (barHeight < barMinHeight) barHeight = barMinHeight;
        u32 barPos = (screenHeight - barHeight) - (((u64) pos * (screenHeight - barHeight)) / (total - nShown));
        
        uiDrawRectangle(screenWidth - barWidth, barPos, barWidth, barHeight, gr, gr, gr);
    } else {
        u32 barHeight = (nShown * screenWidth) / total;
        if (barHeight < barMinHeight) barHeight = barMinHeight;
        u32 barPos = ((u64) pos * (screenWidth - barHeight)) / (total - nShown);
        
        uiDrawRectangle(barPos, 0, barHeight, barWidth, gr, gr, gr);
    }
}

std::string uiTruncateString(const std::string str, int nsize, int pos) {
    int osize = str.size();
    if (pos < 0) pos = nsize + 1 + pos - 3;
    #if defined CTRX_EXTRA_SAFE
    if (pos + 3 > nsize) pos = nsize - 3; // stupidity check #1
    if (pos < 0) pos = 0; // stupidity check #2
    #endif
    if (nsize >= osize) return str;
    std::string truncstring = str.substr(0, pos) + "..." + str.substr(osize - (nsize - pos - 3), nsize - pos - 3);
    return truncstring;
}

std::string uiFormatBytes(u64 bytes) {
    const char* units[] = {" byte", "kB", "MB", "GB"};
    std::stringstream byteStr;
    
    if(bytes < 1024) byteStr << bytes << units[0];
    else {
        int scale = 1;
        u64 bytes100 = (bytes * 100) >> 10;
        for(; (bytes100 >= 1024*100) && (scale < 3); scale++, bytes100 >>= 10);
        byteStr << (bytes100 / 100) << "." << ((bytes100 % 100) / 10) << (bytes100 % 10) << units[scale];
    }
    
    return byteStr.str();
}

bool uiSelectMultiple(const std::string startId, std::vector<SelectableElement> elements, std::function<bool(std::vector<SelectableElement> &currElements, bool &elementsDirty, bool &resetCursorIfDirty)> onLoop, std::function<void(SelectableElement* select)> onUpdateCursor, std::function<void(std::set<SelectableElement*>* marked)> onUpdateMarked, std::function<bool(SelectableElement* selected)> onSelect, bool useTopScreen, bool alphabetize) {
    if(elements.empty()) return false;
    
    int cursor = 0;
    int scroll = 0;
    
    if(!startId.empty()) {
        for(cursor = elements.size() - 1; cursor > 0; cursor--)
            if(startId.compare(elements.at((u32) cursor).id) == 0) break;
        scroll = (cursor < 20) ? 0 : cursor - 19;
    }

    u32 selectionScroll = 0;
    u64 selectionScrollEndTime = 0;

    u64 lastScrollTime = 0;
    
    bool lastMarkedStatus = false;

    bool elementsDirty = false;
    bool resetCursorIfDirty = true;
    
    if(alphabetize) {
        std::sort(elements.begin(), elements.end(), uiAlphabetize());
    }
    
    SelectableElement* selected = &elements.at((u32) cursor);
    std::set<SelectableElement*> markedElements;
    
    if(onUpdateCursor != NULL) onUpdateCursor(selected);
    if(onUpdateMarked != NULL) onUpdateMarked(&markedElements);

    while(core::running()) {
        hid::poll();
        
        if(hid::pressed(hid::BUTTON_A)) {
            if(onSelect == NULL || onSelect(selected)) {
                return true;
            }
        }
        
        if(hid::pressed(hid::BUTTON_L)) {
            std::pair <std::set<SelectableElement*>::iterator,bool> inserted = markedElements.insert(selected);
            if(!inserted.second) markedElements.erase(inserted.first);
            lastMarkedStatus = inserted.second;
            selectionScroll = 0;
            selectionScrollEndTime = core::time() - 3000;
            if(onUpdateMarked != NULL) onUpdateMarked(&markedElements);
        }

        if(hid::held(hid::BUTTON_DOWN) || hid::held(hid::BUTTON_UP) || hid::held(hid::BUTTON_LEFT) || hid::held(hid::BUTTON_RIGHT)) {
            int lastCursor = cursor;
            if(lastScrollTime == 0 || core::time() - lastScrollTime >= 180) {
                if(hid::held(hid::BUTTON_DOWN) && cursor < (int) elements.size() - 1) {
                    cursor++;
                    if(cursor >= scroll + 20) {
                        scroll++;
                    }
                }

                if(hid::held(hid::BUTTON_UP) && cursor > 0) {
                    cursor--;
                    if(cursor < scroll) {
                        scroll--;
                    }
                }

                if(!hid::held(hid::BUTTON_L)) {
                    if(hid::held(hid::BUTTON_RIGHT) && cursor < (int) elements.size() - 1) {
                        cursor += 20;
                        if(cursor >= (int) elements.size()) {
                            cursor = elements.size() - 1;
                            if(cursor < 0) {
                                cursor = 0;
                            }
                        }

                        scroll += 20;
                        if(scroll >= (int) elements.size() - 19) {
                            scroll = elements.size() - 20;
                            if(scroll < 0) {
                                scroll = 0;
                            }
                        }
                    }

                    if(hid::held(hid::BUTTON_LEFT) && cursor > 0) {
                        cursor -= 20;
                        if(cursor < 0) {
                            cursor = 0;
                        }

                        scroll -= 20;
                        if(scroll < 0) {
                            scroll = 0;
                        }
                    }
                }
                
                if(onUpdateCursor != NULL) onUpdateCursor(selected = &elements.at((u32) cursor));
                
                if(hid::held(hid::BUTTON_L)) {
                    if(hid::held(hid::BUTTON_LEFT)) {
                        markedElements.clear();
                        lastMarkedStatus = false;
                    } else if(hid::held(hid::BUTTON_RIGHT)) {
                        std::set<SelectableElement*>::iterator hint = markedElements.begin();
                        for(std::vector<SelectableElement>::iterator it = elements.begin(); it != elements.end(); it++)
                            hint = markedElements.insert(hint, &(*it));
                        lastMarkedStatus = true;
                    } else if(cursor != lastCursor) {
                        std::pair <std::set<SelectableElement*>::iterator,bool> inserted = markedElements.insert(selected);
                        if(!lastMarkedStatus) markedElements.erase(inserted.first);
                    }                    
                    if(onUpdateMarked != NULL) onUpdateMarked(&markedElements);
                }

                selectionScroll = 0;
                selectionScrollEndTime = 0;

                lastScrollTime = core::time();
            }
        } else if(lastScrollTime > 0) {
            lastScrollTime = 0;
        }

        gpu::setViewport(gpu::SCREEN_BOTTOM, 0, 0, gpu::BOTTOM_WIDTH, gpu::BOTTOM_HEIGHT);
        gput::setOrtho(0, gpu::BOTTOM_WIDTH, 0, gpu::BOTTOM_HEIGHT, -1, 1);
        gpu::clear();

        uiDrawPositionBar(scroll, 20, elements.size());
        
        u32 screenWidth;
        u32 screenHeight;
        gpu::getViewportWidth(&screenWidth);
        gpu::getViewportHeight(&screenHeight);
        for(std::vector<SelectableElement>::iterator it = elements.begin() + scroll; it != elements.begin() + scroll + 20 && it != elements.end(); it++) {
            std::string name = (*it).name;
            if (markedElements.find(&(*it)) != markedElements.end()) name.insert(0, 1, 0x10);
            int index = it - elements.begin();
            u8 cl = 0xFF;
            int offset = 0;
            float itemHeight = gput::getStringHeight(name, 8) + 4;
            if(index == cursor) {
                cl = 0x00;
                uiDrawRectangle(0, (screenHeight - 1) - ((index - scroll + 1) * itemHeight), screenWidth, itemHeight);
                u32 width = (u32) gput::getStringWidth(name, 8);
                if(width > screenWidth) {
                    if(selectionScrollEndTime == 0) {
                        if(selectionScroll + screenWidth >= width) {
                            selectionScrollEndTime = core::time();
                        } else {
                            selectionScroll++;
                        }
                    } else if(core::time() - selectionScrollEndTime >= 4000) {
                        selectionScroll = 0;
                        selectionScrollEndTime = 0;
                    }
                }
                offset = -selectionScroll;
            }
            gput::drawString(name, offset, (screenHeight - 1) - ((index - scroll + 1) * itemHeight) + 2, 8, 8, cl, cl, cl);
        }

        gpu::flushCommands();
        gpu::flushBuffer();
        
        if(useTopScreen) {
            gpu::setViewport(gpu::SCREEN_TOP, 0, 0, gpu::TOP_WIDTH, gpu::TOP_HEIGHT);
            gput::setOrtho(0, gpu::TOP_WIDTH, 0, gpu::TOP_HEIGHT, -1, 1);
            gpu::clear();

            gpu::getViewportHeight(&screenHeight);
            if((*selected).details.size() != 0) {
                std::stringstream details;
                for(std::vector<std::string>::iterator it = (*selected).details.begin(); it != (*selected).details.end(); it++) {
                    details << *it << "\n";
                }
                gput::drawString(details.str(), 0, screenHeight - 1 - gput::getStringHeight(details.str(), 8), 8, 8);
            }
        }

        bool result = onLoop != NULL && onLoop(elements, elementsDirty, resetCursorIfDirty);
        if(elementsDirty) {
            if(resetCursorIfDirty) {
                cursor = 0;
                scroll = 0;
            } else if(cursor >= (int) elements.size()) {
                cursor = elements.size() - 1;
                if(cursor < 0) {
                    cursor = 0;
                }

                scroll = elements.size() - 20;
                if(scroll < 0) {
                    scroll = 0;
                }
            }

            selectionScroll = 0;
            selectionScrollEndTime = 0;
            if(alphabetize) {
                std::sort(elements.begin(), elements.end(), uiAlphabetize());
            }
            elementsDirty = false;
            resetCursorIfDirty = true;
            
            if (onUpdateCursor != NULL) onUpdateCursor((selected = &elements.at((u32) cursor)));
            markedElements.clear();
        }
        
        if(useTopScreen) {
            gpu::flushCommands();
            gpu::flushBuffer();
        }

        gpu::swapBuffers(true);
        
        if(result) {
            break;
        }
    }

    return false;
}

void uiGetDirContentsSorted(std::vector<SelectableElement> &elements, const std::string directory, bool isRoot) {
    elements.clear();
    if (!isRoot) elements.push_back({"..", ".."});
    
    std::vector<FileInfoEx> contents = fsGetDirectoryContentsEx(directory);
    for(std::vector<FileInfoEx>::iterator it = contents.begin(); it != contents.end(); it++) {
        const std::string name = (*it).name;
        const std::string path = (*it).path;
        std::vector<std::string> info = {};
        if((*it).isDirectory) {
            info.push_back("folder");
        } else {
            const std::string ext = uiTruncateString(fsGetExtension(name), 8, 3);
            info.push_back((ext.size() > 0) ? (ext + " file") : "file");
            info.push_back(uiFormatBytes((u64) fsGetFileSize(path)));
        }
        elements.push_back({path, name, info});
    }
}

bool uiFileBrowser(const std::string rootDirectory, const std::string startPath, std::function<bool(bool &updateList, bool &resetCursorOnUpdate)> onLoop, std::function<void(SelectableElement* entry)> onUpdateEntry, std::function<void(std::string* currDir)> onUpdateDir, std::function<void(std::set<SelectableElement*>* marked)> onUpdateMarked, std::function<bool(std::string selectedPath, bool &updateList)> onSelect, bool useTopScreen) {
    std::stack<std::string> directoryStack;
    std::string currDirectory = rootDirectory;

    if(startPath.compare(0, currDirectory.size(), currDirectory) == 0) {
        size_t dirSize = startPath.find_first_of('/', currDirectory.size() + 1);
        while(dirSize != std::string::npos) {
            directoryStack.push(currDirectory);
            currDirectory = startPath.substr(0, dirSize);
            dirSize = startPath.find_first_of('/', dirSize + 1);
        }
        if(!fsIsDirectory(currDirectory)) {
            while(!directoryStack.empty()) directoryStack.pop();
            currDirectory = rootDirectory;
        }
    }
    
    std::vector<SelectableElement> elements;
    uiGetDirContentsSorted(elements, currDirectory, directoryStack.empty());
    if (onUpdateDir) onUpdateDir(&currDirectory);
    
    bool updateContents = false;
    bool resetCursor = true;
    SelectableElement* selected;
    bool result = uiSelectMultiple(startPath, elements,
        [&](std::vector<SelectableElement> &currElements, bool &elementsDirty, bool &resetCursorIfDirty) {
            if(onLoop != NULL && onLoop(updateContents, resetCursor)) {
                return true;
            }
            
            if(hid::pressed(hid::BUTTON_B) && !directoryStack.empty()) {
                currDirectory = directoryStack.top();
                directoryStack.pop();
                updateContents = true;
            }

            if(updateContents) {
                if (onUpdateDir) onUpdateDir(&currDirectory);
                uiGetDirContentsSorted(currElements, currDirectory, directoryStack.empty());
                elementsDirty = true;
                resetCursorIfDirty = resetCursor;
                updateContents = false;
                resetCursor = true;
            }

            return false;
        },
        [&](SelectableElement* entry) {
            selected = entry;
            onUpdateEntry(entry);
        },
        [&](std::set<SelectableElement*>* marked) {
            if(!(*marked).empty()) {
                SelectableElement* firstMarked = *((*marked).begin());
                if((*firstMarked).name.compare("..") == 0) {
                    (*marked).erase(firstMarked);                
                }
            }
            onUpdateMarked(marked);
        }, 
        [&](SelectableElement* selected) {
            if((*selected).name.compare("..") == 0) {
                if(!directoryStack.empty()) {
                    currDirectory = directoryStack.top();
                    directoryStack.pop();
                    updateContents = true;
                }
                return false;
            } else if(fsIsDirectory((*selected).id)) {
                directoryStack.push(currDirectory);
                currDirectory = (*selected).id;
                updateContents = true;
                return false;
            }
            
            bool updateList = false;
            bool ret = (onSelect != NULL) && onSelect((*selected).id, updateList);
            if(updateList) {
                updateContents = true;
                resetCursor = false;
            }

            return ret;
        },
        useTopScreen, false);

    return result;
}

bool uiHexViewer(const std::string path, u32 start, std::function<bool(u32 &offset, u32 &markedOffset, u32 &markedLength, bool selectMode)> onLoop, std::function<bool(u32 offset)> onUpdate, std::function<bool(u32 selectedOffset, u32 selectedLength, hid::Button selectButton, bool &updateData)> onSelect) {
    const u32 cpad = 2;
    
    const u32 rows = gpu::BOTTOM_HEIGHT / (8 + (2*cpad));
    const u32 cols = 8;
    const u32 nShown = rows * cols;
    
    const u32 fastMult = 16;
    
    bool result;
    
    u32 fileSize = fsGetFileSize(path);
    u64 lastScrollTime = 0;
    
    u32 currOffset = start;
    u32 maxOffset = (fileSize <= nShown) ? 0 :
        ((fileSize % cols) ? fileSize + (cols - (fileSize % cols)) - nShown : fileSize - nShown);
    
    bool selectMode = false;
    hid::Button selectButton = hid::BUTTON_NONE;
    u32 selectOffset = 0;
    u32 markedOffset = 0;
    u32 markedLength = 0;
    u32 markedOffsetPrev = 0;
    u32 markedLengthPrev = 0;
    
    auto redrawHexView = [&](u8* data) {
        static u8* localData = NULL;
        
        const u8 gr = 0x9F;
        const u8 mr = 0x4F;
        
        if(data != NULL) localData = data;
        
        gpu::setViewport(gpu::SCREEN_BOTTOM, 0, 0, gpu::BOTTOM_WIDTH, gpu::BOTTOM_HEIGHT);
        gput::setOrtho(0, gpu::BOTTOM_WIDTH, 0, gpu::BOTTOM_HEIGHT, -1, 1);
        gpu::clear();
        
        uiDrawPositionBar(currOffset, nShown, fileSize);
        
        for(u32 pos = 0; pos < nShown; ) {
            u32 vDrawPos = gpu::BOTTOM_HEIGHT - (((u32) (pos / cols) + 1) * (8 + (2*cpad))) + cpad;
            
            std::stringstream ssIndex;
            ssIndex << std::hex << std::uppercase << std::setfill('0') << std::setw(8) << (currOffset + pos);
            gput::drawString(ssIndex.str(), 0, vDrawPos, 8, 8, gr, gr, gr);
            
            if(currOffset + pos < fileSize) {
                std::stringstream ssAscii;
                std::stringstream ssHex;
                ssHex << std::hex << std::uppercase << std::setfill('0');
                do {
                    if(currOffset + pos < fileSize) {
                        u32 hDrawPos = 64 + (32 - (cols * cpad)) + ((pos % cols) * 2 * (8 + cpad)) + cpad;
                        u8 symbol = localData[pos];
                        if ((currOffset + pos >= markedOffset) && (currOffset + pos < markedOffset + markedLength)) {
                            uiDrawRectangle(hDrawPos - 1, vDrawPos - 1, 2 + (2*8), 2 + 1 + 8, mr, mr, mr);
                            uiDrawRectangle(gpu::BOTTOM_WIDTH - ((cols-(pos%cols))*8) - 1, vDrawPos - 1,
                                2 + 8, 2 + 1 + 8, mr, mr, mr);
                        }
                        ssHex << std::setw(2) << (u32) symbol;
                        gput::drawString(ssHex.str(), hDrawPos, vDrawPos, 8, 8);
                        ssHex.str("");
                        ssAscii << (((symbol != 0x00) && (symbol != 0x0A) && (symbol != 0x0D)) ? (char) symbol : (char) ' ');
                    }
                    pos++;
                } while(pos % cols);
                gput::drawString(ssAscii.str(), gpu::BOTTOM_WIDTH - (cols*8), vDrawPos, 8, 8, gr, gr, gr);
            } else pos += cols;
        }
        
        gpu::flushCommands();
        for(int b = 0; b < 2; b++) { // fill both buffers
            gpu::flushBuffer();
            gpu::swapBuffers(true);
        }
        
        return false;
    };
    
    result = fsDataProvider(path, start, nShown,
        [&](u32 &offset, bool &forceRefresh) { // onLoop
            hid::poll();
            
            if(!selectMode) { // standard hexviewer mode
                if(hid::pressed(hid::BUTTON_A)) {
                    selectMode = true;
                    selectButton = hid::BUTTON_NONE;
                    markedOffset = offset;
                    markedLength = 1;
                } else if(hid::pressed(hid::BUTTON_B)) {
                    return true;
                }
                if(hid::held(hid::BUTTON_DOWN) || hid::held(hid::BUTTON_RIGHT)) {
                    if(lastScrollTime == 0 || core::time() - lastScrollTime >= 120) {
                        offset += (hid::held(hid::BUTTON_L)) ?
                            (hid::held(hid::BUTTON_RIGHT) ? fastMult * fastMult * nShown : fastMult * nShown) :
                            (hid::held(hid::BUTTON_RIGHT) ? nShown : cols);
                        if(offset > maxOffset) offset = maxOffset;
                        currOffset = offset;
                        lastScrollTime = core::time();
                    }
                } else if(hid::held(hid::BUTTON_UP) || hid::held(hid::BUTTON_LEFT)) {
                    if(lastScrollTime == 0 || core::time() - lastScrollTime >= 120) {
                        u32 sub = (hid::held(hid::BUTTON_L)) ?
                            (hid::held(hid::BUTTON_LEFT) ? fastMult * fastMult * nShown : fastMult * nShown) :
                            (hid::held(hid::BUTTON_LEFT) ? nShown : cols);
                        offset = (offset > sub) ? offset - sub : 0;
                        currOffset = offset;
                        lastScrollTime = core::time();
                    }
                } else if(lastScrollTime > 0) {
                    lastScrollTime = 0;
                }
            } else { // hexviewer select mode
                if(hid::pressed(hid::BUTTON_A)) {
                    selectOffset = markedOffset;
                    markedLength = 1;
                    selectButton = hid::BUTTON_A;
                } else if(hid::pressed(hid::BUTTON_X)) {
                    selectOffset = markedOffset;
                    markedLength = 1;
                    selectButton = hid::BUTTON_X;
                } else if(hid::pressed(hid::BUTTON_Y)) {
                    selectOffset = markedOffset;
                    markedLength = 1;
                    selectButton = hid::BUTTON_Y;
                } else if(hid::pressed(hid::BUTTON_R)) {
                    selectOffset = markedOffset;
                    markedLength = 1;
                    selectButton = hid::BUTTON_R;
                } else if(hid::released(selectButton)) {
                    if(onSelect && onSelect(markedOffset, markedLength, selectButton, forceRefresh))
                        return true;
                    selectButton = hid::BUTTON_NONE;
                    markedLength = 1;
                } else if(hid::pressed(hid::BUTTON_B)) {
                    selectMode = false;
                    markedOffset = markedLength = 0;
                    selectButton = hid::BUTTON_NONE;
                }
                if(hid::held(hid::BUTTON_DOWN) || hid::held(hid::BUTTON_RIGHT) || hid::held(hid::BUTTON_UP) || hid::held(hid::BUTTON_LEFT)) {
                    if(lastScrollTime == 0 || core::time() - lastScrollTime >= 120) {
                        if(!hid::held(selectButton)) {
                            markedLength = 1;
                            if(hid::held(hid::BUTTON_DOWN) && (markedOffset + cols < fileSize))
                                markedOffset += cols;
                            else if(hid::held(hid::BUTTON_RIGHT) && (markedOffset + 1 < fileSize))
                                markedOffset++;
                            else if(hid::held(hid::BUTTON_UP) && (markedOffset >= cols))
                                markedOffset -= cols;
                            else if(hid::held(hid::BUTTON_LEFT) && markedOffset)
                                markedOffset--;
                            selectOffset = markedOffset;
                        } else{
                            u32 selectionEnd = (markedOffset < selectOffset) ?
                                markedOffset : markedOffset + markedLength - 1;
                            if(hid::held(hid::BUTTON_DOWN))
                                selectionEnd += cols;
                            else if(hid::held(hid::BUTTON_RIGHT))
                                selectionEnd++;
                            else if(hid::held(hid::BUTTON_UP))
                                selectionEnd = (selectionEnd >= cols) ? selectionEnd - cols : 0;
                            else if(hid::held(hid::BUTTON_LEFT))
                                selectionEnd = (selectionEnd) ? selectionEnd - 1 : 0;
                            if(selectionEnd < selectOffset) {
                                if(selectOffset + 1 - (selectionEnd - (selectionEnd%cols)) > nShown) {
                                    markedOffset = markedOffsetPrev;
                                    markedLength = markedLengthPrev;
                                } else {
                                    markedOffset = selectionEnd;
                                    markedLength = (selectOffset - selectionEnd) + 1;
                                }
                            } else {
                                if((selectionEnd + cols - (selectionEnd%cols)) - selectOffset > nShown) {
                                    markedOffset = markedOffsetPrev;
                                    markedLength = markedLengthPrev;
                                } else {
                                    markedOffset = selectOffset;
                                    markedLength = (selectionEnd - selectOffset) + 1;
                                }
                            }
                        }
                        lastScrollTime = core::time();
                    }
                } else if(lastScrollTime > 0) {
                    lastScrollTime = 0;
                }
                if(markedOffset >= fileSize) markedOffset = fileSize - 1;
            }
            
            if(forceRefresh) {
                fileSize = fsGetFileSize(path);
                maxOffset = (fileSize <= nShown) ? 0 :
                    ((fileSize % cols) ? fileSize + (cols - (fileSize % cols)) - nShown : fileSize - nShown);
                if(offset > maxOffset) offset = maxOffset;
            }
            
            if(onLoop && onLoop(offset, markedOffset, markedLength, selectMode))
                return true;
            
            if((markedOffset != markedOffsetPrev) || (markedLength != markedLengthPrev)) {
                if(markedOffset + markedLength > fileSize) {
                    if(markedOffset < fileSize)
                        markedLength = fileSize - markedOffset;
                    else markedOffset = markedLength = 0;
                }
                if(markedLength) {
                    if(markedOffset < offset) {
                        offset = markedOffset;
                    } else if(markedOffset + markedLength > offset + nShown) {
                        offset = (markedOffset + markedLength) - nShown + (cols - 1);
                    }
                }
                markedOffsetPrev = markedOffset;
                markedLengthPrev = markedLength;
                if(currOffset == offset)
                    redrawHexView(NULL);
            }
            
            if(currOffset != offset) {
                if(offset > maxOffset) offset = maxOffset;
                else offset -= offset % cols;
                currOffset = offset;
            }
            
            gpu::swapBuffers(true);
            
            return false;
        },
        [&](u8* data) { // onUpdate
            if(redrawHexView(data) || (onUpdate && onUpdate(currOffset)))
                return true;
            return false;
        });
    
    return result;
}

bool uiTextViewer(const std::string path, std::function<bool(void)> onLoop, std::function<bool(u32 offset, u32 plus)> onUpdate) {
    const u32 nLinesDisp = gpu::BOTTOM_HEIGHT / 8;
    const u32 nCharsDisp = gpu::BOTTOM_WIDTH / 8;
    const u32 lineLenMax = 1 * 1024; // careful, this is a sensitive value
    const u32 bufsizeMax = 16 * nLinesDisp * lineLenMax; // see above
    const u32 safeSize = 2 * lineLenMax * nLinesDisp;
    
    u64 lastScrollTime = 0;
    
    u32 fileSize = fsDataSearch(path, std::vector<u8>(1, '\0'), 0, true);
    if(fileSize == (u32) -1) fileSize = fsGetFileSize(path);
    u32 bufsize = (fileSize < bufsizeMax) ? fileSize : bufsizeMax;
    
    std::vector<u32> bufferMap;
    char* localData = NULL;
    
    u32 offsetBuff = (u32) -1;
    u32 offsetDisp = 0;
    u32 offsetDispPrev = (u32) -1;
    u32 charIndex = 0;
    u32 charIndexPrev = (u32) -1;
    u32 lineIndex = 0;
    u32 lineIndexMax = 0;
    u32 lineLenCurr = lineLenMax;
    
    // MAP LINE OFFSETS IN BUFFER
    auto buildBufferMap = [&](void) {
        bufferMap.clear();
        // build line offset list
        u32 end = (offsetBuff + bufsize > fileSize) ? fileSize - offsetBuff : bufsize;
        for (u32 start = 0; start < end;) {
            u32 lf;
            for (lf = start; (lf < end) && localData[lf] != '\n'; lf++);
            while (lf - start > lineLenCurr) {
                u32 space = (u32) -1;
                for (u32 p = start + 1; (p < end) && p < start + lineLenCurr; p++)
                    if(localData[p] == ' ') space = p;
                if(space == (u32) -1) {
                    // no appropriate space character found -> forced word wrap
                    u32 forced = (start + lineLenCurr < end) ? start + lineLenCurr : end;
                    start = forced;
                } else start = space;
                bufferMap.push_back(start);
            }
            if(lf < end) {
                start = lf + 1;
                bufferMap.push_back(start);
            } else break;
        }
        bufferMap.push_back(end);
        lineIndexMax = (bufferMap.size() > nLinesDisp) ? bufferMap.size() - nLinesDisp : 0;
        
        // normalize lineIndex
        for (lineIndex = lineIndexMax; (lineIndex > 0) &&
            (offsetDisp - offsetBuff < bufferMap.at(lineIndex - 1)); lineIndex--);
    };
    
    bool result = fsDataProvider(path, 0, bufsize,
        [&](u32 &offset, bool &forceRefresh) { // onLoop
            if(offsetBuff != offset) {
                offsetBuff = offset;
                buildBufferMap();
            }
            
            hid::poll();   
            
            // ONLOOP FUNCTION
            if(onLoop && onLoop()) return true;
            
            // PROCESS INPUT
            if(hid::pressed(hid::BUTTON_B)) {
                return true;
            } else if(hid::pressed(hid::BUTTON_X)) {
                lineLenCurr = (lineLenCurr == lineLenMax) ? nCharsDisp : lineLenMax;
                charIndex = 0;
                buildBufferMap();
            }
            if(hid::held(hid::BUTTON_LEFT) || hid::held(hid::BUTTON_RIGHT) ||
               hid::held(hid::BUTTON_UP) || hid::held(hid::BUTTON_DOWN) ||
               hid::held(hid::BUTTON_L) || hid::held(hid::BUTTON_R)) {
                if(lastScrollTime == 0 || core::time() - lastScrollTime >= 120) {
                    if(hid::held(hid::BUTTON_DOWN) && (lineIndex < lineIndexMax)) {
                        lineIndex++;
                    } else if(hid::held(hid::BUTTON_UP) && (lineIndex > 0)) {
                        lineIndex--;
                    } else if(hid::held(hid::BUTTON_R)) {
                        lineIndex = (lineIndex + nLinesDisp < lineIndexMax) ?
                            lineIndex + nLinesDisp : lineIndexMax;
                    } else if(hid::held(hid::BUTTON_L)) {
                        lineIndex = (lineIndex > nLinesDisp) ?
                            lineIndex - nLinesDisp : 0;
                    } else if(hid::held(hid::BUTTON_RIGHT) && (charIndex + nCharsDisp < lineLenCurr)) {
                        charIndex++;
                    } else if(hid::held(hid::BUTTON_LEFT) && (charIndex)) {
                        charIndex--;
                    }
                    lastScrollTime = core::time();
                }
            } else if(lastScrollTime > 0) {
                lastScrollTime = 0;
            }
            
            // BUILD STRING TO DISPLAY ON SCREEN
            std::string dispString;
            u32 dispStart = (lineIndex) ? bufferMap.at(lineIndex - 1) : 0;
            u32 lineStart = dispStart;
            offsetDisp = offsetBuff + dispStart;
            for (u32 l = 0; l < nLinesDisp; l++) {
                if(lineIndex + l < bufferMap.size()) {
                    u32 lineEnd = bufferMap.at(lineIndex + l);
                    std::string lineString(localData + lineStart, lineEnd - lineStart);
                    for (u32 badchar = lineString.find_first_of("\n\r");
                         badchar != std::string::npos;
                         badchar = lineString.find_first_of("\n\r", badchar))
                        lineString.erase(badchar, 1);
                    if(lineString.size() > charIndex)
                        dispString += lineString.substr(charIndex, nCharsDisp);
                    lineStart = lineEnd;
                }
                if (l < nLinesDisp - 1) dispString += "\n";
            }
            u32 dispEnd = lineStart;
            
            // BUFFER CHECK (RELOAD IF RUNNING OUT)
            if(fileSize > bufsize) { // no need if the whole file fits into the buffer
                if((dispStart < safeSize) || (bufsize - dispStart < safeSize))
                    offset = (offsetDisp > (bufsize / 2)) ? offsetDisp - (bufsize / 2) : 0;
            }
            
            // ONUPDATE FUNCTION
            if((offsetDisp != offsetDispPrev) || (charIndex != charIndexPrev)) {
                if((onUpdate != NULL) && onUpdate(offsetDisp, charIndex))
                    return true;
                offsetDispPrev = offsetDisp;
                charIndexPrev = charIndex;
            }
            
            // ON SCREEN DISPLAY
            gpu::setViewport(gpu::SCREEN_BOTTOM, 0, 0, gpu::BOTTOM_WIDTH, gpu::BOTTOM_HEIGHT);
            gput::setOrtho(0, gpu::BOTTOM_WIDTH, 0, gpu::BOTTOM_HEIGHT, -1, 1);
            gpu::clear();
            
            gput::drawString(dispString, 0, 0, 8, 8);
            uiDrawPositionBar(offsetDisp, dispEnd - dispStart, fileSize, false);
            uiDrawPositionBar(charIndex, nCharsDisp, lineLenCurr, true);
            
            gpu::flushCommands();
            gpu::flushBuffer();
            gpu::swapBuffers(true);
            
            return false;
        },
        [&](u8* data) { // onUpdate
            localData = (char*) data;
            return false;
        });
    
    return result;
}

void uiDisplayMessage(gpu::Screen screen, const std::string message) {
    gpu::setViewport(screen, 0, 0, screen == gpu::SCREEN_TOP ? gpu::TOP_WIDTH : gpu::BOTTOM_WIDTH, screen == gpu::SCREEN_TOP ? gpu::TOP_HEIGHT : gpu::BOTTOM_HEIGHT);
    gput::setOrtho(0, screen == gpu::SCREEN_TOP ? gpu::TOP_WIDTH : gpu::BOTTOM_WIDTH, 0, screen == gpu::SCREEN_TOP ? gpu::TOP_HEIGHT : gpu::BOTTOM_HEIGHT, -1, 1);
    
    u32 screenWidth;
    u32 screenHeight;
    gpu::getViewportWidth(&screenWidth);
    gpu::getViewportHeight(&screenHeight);

    gpu::clear();
    gput::drawString(message, (screenWidth - gput::getStringWidth(message, 8)) / 2, (screenHeight - gput::getStringHeight(message, 8)) / 2, 8, 8);
    gpu::flushCommands();
    gpu::flushBuffer();
    gpu::swapBuffers(true);

    gpu::setViewport(gpu::SCREEN_TOP, 0, 0, gpu::TOP_WIDTH, gpu::TOP_HEIGHT);
    gput::setOrtho(0, gpu::TOP_WIDTH, 0, gpu::TOP_HEIGHT, -1, 1);
}

bool uiPrompt(gpu::Screen screen, const std::string message, bool question) {
    std::stringstream stream;
    stream << message << "\n";
    if(question) {
        stream << "Press A to confirm, B to cancel." << "\n";
    } else {
        stream << "Press Start to continue." << "\n";
    }

    bool result = false;
    std::string str = stream.str();
    while(core::running()) {
        hid::poll();
        if(question) {
            if(hid::pressed(hid::BUTTON_A)) {
                result = true;
                break;
            }

            if(hid::pressed(hid::BUTTON_B)) {
                result = false;
                break;
            }
        } else {
            if(hid::pressed(hid::BUTTON_START)) {
                result = true;
                break;
            }
        }

        uiDisplayMessage(screen, str);
    }

    hid::poll();
    return result;
}

bool uiErrorPrompt(gpu::Screen screen, const std::string operationStr, const std::string detailStr, bool checkErrno, bool question) {
    std::stringstream stream;
    stream << operationStr << " failed!" << "\n";
    stream << "\"" << uiTruncateString(detailStr, 32, 0 ) << "\"" << "\n";
    if(checkErrno) stream << strerror(errno) << "\n";
    stream << "\n";
    if(question) {
        stream << "Press A to continue, B to cancel." << "\n";
    } else {
        stream << "Press Start to continue." << "\n";
    }

    bool result = false;
    std::string str = stream.str();
    while(core::running()) {
        hid::poll();
        if(question) {
            if(hid::pressed(hid::BUTTON_A)) {
                result = true;
                break;
            }

            if(hid::pressed(hid::BUTTON_B)) {
                result = false;
                break;
            }
        } else {
            if(hid::pressed(hid::BUTTON_START)) {
                result = true;
                break;
            }
        }

        uiDisplayMessage(screen, str);
    }

    hid::poll();
    return result;
}

std::string uiKeyboardInput(std::string preset, const std::string alphabet) {
    std::string resultStr = preset;

    while(core::running()) {
        char textbuffer[1024];
        SwkbdState swkbd;    
        swkbdInit(&swkbd, SWKBD_TYPE_WESTERN, 2, -1);
        swkbdSetValidation(&swkbd, SWKBD_NOTEMPTY_NOTBLANK, SWKBD_FILTER_BACKSLASH, 0);
        swkbdSetFeatures(&swkbd, SWKBD_DARKEN_TOP_SCREEN);
        swkbdSetInitialText(&swkbd, resultStr.c_str());
        if(swkbdInputText(&swkbd, textbuffer, sizeof(textbuffer)) == SWKBD_BUTTON_CONFIRM) {
            resultStr = std::string(textbuffer);
            if(resultStr.find_first_not_of(alphabet, 0) != std::string::npos) {
                uiPrompt(gpu::SCREEN_TOP, (std::string) "Input contains invalid characters." + "\n" + "Try again?" + "\n", true);
            } else break;
        } else {
            resultStr.clear();
            break;
        }
    }
    
    return resultStr;
}

std::string uiStringInput(gpu::Screen screen, std::string preset, const std::string alphabet, const std::string message, u32 resize, bool allow_keyboard) {
    const int dispSize = 30;
    const u64 tapDelay = 360;
    const u64 scrollDelay = 120;
    
    u64 inputXHoldTime = 0;
    u64 inputYHoldTime = 0;
    
    int cursor_s = 0;
    int cursor_a = -1;
    int scroll = 0;
    int fastScroll = (alphabet.size() > 16) ? 4 : 2;
    u64 lastScrollTime = 0;
    
    std::string resultStr = (preset.empty()) ? alphabet.substr(0,1) : preset;
    bool result = false;
    
    if(hid::held(hid::BUTTON_X)) inputXHoldTime = (u64) -1;
    if(hid::held(hid::BUTTON_Y)) inputYHoldTime = (u64) -1;
    
    while(core::running()) {
        std::stringstream stream;
        stream << message << "\n";
        stream << ((scroll > 0) ? "<" : "|");
        stream << resultStr.substr(scroll, dispSize);
        stream << ((resultStr.size() - scroll > dispSize) ? ">" : "|") << "\n";
        for(int i = scroll; i <= cursor_s; i++) stream << " ";
        stream << "^" << "\n" << "\n";
        stream << "L - [h] (" << (char) 0x18 << (char) 0x19 << ") fast scroll" << "\n";
        if (allow_keyboard) {
            stream << "R - use touchscreen keyboard" << "\n";
        }
        if(resize) {
            stream << "X - [t] remove char / [h] clear" << "\n";
            stream << "Y - [t] insert char / [h] reset" << "\n";
        } else {
            stream << "X - [h] clear" << "\n";
            stream << "Y - [h] reset" << "\n";
        }
        stream << "\n" << "Press A to confirm, B to cancel." << "\n";
    
        hid::poll();
        
        if(hid::pressed(hid::BUTTON_A)) {
            result = true;
            break;
        }

        if(hid::pressed(hid::BUTTON_B)) {
            result = false;
            break;
        }
        
        if(allow_keyboard && hid::pressed(hid::BUTTON_R)) {
            std::string keyboard = uiKeyboardInput(resultStr, alphabet);
            if (!keyboard.empty()) {
                resultStr = keyboard;
                while(cursor_s >= (int) resultStr.size()) cursor_s--;
                cursor_a = -1;
            }
        }
        
        if(hid::held(hid::BUTTON_X) && (inputXHoldTime != (u64) -1)) {
            if(inputXHoldTime == 0) inputXHoldTime = core::time();
            else if(core::time() - inputXHoldTime >= tapDelay) {
                if(resize) {
                    resultStr.assign(resize, alphabet.at(0));
                    cursor_s = 0;
                    cursor_a = 0;
                    scroll = 0;
                } else {
                    resultStr.assign(resultStr.size(), alphabet.at(0));
                    cursor_a = 0;
                }
                inputXHoldTime = (u64) -1;
            }
        }
        if(hid::released(hid::BUTTON_X) && (inputXHoldTime != 0)) {
            if((inputXHoldTime != (u64) -1) && (resultStr.size() > resize) && resize) {
                resultStr.erase(cursor_s - (cursor_s % resize), resize);
                while(cursor_s >= (int) resultStr.size()) cursor_s--;
                cursor_a = -1;
            }
            inputXHoldTime = 0;
        }
        
        if(hid::held(hid::BUTTON_Y) && (inputYHoldTime != (u64) -1)) {
            if(inputYHoldTime == 0) inputYHoldTime = core::time();
            else if(core::time() - inputYHoldTime >= tapDelay) {
                resultStr = (preset.empty()) ? alphabet.substr(0,1) : preset;
                cursor_s = 0;
                cursor_a = -1;
                scroll = 0;
                inputYHoldTime = (u64) -1;
            }
        }
        if(hid::released(hid::BUTTON_Y) && (inputYHoldTime != 0) && resize) {
            if(inputYHoldTime != (u64) -1) {
                resultStr.insert(cursor_s - (cursor_s % resize), resize, alphabet.at(0));
                cursor_a = 0;
            }
            inputYHoldTime = 0;
        } 
        
        if(hid::held(hid::BUTTON_DOWN) || hid::held(hid::BUTTON_UP)) {
            if(lastScrollTime == 0 || core::time() - lastScrollTime >= scrollDelay) {
                if(cursor_a < 0) {
                    cursor_a = alphabet.find(resultStr.substr(cursor_s, 1));
                    if (cursor_a < 0) cursor_a = 0;
                }
                
                if(hid::held(hid::BUTTON_UP)) {
                    cursor_a += (hid::held(hid::BUTTON_L)) ? fastScroll : 1;
                    while(cursor_a >= (int) alphabet.size()) cursor_a -= alphabet.size();
                }

                if(hid::held(hid::BUTTON_DOWN)) {
                    cursor_a -= (hid::held(hid::BUTTON_L)) ? fastScroll : 1;
                    while(cursor_a < 0) cursor_a += alphabet.size();
                }
                
                resultStr.replace(cursor_s, 1, alphabet.substr(cursor_a, 1));
                lastScrollTime = core::time();
            }
        } else if(hid::held(hid::BUTTON_LEFT) || hid::held(hid::BUTTON_RIGHT)) {
            if(lastScrollTime == 0 || core::time() - lastScrollTime >= scrollDelay) {
                if(hid::held(hid::BUTTON_LEFT) && (cursor_s > 0)) {
                    if((cursor_s == (int) resultStr.size() - 1) && (resultStr.at(cursor_s) == ' '))
                        resultStr.resize(cursor_s);
                    cursor_s--;
                    if(scroll > cursor_s) scroll = cursor_s;
                    cursor_a = -1;
                }
                
                if(hid::held(hid::BUTTON_RIGHT)) {
                    cursor_s++;
                    if(cursor_s == (int) resultStr.size()) {
                        if(resize) {
                            resultStr.append(resize, alphabet.at(0));
                            cursor_a = 0;
                        } else cursor_s--;
                    } else cursor_a = -1;
                    if(scroll + dispSize <= cursor_s) scroll = cursor_s - dispSize + 1;
                }
                
                lastScrollTime = core::time();
            }
        } else if (lastScrollTime > 0) {
            lastScrollTime = 0;
        }

        uiDisplayMessage(screen, stream.str());
    }
    
    if(result) {
        cursor_s = resultStr.find_last_not_of(" ");
        if(cursor_s < 0) resultStr.clear();
        else resultStr.erase(cursor_s + 1);
    } else resultStr.clear();
    
    hid::poll();
    
    return resultStr;
}

u32 uiNumberInput(gpu::Screen screen, u32 preset, const std::string message, bool hex) {
    std::string resultStr;
    u32 result;
    
    std::stringstream input;
    if(!hex) input << preset;
    else input << std::setfill('0') << std::uppercase << std::hex << std::setw(8) << preset;
    
    resultStr = uiStringInput(screen, input.str(), (hex) ? "0123456789ABCDEF" : "0123456789", message, !hex);
    if(resultStr.empty()) return (u32) -1;
    
    std::istringstream output(resultStr);
    if(!hex) output >> result;
    else output >> std::hex >> result;
    
    return result;
}

std::vector<u8> uiDataInput(gpu::Screen screen, std::vector<u8> preset, const std::string message, bool allowResize) {
    std::string resultStr;
    std::vector<u8> result;
    
    std::stringstream input;
    for(std::vector<u8>::iterator it = preset.begin(); it != preset.end(); it++)
        input << std::setfill('0') << std::uppercase << std::hex << std::setw(2) << (u32) (*it);
    
    resultStr = uiStringInput(screen, input.str(), "0123456789ABCDEF", message, (allowResize) ? 2 : 0);
    if(resultStr.size() % 2) resultStr.erase(resultStr.end() - 1, resultStr.end());
    
    for (u32 p = 0; p < resultStr.size(); p += 2) {
        std::istringstream output(resultStr.substr(p, 2));
        u32 byte = 0;
        output >> std::hex >> byte;
        result.push_back(byte);
    }
    
    return result;
}

void uiDisplayProgress(gpu::Screen screen, const std::string operation, const std::string details, bool quickSwap, u32 progress) {
    std::stringstream stream;
    stream << operation << ": [";
    u32 progressBars = progress / 4;
    for(u32 i = 0; i < 25; i++) {
        if(i < progressBars) {
            stream << '|';
        } else {
            stream << ' ';
        }
    }

    stream << "] " << std::setfill(' ') << std::setw(3) << progress << "%" << "\n";
    stream << details << "\n";

    std::string str = stream.str();

    gpu::setViewport(screen, 0, 0, screen == gpu::SCREEN_TOP ? gpu::TOP_WIDTH : gpu::BOTTOM_WIDTH, screen == gpu::SCREEN_TOP ? gpu::TOP_HEIGHT : gpu::BOTTOM_HEIGHT);
    gput::setOrtho(0, screen == gpu::SCREEN_TOP ? gpu::TOP_WIDTH : gpu::BOTTOM_WIDTH, 0, screen == gpu::SCREEN_TOP ? gpu::TOP_HEIGHT : gpu::BOTTOM_HEIGHT, -1, 1);
    
    u32 screenWidth;
    u32 screenHeight;
    gpu::getViewportWidth(&screenWidth);
    gpu::getViewportHeight(&screenHeight);

    gpu::clear();
    gput::drawString(str, (screenWidth - gput::getStringWidth(str, 8)) / 2, (screenHeight - gput::getStringHeight(str, 8)) / 2, 8, 8);
    gpu::flushCommands();
    gpu::flushBuffer();
    gpu::swapBuffers(!quickSwap);

    gpu::setViewport(gpu::SCREEN_TOP, 0, 0, gpu::TOP_WIDTH, gpu::TOP_HEIGHT);
    gput::setOrtho(0, gpu::TOP_WIDTH, 0, gpu::TOP_HEIGHT, -1, 1);
}
