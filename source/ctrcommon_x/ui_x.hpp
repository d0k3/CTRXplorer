#ifndef __CTRCOMMON_UI_X_HPP__
#define __CTRCOMMON_UI_X_HPP__

#include <ctrcommon/types.hpp>
#include <ctrcommon/ui.hpp>

#include <functional>
#include <set>
#include <string>
#include <vector>

void uiDrawRectangle(int x, int y, u32 width, u32 height, u8 red = 0xFF, u8 green = 0xFF, u8 blue = 0xFF, u8 alpha = 0xFF);
std::string uiTruncateString(const std::string str, int nsize, int pos);
std::string uiFormatBytes(u64 bytes);
bool uiFileBrowser(const std::string rootDirectory, const std::string startPath, std::function<bool(bool &updateList, bool &resetCursorOnUpdate)> onLoop, std::function<void(SelectableElement* entry)> onUpdateEntry, std::function<void(std::string* currDir)> onUpdateDir, std::function<void(std::set<SelectableElement*>* marked)> onUpdateMarked, std::function<bool(std::string selectedPath, bool &updateList)> onSelect, bool useTopScreen = false);
bool uiHexViewer(const std::string path, u32 start, std::function<bool(u32 &offset)> onLoop, std::function<bool(u32 offset)> onUpdate);
bool uiErrorPrompt(Screen screen, const std::string operationStr, const std::string detailStr, bool checkErrno, bool question);
std::string uiStringInput(Screen screen, std::string preset, const std::string alphabet, const std::string message);
u32 uiNumberInput(Screen screen, u32 preset, const std::string message, bool hex = false);

#endif