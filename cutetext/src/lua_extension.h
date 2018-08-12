// This file is part of CuteText project
// Copyright (C) 2018 by James Zeng <james.zeng@hotmail.com>
// The LICENSE file describes the conditions under which this software may be distributed.
/**
 * @file lua_extension.h
 * @author James Zeng
 * @date 2018-08-12
 * @brief Lua scripting extension
 *
 * @see https://github.com/cutetext/cutetext
 */

class LuaExtension : public Extension {
private:
	LuaExtension(); // Singleton

public:
	static LuaExtension &Instance();

	// Deleted so LuaExtension objects can not be copied.
	LuaExtension(const LuaExtension &) = delete;
	void operator=(const LuaExtension &) = delete;
	~LuaExtension() override;

	bool Initialise(ExtensionAPI *host_) override;
	bool Finalise() override;
	bool Clear() override;
	bool Load(const char *filename) override;

	bool InitBuffer(int) override;
	bool ActivateBuffer(int) override;
	bool RemoveBuffer(int) override;

	bool OnOpen(const char *filename) override;
	bool OnSwitchFile(const char *filename) override;
	bool OnBeforeSave(const char *filename) override;
	bool OnSave(const char *filename) override;
	bool OnChar(char ch) override;
	bool OnExecute(const char *s) override;
	bool OnSavePointReached() override;
	bool OnSavePointLeft() override;
	bool OnStyle(unsigned int startPos, int lengthDoc, int initStyle, StyleWriter *styler) override;
	bool OnDoubleClick() override;
	bool OnUpdateUI() override;
	bool OnMarginClick() override;
	bool OnUserListSelection(int listType, const char *selection) override;
	bool OnKey(int keyval, int modifiers) override;
	bool OnDwellStart(int pos, const char *word) override;
	bool OnClose(const char *filename) override;
	bool OnUserStrip(int control, int change) override;
	bool NeedsOnClose() override;
};
