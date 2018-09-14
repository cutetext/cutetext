// This file is part of CuteText project
// Copyright (C) 2018 by James Zeng <james.zeng@hotmail.com>
// The LICENSE file describes the conditions under which this software may be distributed.
/**
 * @file strip_definition.h
 * @author James Zeng
 * @date 2018-08-12
 * @brief Definition of classes build user strips from a description string.
 *
 * @see https://github.com/cutetext/cutetext
 */

class UserControl {
public:
	enum UCControlType { ucStatic, ucEdit, ucCombo, ucButton, ucDefaultButton } controlType;
	GUI::GUIString text;
	int item;
	bool fixedWidth;
	int widthDesired;
	int widthAllocated;
	GUI::Window w;
	UserControl(UCControlType controlType_, const GUI::GUIString &text_, int item_) :
		controlType(controlType_),
		text(text_),
		item(item_),
		fixedWidth(true),
		widthDesired(20),
		widthAllocated(20)	{
	}
};

struct ColumnWidth {
	int widthDesired;
	int widthAllocated;
	bool isResizeable;
	ColumnWidth() noexcept : widthDesired(0), widthAllocated(0), isResizeable(false) {
	}
};

class StripDefinition {
public:
	bool hasClose;
	unsigned int columns;
	std::vector<std::vector<UserControl> > controls;
	std::vector<ColumnWidth> widths;

	explicit StripDefinition(GUI::GUIString definition) {
		hasClose = false;
		controls.clear();
		columns = 0;
		controls.push_back(std::vector<UserControl>());
		const GUI::GUIChar *pdef=definition.c_str();
		int line = 0;
		unsigned int column = 0;
		int item = 0;
		while (*pdef) {
			if (*pdef == '\n') {
				controls.push_back(std::vector<UserControl>());
				column = 0;
				line++;
				pdef++;
				continue;
			}
			if (*pdef == '!') {
				hasClose = true;
				pdef++;
				continue;
			}
			UserControl::UCControlType controlType = UserControl::ucStatic;
			GUI::GUIChar endChar = 0;
			switch (*pdef) {
				case '\'':
					controlType = UserControl::ucStatic;
					endChar = '\'';
					break;
				case '[':
					controlType = UserControl::ucEdit;
					endChar = ']';
					break;
				case '{':
					controlType = UserControl::ucCombo;
					endChar = '}';
					break;
				case '(':
					if (pdef[1] == '(') {
						controlType = UserControl::ucDefaultButton;
						pdef++;
					} else {
						controlType = UserControl::ucButton;
					}
					endChar = ')';
					break;
			}
			pdef++;
			GUI::GUIString text;
			while (*pdef && (*pdef != endChar)) {
				text += *pdef;
				pdef++;
			}
			if ((controlType == UserControl::ucDefaultButton) && *pdef)
				pdef++;
			controls.back().push_back(UserControl(controlType, text, item));
			column++;
			if (columns < column)
				columns = column;
			if (*pdef)
				pdef++;
			item++;
		}
	}

	void CalculateColumnWidths(int widthToAllocate) {
		widths.clear();

		int widthOfNonResizeables = 4 * (columns - 1);
		int resizeables = 0;
		for (size_t column=0; column<columns; column++) {
			ColumnWidth cw;
			for (size_t line=0; line<controls.size(); line++) {
				if (column <  controls[line].size()) {
					const UserControl *ctl = &controls[line][column];
					if (ctl->fixedWidth) {
						if  (cw.widthDesired < ctl->widthDesired)
							cw.widthDesired = ctl->widthDesired;
					} else {
						cw.isResizeable = true;
					}
				}
			}
			widths.push_back(cw);
			widthOfNonResizeables += cw.widthDesired;
			if (cw.isResizeable)
				resizeables++;
		}
		const int widthSpareEach = resizeables ?
			(widthToAllocate - widthOfNonResizeables) / resizeables : 0;
		for (std::vector<ColumnWidth>::iterator cw=widths.begin(); cw != widths.end(); ++cw) {
			if (cw->isResizeable) {
				cw->widthAllocated = widthSpareEach + cw->widthDesired;
			} else {
				cw->widthAllocated = cw->widthDesired;
			}
		}
	}

	UserControl *FindControl(int control) {
		int controlID=0;
		for (std::vector<std::vector<UserControl> >::iterator line=controls.begin(); line != controls.end(); ++line) {
			for (std::vector<UserControl>::iterator ctl=line->begin(); ctl != line->end(); ++ctl) {
				if (controlID == control) {
					return &(*ctl);
				}
				controlID++;
			}
		}
		return 0;
	}
};

enum StripCommand { scUnknown, scClicked, scChange, scFocusIn, scFocusOut };
