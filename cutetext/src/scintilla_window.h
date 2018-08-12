// This file is part of CuteText project
// Copyright (C) 2018 by James Zeng <james.zeng@hotmail.com>
// The LICENSE file describes the conditions under which this software may be distributed.
/**
 * @file scintilla_window.h
 * @author James Zeng
 * @date 2018-08-12
 * @brief Interface to a Scintilla instance.
 *
 * @see https://github.com/cutetext/cutetext
 */

#ifndef SCINTILLAWINDOW_H
#define SCINTILLAWINDOW_H

namespace GUI {

struct ScintillaFailure {
	sptr_t status;
	explicit ScintillaFailure(sptr_t status_) : status(status_) {
	}
};

class ScintillaWindow : public ScintillaPrimitive {
	SciFnDirect fn;
	sptr_t ptr;
public:
	sptr_t status;
	ScintillaWindow();
	~ScintillaWindow() override;
	// Deleted so ScintillaWindow objects can not be copied.
	ScintillaWindow(const ScintillaWindow &source) = delete;
	ScintillaWindow(ScintillaWindow &&) = delete;
	ScintillaWindow &operator=(const ScintillaWindow &) = delete;
	ScintillaWindow &operator=(ScintillaWindow &&) = delete;

	void SetScintilla(GUI::WindowID wid_);
	bool CanCall() const;
	int Call(unsigned int msg, uptr_t wParam=0, sptr_t lParam=0);
	sptr_t CallReturnPointer(unsigned int msg, uptr_t wParam=0, sptr_t lParam=0);
	int CallPointer(unsigned int msg, uptr_t wParam, void *s);
	int CallString(unsigned int msg, uptr_t wParam, const char *s);

	// Common APIs made more accessible
	int LineStart(int line);
	int LineFromPosition(int position);
};

}

#endif
