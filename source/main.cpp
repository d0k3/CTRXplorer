#include "fs.hpp"
#include "ui.hpp"

#include <citrus/core.hpp>
#include <citrus/gpu.hpp>
#include <citrus/gput.hpp>
#include <citrus/hid.hpp>

#include <string>
#include <sstream>
#include <iomanip>

using namespace ctr;

typedef enum {
    M_BROWSER,
    M_HEXVIEWER,
    M_TEXTVIEWER
} Mode;

typedef enum  {
    A_DELETE,
    A_RENAME,
    A_COPY,
    A_MOVE,
    A_CREATE_DIR,
    A_CREATE_DUMMY
} Action;

int main(int argc, char **argv) {
    if(!core::init(argc)) {
        return 0;
    }
    
    const std::string title = "CTRX SD Explorer v0.9.5b";
    const u64 tapDelay = 240;

    bool launcher = core::launcher();
    bool exit = false;
    
    Mode mode = M_BROWSER;
    
    u64 inputRHoldTime = 0;
    u64 inputXHoldTime = 0;
    u64 inputYHoldTime = 0;
    
    std::string currentDir = "";
    SelectableElement currentFile = { "", "" };
    std::set<SelectableElement*>* markedElements = NULL;
    std::vector<SelectableElement> clipboard;
    u64 freeSpace = fsGetFreeSpace();
    
    u32 dummySize = (u32) -1;
    int dummyContent = 0x00;
    
    bool hvSelectMode = false;
    u32 hvStoredOffset = (u32) -1;
    u32 hvLastFoundOffset = (u32) -1;
    std::string hvLastSearchStr = "?";
    std::vector<u8> hvLastSearchHex(1, 0);
    std::vector<u8> hvLastSearch(1, 0);
    std::vector<u8> hvClipboard;
    
    auto processAction = [&](Action action, bool &updateList, bool &resetCursor) {
        const std::string alphabet = " ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz(){}[]'`^,~!@#$%&0123456789=+-_.";

        switch(action) {
            case A_DELETE: {
                if((*markedElements).empty()) {
                    if(currentFile.name.compare("..") != 0) {
                        std::string confirmMsg = "Delete \"" + uiTruncateString(currentFile.name, 24, -8) + "\"?" + "\n";
                        if(uiPrompt(gpu::SCREEN_TOP, confirmMsg, true)) {
                            if(!fsPathDelete(currentFile.id)) {
                                uiErrorPrompt(gpu::SCREEN_TOP, "Deleting", currentFile.name, true, false);
                            }
                        }
                        freeSpace = fsGetFreeSpace();
                        updateList = true;
                        resetCursor = false;
                    }
                } else {
                    u32 successCount = 0;
                    std::stringstream object;
                    if((*markedElements).size() == 1) {
                        object << "\"" << uiTruncateString((**((*markedElements).begin())).name, 24, -8) << "\"";
                    } else object << (*markedElements).size() << " paths";
                    std::string confirmMsg = "Delete " + object.str() + "?" + "\n";
                    if(uiPrompt(gpu::SCREEN_TOP, confirmMsg, true)) {                        
                        for(std::set<SelectableElement*>::iterator it = (*markedElements).begin(); it != (*markedElements).end(); it++) {
                            if(!fsPathDelete((**it).id)) {
                                std::set<SelectableElement*>::iterator next = it;
                                next++;
                                if (!uiErrorPrompt(gpu::SCREEN_TOP, "Deleting", (**it).name, true, next != (*markedElements).end())) break;
                            } else successCount++;
                        }
                        if((successCount < (*markedElements).size()) && ((*markedElements).size() > 1)) {
                            std::stringstream errorMsg;
                            errorMsg << "Deleted" << successCount << " of " << (*markedElements).size() << " paths!" << "\n";
                            uiPrompt(gpu::SCREEN_TOP, errorMsg.str(), false);
                        }
                        freeSpace = fsGetFreeSpace();
                        (*markedElements).clear();
                        updateList = true;
                        resetCursor = false;
                    }
                }
                break;
            }
                
            case A_RENAME: {
                if(currentFile.name.compare("..") != 0) { // RENAME
                    std::string confirmMsg = "Rename \"" + uiTruncateString(currentFile.name, 24, -8) + "\"?\nEnter new name below:\n";
                    std::string name = uiStringInput(gpu::SCREEN_TOP, currentFile.name, alphabet, confirmMsg);
                    if(!name.empty()) {
                        if(!fsPathRename(currentFile.id, currentDir + "/" + name)) {
                            uiErrorPrompt(gpu::SCREEN_TOP, "Renaming", currentFile.name, true, false);
                        } else {
                            updateList = true;
                            resetCursor = false;
                        }
                    }
                }
                break;
            }
                
            case A_COPY:    
            case A_MOVE: {
                if(!clipboard.empty()) {
                    u32 successCount = 0;
                    std::stringstream object;
                    if(clipboard.size() == 1) object << "\"" << uiTruncateString(clipboard.at(0).name, 18, -8) << "\"";
                    else object << clipboard.size() << " paths";
                    std::string confirmMsg = ((action == A_COPY) ? "Copy " : "Move ") + object.str() + " to this destination?" + "\n";
                    if(uiPrompt(gpu::SCREEN_TOP, confirmMsg, true)) {
                        bool overwrite = false;
                        bool overwrite_remember = false;
                        bool overwrite_remember_ask = (clipboard.size() > 1);
                        for(std::vector<SelectableElement>::iterator it = clipboard.begin(); it != clipboard.end(); it++) {
                            const std::string dest = (currentDir.compare("/") == 0) ? "/" + (*it).name : currentDir + "/" + (*it).name;
                            bool fail = false;
                            if(fsExists(dest)) {
                                if(!overwrite_remember) {
                                    std::string existMsg = "Destination exists: " + uiTruncateString((*it).name, 28, -8) + "\n" + "Overwrite existing file(s)?" + "\n";
                                    overwrite = uiPrompt(gpu::SCREEN_TOP, existMsg, true);
                                    if(overwrite_remember_ask) {
                                        existMsg = ((overwrite) ? "Overwrite all existing files?\n" : "Skip all existing files?\n");
                                        overwrite_remember = uiPrompt(gpu::SCREEN_TOP, existMsg, true);
                                        overwrite_remember_ask = false;
                                    }
                                }
                                if(!overwrite) continue;
                            }
                            fail = (action == A_COPY) ?
                                !fsPathCopy((*it).id, dest, overwrite, true) :
                                !fsPathMove((*it).id, dest, overwrite);
                            if(fail) {
                                std::string operationStr = (action == A_COPY) ? "Copying" : "Moving";
                                if(!uiErrorPrompt(gpu::SCREEN_TOP, operationStr, (*it).name, true, it + 1 != clipboard.end())) 
                                    break;
                            } else successCount++;
                        }
                        if((successCount < clipboard.size()) && (clipboard.size() > 1)) {
                            std::stringstream errorMsg;
                            errorMsg << ((action == A_COPY) ? "Copied " : "Moved ");
                            errorMsg << successCount << " of " << clipboard.size() << " paths!" << "\n";
                            uiPrompt(gpu::SCREEN_TOP, errorMsg.str(), false);
                        }
                        freeSpace = fsGetFreeSpace();
                        if(action == A_MOVE) clipboard.clear();
                        updateList = true;
                        resetCursor = false;
                    }
                }
                break;
            }
                
            case A_CREATE_DIR: {
                std::string confirmMsg = "Create new folder here?\nEnter name below:\n";
                std::string name = uiStringInput(gpu::SCREEN_TOP, "newdir", alphabet, confirmMsg); 
                if(!name.empty()) {
                    if(!fsCreateDir(currentDir + "/" + name)) {
                        uiErrorPrompt(gpu::SCREEN_TOP, "Create Folder", name, true, false);
                    }
                    updateList = true;
                    resetCursor = false;
                }
                break;
            }
                
            case A_CREATE_DUMMY: {
                std::stringstream object;
                if(dummySize == 0) object << "zero byte dummy file";
                else object << uiFormatBytes(dummySize) << " dummy file";
                std::string confirmMsg = "Generate " + object.str() + " here?\nEnter name below:\n";
                std::string name = uiStringInput(gpu::SCREEN_TOP, "dummy.bin", alphabet, confirmMsg);
                if(!name.empty()) {
                    bool overwrite = false;
                    if(fsExists(currentDir + "/" + name)) {
                        std::string existMsg = "Destination already exists. Overwrite?\n";
                        overwrite = uiPrompt(gpu::SCREEN_TOP, existMsg, true);
                    }
                    if(!fsCreateDummyFile(currentDir + "/" + name, dummySize, dummyContent, overwrite, true)) {
                        uiErrorPrompt(gpu::SCREEN_TOP, "Generating", name, true, false);
                    } 
                    freeSpace = fsGetFreeSpace();
                    updateList = true;
                    resetCursor = false;
                }
                break;
            }
                
            default:
                break;                
        }
    };
    
    auto instructionBlockBrowser = [&]() {
        std::stringstream stream;
        stream << "L - MARK files (use with " << (char) 0x018 << (char) 0x19 << (char) 0x1A << (char) 0x1B << ")" << "\n";
        if(dummySize == (u32) -1) stream << "R - [t] CREATE folder / [h] file" << "\n";
        else {
            stream << "R - [r] GENERATE " << ((dummySize == 0) ? "zero byte" : uiFormatBytes(dummySize)) << " dummy file";
            if(dummySize > 0) {
                if(dummyContent > 0xFF) stream << " (XX)";
                else stream << " (" << std::uppercase << std::setfill('0') << std::hex << std::setw(2) << (dummyContent & 0xFF) << std::nouppercase << ")";
            }
            stream << "\n";
        }
        stream << "X - [t] DELETE / [h] RENAME selected" << "\n";
        if(clipboard.empty()) stream << "Y - COPY/MOVE selected " <<  (((*markedElements).size() > 1) ? "files" : "file") << "\n";
        else stream << "Y - [t] COPY / [h] MOVE to this folder" << "\n";
        stream << "A - VIEW file in [t] hex / [h] text" << "\n";
        if(clipboard.size()) stream << "SELECT - Clear Clipboard" << "\n";
        
        return stream.str();
    };
    
    auto instructionBlockHexViewer = [&]() {
        std::stringstream stream;
        stream << std::setfill('0');
        stream << "L - [h] (" << (char) 0x18 << (char) 0x19 << (char) 0x1A << (char) 0x1B << ") fast scroll" << "\n";
        if(hvStoredOffset != (u32) -1) {
            stream << "R - GO TO begin / " << std::hex << std::uppercase << std::setw(8) << hvStoredOffset <<
             std::nouppercase << " / end" << "\n";
        } else stream << "R - GO TO begin / end" << "\n";
        stream << "X - GO TO ... ([t] hex / [h] dec)" << "\n";
        if (hvLastFoundOffset == (u32) -1) stream << "Y - SEARCH ... ([t] hex / [h] string)" << "\n";
        else stream << "Y - SEARCH [t] next / [h] new" << "\n";
        stream << "A - Enter EDIT mode" << "\n";
        
        return stream.str();
    };
    
    auto instructionBlockHexEditor = [&]() {
        std::stringstream stream;
        stream << "A - [h] ("  << (char) 0x18 << (char) 0x19 << (char) 0x1A << (char) 0x1B << ") / [t] EDIT data" << "\n";
        stream << "X - [h] ("  << (char) 0x18 << (char) 0x19 << (char) 0x1A << (char) 0x1B << ") / [t] DELETE data" << "\n";
        if(hvClipboard.empty()) {
            stream << "Y - [h] ("  << (char) 0x18 << (char) 0x19 << (char) 0x1A << (char) 0x1B << ") / [t] COPY data" << "\n";
        } else {
            stream << "Y - [h] ("  << (char) 0x18 << (char) 0x19 << (char) 0x1A << (char) 0x1B << ") / [t] PASTE data" << "\n";
        }
        if(hvClipboard.size()) stream << "SELECT - Clear paste data" << "\n";
        
        return stream.str();
    };
    
    auto instructionBlockTextViewer = [&]() {
        std::stringstream stream;
        stream << "L/R - PAGE up / PAGE down" << "\n";
        stream << "X - Enable / disable wordwrap" << "\n";
        
        return stream.str();
    };
    
    auto onLoopDisplay = [&]() {
        gpu::setViewport(gpu::SCREEN_TOP, 0, 0, gpu::TOP_WIDTH, gpu::TOP_HEIGHT);
        gput::setOrtho(0, gpu::TOP_WIDTH, 0, gpu::TOP_HEIGHT, -1, 1);        
        gpu::clear();
        
        std::string str;
        
        u32 screenWidth;
        u32 screenHeight;
        gpu::getViewportWidth(&screenWidth);
        gpu::getViewportHeight(&screenHeight);
        
        u32 vpos0 = screenHeight - 1 - 12 - 4;
        u32 vpos1 = vpos0 - 11;
        u32 cbDisplay = 10;
        u8 gr = 0x9F;
        
        // TOP BAR -> CURRENT DIRECTORY & FREE SPACE
        uiDrawRectangle(0, (screenHeight - 1) - 12, screenWidth, 12);
        str = uiTruncateString(currentDir, 36, 0); // current directory
        gput::drawString(str, 0, (screenHeight - 1) - 10, 8, 8, 0x00, 0x00, 0x00);
        str = uiFormatBytes(freeSpace) + " free"; // free space
        gput::drawString(str, (screenWidth - 1) - gput::getStringWidth(str, 8), (screenHeight - 1) - 10, 8, 8, 0x00, 0x00, 0x00);
        
        // CURRENT FILE DETAILS
        if(currentFile.name.compare("..") != 0) {
            str = "[SELECTED]";
            gput::drawString(str, 0, vpos0 - 8, 8, 8);
            str = uiTruncateString(currentFile.name, 22, -8);
            gput::drawString(str, 0, vpos1 - 8, 8, 8);
            u32 vpos = vpos1 - 9;
            for(std::vector<std::string>::iterator it = currentFile.details.begin(); it != currentFile.details.end(); it++, vpos -= 9) {
                gput::drawString(*it, 0, vpos - 8, 8, 8, gr, gr, gr);
            }
        }
        
        // CLIPBOARD DETAILS
        if(clipboard.size() > 0) {
            std::stringstream stream;
            stream << "[CLIPBOARD(" << clipboard.size() << ")]";
            str = stream.str();
            gput::drawString(str, (screenWidth - 1) - gput::getStringWidth(str, 8), vpos0 - 8, 8, 8);
            u32 vpos = vpos1;
            for(u32 i = 0; (i < clipboard.size()) && (i < cbDisplay); i++, vpos -= 9) {
                str = uiTruncateString(clipboard.at(i).name, 22, -8);
                gput::drawString(str, (screenWidth - 1) - gput::getStringWidth(str, 8), vpos - 8, 8, 8);
            }
            if(clipboard.size() > cbDisplay) {
                stream.str("");
                stream << "(+ " << clipboard.size() - cbDisplay << " more files)";
                str = stream.str();
                gput::drawString(str, (screenWidth - 1) - gput::getStringWidth(str, 8), vpos - 8, 8, 8, gr, gr, gr);
            } else if(clipboard.size() == 1) {
                for(std::vector<std::string>::iterator it = clipboard.at(0).details.begin(); it != clipboard.at(0).details.end(); it++, vpos -= 9) {
                    gput::drawString(*it, (screenWidth - 1) - gput::getStringWidth(*it, 8), vpos - 8, 8, 8, gr, gr, gr);
                }
            }
        }
        
        // INSTRUCTIONS BLOCK
        str = title + "\n";
        if(mode == M_BROWSER) str += instructionBlockBrowser();
        else if(mode == M_HEXVIEWER) str += (hvSelectMode) ? instructionBlockHexEditor() : instructionBlockHexViewer();
        else if(mode == M_TEXTVIEWER) str += instructionBlockTextViewer();
        if(launcher) str += "START - Exit to launcher\n";
        gput::drawString(str, (screenWidth - 320) / 2, 4, 8, 8);
        
        gpu::flushCommands();
        gpu::flushBuffer();
        
        return;
    };
    
    auto onLoopBrowser = [&](bool &updateList, bool &resetCursor) {
        bool breakLoop = false;
        
        onLoopDisplay();
        
        // START - EXIT TO HB LAUNCHER
        if(hid::pressed(hid::BUTTON_START) && launcher) {
            exit = true;
            return true;
        }
        
        // SELECT - CLEAR CLIPBOARD
        if(hid::pressed(hid::BUTTON_SELECT)) {
            clipboard.clear();
        }
        
        // R - (TAP) CREATE DIRECTORY / (HOLD) GENERATE DUMMY FILE
        if(hid::held(hid::BUTTON_R) && (inputRHoldTime != (u64) -1)) {
            if(inputRHoldTime == 0) inputRHoldTime = core::time();
            else if(core::time() - inputRHoldTime >= tapDelay) {
                const u64 scrollDelay = 120;
                u64 lastChangeTime = 0;
                dummySize = 0;
                dummyContent = 0x00;
                while(core::running() && hid::held(hid::BUTTON_R)) {
                    if(hid::held(hid::BUTTON_DOWN) || hid::held(hid::BUTTON_UP) || hid::held(hid::BUTTON_LEFT) || hid::held(hid::BUTTON_RIGHT)) {
                        if(lastChangeTime == 0 || core::time() - lastChangeTime >= scrollDelay) {
                            if(hid::held(hid::BUTTON_DOWN) && dummySize) {
                                dummyContent--;
                                if(dummyContent < 0x00) dummyContent = 0x100;
                            }
                            if(hid::held(hid::BUTTON_UP) && dummySize) {
                                dummyContent++;
                                if(dummyContent > 0x100) dummyContent = 0x00;
                            }
                            if(hid::held(hid::BUTTON_RIGHT) && (dummySize < 1 << 30)) {
                                dummySize = (dummySize == 0) ? 1 : dummySize << 1;
                            }
                            if(hid::held(hid::BUTTON_LEFT) && (dummySize > 0)) {
                                dummySize = (dummySize == 1) ? 0 : dummySize >> 1;
                            }
                            lastChangeTime = core::time();
                        }
                    } else if (lastChangeTime != 0) lastChangeTime = 0;                    
                    onLoopDisplay();
                    gpu::swapBuffers(true);
                    hid::poll();
                }
                processAction(A_CREATE_DUMMY, updateList, resetCursor);
                inputRHoldTime = 0;
                dummySize = (u32) -1;
            }
        }
        if(hid::released(hid::BUTTON_R) && (inputRHoldTime != 0)) {
            if(inputRHoldTime != (u64) -1) {
                processAction(A_CREATE_DIR, updateList, resetCursor);
            }
            inputRHoldTime = 0;
        }
        
        // Y - (PRESS) FILL CLIPBOARD IF EMPTY / (TAP) COPY / (HOLD) MOVE
        if(clipboard.empty()) {
            if(hid::pressed(hid::BUTTON_Y)) {
                if(markedElements != NULL && !(*markedElements).empty()) {
                    for(std::set<SelectableElement*>::iterator it = (*markedElements).begin(); it != (*markedElements).end(); it++)
                        clipboard.push_back(*(*it));
                    (*markedElements).clear();
                } else if(currentFile.name.compare("..") != 0) clipboard.push_back(currentFile);
                inputYHoldTime = (u64) -1;
            }
        }  else {
            if(hid::held(hid::BUTTON_Y) && (inputYHoldTime != (u64) -1)) {
                if(inputYHoldTime == 0) inputYHoldTime = core::time();
                else if(core::time() - inputYHoldTime >= tapDelay) {
                    processAction(A_MOVE, updateList, resetCursor);
                    inputYHoldTime = 0;
                }
            }
            if(hid::released(hid::BUTTON_Y) && (inputYHoldTime != 0)) {
                if(inputYHoldTime != (u64) -1) {
                    processAction(A_COPY, updateList, resetCursor);
                }
                inputYHoldTime = 0;
            }
        }
        
        // X - (TAP) DELETE / (HOLD) RENAME
        if(hid::held(hid::BUTTON_X) && (inputXHoldTime != (u64) -1)) {
            if(inputXHoldTime == 0) inputXHoldTime = core::time();
            else if(core::time() - inputXHoldTime >= tapDelay) {
                if(currentFile.name.compare("..") != 0) {
                    processAction(A_RENAME, updateList, resetCursor);
                    inputXHoldTime = 0;
                } else inputXHoldTime = (u64) -1;
            }
        }
        if(hid::released(hid::BUTTON_X) && (inputXHoldTime != 0)) {
            if(inputXHoldTime != (u64) -1) {
                if((currentFile.name.compare("..") != 0) || !(*markedElements).empty()) {
                    processAction(A_DELETE, updateList, resetCursor);
                }
            }
            inputXHoldTime = 0;
        }
        
        return breakLoop;
    };
    
    auto onLoopHexViewer = [&](u32 &offset, u32 &markedOffset, u32 &markedLength) {
        bool breakLoop = false;
        
        onLoopDisplay();
        
        // START - EXIT TO HB LAUNCHER
        if(hid::pressed(hid::BUTTON_START) && launcher) {
            exit = true;
            return true;
        }
        
        if(!hvSelectMode) {
            // R - GO TO FILE BEGIN/END/STORED
            if(hid::pressed(hid::BUTTON_R)) {
                if(hvStoredOffset == (u32) -1) offset = (offset) ? 0 : (u32) -1;
                else offset = (offset) ? ((offset != hvStoredOffset) ? 0 : (u32) -1) : hvStoredOffset;
            }

            // X - GO TO OFFSET
            if(hid::held(hid::BUTTON_X) && (inputXHoldTime != (u64) -1)) {
                if(inputXHoldTime == 0) inputXHoldTime = core::time();
                else if(core::time() - inputXHoldTime >= tapDelay) {
                    std::string confirmMsg = "Enter new decimal offset below:\n";
                    u32 offsetNew = uiNumberInput(gpu::SCREEN_TOP, offset, confirmMsg, false);
                    if(offsetNew != (u32) -1) hvStoredOffset = offset = offsetNew;
                    inputXHoldTime = 0;
                }
            }
            if(hid::released(hid::BUTTON_X) && (inputXHoldTime != 0)) {
                if(inputXHoldTime != (u64) -1) {
                    std::string confirmMsg = "Enter new hexadecimal offset below:\n";
                    u32 offsetNew = uiNumberInput(gpu::SCREEN_TOP, offset, confirmMsg, true);
                    if(offsetNew != (u32) -1) hvStoredOffset = offset = offsetNew;
                }
                inputXHoldTime = 0;
            }
            
            // Y - SEARCH STRING / DATA
            if(hid::held(hid::BUTTON_Y) && (inputYHoldTime != (u64) -1)) {
                if(inputYHoldTime == 0) inputYHoldTime = core::time();
                else if(core::time() - inputYHoldTime >= tapDelay) {
                    if (hvLastFoundOffset != (u32) -1) {
                        markedOffset = markedLength = 0;
                        hvLastFoundOffset = (u32) -1;
                        inputYHoldTime = (u64) -1;
                    } else {
                        const std::string alphabet = "?ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz(){}[]<>/\\|*:=+-_.'\"`^,~!@#$%& 0123456789";
                        std::string confirmMsg = "Enter search string below:\n";
                        std::string searchStr = uiStringInput(gpu::SCREEN_TOP, hvLastSearchStr, alphabet, confirmMsg);
                        if(!searchStr.empty()) {
                            u32 offsetNew;
                            hvLastSearchStr = searchStr;
                            hvLastSearch = std::vector<u8>(hvLastSearchStr.begin(), hvLastSearchStr.end());
                            offsetNew = fsDataSearch(currentFile.id, hvLastSearch, offset, true);
                            if(offsetNew != (u32) -1) {
                                markedOffset = hvLastFoundOffset = offsetNew;
                                markedLength = hvLastSearch.size();
                            } else uiErrorPrompt(gpu::SCREEN_TOP, "Searching", "Not found: " + searchStr, false, false);
                        }
                        inputYHoldTime = 0;
                    }
                }
            }
            if(hid::released(hid::BUTTON_Y) && (inputYHoldTime != 0)) {
                if(inputYHoldTime != (u64) -1) {
                    u32 offsetNew = (u32) -1;
                    if(hvLastFoundOffset == (u32) -1) {
                        std::string confirmMsg = "Enter search value below:\n";
                        std::vector<u8> searchTerm = uiDataInput(gpu::SCREEN_TOP, hvLastSearchHex, confirmMsg);
                        if(!searchTerm.empty()) {
                            hvLastSearchHex = hvLastSearch = searchTerm;
                            offsetNew = fsDataSearch(currentFile.id, hvLastSearch, offset, true);
                            if(offsetNew == (u32) -1) {
                                std::stringstream searchText;
                                for(std::vector<u8>::iterator it = searchTerm.begin(); it != searchTerm.end(); it++)
                                    searchText << std::setfill('0') << std::uppercase << std::hex << std::setw(2) << (u32) (*it);
                                uiErrorPrompt(gpu::SCREEN_TOP, "Searching", "Not found: " + searchText.str(), false, false);
                            }
                        }
                    } else offsetNew = fsDataSearch(currentFile.id, hvLastSearch, hvLastFoundOffset + 1, true);
                    if(offsetNew != (u32) -1) {
                        markedOffset = hvLastFoundOffset = offsetNew;
                        markedLength = hvLastSearch.size();
                    }
                }
                inputYHoldTime = 0;
            }
        } else {
            // SELECT - CLEAR PASTE DATA
            if(hid::pressed(hid::BUTTON_SELECT)) {
                hvClipboard.clear();
            }
        }
        
        return breakLoop;
    };
    
    auto onLoopTextViewer = [&]() {
        bool breakLoop = false;
        
        onLoopDisplay();
        
        // START - EXIT TO HB LAUNCHER
        if(hid::pressed(hid::BUTTON_START) && launcher) {
            exit = true;
            return true;
        }
        
        return breakLoop;
    };
    
    auto onSelectHexViewer = [&](u32 selectedOffset, u32 selectedLength, hid::Button selectButton, bool &forceRefresh) {
        bool breakLoop = false;
        
        if(selectButton == hid::BUTTON_A) { // A - EDIT DATA
            std::string confirmMsg = "Enter new hex value(s) below:\n";
            std::vector<u8> input = fsDataGet(currentFile.id, selectedOffset, selectedLength);
            if(input.size() != selectedLength) {
                uiErrorPrompt(gpu::SCREEN_TOP, "Reading", currentFile.id, true, false);
            } else {
                input = uiDataInput(gpu::SCREEN_TOP, input, confirmMsg, true);
                if(!input.empty() && (input.size() != selectedLength) &&
                    !uiPrompt(gpu::SCREEN_TOP, "Warning: This will change file size.\n", true));
                else if(!input.empty() && !fsDataReplace(currentFile.id, input, selectedOffset, selectedLength))
                    uiErrorPrompt(gpu::SCREEN_TOP, "Writing", currentFile.id, true, false);
                else forceRefresh = true;
            }
        } else if(selectButton == hid::BUTTON_X) { // X - DELETE DATA
            if(uiPrompt(gpu::SCREEN_TOP, "Warning: This will remove data\nand change file size.\n", true)) {
                if(!fsFileResize(currentFile.id, selectedOffset, selectedLength, 0, true))
                    uiErrorPrompt(gpu::SCREEN_TOP, "Resizing", currentFile.id, true, false);
                else forceRefresh = true;
            }
        } else if((selectButton == hid::BUTTON_Y) && hvClipboard.empty()) { // Y - COPY DATA
            hvClipboard = fsDataGet(currentFile.id, selectedOffset, selectedLength);
            if(hvClipboard.size() != selectedLength)
                uiErrorPrompt(gpu::SCREEN_TOP, "Reading", currentFile.id, true, false);
        } else if((selectButton == hid::BUTTON_Y) && !hvClipboard.empty()) { // Y - PASTE DATA
            std::string confirmMsg = "Edit paste data below:\n";
            std::vector<u8> input = uiDataInput(gpu::SCREEN_TOP, hvClipboard, confirmMsg, true);
            if(!input.empty() && (input.size() != selectedLength) &&
                !uiPrompt(gpu::SCREEN_TOP, "Warning: This will change file size.\n", true));
            else if(!input.empty() && !fsDataReplace(currentFile.id, input, selectedOffset, selectedLength))
                uiErrorPrompt(gpu::SCREEN_TOP, "Writing", currentFile.id, true, false);
            else forceRefresh = true;
        }
        
        if(forceRefresh)
            currentFile.details.at(2) = uiFormatBytes((u64) fsGetFileSize(currentFile.id)); 
        
        return breakLoop;
    };
    
    while(core::running()) {
        uiInit();
        if(mode == M_HEXVIEWER) {
            hvStoredOffset = (u32) -1;
            currentFile.details.insert(currentFile.details.begin(), "@FFFFFFFF (-1)");
            if(!uiHexViewer(currentFile.id, 0,
                [&](u32 &offset, u32 &markedOffset, u32 &markedLength, bool selectMode) { // onLoop
                    if(hvSelectMode != selectMode) hvSelectMode = selectMode;
                    return onLoopHexViewer(offset, markedOffset, markedLength);
                },
                [&](u32 offset) { // onUpdate
                    std::stringstream ssOffset;
                    ssOffset << "@" << std::setfill('0') << std::uppercase;
                    ssOffset << std::hex << std::setw(8) << offset << " (" << std::dec << offset << ")";
                    currentFile.details.at(0) = ssOffset.str();
                    return false;
                },
                [&](u32 selectedOffset, u32 selectedLength, hid::Button selectButton, bool &forceRefresh) { // onSelect
                    return onSelectHexViewer(selectedOffset, selectedLength, selectButton, forceRefresh);
                })) {
                uiErrorPrompt(gpu::SCREEN_TOP, "Hexview", currentFile.name, true, false);
            }
            mode = M_BROWSER;
        } else if(mode == M_TEXTVIEWER) {
            currentFile.details.insert(currentFile.details.begin(), "@FFFFFFFF+F (-1+-1)");
            if(!uiTextViewer(currentFile.id, onLoopTextViewer,
                [&](u32 offset, u32 plus) { // onUpdate
                    std::stringstream ssOffset;
                    ssOffset << "@" << std::setfill('0') << std::uppercase;
                    ssOffset << std::hex << std::setw(8) << offset << "+" << plus;
                    ssOffset << " (" << std::dec << offset << "+" << plus << ")";
                    currentFile.details.at(0) = ssOffset.str();
                    return false;
                }))
                uiErrorPrompt(gpu::SCREEN_TOP, "Textview", currentFile.name, true, false);
            mode = M_BROWSER;
        } else {
            uiFileBrowser( "sdmc:/", currentFile.id,
                [&](bool &updateList, bool &resetCursor) { // onLoop function
                    return onLoopBrowser(updateList, resetCursor);
                },
                [&](SelectableElement* entry) { // onUpdateEntry function
                    currentFile = *entry;
                },
                [&](std::string* currDir) { // onUpdateDir function
                    currentDir = *currDir;
                },
                [&](std::set<SelectableElement*>* marked) { // onUpdateMarked function
                    markedElements = marked;
                },
                [&](std::string selectedPath, bool &updateList) { // onSelect function
                    u64 inputAHoldTime = core::time();
                    for (hid::poll();
                        hid::held(hid::BUTTON_A) && core::time() - inputAHoldTime < tapDelay;
                        hid::poll()) gpu::swapBuffers(true);
                    mode = (core::time() - inputAHoldTime >= tapDelay) ? M_TEXTVIEWER : M_HEXVIEWER;
                    return true;
                });
        }
        
        if(exit) {
            break;
        }
    }

    core::exit();
    uiCleanup();
    
    return 0;
}
