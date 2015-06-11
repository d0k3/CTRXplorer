#include "ui_x.hpp"
#include "fs_x.hpp"

#include <ctrcommon/fs.hpp>
#include <ctrcommon/gpu.hpp>
#include <ctrcommon/input.hpp>
#include <ctrcommon/platform.hpp>

#include <sys/errno.h>
#include <string.h>

#include <algorithm>
#include <sstream>
#include <stack>

// #define CTRX_EXTRA_SAFE // additional safety checks, not needed by the responsible programmer

struct uiAlphabetize {
	inline bool operator()(SelectableElement a, SelectableElement b) {
		return strcasecmp(a.name.c_str(), b.name.c_str()) < 0;
	}
};

void uiDrawRectangleCrude(int x, int y, u32 width, u32 height, u8 red, u8 green, u8 blue, u8 alpha) {
	// very crude, and works only with rectangles >= 8 in height
	gputDrawString(std::string(1, 0xDB), x, y, width, 8, red, green, blue, alpha);
	if(height > 8) for(u32 v = height % 8; v < height; v += 8)
		gputDrawString(std::string(1, 0xDB), x, y + v, width, 8, red, green, blue, alpha);
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

	bool elementsDirty = false;
	bool resetCursorIfDirty = true;
	
	if(alphabetize) {
		std::sort(elements.begin(), elements.end(), uiAlphabetize());
	}
	
	SelectableElement* selected = &elements.at((u32) cursor);
	std::set<SelectableElement*> markedElements;
	
	if(onUpdateCursor != NULL) onUpdateCursor(selected);
	if(onUpdateMarked != NULL) onUpdateMarked(&markedElements);

	while(platformIsRunning()) {
		inputPoll();
		
		if(inputIsPressed(BUTTON_A)) {
            if(onSelect == NULL || onSelect(selected)) {
                return true;
            }
        }
		
		if(inputIsPressed(BUTTON_L)) {
			std::pair <std::set<SelectableElement*>::iterator,bool> inserted = markedElements.insert(selected);
			if(!inserted.second) markedElements.erase(inserted.first);
			selectionScroll = 0;
			selectionScrollEndTime = platformGetTime() - 3000;
			if(onUpdateMarked != NULL) onUpdateMarked(&markedElements);
		}

		if(inputIsHeld(BUTTON_DOWN) || inputIsHeld(BUTTON_UP) || inputIsHeld(BUTTON_LEFT) || inputIsHeld(BUTTON_RIGHT)) {
			int lastCursor = cursor;
			if(lastScrollTime == 0 || platformGetTime() - lastScrollTime >= 180) {
				if(inputIsHeld(BUTTON_DOWN) && cursor < (int) elements.size() - 1) {
					cursor++;
					if(cursor >= scroll + 20) {
						scroll++;
					}
				}

				if(inputIsHeld(BUTTON_UP) && cursor > 0) {
					cursor--;
					if(cursor < scroll) {
						scroll--;
					}
				}

				if(!inputIsHeld(BUTTON_L)) {
					if(inputIsHeld(BUTTON_RIGHT) && cursor < (int) elements.size() - 1) {
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

					if(inputIsHeld(BUTTON_LEFT) && cursor > 0) {
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
				
				if(inputIsHeld(BUTTON_L)) {
					if(inputIsHeld(BUTTON_LEFT)) {
						markedElements.clear();
					} else if(inputIsHeld(BUTTON_RIGHT)) {
						std::set<SelectableElement*>::iterator hint = markedElements.begin();
						for(std::vector<SelectableElement>::iterator it = elements.begin(); it != elements.end(); it++)
							hint = markedElements.insert(hint, &(*it));
					} else if(cursor != lastCursor) {
						std::pair <std::set<SelectableElement*>::iterator,bool> inserted = markedElements.insert(selected);
						if(!inserted.second) markedElements.erase(inserted.first);
					}					
					if(onUpdateMarked != NULL) onUpdateMarked(&markedElements);
				}

				selectionScroll = 0;
				selectionScrollEndTime = 0;

				lastScrollTime = platformGetTime();
			}
		} else if(lastScrollTime > 0) {
			lastScrollTime = 0;
		}

		gpuViewport(BOTTOM_SCREEN, 0, 0, BOTTOM_WIDTH, BOTTOM_HEIGHT);
		gputOrtho(0, BOTTOM_WIDTH, 0, BOTTOM_HEIGHT, -1, 1);
		gpuClear();

		u32 screenWidth = (u32) gpuGetViewportWidth();
		int screenHeight = gpuGetViewportHeight();
		for(std::vector<SelectableElement>::iterator it = elements.begin() + scroll; it != elements.begin() + scroll + 20 && it != elements.end(); it++) {
			std::string name = (*it).name;
			if (markedElements.find(&(*it)) != markedElements.end()) name.insert(0, 1, 0x10);
			int index = it - elements.begin();
			u8 cl = 0xFF;
			int offset = 0;
			float itemHeight = gputGetStringHeight(name, 8) + 4;
			if(index == cursor) {
				cl = 0x00;
				uiDrawRectangleCrude(0, (screenHeight - 1) - ((index - scroll + 1) * itemHeight), screenWidth, itemHeight);
				u32 width = (u32) gputGetStringWidth(name, 8);
				if(width > screenWidth) {
					if(selectionScrollEndTime == 0) {
						if(selectionScroll + screenWidth >= width) {
							selectionScrollEndTime = platformGetTime();
						} else {
							selectionScroll++;
						}
					} else if(platformGetTime() - selectionScrollEndTime >= 4000) {
						selectionScroll = 0;
						selectionScrollEndTime = 0;
					}
				}
				offset = -selectionScroll;
			}
			gputDrawString(name, offset, (screenHeight - 1) - ((index - scroll + 1) * itemHeight) + 2, 8, 8, cl, cl, cl);
		}

		gpuFlush();
		gpuFlushBuffer();
		
		if(useTopScreen) {
			gpuViewport(TOP_SCREEN, 0, 0, TOP_WIDTH, TOP_HEIGHT);
			gputOrtho(0, TOP_WIDTH, 0, TOP_HEIGHT, -1, 1);
			gpuClear();

			if((*selected).details.size() != 0) {
				std::stringstream details;
				for(std::vector<std::string>::iterator it = (*selected).details.begin(); it != (*selected).details.end(); it++) {
					details << *it << "\n";
				}

				gputDrawString(details.str(), 0, gpuGetViewportHeight() - 1 - gputGetStringHeight(details.str(), 8), 8, 8);
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
			gpuFlush();
			gpuFlushBuffer();
		}

		gpuSwapBuffers(true);
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
			
			if(inputIsPressed(BUTTON_B) && !directoryStack.empty()) {
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

bool uiErrorPrompt(Screen screen, const std::string operationStr, const std::string detailStr, bool checkErrno, bool question) {
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
	while(platformIsRunning()) {
		inputPoll();
		if(question) {
			if(inputIsPressed(BUTTON_A)) {
				result = true;
				break;
			}

			if(inputIsPressed(BUTTON_B)) {
				result = false;
				break;
			}
		} else {
			if(inputIsPressed(BUTTON_START)) {
				result = true;
				break;
			}
		}

		uiDisplayMessage(screen, str);
	}

	inputPoll();
	return result;
}

std::string uiStringInput(Screen screen, std::string preset, const std::string alphabet, const std::string message) {
	const int dispSize = 30;
	int fastscroll = (alphabet.size() > 16) ? 4 : 2;
	
	const u64 tapTime =  360;
	u64 inputXHoldTime = 0;
	u64 inputYHoldTime = 0;
	
	int cursor_s = 0;
	int cursor_a = -1;
	int scroll = 0;	
	u64 lastScrollTime = 0;
	
	std::string resultStr = (preset.empty()) ? alphabet.substr(0,1) : preset;
	bool result = false;
	
	if(inputIsHeld(BUTTON_X)) inputXHoldTime = (u64) -1;
	if(inputIsHeld(BUTTON_Y)) inputYHoldTime = (u64) -1;
	
	while(platformIsRunning()) {
		std::stringstream stream;
		stream << message << "\n";
		stream << ((scroll > 0) ? "<" : "|");
		stream << resultStr.substr(scroll, dispSize);
		stream << ((resultStr.size() - scroll > dispSize) ? ">" : "|") << "\n";
		for(int i = scroll; i <= cursor_s; i++) stream << " ";
		stream << "^" << "\n" << "\n";
		stream << "R - [h] (" << (char) 0x18 << (char) 0x19 << ") fast scroll" << "\n";
		stream << "X - [t] remove char / [h] clear" << "\n";
		stream << "Y - [t] insert char / [h] reset" << "\n" << "\n";
		stream << "Press A to confirm, B to cancel." << "\n";
	
		inputPoll();
		
		if(inputIsPressed(BUTTON_A)) {
			result = true;
			break;
		}

		if(inputIsPressed(BUTTON_B)) {
			result = false;
			break;
		}
		
		if(inputIsHeld(BUTTON_X) && (inputXHoldTime != (u64) -1)) {
			if(inputXHoldTime == 0) inputXHoldTime = platformGetTime();
			else if(platformGetTime() - inputXHoldTime >= tapTime) {
				resultStr = alphabet.substr(0, 1);
				cursor_s = 0;
				cursor_a = 0;
				scroll = 0;
				inputXHoldTime = (u64) -1;
			}
		}
		if(inputIsReleased(BUTTON_X) && (inputXHoldTime != 0)) {
			if((inputXHoldTime != (u64) -1) && (resultStr.size() > 1)) {
				resultStr.erase(cursor_s, 1);
				if(cursor_s == (int) resultStr.size()) cursor_s--;
				cursor_a = -1;
			}
			inputXHoldTime = 0;
		}
		
		if(inputIsHeld(BUTTON_Y) && (inputYHoldTime != (u64) -1)) {
			if(inputYHoldTime == 0) inputYHoldTime = platformGetTime();
			else if(platformGetTime() - inputYHoldTime >= tapTime) {
				resultStr = (preset.empty()) ? alphabet.substr(0,1) : preset;
				cursor_s = 0;
				cursor_a = -1;
				scroll = 0;
				inputYHoldTime = (u64) -1;
			}
		}
		if(inputIsReleased(BUTTON_Y) && (inputYHoldTime != 0)) {
			if(inputYHoldTime != (u64) -1) {
				resultStr.insert(cursor_s, alphabet.substr(0,1));
				cursor_a = 0;
			}
			inputYHoldTime = 0;
		}
		
		if(inputIsHeld(BUTTON_DOWN) || inputIsHeld(BUTTON_UP)) {
			if(lastScrollTime == 0 || platformGetTime() - lastScrollTime >= 120) {
				if(cursor_a < 0) {
					cursor_a = alphabet.find(resultStr.substr(cursor_s, 1));
					if (cursor_a < 0) cursor_a = 0;
				}
				
				if(inputIsHeld(BUTTON_UP)) {
					cursor_a += (inputIsHeld(BUTTON_R)) ? fastscroll : 1;
					while(cursor_a >= (int) alphabet.size()) cursor_a -= alphabet.size();
				}

				if(inputIsHeld(BUTTON_DOWN)) {
					cursor_a -= (inputIsHeld(BUTTON_R)) ? fastscroll : 1;
					while(cursor_a < 0) cursor_a += alphabet.size();
				}
				
				resultStr.replace(cursor_s, 1, alphabet.substr(cursor_a, 1));
				lastScrollTime = platformGetTime();
			}
		} else if(inputIsHeld(BUTTON_LEFT) || inputIsHeld(BUTTON_RIGHT)) {
			if(lastScrollTime == 0 || platformGetTime() - lastScrollTime >= 120) {
				if(inputIsHeld(BUTTON_LEFT) && cursor_s > 0) {
					if((cursor_s == (int) resultStr.size() - 1) && (resultStr.at(cursor_s) == ' '))
						resultStr.resize(cursor_s);
					cursor_s--;
					if(scroll > cursor_s) scroll = cursor_s;
					cursor_a = -1;
				}
				
				if(inputIsHeld(BUTTON_RIGHT)) {
					cursor_s++;
					if(scroll + dispSize <= cursor_s) scroll = cursor_s - dispSize + 1;
					if(cursor_s == (int) resultStr.size()) {
						resultStr.append(alphabet.substr(0, 1));
						cursor_a = 0;
					} else cursor_a = -1;
				}
				
				lastScrollTime = platformGetTime();
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
	
	inputPoll();
	
	return resultStr;
}
