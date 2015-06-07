#include "ctrcommon_x/fs_x.hpp"
#include "ctrcommon_x/ui_x.hpp"

#include <ctrcommon/app.hpp>
#include <ctrcommon/gpu.hpp>
#include <ctrcommon/input.hpp>
#include <ctrcommon/platform.hpp>
#include <ctrcommon/ui.hpp>

#include <sys/dirent.h>
#include <sys/errno.h>
#include <stdio.h>
#include <string.h>

#include <sstream>
#include <iomanip>

typedef enum {
    M_DEL_COPY,
    M_REN_MOVE,
	M_CREATE,
	M_HEXVIEW
} Mode;

int main(int argc, char **argv) {
    if(!platformInit()) {
        return 0;
    }

    bool ninjhax = platformIsNinjhax();
	bool exit = false;
    
    Mode mode = M_DEL_COPY;
	u64 lastSwitchTime = 0;
	
	std::string currentDir = "";
	SelectableElement currentFile = { "", "" };
	std::set<SelectableElement*>* markedElements = NULL;
	std::vector<SelectableElement> clipboard;
    u64 freeSpace = fsGetFreeSpace(SD);
	
	u32 dummySize = 0;
	int dummyContent = 0x00;
	
	auto onLoopDisplay = [&]() {
		gpuViewport(TOP_SCREEN, 0, 0, 400, 240);
		gpuClear();
		
		const std::string title = "CTRX SD Explorer v0.7.3";
		std::string str;
		
		u32 screenWidth  = gpuGetViewportWidth();
        u32 screenHeight = gpuGetViewportHeight();
		
		u32 vpos0 = screenHeight - 1 - 12 - 4;
		u32 vpos1 = vpos0 - 11;
		u32 vdec = gputGetStringHeight("ABC") + 1;
		u32 cbdisp = 10;
		u8 grey = 0x9F;
		
		// TOP BAR -> CURRENT DIRECTORY & FREE SPACE
		str = uiTruncateString(currentDir, 36, 0); // current directory
		gputDrawRectangle(0, screenHeight - 1 - gputGetStringHeight(str) - 4, screenWidth, gputGetStringHeight(str) + 4);
		gputDrawString(str, 0, screenHeight - 1 - gputGetStringHeight(str) - 2, 1.0, 0x00, 0x00, 0x00);
		str = uiFormatBytes(freeSpace) + " free"; // free space
		gputDrawString(str, screenWidth - gputGetStringWidth(str) - 1, screenHeight - 1 - gputGetStringHeight(str) - 2, 1.0, 0x00, 0x00, 0x00);
		
		// CURRENT FILE DETAILS
		if(currentFile.name.compare("..") != 0) {
			str = "[SELECTED]";
			gputDrawString(str, 0, vpos0 - gputGetStringHeight(str));
			str = uiTruncateString(currentFile.name, 22, -8);
			gputDrawString(str, 0, vpos1 - gputGetStringHeight(str));
			u32 vpos = vpos1 - vdec;
			for(std::vector<std::string>::iterator it = currentFile.details.begin(); it != currentFile.details.end(); it++, vpos -= vdec) {
				gputDrawString(*it, 0, vpos - gputGetStringHeight(*it), 1.0, grey, grey, grey);
			}
		}
		
		// CLIPBOARD DETAILS
		if(clipboard.size() > 0) {
			std::stringstream stream;
			stream << "[CLIPBOARD (" << clipboard.size() << ")]";
			str = stream.str();
			gputDrawString(str, screenWidth - 1 - gputGetStringWidth(str), vpos0 - gputGetStringHeight(str));
			u32 vpos = vpos1;
			for(u32 i = 0; (i < clipboard.size()) && (i < cbdisp); i++, vpos -= vdec) {
				str = uiTruncateString(clipboard.at(i).name, 22, -8);
				gputDrawString(str, screenWidth - 1 - gputGetStringWidth(str), vpos - gputGetStringHeight(str));
			}
			if(clipboard.size() > cbdisp) {
				stream.str("");
				stream << "(+ " << clipboard.size() - cbdisp << " more files)";
				str = stream.str();
				gputDrawString(str, screenWidth - 1 - gputGetStringWidth(str), vpos - gputGetStringHeight(str), 1.0, grey, grey, grey);
			} else if(clipboard.size() == 1) {
				for(std::vector<std::string>::iterator it = clipboard.at(0).details.begin(); it != clipboard.at(0).details.end(); it++, vpos -= vdec) {
					gputDrawString(*it, screenWidth - 1 - gputGetStringWidth(*it), vpos - gputGetStringHeight(*it), 1.0, grey, grey, grey);
				}
			}
		}
		
		// INSTRUCTIONS BLOCK
		std::stringstream stream;
		std::stringstream object;
		if((*markedElements).size() > 1) object << "marked files";
		else object << (((*markedElements).empty()) ? "selected file" : "marked file");
		stream << title << "\n";
		stream << "L - Mark files (use with " << (char) 0x018 << (char) 0x19 << (char) 0x1A << (char) 0x1B << ")" << "\n";
		stream << "R - (Tap) Switch mode / (Hold) Create..." << "\n";
		if(mode == M_DEL_COPY) {
			stream << "X - DELETE " << object.str() << "\n";
			if(clipboard.size() > 0) stream << "Y - PASTE/COPY" << ((clipboard.size() > 1) ? " files" : " file") << " from clipboard" << "\n";
			else stream << "Y - COPY " << object.str() <<  "\n";
		} else if(mode == M_REN_MOVE) {
			stream << "X - RENAME selected file" << "\n";
			if(clipboard.size() > 0) stream << "Y - PASTE/MOVE" << ((clipboard.size() > 1) ? " files" : " file") << " from clipboard" << "\n";
			else stream << "Y - MOVE " << object.str() <<  "\n";
		} else if(mode == M_CREATE) {
			stream << "X - CREATE new subdirectory" << "\n";
			stream << "Y - GENERATE " << ((dummySize == 0) ? "zero byte" : uiFormatBytes(dummySize)) << " dummy file";
			if(dummySize > 0) {
				if(dummyContent > 0xFF) stream << " (XX)";
				else stream << " (" << std::uppercase << std::setfill('0') << std::hex << std::setw(2) << (dummyContent & 0xFF) << std::nouppercase << ")";
			}
			stream << "\n";
		}
		if(clipboard.size() > 0) {
			stream << "SELECT - Clear Clipboard" << "\n";
		}
		if(ninjhax) {
			stream << "START - Exit to launcher" << "\n";
		}
		str = stream.str();
		gputDrawString(str, (screenWidth - 320) / 2, 4);
		
		gpuFlush();
        gpuFlushBuffer();
		
        return;
    };
	
	auto onLoop = [&](bool &updateList, bool &resetCursor) {
		const std::string alphabet = " ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz(){}[]'`^,~!@#$%&0123456789=+-_.";
        bool breakLoop = false;
		
		// START - EXIT TO HB LAUNCHER
		if(inputIsPressed(BUTTON_START) && ninjhax) {
            exit = true;
            return true;
        }
		
		// SELECT - CLEAR CLIPBOARD
		if(inputIsPressed(BUTTON_SELECT)) {
			clipboard.clear();
		}
		
		// R - SWITCH BETWEEN MODES
        if(inputIsHeld(BUTTON_R)) {
			if(lastSwitchTime == 0) lastSwitchTime = platformGetTime();
			else if(platformGetTime() - lastSwitchTime >= 240) {
				u64 lastChangeTime = 0;
				Mode lastMode = mode;
				mode = M_CREATE;
				dummySize = 0;
				dummyContent = 0x00;
				while(platformIsRunning() && inputIsHeld(BUTTON_R)) {
					if(inputIsPressed(BUTTON_X)) {
						std::string confirmMsg = "Create new folder here?\nEnter name below:\n";
						std::string name = uiStringInput(TOP_SCREEN, "newdir", alphabet, confirmMsg); 
						if(!name.empty()) {
							if(!fsCreateDir(currentDir + "/" + name)) {
								uiErrorPrompt(TOP_SCREEN, "Create Folder", name, true, false);
							}
							updateList = true;
							resetCursor = false;
						}
					}
					
					if(inputIsPressed(BUTTON_Y)) {
						std::stringstream object;
						if(dummySize == 0) object << "zero byte dummy file";
						else object << uiFormatBytes(dummySize) << " dummy file";
						std::string confirmMsg = "Generate " + object.str() + " here?\nEnter name below:\n";
						std::string name = uiStringInput(TOP_SCREEN, "dummy.bin", alphabet, confirmMsg);
						if(!name.empty()) {
							if(!fsCreateDummyFile(currentDir + "/" + name, dummySize, dummyContent, true)) {
								uiErrorPrompt(TOP_SCREEN, "Generating", name, true, false);
							} 
							freeSpace = fsGetFreeSpace(SD);
							updateList = true;
							resetCursor = false;
						}
					}
					
					if(inputIsHeld(BUTTON_DOWN) || inputIsHeld(BUTTON_UP) || inputIsHeld(BUTTON_LEFT) || inputIsHeld(BUTTON_RIGHT)) {
						if(lastChangeTime == 0 || platformGetTime() - lastChangeTime >= 120) {
							if(inputIsHeld(BUTTON_DOWN)) {
								dummyContent--;
								if(dummyContent < 0x00) dummyContent = 0x100;
							}
							
							if(inputIsHeld(BUTTON_UP)) {
								dummyContent++;
								if(dummyContent > 0x100) dummyContent = 0x00;
							}
							
							if(inputIsHeld(BUTTON_RIGHT) && (dummySize < 1 << 30)) {
								dummySize = (dummySize == 0) ? 1 : dummySize << 1;
							}
							
							if(inputIsHeld(BUTTON_LEFT) && (dummySize > 0)) {
								dummySize = (dummySize == 1) ? 0 : dummySize >> 1;
							}
							
							lastChangeTime = platformGetTime();
						}
					} else if (lastChangeTime != 0) lastChangeTime = 0;
					
					onLoopDisplay();
					gpuSwapBuffers(true);
					inputPoll();
				}
				
				mode = lastMode;
				lastSwitchTime = 0;
			}
		}
		if(inputIsReleased(BUTTON_R) && (lastSwitchTime != 0)) {
			mode = ( mode == M_DEL_COPY ) ? M_REN_MOVE : M_DEL_COPY;
			lastSwitchTime = 0;
		}
		
		// Y - COPY / MOVE / FILL CLIPBOARD IF EMPTY
		if(inputIsPressed(BUTTON_Y)) {
			if(clipboard.empty()) { // FILL CLIPBOARD
				if(markedElements != NULL && !(*markedElements).empty()) {
					for(std::set<SelectableElement*>::iterator it = (*markedElements).begin(); it != (*markedElements).end(); it++)
						clipboard.push_back(*(*it));
					(*markedElements).clear();
				} else if(currentFile.name.compare("..") != 0) clipboard.push_back(currentFile);
			} else { // COPY / MOVE FILES FROM CLIPBOARD
				int failCount = 0;
				std::stringstream object;
				if(clipboard.size() == 1) object << "\"" << uiTruncateString(clipboard.at(0).name, 18, -8) << "\"";
				else object << clipboard.size() << " paths";
				std::string confirmMsg = ((mode == M_DEL_COPY) ? "Copy " : "Move ") + object.str() + " to this destination?" + "\n";
				if(uiPrompt(TOP_SCREEN, confirmMsg, true)) {
					bool fail = false;
					for(std::vector<SelectableElement>::iterator it = clipboard.begin(); it != clipboard.end(); it++) {
						const std::string dest = (currentDir.compare("/") == 0) ? "/" + (*it).name : currentDir + "/" + (*it).name;
						fail = (mode == M_DEL_COPY) ?
							!fsPathCopy((*it).id, dest, true) :
							!fsPathRename((*it).id, dest);
						if(fail) {
							failCount++;
							std::string operationStr = (mode == M_DEL_COPY) ? "Copying" : "Moving";
							if (!uiErrorPrompt(TOP_SCREEN, operationStr, (*it).name, true, true)) break;
						}
					}
					if((failCount > 0) && (clipboard.size() > 1)) {
						std::stringstream errorMsg;
						errorMsg << ((mode == M_DEL_COPY) ? "Copied " : "Moved ");
						errorMsg << (clipboard.size() - failCount) << " of " << clipboard.size() << " paths!" << "\n";
						uiPrompt(TOP_SCREEN, errorMsg.str(), false);
					}
					freeSpace = fsGetFreeSpace(SD);
					clipboard.clear();
					updateList = true;
					resetCursor = false;
				}
			}
		}
		
		// X - DELETE / RENAME
		if(inputIsPressed(BUTTON_X)) {
			if(mode == M_DEL_COPY) { // DELETE
				if((*markedElements).empty()) {
					if(currentFile.name.compare("..") != 0) {
						std::string confirmMsg = "Delete \"" + uiTruncateString(currentFile.name, 24, -8) + "\"?" + "\n";
						if(uiPrompt(TOP_SCREEN, confirmMsg, true)) {
							if(!fsPathDelete(currentFile.id)) {
								uiErrorPrompt(TOP_SCREEN, "Deleting", currentFile.name, true, false);
							}
						}
						freeSpace = fsGetFreeSpace(SD);
						updateList = true;
						resetCursor = false;
					}
				} else {
					int failCount = 0;
					std::stringstream object;
					if((*markedElements).size() == 1) {
						object << "\"" << uiTruncateString((**((*markedElements).begin())).name, 24, -8) << "\"";
					} else object << (*markedElements).size() << " paths";
					std::string confirmMsg = "Delete " + object.str() + "?" + "\n";
					if(uiPrompt(TOP_SCREEN, confirmMsg, true)) {						
						for(std::set<SelectableElement*>::iterator it = (*markedElements).begin(); it != (*markedElements).end(); it++) {
							if(!fsPathDelete((**it).id)) {
								failCount++;
								if (!uiErrorPrompt(TOP_SCREEN, "Deleting", (**it).name, true, true)) break;
							}
						}
						if((failCount > 0) && ((*markedElements).size() > 1)) {
							std::stringstream errorMsg;
							errorMsg << "Deleted" << ((*markedElements).size() - failCount) << " of " << (*markedElements).size() << " paths!" << "\n";
							uiPrompt(TOP_SCREEN, errorMsg.str(), false);
						}
						freeSpace = fsGetFreeSpace(SD);
						(*markedElements).clear();
						updateList = true;
						resetCursor = false;
					}
				}
			} else if(currentFile.name.compare("..") != 0) { // RENAME
				std::string confirmMsg = "Rename \"" + uiTruncateString(currentFile.name, 24, -8) + "\"?\nEnter new name below:\n";
				std::string name = uiStringInput(TOP_SCREEN, currentFile.name, alphabet, confirmMsg);
				if(!name.empty()) {
					if(!fsPathRename(currentFile.id, currentDir + "/" + name)) {
						uiErrorPrompt(TOP_SCREEN, "Renaming", currentFile.name, true, false);
					} else {
						updateList = true;
						resetCursor = false;
					}
				}
			}
		}
		
		onLoopDisplay();
		
		return breakLoop;
	};
	
	while(platformIsRunning()) {
		uiFileBrowser( "/",
			[&](bool &updateList, bool &resetCursor) { // onLoop function
				return onLoop(updateList, resetCursor);
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
			false );
		
        if(exit) {
			break;
		}
    }

    platformCleanup();
    return 0;
}
