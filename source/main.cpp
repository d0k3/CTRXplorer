#include "ctrcommon_x/fs_x.hpp"
#include "ctrcommon_x/ui_x.hpp"

#include <ctrcommon/gpu.hpp>
#include <ctrcommon/input.hpp>
#include <ctrcommon/platform.hpp>
#include <ctrcommon/ui.hpp>

#include <string>
#include <sstream>
#include <iomanip>

typedef enum  {
	A_DELETE,
	A_RENAME,
	A_COPY,
	A_MOVE,
	A_CREATE_DIR,
	A_CREATE_DUMMY
} Action;

int main(int argc, char **argv) {
	if(!platformInit()) {
		return 0;
	}
	
	const std::string title = "CTRX SD Explorer v0.8.0";

	bool ninjhax = platformIsNinjhax();
	bool exit = false;
	
	u64 inputRHoldTime = 0;
	u64 inputXHoldTime = 0;
	u64 inputYHoldTime = 0;
	
	std::string currentDir = "";
	SelectableElement currentFile = { "", "" };
	std::set<SelectableElement*>* markedElements = NULL;
	std::vector<SelectableElement> clipboard;
	u64 freeSpace = fsGetFreeSpace(SD);
	
	u32 dummySize = (u32) -1;
	int dummyContent = 0x00;
	
	auto processAction = [&](Action action, bool &updateList, bool &resetCursor) {
		const std::string alphabet = " ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz(){}[]'`^,~!@#$%&0123456789=+-_.";
		
		switch(action) {
			case A_DELETE: {
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
								std::set<SelectableElement*>::iterator next = it;
								next++;
								failCount++;
								if (!uiErrorPrompt(TOP_SCREEN, "Deleting", (**it).name, true, next != (*markedElements).end())) break;
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
				break;
			}
				
			case A_RENAME: {
				if(currentFile.name.compare("..") != 0) { // RENAME
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
				break;
			}
				
			case A_COPY:	
			case A_MOVE: {
				if(!clipboard.empty()) {
					int failCount = 0;
					std::stringstream object;
					if(clipboard.size() == 1) object << "\"" << uiTruncateString(clipboard.at(0).name, 18, -8) << "\"";
					else object << clipboard.size() << " paths";
					std::string confirmMsg = ((action == A_COPY) ? "Copy " : "Move ") + object.str() + " to this destination?" + "\n";
					if(uiPrompt(TOP_SCREEN, confirmMsg, true)) {
						bool fail = false;
						for(std::vector<SelectableElement>::iterator it = clipboard.begin(); it != clipboard.end(); it++) {
							const std::string dest = (currentDir.compare("/") == 0) ? "/" + (*it).name : currentDir + "/" + (*it).name;
							fail = (action == A_COPY) ?
								!fsPathCopy((*it).id, dest, true) :
								!fsPathRename((*it).id, dest);
							if(fail) {
								failCount++;
								std::string operationStr = (action == A_COPY) ? "Copying" : "Moving";
								if (!uiErrorPrompt(TOP_SCREEN, operationStr, (*it).name, true, it + 1 != clipboard.end())) break;
							}
						}
						if((failCount > 0) && (clipboard.size() > 1)) {
							std::stringstream errorMsg;
							errorMsg << ((action == A_COPY) ? "Copied " : "Moved ");
							errorMsg << (clipboard.size() - failCount) << " of " << clipboard.size() << " paths!" << "\n";
							uiPrompt(TOP_SCREEN, errorMsg.str(), false);
						}
						freeSpace = fsGetFreeSpace(SD);
						clipboard.clear();
						updateList = true;
						resetCursor = false;
					}
				}
				break;
			}
				
			case A_CREATE_DIR: {
				std::string confirmMsg = "Create new folder here?\nEnter name below:\n";
				std::string name = uiStringInput(TOP_SCREEN, "newdir", alphabet, confirmMsg); 
				if(!name.empty()) {
					if(!fsCreateDir(currentDir + "/" + name)) {
						uiErrorPrompt(TOP_SCREEN, "Create Folder", name, true, false);
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
				std::string name = uiStringInput(TOP_SCREEN, "dummy.bin", alphabet, confirmMsg);
				if(!name.empty()) {
					if(!fsCreateDummyFile(currentDir + "/" + name, dummySize, dummyContent, true)) {
						uiErrorPrompt(TOP_SCREEN, "Generating", name, true, false);
					} 
					freeSpace = fsGetFreeSpace(SD);
					updateList = true;
					resetCursor = false;
				}
				break;
			}
				
			default:
				break;				
		}
	};
	
	auto instructionBlock = [&]() {
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
		if(clipboard.size() > 0) stream << "SELECT - Clear Clipboard" << "\n";
		if(ninjhax) stream << "START - Exit to launcher" << "\n";
		
		return stream.str();
	};
	
	auto onLoopDisplay = [&]() {
		gpuViewport(TOP_SCREEN, 0, 0, TOP_WIDTH, TOP_HEIGHT);
		gputOrtho(0, TOP_WIDTH, 0, TOP_HEIGHT, -1, 1);		
		gpuClear();
		
		std::string str;
		
		u32 screenWidth = gpuGetViewportWidth();
		u32 screenHeight = gpuGetViewportHeight();
		
		u32 vpos0 = screenHeight - 1 - 12 - 4;
		u32 vpos1 = vpos0 - 11;
		u32 cbDisplay = 10;
		u8 gr = 0x9F;
		
		// TOP BAR -> CURRENT DIRECTORY & FREE SPACE
		uiDrawRectangleCrude(0, (screenHeight - 1) - 12, screenWidth, 12);
		str = uiTruncateString(currentDir, 36, 0); // current directory
		gputDrawString(str, 0, (screenHeight - 1) - 10, 8, 8, 0x00, 0x00, 0x00);
		str = uiFormatBytes(freeSpace) + " free"; // free space
		gputDrawString(str, (screenWidth - 1) - gputGetStringWidth(str, 8), (screenHeight - 1) - 10, 8, 8, 0x00, 0x00, 0x00);
		
		// CURRENT FILE DETAILS
		if(currentFile.name.compare("..") != 0) {
			str = "[SELECTED]";
			gputDrawString(str, 0, vpos0 - 8, 8, 8);
			str = uiTruncateString(currentFile.name, 22, -8);
			gputDrawString(str, 0, vpos1 - 8, 8, 8);
			u32 vpos = vpos1 - 9;
			for(std::vector<std::string>::iterator it = currentFile.details.begin(); it != currentFile.details.end(); it++, vpos -= 9) {
				gputDrawString(*it, 0, vpos - 8, 8, 8, gr, gr, gr);
			}
		}
		
		// CLIPBOARD DETAILS
		if(clipboard.size() > 0) {
			std::stringstream stream;
			stream << "[CLIPBOARD(" << clipboard.size() << ")]";
			str = stream.str();
			gputDrawString(str, (screenWidth - 1) - gputGetStringWidth(str, 8), vpos0 - 8, 8, 8);
			u32 vpos = vpos1;
			for(u32 i = 0; (i < clipboard.size()) && (i < cbDisplay); i++, vpos -= 9) {
				str = uiTruncateString(clipboard.at(i).name, 22, -8);
				gputDrawString(str, (screenWidth - 1) - gputGetStringWidth(str, 8), vpos - 8, 8, 8);
			}
			if(clipboard.size() > cbDisplay) {
				stream.str("");
				stream << "(+ " << clipboard.size() - cbDisplay << " more files)";
				str = stream.str();
				gputDrawString(str, (screenWidth - 1) - gputGetStringWidth(str, 8), vpos - 8, 8, 8, gr, gr, gr);
			} else if(clipboard.size() == 1) {
				for(std::vector<std::string>::iterator it = clipboard.at(0).details.begin(); it != clipboard.at(0).details.end(); it++, vpos -= 9) {
					gputDrawString(*it, (screenWidth - 1) - gputGetStringWidth(*it, 8), vpos - 8, 8, 8, gr, gr, gr);
				}
			}
		}
		
		// INSTRUCTIONS BLOCK
		str = title + "\n" + instructionBlock();
		gputDrawString(str, (screenWidth - 320) / 2, 4, 8, 8);
		
		gpuFlush();
		gpuFlushBuffer();
		
		return;
	};
	
	auto onLoop = [&](bool &updateList, bool &resetCursor) {
		const u64 tapTime = 240;
		const u64 scrollTime = 120;
		bool breakLoop = false;
		
		onLoopDisplay();
		
		// START - EXIT TO HB LAUNCHER
		if(inputIsPressed(BUTTON_START) && ninjhax) {
			exit = true;
			return true;
		}
		
		// SELECT - CLEAR CLIPBOARD
		if(inputIsPressed(BUTTON_SELECT)) {
			clipboard.clear();
		}
		
		// R - (TAP) CREATE DIRECTORY / (HOLD) GENERATE DUMMY FILE
		if(inputIsHeld(BUTTON_R) && (inputRHoldTime != (u64) -1)) {
			if(inputRHoldTime == 0) inputRHoldTime = platformGetTime();
			else if(platformGetTime() - inputRHoldTime >= tapTime) {
				u64 lastChangeTime = 0;
				dummySize = 0;
				dummyContent = 0x00;
				while(platformIsRunning() && inputIsHeld(BUTTON_R)) {
					if(inputIsHeld(BUTTON_DOWN) || inputIsHeld(BUTTON_UP) || inputIsHeld(BUTTON_LEFT) || inputIsHeld(BUTTON_RIGHT)) {
						if(lastChangeTime == 0 || platformGetTime() - lastChangeTime >= scrollTime) {
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
				processAction(A_CREATE_DUMMY, updateList, resetCursor);
				inputRHoldTime = 0;
				dummySize = (u32) -1;
			}
		}
		if(inputIsReleased(BUTTON_R) && (inputRHoldTime != 0)) {
			if(inputRHoldTime != (u64) -1) {
				processAction(A_CREATE_DIR, updateList, resetCursor);
			}
			inputRHoldTime = 0;
		}
		
		// Y - (PRESS) FILL CLIPBOARD IF EMPTY / (TAP) COPY / (HOLD) MOVE
		if(clipboard.empty()) {
			if(inputIsPressed(BUTTON_Y)) {
				if(markedElements != NULL && !(*markedElements).empty()) {
					for(std::set<SelectableElement*>::iterator it = (*markedElements).begin(); it != (*markedElements).end(); it++)
						clipboard.push_back(*(*it));
					(*markedElements).clear();
				} else if(currentFile.name.compare("..") != 0) clipboard.push_back(currentFile);
				inputYHoldTime = (u64) -1;
			}
		}  else {
			if(inputIsHeld(BUTTON_Y) && (inputYHoldTime != (u64) -1)) {
				if(inputYHoldTime == 0) inputYHoldTime = platformGetTime();
				else if(platformGetTime() - inputYHoldTime >= tapTime) {
					processAction(A_MOVE, updateList, resetCursor);
					inputYHoldTime = 0;
				}
			}
			if(inputIsReleased(BUTTON_Y) && (inputYHoldTime != 0)) {
				if(inputYHoldTime != (u64) -1) {
					processAction(A_COPY, updateList, resetCursor);
				}
				inputYHoldTime = 0;
			}
		}
		
		// X - (TAP) DELETE / (HOLD) RENAME
		if(inputIsHeld(BUTTON_X) && (inputXHoldTime != (u64) -1)) {
			if(inputXHoldTime == 0) inputXHoldTime = platformGetTime();
			else if(platformGetTime() - inputXHoldTime >= tapTime) {
				if(currentFile.name.compare("..") != 0) {
					processAction(A_RENAME, updateList, resetCursor);
					inputXHoldTime = 0;
				} else inputXHoldTime = (u64) -1;
			}
		}
		if(inputIsReleased(BUTTON_X) && (inputXHoldTime != 0)) {
			if(inputXHoldTime != (u64) -1) {
				if((currentFile.name.compare("..") != 0) || !(*markedElements).empty()) {
					processAction(A_DELETE, updateList, resetCursor);
				}
			}
			inputXHoldTime = 0;
		}
		
		return breakLoop;
	};
	
	while(platformIsRunning()) {
		uiFileBrowser( "sdmc:/", currentFile.id,
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
			[&](std::string selectedPath, bool &updateList) { // onSelect function
				return true;
			});
		
		if(exit) {
			break;
		}
		
		if(!uiHexViewer(currentFile.id, 0, 1, NULL)) {
			uiErrorPrompt(TOP_SCREEN, "Hexview", currentFile.name, true, false);
		}
	}

	platformCleanup();
	return 0;
}
