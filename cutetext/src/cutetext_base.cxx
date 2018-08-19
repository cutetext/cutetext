// This file is part of CuteText project
// Copyright (C) 2018 by James Zeng <james.zeng@hotmail.com>
// The LICENSE file describes the conditions under which this software may be distributed.
/**
 * @file cutetext_base.cxx
 * @author James Zeng
 * @date 2018-08-12
 * @brief Platform independent base class of editor.
 *
 * @see https://github.com/cutetext/cutetext
 */

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <cstdio>
#include <cstdarg>
#include <ctime>
#include <cmath>

#include <string>
#include <vector>
#include <map>
#include <set>
#include <algorithm>
#include <memory>

#include <fcntl.h>
#include <sys/stat.h>

#include "ILoader.h"
#include "Scintilla.h"
#include "SciLexer.h"

#include "gui.h"
#include "scintilla_window.h"
#include "string_list.h"
#include "string_helpers.h"
#include "filepath.h"
#include "style_definition.h"
#include "propset_file.h"
#include "style_writer.h"
#include "extender.h"
#include "cutetext.h"
#include "mutex.h"
#include "job_queue.h"

#include "cookie.h"
#include "worker.h"
#include "fileworker.h"
#include "match_marker.h"
#include "editor_config.h"
#include "cutetext_base.h"

Searcher::Searcher() {
	wholeWord_ = false;
	matchCase_ = false;
	regExp_ = false;
	unSlash_ = false;
	wrapFind_ = true;
	reverseFind_ = false;

	searchStartPosition_ = 0;
	replacing_ = false;
	havefound_ = false;
	failedfind_ = false;
	findInStyle_ = false;
	findStyle_ = 0;
	closeFind_ = CloseFind::kCloseAlways;

	focusOnReplace_ = false;
}

void Searcher::InsertFindInMemory() {
	memFinds_.Insert(findWhat_.c_str());
}

// The find and replace dialogs and strips often manipulate boolean
// flags based on dialog control IDs and menu IDs.
bool &Searcher::FlagFromCmd(int cmd) {
	static bool notFound;
	switch (cmd) {
		case IDWHOLEWORD:
		case IDM_WHOLEWORD:
			return wholeWord_;
		case IDMATCHCASE:
		case IDM_MATCHCASE:
			return matchCase_;
		case IDREGEXP:
		case IDM_REGEXP:
			return regExp_;
		case IDUNSLASH:
		case IDM_UNSLASH:
			return unSlash_;
		case IDWRAP:
		case IDM_WRAPAROUND:
			return wrapFind_;
		case IDDIRECTIONUP:
		case IDM_DIRECTIONUP:
			return reverseFind_;
	}
	return notFound;
}

CuteTextBase::CuteTextBase(Extension *ext) : apis_(true), pwFocussed_(&wEditor_), extender_(ext) {
	needIdle_ = false;
	codePage_ = 0;
	characterSet_ = 0;
	language_ = "java";
	lexLanguage_ = SCLEX_CPP;
	lexLPeg_ = -1;
	functionDefinition_ = "";
	diagnosticStyleStart_ = 0;
	stripTrailingSpaces_ = false;
	ensureFinalLineEnd_ = false;
	ensureConsistentLineEnds_ = false;
	indentOpening_ = true;
	indentClosing_ = true;
	indentMaintain_ = false;
	statementLookback_ = 10;
	preprocessorSymbol_ = '\0';

	tbVisible_ = false;
	sbVisible_ = false;
	tabVisible_ = false;
	tabHideOne_ = false;
	tabMultiLine_ = false;
	sbNum_ = 1;
	visHeightTools_ = 0;
	visHeightTab_ = 0;
	visHeightStatus_ = 0;
	visHeightEditor_ = 1;
	heightBar_ = 7;
	dialogsOnScreen_ = 0;
	topMost_ = false;
	wrap_ = false;
	wrapOutput_ = false;
	wrapStyle_ = SC_WRAP_WORD;
	alphaIndicator_ = 30;
	underIndicator_ = false;
	openFilesHere_ = false;
	fullScreen_ = false;

	heightOutput_ = 0;
	heightOutputStartDrag_ = 0;
	previousHeightOutput_ = 0;

	allowMenuActions_ = true;
	scrollOutput_ = 1;
	returnOutputToCommand_ = true;

	ptStartDrag_.x_ = 0;
	ptStartDrag_.y_ = 0;
	capturedMouse_ = false;
	firstPropertiesRead_ = true;
	localiser_.read_ = false;
	splitVertical_ = false;
	bufferedDraw_ = true;
	bracesCheck_ = true;
	bracesSloppy_ = false;
	bracesStyle_ = 0;
	braceCount_ = 0;

	indentationWSVisible_ = true;
	indentExamine_ = SC_IV_LOOKBOTH;
	autoCompleteIgnoreCase_ = false;
	imeAutoComplete_ = false;
	callTipUseEscapes_ = false;
	callTipIgnoreCase_ = false;
	autoCCausedByOnlyOne_ = false;
	startCalltipWord_ = 0;
	currentCallTip_ = 0;
	maxCallTips_ = 1;
	currentCallTipWord_ = "";
	lastPosCallTip_ = 0;

	margin_ = false;
	marginWidth_ = kMarginWidthDefault;
	foldMargin_ = true;
	foldMarginWidth_ = kFoldMarginWidthDefault;
	lineNumbers_ = false;
	lineNumbersWidth_ = kLineNumbersWidthDefault;
	lineNumbersExpand_ = false;

	macrosEnabled_ = false;
	recording_ = false;

	propsEmbed_.superPS_ = &propsPlatform_;
	propsBase_.superPS_ = &propsEmbed_;
	propsUser_.superPS_ = &propsBase_;
	propsDirectory_.superPS_ = &propsUser_;
	propsLocal_.superPS_ = &propsDirectory_;
	propsDiscovered_.superPS_ = &propsLocal_;
	props_.superPS_ = &propsDiscovered_;

	propsStatus_.superPS_ = &props_;

	needReadProperties_ = false;
	quitting_ = false;

	timerMask_ = 0;
	delayBeforeAutoSave_ = 0;

	editorConfig_ = IEditorConfig::Create();
}

CuteTextBase::~CuteTextBase() {
	if (extender_)
		extender_->Finalise();
	popup_.Destroy();
}

void CuteTextBase::Finalise() {
	TimerEnd(kTimerAutoSave);
}

void CuteTextBase::WorkerCommand(int cmd, Worker *pWorker) {
	switch (cmd) {
	case kWorkFileRead:
		TextRead(static_cast<FileLoader *>(pWorker));
		UpdateProgress(pWorker);
		break;
	case kWorkFileWritten:
		TextWritten(static_cast<FileStorer *>(pWorker));
		UpdateProgress(pWorker);
		break;
	case kWorkFileProgress:
 		UpdateProgress(pWorker);
		break;
	}
}

// The system focus may move to other controls including the menu bar
// but we are normally interested in whether the edit or output pane was
// most recently focused and should be used by menu commands.
void CuteTextBase::SetPaneFocus(bool editPane) {
	pwFocussed_ = editPane ? &wEditor_ : &wOutput_;
}

int CuteTextBase::CallFocused(unsigned int msg, uptr_t wParam, sptr_t lParam) {
	if (wOutput_.HasFocus())
		return wOutput_.Call(msg, wParam, lParam);
	else
		return wEditor_.Call(msg, wParam, lParam);
}

int CuteTextBase::CallFocusedElseDefault(int defaultValue, unsigned int msg, uptr_t wParam, sptr_t lParam) {
	if (wOutput_.HasFocus())
		return wOutput_.Call(msg, wParam, lParam);
	else if (wEditor_.HasFocus())
		return wEditor_.Call(msg, wParam, lParam);
	else
		return defaultValue;
}

sptr_t CuteTextBase::CallPane(int destination, unsigned int msg, uptr_t wParam, sptr_t lParam) {
	if (destination == IDM_SRCWIN)
		return wEditor_.Call(msg, wParam, lParam);
	else if (destination == IDM_RUNWIN)
		return wOutput_.Call(msg, wParam, lParam);
	else
		return CallFocused(msg, wParam, lParam);
}

void CuteTextBase::CallChildren(unsigned int msg, uptr_t wParam, sptr_t lParam) {
	wEditor_.Call(msg, wParam, lParam);
	wOutput_.Call(msg, wParam, lParam);
}

std::string CuteTextBase::GetTranslationToAbout(const char * const propname, bool retainIfNotFound) {
#if !defined(GTK)
	return GUI::UTF8FromString(localiser_.Text(propname, retainIfNotFound));
#else
	// On GTK+, localiser.Text always converts to UTF-8.
	return localiser_.Text(propname, retainIfNotFound);
#endif
}

void CuteTextBase::ViewWhitespace(bool view) {
	if (view && indentationWSVisible_ == 2)
		wEditor_.Call(SCI_SETVIEWWS, SCWS_VISIBLEONLYININDENT);
	else if (view && indentationWSVisible_)
		wEditor_.Call(SCI_SETVIEWWS, SCWS_VISIBLEALWAYS);
	else if (view)
		wEditor_.Call(SCI_SETVIEWWS, SCWS_VISIBLEAFTERINDENT);
	else
		wEditor_.Call(SCI_SETVIEWWS, SCWS_INVISIBLE);
}

StyleAndWords CuteTextBase::GetStyleAndWords(const char *base) {
	StyleAndWords sw;
	std::string fileNameForExtension = ExtensionFileName();
	std::string sAndW = props_.GetNewExpandString(base, fileNameForExtension.c_str());
	sw.styleNumber_ = atoi(sAndW.c_str());
	const char *space = strchr(sAndW.c_str(), ' ');
	if (space)
		sw.words_ = space + 1;
	return sw;
}

void CuteTextBase::AssignKey(int key, int mods, int cmd) {
	wEditor_.Call(SCI_ASSIGNCMDKEY,
	        LongFromTwoShorts(static_cast<short>(key),
	                static_cast<short>(mods)), cmd);
}

/**
 * Override the language of the current file with the one indicated by @a cmdID.
 * Mostly used to set a language on a file of unknown extension.
 */
void CuteTextBase::SetOverrideLanguage(int cmdID) {
	RecentFile rf = GetFilePosition();
	EnsureRangeVisible(wEditor_, 0, wEditor_.Call(SCI_GETLENGTH), false);
	// Zero all the style bytes
	wEditor_.Call(SCI_CLEARDOCUMENTSTYLE);

	CurrentBuffer()->overrideExtension_ = "x.";
	CurrentBuffer()->overrideExtension_ += languageMenu_[cmdID].extension_;
	ReadProperties();
	SetIndentSettings();
	wEditor_.Call(SCI_COLOURISE, 0, -1);
	Redraw();
	DisplayAround(rf);
}

int CuteTextBase::LengthDocument() {
	return wEditor_.Call(SCI_GETLENGTH);
}

int CuteTextBase::GetCaretInLine() {
	const int caret = wEditor_.Call(SCI_GETCURRENTPOS);
	const int line = wEditor_.Call(SCI_LINEFROMPOSITION, caret);
	const int lineStart = wEditor_.Call(SCI_POSITIONFROMLINE, line);
	return caret - lineStart;
}

void CuteTextBase::GetLine(char *text, int sizeText, int line) {
	if (line < 0)
		line = GetCurrentLineNumber();
	int lineStart = wEditor_.Call(SCI_POSITIONFROMLINE, line);
	int lineEnd = wEditor_.Call(SCI_GETLINEENDPOSITION, line);
	const int lineMax = lineStart + sizeText - 1;
	if (lineEnd > lineMax)
		lineEnd = lineMax;
	GetRange(wEditor_, lineStart, lineEnd, text);
	text[lineEnd - lineStart] = '\0';
}

std::string CuteTextBase::GetCurrentLine() {
	// Get needed buffer size
	const int len = wEditor_.Call(SCI_GETCURLINE, 0, 0);
	// Allocate buffer
	std::string text(len+1, '\0');
	// And get the line
	wEditor_.CallString(SCI_GETCURLINE, len, &text[0]);
	return text.substr(0, text.length()-1);
}

void CuteTextBase::GetRange(GUI::ScintillaWindow &win, int start, int end, char *text) {
	win.Call(SCI_SETTARGETRANGE, start, end);
	win.CallPointer(SCI_GETTARGETTEXT, 0, text);
	text[end - start] = '\0';
}

/**
 * Check if the given line is a preprocessor condition line.
 * @return The kind of preprocessor condition (enum values).
 */
int CuteTextBase::IsLinePreprocessorCondition(char *line) {
	char *currChar = line;

	if (!currChar) {
		return false;
	}
	while (isspacechar(*currChar) && *currChar) {
		currChar++;
	}
	if (preprocessorSymbol_ && (*currChar == preprocessorSymbol_)) {
		currChar++;
		while (isspacechar(*currChar) && *currChar) {
			currChar++;
		}
		char word[32];
		size_t i = 0;
		while (!isspacechar(*currChar) && *currChar && (i < (sizeof(word) - 1))) {
			word[i++] = *currChar++;
		}
		word[i] = '\0';
		std::map<std::string, PreProcKind>::const_iterator it = preprocOfString_.find(word);
		if (it != preprocOfString_.end()) {
			return it->second;
		}
	}
	return kPpcNone;
}

/**
 * Search a matching preprocessor condition line.
 * @return @c true if the end condition are meet.
 * Also set curLine to the line where one of these conditions is mmet.
 */
bool CuteTextBase::FindMatchingPreprocessorCondition(
    int &curLine,   		///< Number of the line where to start the search
    int direction,   		///< Direction of search: 1 = forward, -1 = backward
    int condEnd1,   		///< First status of line for which the search is OK
    int condEnd2) {		///< Second one

	bool isInside = false;
	char line[800];	// No need for full line
	int level = 0;
	const int maxLines = wEditor_.Call(SCI_GETLINECOUNT) - 1;

	while (curLine < maxLines && curLine > 0 && !isInside) {
		curLine += direction;	// Increment or decrement
		GetLine(line, sizeof(line), curLine);
		const int status = IsLinePreprocessorCondition(line);

		if ((direction == 1 && status == kPpcStart) || (direction == -1 && status == kPpcEnd)) {
			level++;
		} else if (level > 0 && ((direction == 1 && status == kPpcEnd) || (direction == -1 && status == kPpcStart))) {
			level--;
		} else if (level == 0 && (status == condEnd1 || status == condEnd2)) {
			isInside = true;
		}
	}

	return isInside;
}

/**
 * Find if there is a preprocessor condition after or before the caret position,
 * @return @c true if inside a preprocessor condition.
 */
bool CuteTextBase::FindMatchingPreprocCondPosition(
    bool isForward,   		///< @c true if search forward
    int &mppcAtCaret,   	///< Matching preproc. cond.: current position of caret
    int &mppcMatch) {		///< Matching preproc. cond.: matching position

	bool isInside = false;
	int curLine;
	char line[800];	// Probably no need to get more characters, even if the line is longer, unless very strange layout...
	int status;

	// Get current line
	curLine = wEditor_.Call(SCI_LINEFROMPOSITION, mppcAtCaret);
	GetLine(line, sizeof(line), curLine);
	status = IsLinePreprocessorCondition(line);

	switch (status) {
	case kPpcStart:
		if (isForward) {
			isInside = FindMatchingPreprocessorCondition(curLine, 1, kPpcMiddle, kPpcEnd);
		} else {
			mppcMatch = mppcAtCaret;
			return true;
		}
		break;
	case kPpcMiddle:
		if (isForward) {
			isInside = FindMatchingPreprocessorCondition(curLine, 1, kPpcMiddle, kPpcEnd);
		} else {
			isInside = FindMatchingPreprocessorCondition(curLine, -1, kPpcStart, kPpcMiddle);
		}
		break;
	case kPpcEnd:
		if (isForward) {
			mppcMatch = mppcAtCaret;
			return true;
		} else {
			isInside = FindMatchingPreprocessorCondition(curLine, -1, kPpcStart, kPpcMiddle);
		}
		break;
	default:   	// Should be noPPC

		if (isForward) {
			isInside = FindMatchingPreprocessorCondition(curLine, 1, kPpcMiddle, kPpcEnd);
		} else {
			isInside = FindMatchingPreprocessorCondition(curLine, -1, kPpcStart, kPpcMiddle);
		}
		break;
	}

	if (isInside) {
		mppcMatch = wEditor_.Call(SCI_POSITIONFROMLINE, curLine);
	}
	return isInside;
}

static bool IsBrace(char ch) {
	return ch == '[' || ch == ']' || ch == '(' || ch == ')' || ch == '{' || ch == '}';
}

/**
 * Find if there is a brace next to the caret, checking before caret first, then
 * after caret. If brace found also find its matching brace.
 * @return @c true if inside a bracket pair.
 */
bool CuteTextBase::FindMatchingBracePosition(bool editor, int &braceAtCaret, int &braceOpposite, bool sloppy) {
	bool isInside = false;
	GUI::ScintillaWindow &win = editor ? wEditor_ : wOutput_;

	const int mainSel = win.Call(SCI_GETMAINSELECTION, 0, 0);
	if (win.Call(SCI_GETSELECTIONNCARETVIRTUALSPACE, mainSel, 0) > 0)
		return false;

	const int bracesStyleCheck = editor ? bracesStyle_ : 0;
	int caretPos = win.Call(SCI_GETCURRENTPOS, 0, 0);
	braceAtCaret = -1;
	braceOpposite = -1;
	char charBefore = '\0';
	int styleBefore = 0;
	const int lengthDoc = win.Call(SCI_GETLENGTH, 0, 0);
	TextReader acc(win);
	if ((lengthDoc > 0) && (caretPos > 0)) {
		// Check to ensure not matching brace that is part of a multibyte character
		if (win.Call(SCI_POSITIONBEFORE, caretPos) == (caretPos - 1)) {
			charBefore = acc[caretPos - 1];
			styleBefore = acc.StyleAt(caretPos - 1);
		}
	}
	// Priority goes to character before caret
	if (charBefore && IsBrace(charBefore) &&
	        ((styleBefore == bracesStyleCheck) || (!bracesStyle_))) {
		braceAtCaret = caretPos - 1;
	}
	bool colonMode = false;
	if ((lexLanguage_ == SCLEX_PYTHON) &&
	        (':' == charBefore) && (SCE_P_OPERATOR == styleBefore)) {
		braceAtCaret = caretPos - 1;
		colonMode = true;
	}
	bool isAfter = true;
	if (lengthDoc > 0 && sloppy && (braceAtCaret < 0) && (caretPos < lengthDoc)) {
		// No brace found so check other side
		// Check to ensure not matching brace that is part of a multibyte character
		if (win.Call(SCI_POSITIONAFTER, caretPos) == (caretPos + 1)) {
			const char charAfter = acc[caretPos];
			const int styleAfter = acc.StyleAt(caretPos);
			if (charAfter && IsBrace(charAfter) && ((styleAfter == bracesStyleCheck) || (!bracesStyle_))) {
				braceAtCaret = caretPos;
				isAfter = false;
			}
			if ((lexLanguage_ == SCLEX_PYTHON) &&
			        (':' == charAfter) && (SCE_P_OPERATOR == styleAfter)) {
				braceAtCaret = caretPos;
				colonMode = true;
			}
		}
	}
	if (braceAtCaret >= 0) {
		if (colonMode) {
			const int lineStart = win.Call(SCI_LINEFROMPOSITION, braceAtCaret);
			const int lineMaxSubord = win.Call(SCI_GETLASTCHILD, lineStart, -1);
			braceOpposite = win.Call(SCI_GETLINEENDPOSITION, lineMaxSubord);
		} else {
			braceOpposite = win.Call(SCI_BRACEMATCH, braceAtCaret, 0);
		}
		if (braceOpposite > braceAtCaret) {
			isInside = isAfter;
		} else {
			isInside = !isAfter;
		}
	}
	return isInside;
}

void CuteTextBase::BraceMatch(bool editor) {
	if (!bracesCheck)
		return;
	int braceAtCaret = -1;
	int braceOpposite = -1;
	FindMatchingBracePosition(editor, braceAtCaret, braceOpposite, bracesSloppy);
	GUI::ScintillaWindow &win = editor ? wEditor_ : wOutput_;
	if ((braceAtCaret != -1) && (braceOpposite == -1)) {
		win.Call(SCI_BRACEBADLIGHT, braceAtCaret, 0);
		wEditor_.Call(SCI_SETHIGHLIGHTGUIDE, 0);
	} else {
		char chBrace = 0;
		if (braceAtCaret >= 0)
			chBrace = static_cast<char>(win.Call(
			            SCI_GETCHARAT, braceAtCaret, 0));
		win.Call(SCI_BRACEHIGHLIGHT, braceAtCaret, braceOpposite);
		int columnAtCaret = win.Call(SCI_GETCOLUMN, braceAtCaret, 0);
		int columnOpposite = win.Call(SCI_GETCOLUMN, braceOpposite, 0);
		if (chBrace == ':') {
			const int lineStart = win.Call(SCI_LINEFROMPOSITION, braceAtCaret);
			const int indentPos = win.Call(SCI_GETLINEINDENTPOSITION, lineStart, 0);
			const int indentPosNext = win.Call(SCI_GETLINEINDENTPOSITION, lineStart + 1, 0);
			columnAtCaret = win.Call(SCI_GETCOLUMN, indentPos, 0);
			const int columnAtCaretNext = win.Call(SCI_GETCOLUMN, indentPosNext, 0);
			const int indentSize = win.Call(SCI_GETINDENT);
			if (columnAtCaretNext - indentSize > 1)
				columnAtCaret = columnAtCaretNext - indentSize;
			if (columnOpposite == 0)	// If the final line of the structure is empty
				columnOpposite = columnAtCaret;
		} else {
			if (win.Call(SCI_LINEFROMPOSITION, braceAtCaret) == win.Call(SCI_LINEFROMPOSITION, braceOpposite)) {
				// Avoid attempting to draw a highlight guide
				columnAtCaret = 0;
				columnOpposite = 0;
			}
		}

		if (props.GetInt("highlight.indentation.guides"))
			win.Call(SCI_SETHIGHLIGHTGUIDE, Minimum(columnAtCaret, columnOpposite), 0);
	}
}

void CuteTextBase::SetWindowName() {
	if (filePath_.IsUntitled()) {
		windowName_ = localiser.Text("Untitled");
		windowName_.insert(0, GUI_TEXT("("));
		windowName_ += GUI_TEXT(")");
	} else if (props.GetInt("title.full.path") == 2) {
		windowName_ = FileNameExt().AsInternal();
		windowName_ += GUI_TEXT(" ");
		windowName_ += localiser.Text("in");
		windowName_ += GUI_TEXT(" ");
		windowName_ += filePath_.Directory().AsInternal();
	} else if (props.GetInt("title.full.path") == 1) {
		windowName_ = filePath_.AsInternal();
	} else {
		windowName_ = FileNameExt().AsInternal();
	}
	if (CurrentBufferConst()->isDirty)
		windowName_ += GUI_TEXT(" * ");
	else
		windowName_ += GUI_TEXT(" - ");
	windowName_ += appName;

	if (buffers.length > 1 && props.GetInt("title.show.buffers")) {
		windowName_ += GUI_TEXT(" [");
		windowName_ += GUI::StringFromInteger(buffers.Current() + 1);
		windowName_ += GUI_TEXT(" ");
		windowName_ += localiser.Text("of");
		windowName_ += GUI_TEXT(" ");
		windowName_ += GUI::StringFromInteger(buffers.length);
		windowName_ += GUI_TEXT("]");
	}

	wCuteText_.SetTitle(windowName_.c_str());
}

Sci_CharacterRange CuteTextBase::GetSelection() {
	Sci_CharacterRange crange;
	crange.cpMin = wEditor_.Call(SCI_GETSELECTIONSTART);
	crange.cpMax = wEditor_.Call(SCI_GETSELECTIONEND);
	return crange;
}

SelectedRange CuteTextBase::GetSelectedRange() {
	return SelectedRange(wEditor_.Call(SCI_GETCURRENTPOS), wEditor_.Call(SCI_GETANCHOR));
}

void CuteTextBase::SetSelection(int anchor, int currentPos) {
	wEditor_.Call(SCI_SETSEL, anchor, currentPos);
}

std::string CuteTextBase::GetCTag() {
	int lengthDoc, selStart, selEnd;
	int mustStop = 0;
	char c;

	lengthDoc = pwFocussed->Call(SCI_GETLENGTH);
	selStart = selEnd = pwFocussed->Call(SCI_GETSELECTIONEND);
	TextReader acc(*pwFocussed);
	while (!mustStop) {
		if (selStart < lengthDoc - 1) {
			selStart++;
			c = acc[selStart];
			if (c == '\r' || c == '\n') {
				mustStop = -1;
			} else if (c == '\t' && ((acc[selStart + 1] == '/' && acc[selStart + 2] == '^') || isdigit(acc[selStart + 1]))) {
				mustStop = 1;
			}
		} else {
			mustStop = -1;
		}
	}
	if (mustStop == 1 && (acc[selStart + 1] == '/' && acc[selStart + 2] == '^')) {	// Found
		selEnd = selStart += 3;
		mustStop = 0;
		while (!mustStop) {
			if (selEnd < lengthDoc - 1) {
				selEnd++;
				c = acc[selEnd];
				if (c == '\r' || c == '\n') {
					mustStop = -1;
				} else if (c == '$' && acc[selEnd + 1] == '/') {
					mustStop = 1;	// Found!
				}

			} else {
				mustStop = -1;
			}
		}
	} else if (mustStop == 1 && isdigit(acc[selStart + 1])) {
		// a Tag can be referenced by line Number also
		selEnd = selStart += 1;
		while ((selEnd < lengthDoc) && isdigit(acc[selEnd])) {
			selEnd++;
		}
	}

	if (selStart < selEnd) {
		return GetRangeString(*pwFocussed, selStart, selEnd);
	} else {
		return std::string();
	}
}

// Default characters that can appear in a word
bool CuteTextBase::iswordcharforsel(char ch) {
	return !strchr("\t\n\r !\"#$%&'()*+,-./:;<=>?@[\\]^`{|}~", ch);
}

// Accept slightly more characters than for a word
// Doesn't accept all valid characters, as they are rarely used in source filenames...
// Accept path separators '/' and '\', extension separator '.', and ':', MS drive unit
// separator, and also used for separating the line number for grep. Same for '(' and ')' for cl.
// Accept '?' and '%' which are used in URL.
bool CuteTextBase::isfilenamecharforsel(char ch) {
	return !strchr("\t\n\r \"$'*,;<>[]^`{|}", ch);
}

bool CuteTextBase::islexerwordcharforsel(char ch) {
	// If there are no word.characters defined for the current file, fall back on the original function
	if (wordCharacters.length())
		return Contains(wordCharacters, ch);
	else
		return iswordcharforsel(ch);
}

void CuteTextBase::HighlightCurrentWord(bool highlight) {
	if (!currentWordHighlight.isEnabled)
		return;
	if (!wEditor_.HasFocus() && !wOutput_.HasFocus()) {
		// Neither text window has focus, possibly app is inactive so do not highlight
		return;
	}
	GUI::ScintillaWindow &wCurrent = wOutput_.HasFocus() ? wOutput_ : wEditor_;
	// Remove old indicators if any exist.
	wCurrent.Call(SCI_SETINDICATORCURRENT, kIndicatorHighlightCurrentWord);
	const int lenDoc = wCurrent.Call(SCI_GETLENGTH);
	wCurrent.Call(SCI_INDICATORCLEARRANGE, 0, lenDoc);
	if (!highlight)
		return;
	// Get start & end selection.
	int selStart = wCurrent.Call(SCI_GETSELECTIONSTART);
	int selEnd = wCurrent.Call(SCI_GETSELECTIONEND);
	const bool noUserSelection = selStart == selEnd;
	std::string sWordToFind = RangeExtendAndGrab(wCurrent, selStart, selEnd,
	        &CuteTextBase::islexerwordcharforsel);
	if (sWordToFind.length() == 0 || (sWordToFind.find_first_of("\n\r ") != std::string::npos))
		return; // No highlight when no selection or multi-lines selection.
	if (noUserSelection && currentWordHighlight.statesOfDelay == currentWordHighlight.kNoDelay) {
		// Manage delay before highlight when no user selection but there is word at the caret.
		currentWordHighlight.statesOfDelay = currentWordHighlight.kDelay;
		// Reset timer
		currentWordHighlight.elapsedTimes.Duration(true);
		return;
	}
	// Get style of the current word to highlight only word with same style.
	int selectedStyle = wCurrent.Call(SCI_GETSTYLEAT, selStart);
	if (!currentWordHighlight.isOnlyWithSameStyle)
		selectedStyle = -1;

	// Manage word with DBCS.
	const std::string wordToFind = EncodeString(sWordToFind);

	matchMarker.StartMatch(&wCurrent, wordToFind,
		SCFIND_MATCHCASE | SCFIND_WHOLEWORD, selectedStyle,
		kIndicatorHighlightCurrentWord, -1);
	SetIdler(true);
}

std::string CuteTextBase::GetRangeString(GUI::ScintillaWindow &win, int selStart, int selEnd) {
	if (selStart == selEnd) {
		return std::string();
	} else {
		std::string sel(selEnd - selStart, '\0');
		win.Call(SCI_SETTARGETRANGE, selStart, selEnd);
		win.CallPointer(SCI_GETTARGETTEXT, 0, &sel[0]);
		return sel;
	}
}

std::string CuteTextBase::GetRangeInUIEncoding(GUI::ScintillaWindow &win, int selStart, int selEnd) {
	return GetRangeString(win, selStart, selEnd);
}

std::string CuteTextBase::GetLine(GUI::ScintillaWindow &win, int line) {
	const int lineStart = win.Call(SCI_POSITIONFROMLINE, line);
	const int lineEnd = win.Call(SCI_GETLINEENDPOSITION, line);
	if ((lineStart < 0) || (lineEnd < 0))
		return std::string();
	return GetRangeString(win, lineStart, lineEnd);
}

void CuteTextBase::RangeExtend(
    GUI::ScintillaWindow &wCurrent,
    int &selStart,
    int &selEnd,
    bool (CuteTextBase::*ischarforsel)(char ch)) {	///< Function returning @c true if the given char. is part of the selection.
	if (selStart == selEnd && ischarforsel) {
		// Empty range and have a function to extend it
		const int lengthDoc = wCurrent.Call(SCI_GETLENGTH);
		TextReader acc(wCurrent);
		// Try and find a word at the caret
		// On the left...
		while ((selStart > 0) && ((this->*ischarforsel)(acc[selStart - 1]))) {
			selStart--;
		}
		// and on the right
		while ((selEnd < lengthDoc) && ((this->*ischarforsel)(acc[selEnd]))) {
			selEnd++;
		}
	}
}

std::string CuteTextBase::RangeExtendAndGrab(
    GUI::ScintillaWindow &wCurrent,
    int &selStart,
    int &selEnd,
    bool (CuteTextBase::*ischarforsel)(char ch),	///< Function returning @c true if the given char. is part of the selection.
    bool stripEol /*=true*/) {

	RangeExtend(wCurrent, selStart, selEnd, ischarforsel);
	std::string selected;
	if (selStart != selEnd) {
		selected = GetRangeInUIEncoding(wCurrent, selStart, selEnd);
	}
	if (stripEol) {
		// Change whole line selected but normally end of line characters not wanted.
		// Remove possible terminating \r, \n, or \r\n.
		const size_t sellen = selected.length();
		if (sellen >= 2 && (selected[sellen - 2] == '\r' && selected[sellen - 1] == '\n')) {
			selected.erase(sellen - 2);
		} else if (sellen >= 1 && (selected[sellen - 1] == '\r' || selected[sellen - 1] == '\n')) {
			selected.erase(sellen - 1);
		}
	}

	return selected;
}

/**
 * If there is selected text, either in the editor or the output pane,
 * put the selection in the @a sel buffer, up to @a len characters.
 * Otherwise, try and select characters around the caret, as long as they are OK
 * for the @a ischarforsel function.
 * Remove the last two character controls from the result, as they are likely
 * to be CR and/or LF.
 */
std::string CuteTextBase::SelectionExtend(
    bool (CuteTextBase::*ischarforsel)(char ch),	///< Function returning @c true if the given char. is part of the selection.
    bool stripEol /*=true*/) {

	int selStart = pwFocussed->Call(SCI_GETSELECTIONSTART);
	int selEnd = pwFocussed->Call(SCI_GETSELECTIONEND);
	return RangeExtendAndGrab(*pwFocussed, selStart, selEnd, ischarforsel, stripEol);
}

std::string CuteTextBase::SelectionWord(bool stripEol /*=true*/) {
	return SelectionExtend(&CuteTextBase::islexerwordcharforsel, stripEol);
}

std::string CuteTextBase::SelectionFilename() {
	return SelectionExtend(&CuteTextBase::isfilenamecharforsel);
}

void CuteTextBase::SelectionIntoProperties() {
	std::string currentSelection = SelectionExtend(0, false);
	props.Set("CurrentSelection", currentSelection.c_str());

	std::string word = SelectionWord();
	props.Set("CurrentWord", word.c_str());

	const int selStart = CallFocused(SCI_GETSELECTIONSTART);
	const int selEnd = CallFocused(SCI_GETSELECTIONEND);
	props.SetInteger("SelectionStartLine", CallFocused(SCI_LINEFROMPOSITION, selStart) + 1);
	props.SetInteger("SelectionStartColumn", CallFocused(SCI_GETCOLUMN, selStart) + 1);
	props.SetInteger("SelectionEndLine", CallFocused(SCI_LINEFROMPOSITION, selEnd) + 1);
	props.SetInteger("SelectionEndColumn", CallFocused(SCI_GETCOLUMN, selEnd) + 1);
}

void CuteTextBase::SelectionIntoFind(bool stripEol /*=true*/) {
	std::string sel = SelectionWord(stripEol);
	if (sel.length() && (sel.find_first_of("\r\n") == std::string::npos)) {
		// The selection does not include a new line, so is likely to be
		// the expression to search...
		findWhat = sel;
		if (unSlash) {
			std::string slashedFind = Slash(findWhat, false);
			findWhat = slashedFind;
		}
	}
	// else findWhat remains the same as last time.
}

void CuteTextBase::SelectionAdd(AddSelection add) {
	int flags = 0;
	if (!pwFocussed->Call(SCI_GETSELECTIONEMPTY)) {
		// If selection is word then match as word.
		if (pwFocussed->Call(SCI_ISRANGEWORD, pwFocussed->Call(SCI_GETSELECTIONSTART),
			pwFocussed->Call(SCI_GETSELECTIONEND)))
			flags = SCFIND_WHOLEWORD;
	}
	pwFocussed->Call(SCI_TARGETWHOLEDOCUMENT);
	pwFocussed->Call(SCI_SETSEARCHFLAGS, flags);
	if (add == kAddNext) {
		pwFocussed->Call(SCI_MULTIPLESELECTADDNEXT);
	} else {
		if (pwFocussed->Call(SCI_GETSELECTIONEMPTY)) {
			pwFocussed->Call(SCI_MULTIPLESELECTADDNEXT);
		}
		pwFocussed->Call(SCI_MULTIPLESELECTADDEACH);
	}
}

std::string CuteTextBase::EncodeString(const std::string &s) {
	return s;
}

static std::string UnSlashAsNeeded(const std::string &s, bool escapes, bool regularExpression) {
	if (escapes) {
		if (regularExpression) {
			// For regular expressions, the only escape sequences allowed start with \0
			// Other sequences, like \t, are handled by the RE engine.
			return UnSlashLowOctalString(s.c_str());
		} else {
			// C style escapes allowed
			return UnSlashString(s.c_str());
		}
	} else {
		return s;
	}
}

void CuteTextBase::RemoveFindMarks() {
	findMarker.Stop();	// Cancel ongoing background find
	if (CurrentBuffer()->findMarks != Buffer::kFmNone) {
		wEditor_.Call(SCI_SETINDICATORCURRENT, kIndicatorMatch);
		wEditor_.Call(SCI_INDICATORCLEARRANGE, 0, LengthDocument());
		CurrentBuffer()->findMarks = Buffer::kFmNone;
	}
	wEditor_.Call(SCI_ANNOTATIONCLEARALL);
}

int CuteTextBase::SearchFlags(bool regularExpressions) const {
	return (wholeWord ? SCFIND_WHOLEWORD : 0) |
	        (matchCase ? SCFIND_MATCHCASE : 0) |
	        (regularExpressions ? SCFIND_REGEXP : 0) |
	        (props.GetInt("find.replace.regexp.posix") ? SCFIND_POSIX : 0) |
		(props.GetInt("find.replace.regexp.cpp11") ? SCFIND_CXX11REGEX : 0);
}

void CuteTextBase::MarkAll(MarkPurpose purpose) {
	RemoveFindMarks();
	wEditor_.Call(SCI_SETINDICATORCURRENT, kIndicatorMatch);
	if (purpose == kMarkIncremental) {
		CurrentBuffer()->findMarks = Buffer::kFmTemporary;
		SetOneIndicator(wEditor_, kIndicatorMatch,
			IndicatorDefinition(props.GetString("find.indicator.incremental")));
	} else {
		CurrentBuffer()->findMarks = Buffer::kFmMarked;
		std::string findIndicatorString = props.GetString("find.mark.indicator");
		IndicatorDefinition findIndicator(findIndicatorString);
		if (!findIndicatorString.length()) {
			findIndicator.style = INDIC_ROUNDBOX;
			std::string findMark = props.GetString("find.mark");
			if (findMark.length())
				findIndicator.colour = ColourFromString(findMark);
			findIndicator.fillAlpha = alphaIndicator;
			findIndicator.under = underIndicator;
		}
		SetOneIndicator(wEditor_, kIndicatorMatch, findIndicator);
	}

	const std::string findTarget = UnSlashAsNeeded(EncodeString(findWhat), unSlash, regExp);
	if (findTarget.length() == 0) {
		return;
	}

	findMarker.StartMatch(&wEditor_, findTarget,
		SearchFlags(regExp), -1,
		kIndicatorMatch, (purpose == kMarkWithBookMarks) ? kMarkerBookmark : -1);
	SetIdler(true);
}

int CuteTextBase::IncrementSearchMode() {
	FindIncrement();
	return 0;
}

void CuteTextBase::FailedSaveMessageBox(const FilePath &filePathSaving) {
	const GUI::gui_string msg = LocaliseMessage(
		"Could not save file \"^0\".", filePathSaving.AsInternal());
	WindowMessageBox(wCuteText_, msg);
}

bool CuteTextBase::FindReplaceAdvanced() const {
	return props.GetInt("find.replace.advanced");
}

int CuteTextBase::FindInTarget(const std::string &findWhatText, int startPosition, int endPosition) {
	const size_t lenFind = findWhatText.length();
	wEditor_.Call(SCI_SETTARGETSTART, startPosition);
	wEditor_.Call(SCI_SETTARGETEND, endPosition);
	int posFind = wEditor_.CallString(SCI_SEARCHINTARGET, lenFind, findWhatText.c_str());
	while (findInStyle && posFind != -1 && findStyle != wEditor_.Call(SCI_GETSTYLEAT, posFind)) {
		if (startPosition < endPosition) {
			wEditor_.Call(SCI_SETTARGETSTART, posFind + 1);
			wEditor_.Call(SCI_SETTARGETEND, endPosition);
		} else {
			wEditor_.Call(SCI_SETTARGETSTART, startPosition);
			wEditor_.Call(SCI_SETTARGETEND, posFind + 1);
		}
		posFind = wEditor_.CallString(SCI_SEARCHINTARGET, lenFind, findWhatText.c_str());
	}
	return posFind;
}

void CuteTextBase::SetFindText(const char *sFind) {
	findWhat = sFind;
	props.Set("find.what", findWhat.c_str());
}

void CuteTextBase::SetFind(const char *sFind) {
	SetFindText(sFind);
	InsertFindInMemory();
}

bool CuteTextBase::FindHasText() const {
	return findWhat[0];
}

void CuteTextBase::SetReplace(const char *sReplace) {
	replaceWhat = sReplace;
	memReplaces.Insert(replaceWhat.c_str());
}

void CuteTextBase::SetCaretAsStart() {
	const Sci_CharacterRange cr = GetSelection();
	searchStartPosition = static_cast<int>(cr.cpMin);
}

void CuteTextBase::MoveBack() {
	SetSelection(searchStartPosition, searchStartPosition);
}

void CuteTextBase::ScrollEditorIfNeeded() {
	GUI::Point ptCaret;
	const int caret = wEditor_.Call(SCI_GETCURRENTPOS);
	ptCaret.x = wEditor_.Call(SCI_POINTXFROMPOSITION, 0, caret);
	ptCaret.y = wEditor_.Call(SCI_POINTYFROMPOSITION, 0, caret);
	ptCaret.y += wEditor_.Call(SCI_TEXTHEIGHT, 0, 0) - 1;

	const GUI::Rectangle rcEditor = wEditor_.GetClientPosition();
	if (!rcEditor.Contains(ptCaret))
		wEditor_.Call(SCI_SCROLLCARET);
}

int CuteTextBase::FindNext(bool reverseDirection, bool showWarnings, bool allowRegExp) {
	if (findWhat.length() == 0) {
		Find();
		return -1;
	}
	const std::string findTarget = UnSlashAsNeeded(EncodeString(findWhat), unSlash, regExp);
	if (findTarget.length() == 0)
		return -1;

	const Sci_CharacterRange cr = GetSelection();
	int startPosition = static_cast<int>(cr.cpMax);
	int endPosition = LengthDocument();
	if (reverseDirection) {
		startPosition = static_cast<int>(cr.cpMin);
		endPosition = 0;
	}

	wEditor_.Call(SCI_SETSEARCHFLAGS, SearchFlags(allowRegExp && regExp));
	int posFind = FindInTarget(findTarget, startPosition, endPosition);
	if (posFind == -1 && wrapFind) {
		// Failed to find in indicated direction
		// so search from the beginning (forward) or from the end (reverse)
		// unless wrapFind is false
		if (reverseDirection) {
			startPosition = LengthDocument();
			endPosition = 0;
		} else {
			startPosition = 0;
			endPosition = LengthDocument();
		}
		posFind = FindInTarget(findTarget, startPosition, endPosition);
		WarnUser(kWarnFindWrapped);
	}
	if (posFind == -1) {
		havefound = false;
		failedfind = true;
		if (showWarnings) {
			WarnUser(kWarnNotFound);
			FindMessageBox("Can not find the string '^0'.",
			        &findWhat);
		}
	} else {
		havefound = true;
		failedfind = false;
		const int start = wEditor_.Call(SCI_GETTARGETSTART);
		const int end = wEditor_.Call(SCI_GETTARGETEND);
		// Ensure found text is styled so that caret will be made visible.
		const int endStyled = wEditor_.Call(SCI_GETENDSTYLED);
		if (endStyled < end)
			wEditor_.Call(SCI_COLOURISE, endStyled,
				wEditor_.LineStart(wEditor_.LineFromPosition(end) + 1));
		EnsureRangeVisible(wEditor_, start, end);
		wEditor_.Call(SCI_SCROLLRANGE, start, end);
		wEditor_.Call(SCI_SETTARGETRANGE, start, end);
		SetSelection(start, end);
		if (!replacing && (closeFind != CloseFind::kClosePrevent)) {
			DestroyFindReplace();
		}
	}
	return posFind;
}

void CuteTextBase::HideMatch() {
}

void CuteTextBase::ReplaceOnce(bool showWarnings) {
	if (!FindHasText())
		return;

	bool haveWarned = false;
	if (!havefound) {
		const Sci_CharacterRange crange = GetSelection();
		SetSelection(static_cast<int>(crange.cpMin), static_cast<int>(crange.cpMin));
		FindNext(false);
		haveWarned = !havefound;
	}

	if (havefound) {
		const std::string replaceTarget = UnSlashAsNeeded(EncodeString(replaceWhat), unSlash, regExp);
		const Sci_CharacterRange cr = GetSelection();
		wEditor_.Call(SCI_SETTARGETSTART, cr.cpMin);
		wEditor_.Call(SCI_SETTARGETEND, cr.cpMax);
		int lenReplaced = static_cast<int>(replaceTarget.length());
		if (regExp)
			lenReplaced = wEditor_.CallString(SCI_REPLACETARGETRE, replaceTarget.length(), replaceTarget.c_str());
		else	// Allow \0 in replacement
			wEditor_.CallString(SCI_REPLACETARGET, replaceTarget.length(), replaceTarget.c_str());
		SetSelection(static_cast<int>(cr.cpMin) + lenReplaced, static_cast<int>(cr.cpMin));
		havefound = false;
	}

	FindNext(false, showWarnings && !haveWarned);
}

int CuteTextBase::DoReplaceAll(bool inSelection) {
	const std::string findTarget = UnSlashAsNeeded(EncodeString(findWhat), unSlash, regExp);
	if (findTarget.length() == 0) {
		return -1;
	}

	const Sci_CharacterRange cr = GetSelection();
	int startPosition = static_cast<int>(cr.cpMin);
	int endPosition = static_cast<int>(cr.cpMax);
	const int countSelections = wEditor_.Call(SCI_GETSELECTIONS);
	if (inSelection) {
		const int selType = wEditor_.Call(SCI_GETSELECTIONMODE);
		if (selType == SC_SEL_LINES) {
			// Take care to replace in whole lines
			const int startLine = wEditor_.Call(SCI_LINEFROMPOSITION, startPosition);
			startPosition = wEditor_.Call(SCI_POSITIONFROMLINE, startLine);
			const int endLine = wEditor_.Call(SCI_LINEFROMPOSITION, endPosition);
			endPosition = wEditor_.Call(SCI_POSITIONFROMLINE, endLine + 1);
		} else {
			for (int i=0; i<countSelections; i++) {
				startPosition = Minimum(startPosition, wEditor_.Call(SCI_GETSELECTIONNSTART, i));
				endPosition = Maximum(endPosition, wEditor_.Call(SCI_GETSELECTIONNEND, i));
			}
		}
		if (startPosition == endPosition) {
			return -2;
		}
	} else {
		endPosition = LengthDocument();
		if (wrapFind) {
			// Whole document
			startPosition = 0;
		}
		// If not wrapFind, replace all only from caret to end of document
	}

	const std::string replaceTarget = UnSlashAsNeeded(EncodeString(replaceWhat), unSlash, regExp);
	wEditor_.Call(SCI_SETSEARCHFLAGS, SearchFlags(regExp));
	int posFind = FindInTarget(findTarget, startPosition, endPosition);
	if ((posFind != -1) && (posFind <= endPosition)) {
		int lastMatch = posFind;
		int replacements = 0;
		wEditor_.Call(SCI_BEGINUNDOACTION);
		// Replacement loop
		while (posFind != -1) {
			const int lenTarget = wEditor_.Call(SCI_GETTARGETEND) - wEditor_.Call(SCI_GETTARGETSTART);
			if (inSelection && countSelections > 1) {
				// We must check that the found target is entirely inside a selection
				bool insideASelection = false;
				for (int i=0; i<countSelections && !insideASelection; i++) {
					const int startPos= wEditor_.Call(SCI_GETSELECTIONNSTART, i);
					const int endPos = wEditor_.Call(SCI_GETSELECTIONNEND, i);
					if (posFind >= startPos && posFind + lenTarget <= endPos)
						insideASelection = true;
				}
				if (!insideASelection) {
					// Found target is totally or partly outside the selections
					lastMatch = posFind + 1;
					if (lastMatch >= endPosition) {
						// Run off the end of the document/selection with an empty match
						posFind = -1;
					} else {
						posFind = FindInTarget(findTarget, lastMatch, endPosition);
					}
					continue;	// No replacement
				}
			}
			int lenReplaced = static_cast<int>(replaceTarget.length());
			if (regExp) {
				lenReplaced = wEditor_.CallString(SCI_REPLACETARGETRE, replaceTarget.length(), replaceTarget.c_str());
			} else {
				wEditor_.CallString(SCI_REPLACETARGET, replaceTarget.length(), replaceTarget.c_str());
			}
			// Modify for change caused by replacement
			endPosition += lenReplaced - lenTarget;
			// For the special cases of start of line and end of line
			// something better could be done but there are too many special cases
			lastMatch = posFind + lenReplaced;
			if (lenTarget <= 0) {
				lastMatch = wEditor_.Call(SCI_POSITIONAFTER, lastMatch);
			}
			if (lastMatch >= endPosition) {
				// Run off the end of the document/selection with an empty match
				posFind = -1;
			} else {
				posFind = FindInTarget(findTarget, lastMatch, endPosition);
			}
			replacements++;
		}
		if (inSelection) {
			if (countSelections == 1)
				SetSelection(startPosition, endPosition);
		} else {
			SetSelection(lastMatch, lastMatch);
		}
		wEditor_.Call(SCI_ENDUNDOACTION);
		return replacements;
	}
	return 0;
}

int CuteTextBase::ReplaceAll(bool inSelection) {
	const int replacements = DoReplaceAll(inSelection);
	props.SetInteger("Replacements", (replacements > 0 ? replacements : 0));
	UpdateStatusBar(false);
	if (replacements == -1) {
		FindMessageBox(
		    inSelection ?
		    "Find string must not be empty for 'Replace in Selection' command." :
		    "Find string must not be empty for 'Replace All' command.");
	} else if (replacements == -2) {
		FindMessageBox(
		    "Selection must not be empty for 'Replace in Selection' command.");
	} else if (replacements == 0) {
		FindMessageBox(
		    "No replacements because string '^0' was not present.", &findWhat);
	}
	return replacements;
}

int CuteTextBase::ReplaceInBuffers() {
	const int currentBuffer = buffers.Current();
	int replacements = 0;
	for (int i = 0; i < buffers.length; i++) {
		SetDocumentAt(i);
		replacements += DoReplaceAll(false);
		if (i == 0 && replacements < 0) {
			FindMessageBox(
			    "Find string must not be empty for 'Replace in Buffers' command.");
			break;
		}
	}
	SetDocumentAt(currentBuffer);
	props.SetInteger("Replacements", replacements);
	UpdateStatusBar(false);
	if (replacements == 0) {
		FindMessageBox(
		    "No replacements because string '^0' was not present.", &findWhat);
	}
	return replacements;
}

void CuteTextBase::UIClosed() {
	if (CurrentBuffer()->findMarks == Buffer::kFmTemporary) {
		RemoveFindMarks();
	}
}

void CuteTextBase::UIHasFocus() {
}

void CuteTextBase::OutputAppendString(const char *s, int len) {
	if (len == -1)
		len = static_cast<int>(strlen(s));
	wOutput_.CallString(SCI_APPENDTEXT, len, s);
	if (scrollOutput) {
		const int line = wOutput_.Call(SCI_GETLINECOUNT, 0, 0);
		const int lineStart = wOutput_.Call(SCI_POSITIONFROMLINE, line);
		wOutput_.Call(SCI_GOTOPOS, lineStart);
	}
}

void CuteTextBase::OutputAppendStringSynchronised(const char *s, int len) {
	if (len == -1)
		len = static_cast<int>(strlen(s));
	wOutput_.Send(SCI_APPENDTEXT, len, SptrFromString(s));
	if (scrollOutput) {
		const sptr_t line = wOutput_.Send(SCI_GETLINECOUNT);
		const sptr_t lineStart = wOutput_.Send(SCI_POSITIONFROMLINE, line);
		wOutput_.Send(SCI_GOTOPOS, lineStart);
	}
}

void CuteTextBase::Execute() {
	props.Set("CurrentMessage", "");
	dirNameForExecute_ = FilePath();
	bool displayParameterDialog = false;
	int ic;
	parameterisedCommand_ = "";
	for (ic = 0; ic < jobQueue.commandMax; ic++) {
		if (StartsWith(jobQueue.jobQueue[ic].command, "*")) {
			displayParameterDialog = true;
			jobQueue.jobQueue[ic].command.erase(0, 1);
			parameterisedCommand_ = jobQueue.jobQueue[ic].command;
		}
		if (jobQueue.jobQueue[ic].directory.IsSet()) {
			dirNameForExecute_ = jobQueue.jobQueue[ic].directory;
		}
	}
	if (displayParameterDialog) {
		if (!ParametersDialog(true)) {
			jobQueue.ClearJobs();
			return;
		}
	} else {
		ParamGrab();
	}
	for (ic = 0; ic < jobQueue.commandMax; ic++) {
		if (jobQueue.jobQueue[ic].jobType != jobGrep) {
			jobQueue.jobQueue[ic].command = props.Expand(jobQueue.jobQueue[ic].command);
		}
	}

	if (jobQueue.ClearBeforeExecute()) {
		wOutput_.Call(SCI_CLEARALL);
	}

	wOutput_.Call(SCI_MARKERDELETEALL, static_cast<uptr_t>(-1));
	wEditor_.Call(SCI_MARKERDELETEALL, 0);
	// Ensure the output pane is visible
	if (jobQueue.ShowOutputPane()) {
		SetOutputVisibility(true);
	}

	jobQueue.cancelFlag = 0L;
	if (jobQueue.HasCommandToRun()) {
		jobQueue.SetExecuting(true);
	}
	CheckMenus();
	dirNameAtExecute_ = filePath_.Directory();
}

void CuteTextBase::SetOutputVisibility(bool show) {
	if (show) {
		if (heightOutput <= 0) {
			if (previousHeightOutput < 20) {
				if (splitVertical)
					heightOutput = NormaliseSplit(300);
				else
					heightOutput = NormaliseSplit(100);
				previousHeightOutput = heightOutput;
			} else {
				heightOutput = NormaliseSplit(previousHeightOutput);
			}
		}
	} else {
		if (heightOutput > 0) {
			heightOutput = NormaliseSplit(0);
			WindowSetFocus(wEditor_);
		}
	}
	SizeSubWindows();
	Redraw();
}

// Background threads that are send text to the output pane want it to be made visible.
// Derived methods for each platform may perform thread synchronization.
void CuteTextBase::ShowOutputOnMainThread() {
	SetOutputVisibility(true);
}

void CuteTextBase::ToggleOutputVisible() {
	SetOutputVisibility(heightOutput <= 0);
}

void CuteTextBase::BookmarkAdd(int lineno) {
	if (lineno == -1)
		lineno = GetCurrentLineNumber();
	if (!BookmarkPresent(lineno))
		wEditor_.Call(SCI_MARKERADD, lineno, kMarkerBookmark);
}

void CuteTextBase::BookmarkDelete(int lineno) {
	if (lineno == -1)
		lineno = GetCurrentLineNumber();
	if (BookmarkPresent(lineno))
		wEditor_.Call(SCI_MARKERDELETE, lineno, kMarkerBookmark);
}

bool CuteTextBase::BookmarkPresent(int lineno) {
	if (lineno == -1)
		lineno = GetCurrentLineNumber();
	const int state = wEditor_.Call(SCI_MARKERGET, lineno);
	return state & (1 << kMarkerBookmark);
}

void CuteTextBase::BookmarkToggle(int lineno) {
	if (lineno == -1)
		lineno = GetCurrentLineNumber();
	if (BookmarkPresent(lineno)) {
		while (BookmarkPresent(lineno)) {
			BookmarkDelete(lineno);
		}
	} else {
		BookmarkAdd(lineno);
	}
}

void CuteTextBase::BookmarkNext(bool forwardScan, bool select) {
	const int lineno = GetCurrentLineNumber();
	int sci_marker = SCI_MARKERNEXT;
	int lineStart = lineno + 1;	//Scan starting from next line
	int lineRetry = 0;				//If not found, try from the beginning
	const int anchor = wEditor_.Call(SCI_GETANCHOR);
	if (!forwardScan) {
		lineStart = lineno - 1;		//Scan starting from previous line
		lineRetry = wEditor_.Call(SCI_GETLINECOUNT, 0, 0L);	//If not found, try from the end
		sci_marker = SCI_MARKERPREVIOUS;
	}
	int nextLine = wEditor_.Call(sci_marker, lineStart, 1 << kMarkerBookmark);
	if (nextLine < 0)
		nextLine = wEditor_.Call(sci_marker, lineRetry, 1 << kMarkerBookmark);
	if (nextLine < 0 || nextLine == lineno)	// No bookmark (of the given type) or only one, and already on it
		WarnUser(kWarnNoOtherBookmark);
	else {
		GotoLineEnsureVisible(nextLine);
		if (select) {
			wEditor_.Call(SCI_SETANCHOR, anchor);
		}
	}
}

void CuteTextBase::BookmarkSelectAll() {
	std::vector<int> bookmarks;
	int lineBookmark = -1;
	while ((lineBookmark = wEditor_.Call(SCI_MARKERNEXT, lineBookmark + 1, 1 << kMarkerBookmark)) >= 0) {
		bookmarks.push_back(lineBookmark);
	}
	for (size_t i = 0; i < bookmarks.size(); i++) {
		Sci_CharacterRange crange = {
			wEditor_.Call(SCI_POSITIONFROMLINE, bookmarks[i]),
			wEditor_.Call(SCI_POSITIONFROMLINE, bookmarks[i] + 1)
		};
		if (i == 0) {
			wEditor_.Call(SCI_SETSELECTION, crange.cpMax, crange.cpMin);
		} else {
			wEditor_.Call(SCI_ADDSELECTION, crange.cpMax, crange.cpMin);
		}
	}
}

GUI::Rectangle CuteTextBase::GetClientRectangle() {
	return wContent_.GetClientPosition();
}

void CuteTextBase::Redraw() {
	wCuteText_.InvalidateAll();
	wEditor_.InvalidateAll();
	wOutput_.InvalidateAll();
}

std::string CuteTextBase::GetNearestWords(const char *wordStart, size_t searchLen,
		const char *separators, bool ignoreCase /*=false*/, bool exactLen /*=false*/) {
	std::string words;
	while (words.empty() && *separators) {
		words = apis_.GetNearestWords(wordStart, searchLen, ignoreCase, *separators, exactLen);
		separators++;
	}
	return words;
}

void CuteTextBase::FillFunctionDefinition(int pos /*= -1*/) {
	if (pos > 0) {
		lastPosCallTip = pos;
	}
	if (apis_) {
		std::string words = GetNearestWords(currentCallTipWord.c_str(), currentCallTipWord.length(),
			calltipParametersStart.c_str(), callTipIgnoreCase, true);
		if (words.empty())
			return;
		// Counts how many call tips
		maxCallTips = static_cast<int>(std::count(words.begin(), words.end(), ' ') + 1);

		// Should get current api definition
		std::string word = apis_.GetNearestWord(currentCallTipWord.c_str(), currentCallTipWord.length(),
		        callTipIgnoreCase, calltipWordCharacters, currentCallTip);
		if (word.length()) {
			functionDefinition_ = word;
			if (maxCallTips > 1) {
				functionDefinition_.insert(0, "\001");
			}

			if (calltipEndDefinition != "") {
				const size_t posEndDef = functionDefinition_.find(calltipEndDefinition.c_str());
				if (maxCallTips > 1) {
					if (posEndDef != std::string::npos) {
						functionDefinition_.insert(posEndDef + calltipEndDefinition.length(), "\n\002");
					} else {
						functionDefinition_.append("\n\002");
					}
				} else {
					if (posEndDef != std::string::npos) {
						functionDefinition_.insert(posEndDef + calltipEndDefinition.length(), "\n");
					}
				}
			} else if (maxCallTips > 1) {
				functionDefinition_.insert(1, "\002");
			}

			std::string definitionForDisplay;
			if (callTipUseEscapes) {
				definitionForDisplay = UnSlashString(functionDefinition_.c_str());
			} else {
				definitionForDisplay = functionDefinition_;
			}

			wEditor_.CallString(SCI_CALLTIPSHOW, lastPosCallTip - currentCallTipWord.length(), definitionForDisplay.c_str());
			ContinueCallTip();
		}
	}
}

bool CuteTextBase::StartCallTip() {
	currentCallTip = 0;
	currentCallTipWord = "";
	std::string line = GetCurrentLine();
	int current = GetCaretInLine();
	int pos = wEditor_.Call(SCI_GETCURRENTPOS);
	do {
		int braces = 0;
		while (current > 0 && (braces || !Contains(calltipParametersStart, line[current - 1]))) {
			if (Contains(calltipParametersStart, line[current - 1]))
				braces--;
			else if (Contains(calltipParametersEnd, line[current - 1]))
				braces++;
			current--;
			pos--;
		}
		if (current > 0) {
			current--;
			pos--;
		} else
			break;
		while (current > 0 && isspacechar(line[current - 1])) {
			current--;
			pos--;
		}
	} while (current > 0 && !Contains(calltipWordCharacters, line[current - 1]));
	if (current <= 0)
		return true;

	startCalltipWord = current - 1;
	while (startCalltipWord > 0 &&
	        Contains(calltipWordCharacters, line[startCalltipWord - 1])) {
		startCalltipWord--;
	}

	line.at(current) = '\0';
	currentCallTipWord = line.c_str() + startCalltipWord;
	functionDefinition_ = "";
	FillFunctionDefinition(pos);
	return true;
}

void CuteTextBase::ContinueCallTip() {
	std::string line = GetCurrentLine();
	const int current = GetCaretInLine();

	int braces = 0;
	int commas = 0;
	for (int i = startCalltipWord; i < current; i++) {
		if (Contains(calltipParametersStart, line[i]))
			braces++;
		else if (Contains(calltipParametersEnd, line[i]) && braces > 0)
			braces--;
		else if (braces == 1 && Contains(calltipParametersSeparators, line[i]))
			commas++;
	}

	size_t startHighlight = 0;
	while ((startHighlight < functionDefinition_.length()) && !Contains(calltipParametersStart, functionDefinition_[startHighlight]))
		startHighlight++;
	if ((startHighlight < functionDefinition_.length()) && Contains(calltipParametersStart, functionDefinition_[startHighlight]))
		startHighlight++;
	while ((startHighlight < functionDefinition_.length()) && commas > 0) {
		if (Contains(calltipParametersSeparators, functionDefinition_[startHighlight]))
			commas--;
		// If it reached the end of the argument list it means that the user typed in more
		// arguments than the ones listed in the calltip
		if (Contains(calltipParametersEnd, functionDefinition_[startHighlight]))
			commas = 0;
		else
			startHighlight++;
	}
	if ((startHighlight < functionDefinition_.length()) && Contains(calltipParametersSeparators, functionDefinition_[startHighlight]))
		startHighlight++;
	size_t endHighlight = startHighlight;
	while ((endHighlight < functionDefinition_.length()) && !Contains(calltipParametersSeparators, functionDefinition_[endHighlight]) && !Contains(calltipParametersEnd, functionDefinition_[endHighlight]))
		endHighlight++;
	if (callTipUseEscapes) {
		std::string sPreHighlight = functionDefinition_.substr(0, startHighlight);
		std::vector<char> vPreHighlight(sPreHighlight.c_str(), sPreHighlight.c_str() + sPreHighlight.length() + 1);
		const int unslashedStartHighlight = UnSlash(&vPreHighlight[0]);

		int unslashedEndHighlight = unslashedStartHighlight;
		if (startHighlight < endHighlight) {
			std::string sHighlight = functionDefinition_.substr(startHighlight, endHighlight - startHighlight);
			std::vector<char> vHighlight(sHighlight.c_str(), sHighlight.c_str() + sHighlight.length() + 1);
			unslashedEndHighlight = unslashedStartHighlight + UnSlash(&vHighlight[0]);
		}

		startHighlight = unslashedStartHighlight;
		endHighlight = unslashedEndHighlight;
	}

	wEditor_.Call(SCI_CALLTIPSETHLT, startHighlight, endHighlight);
}

void CuteTextBase::EliminateDuplicateWords(std::string &words) {
	std::set<std::string> wordSet;
	std::vector<char> wordsOut(words.length() + 1);
	char *wordsWrite = &wordsOut[0];

	const char *wordCurrent = words.c_str();
	while (*wordCurrent) {
		const char *afterWord = strchr(wordCurrent, ' ');
		if (!afterWord)
			afterWord = wordCurrent + strlen(wordCurrent);
		std::string word(wordCurrent, afterWord);
		if (wordSet.count(word) == 0) {
			wordSet.insert(word);
			if (wordsWrite != &wordsOut[0])
				*wordsWrite++ = ' ';
			strcpy(wordsWrite, word.c_str());
			wordsWrite += word.length();
		}
		wordCurrent = afterWord;
		if (*wordCurrent)
			wordCurrent++;
	}

	*wordsWrite = '\0';
	words = &wordsOut[0];
}

bool CuteTextBase::StartAutoComplete() {
	std::string line = GetCurrentLine();
	const int current = GetCaretInLine();

	int startword = current;

	while ((startword > 0) &&
	        (Contains(calltipWordCharacters, line[startword - 1]) ||
		Contains(autoCompleteStartCharacters, line[startword - 1]))) {
		startword--;
	}

	std::string root = line.substr(startword, current - startword);
	if (apis_) {
		std::string words = GetNearestWords(root.c_str(), root.length(),
			calltipParametersStart.c_str(), autoCompleteIgnoreCase);
		if (words.length()) {
			EliminateDuplicateWords(words);
			wEditor_.Call(SCI_AUTOCSETSEPARATOR, ' ');
			wEditor_.CallString(SCI_AUTOCSHOW, root.length(), words.c_str());
		}
	}
	return true;
}

bool CuteTextBase::StartAutoCompleteWord(bool onlyOneWord) {
	const std::string line = GetCurrentLine();
	const int current = GetCaretInLine();

	int startword = current;
	// Autocompletion of pure numbers is mostly an annoyance
	bool allNumber = true;
	while (startword > 0 && Contains(wordCharacters, line[startword - 1])) {
		startword--;
		if (line[startword] < '0' || line[startword] > '9') {
			allNumber = false;
		}
	}
	if (startword == current || allNumber)
		return true;
	const std::string root = line.substr(startword, current - startword);
	const int doclen = LengthDocument();
	Sci_TextToFind ft = {{0, 0}, 0, {0, 0}};
	ft.lpstrText = root.c_str();
	ft.chrg.cpMin = 0;
	ft.chrg.cpMax = doclen;
	ft.chrgText.cpMin = 0;
	ft.chrgText.cpMax = 0;
	const int flags = SCFIND_WORDSTART | (autoCompleteIgnoreCase ? 0 : SCFIND_MATCHCASE);
	const int posCurrentWord = wEditor_.Call(SCI_GETCURRENTPOS) - static_cast<int>(root.length());
	unsigned int minWordLength = 0;
	unsigned int nwords = 0;

	// wordsNear contains a list of words separated by single spaces and with a space
	// at the start and end. This makes it easy to search for words.
	std::string wordsNear;
	wordsNear.append("\n");

	int posFind = wEditor_.CallPointer(SCI_FINDTEXT, flags, &ft);
	TextReader acc(wEditor_);
	while (posFind >= 0 && posFind < doclen) {	// search all the document
		int wordEnd = posFind + static_cast<int>(root.length());
		if (posFind != posCurrentWord) {
			while (Contains(wordCharacters, acc.SafeGetCharAt(wordEnd)))
				wordEnd++;
			const unsigned int wordLength = wordEnd - posFind;
			if (wordLength > root.length()) {
				std::string word = GetRangeString(wEditor_, posFind, wordEnd);
				word.insert(0, "\n");
				word.append("\n");
				if (wordsNear.find(word.c_str()) == std::string::npos) {	// add a new entry
					wordsNear += word.c_str() + 1;
					if (minWordLength < wordLength)
						minWordLength = wordLength;

					nwords++;
					if (onlyOneWord && nwords > 1) {
						return true;
					}
				}
			}
		}
		ft.chrg.cpMin = wordEnd;
		posFind = wEditor_.CallPointer(SCI_FINDTEXT, flags, &ft);
	}
	const size_t length = wordsNear.length();
	if ((length > 2) && (!onlyOneWord || (minWordLength > root.length()))) {
		// Protect spaces by temporrily transforming to \001
		std::replace(wordsNear.begin(), wordsNear.end(), ' ', '\001');
		StringList wl(true);
		wl.Set(wordsNear.c_str());
		std::string acText = wl.GetNearestWords("", 0, autoCompleteIgnoreCase);
		// Use \n as word separator
		std::replace(acText.begin(), acText.end(), ' ', '\n');
		// Return spaces from \001
		std::replace(acText.begin(), acText.end(), '\001', ' ');
		wEditor_.Call(SCI_AUTOCSETSEPARATOR, '\n');
		wEditor_.CallString(SCI_AUTOCSHOW, root.length(), acText.c_str());
	} else {
		wEditor_.Call(SCI_AUTOCCANCEL);
	}
	return true;
}

bool CuteTextBase::PerformInsertAbbreviation() {
	const std::string data = propsAbbrev.GetString(abbrevInsert_.c_str());
	if (data.empty()) {
		return true; // returning if expanded abbreviation is empty
	}

	const std::string expbuf = UnSlashString(data.c_str());
	const size_t expbuflen = expbuf.length();

	int caret_pos = wEditor_.Call(SCI_GETSELECTIONSTART);
	int sel_start = caret_pos;
	int sel_length = wEditor_.Call(SCI_GETSELECTIONEND) - sel_start;
	bool at_start = true;
	bool double_pipe = false;
	size_t last_pipe = expbuflen;
	int currentLineNumber = wEditor_.Call(SCI_LINEFROMPOSITION, caret_pos);
	int indent = 0;
	const int indentSize = wEditor_.Call(SCI_GETINDENT);
	const int indentChars = (wEditor_.Call(SCI_GETUSETABS) && wEditor_.Call(SCI_GETTABWIDTH) ? wEditor_.Call(SCI_GETTABWIDTH) : 1);
	int indentExtra = 0;
	bool isIndent = true;
	const int eolMode = wEditor_.Call(SCI_GETEOLMODE);
	if (props.GetInt("indent.automatic")) {
		indent = GetLineIndentation(currentLineNumber);
	}

	// find last |, can't be strrchr(exbuf, '|') because of ||
	for (size_t i = expbuflen; i--; ) {
		if (expbuf[i] == '|' && (i == 0 || expbuf[i-1] != '|')) {
			last_pipe = i;
			break;
		}
	}

	wEditor_.Call(SCI_BEGINUNDOACTION);

	// add the abbreviation one character at a time
	for (size_t i = 0; i < expbuflen; i++) {
		const char c = expbuf[i];
		std::string abbrevText("");
		if (isIndent && c == '\t') {
			if (props.GetInt("indent.automatic")) {
				indentExtra++;
				SetLineIndentation(currentLineNumber, indent + indentSize * indentExtra);
				caret_pos += indentSize / indentChars;
			}
		} else {
			switch (c) {
			case '|':
				// user may want to insert '|' instead of caret
				if (i < (expbuflen - 1) && expbuf[i + 1] == '|') {
					// put '|' into the line
					abbrevText += c;
					i++;
				} else if (i != last_pipe) {
					double_pipe = true;
				} else {
					// indent on multiple lines
					int j = currentLineNumber + 1; // first line indented as others
					currentLineNumber = wEditor_.Call(SCI_LINEFROMPOSITION, caret_pos + sel_length);
					for (; j <= currentLineNumber; j++) {
						SetLineIndentation(j, GetLineIndentation(j) + indentSize * indentExtra);
						caret_pos += indentExtra * indentSize / indentChars;
					}

					at_start = false;
					caret_pos += sel_length;
				}
				break;
			case '\r':
				// backward compatibility
				break;
			case '\n':
				if (eolMode == SC_EOL_CRLF || eolMode == SC_EOL_CR) {
					abbrevText += '\r';
				}
				if (eolMode == SC_EOL_CRLF || eolMode == SC_EOL_LF) {
					abbrevText += '\n';
				}
				break;
			default:
				abbrevText += c;
				break;
			}
			if (caret_pos > wEditor_.Call(SCI_GETLENGTH)) {
				caret_pos = wEditor_.Call(SCI_GETLENGTH);
			}
			wEditor_.CallString(SCI_INSERTTEXT, caret_pos, abbrevText.c_str());
			if (!double_pipe && at_start) {
				sel_start += static_cast<int>(abbrevText.length());
			}
			caret_pos += static_cast<int>(abbrevText.length());
			if (c == '\n') {
				isIndent = true;
				indentExtra = 0;
				currentLineNumber++;
				SetLineIndentation(currentLineNumber, indent);
				caret_pos += indent / indentChars;
				if (!double_pipe && at_start) {
					sel_start += indent / indentChars;
				}
			} else {
				isIndent = false;
			}
		}
	}

	// set the caret to the desired position
	if (double_pipe) {
		sel_length = 0;
	}
	wEditor_.Call(SCI_SETSEL, sel_start, sel_start + sel_length);

	wEditor_.Call(SCI_ENDUNDOACTION);
	return true;
}

bool CuteTextBase::StartInsertAbbreviation() {
	if (!AbbrevDialog()) {
		return true;
	}

	return PerformInsertAbbreviation();
}

bool CuteTextBase::StartExpandAbbreviation() {
	const int currentPos = GetCaretInLine();
	const int position = wEditor_.Call(SCI_GETCURRENTPOS); // from the beginning
	const std::string linebuf(GetCurrentLine(), 0, currentPos);	// Just get text to the left of the caret
	const int abbrevPos = (currentPos > 32 ? currentPos - 32 : 0);
	const char *abbrev = linebuf.c_str() + abbrevPos;
	std::string data;
	int abbrevLength = currentPos - abbrevPos;
	// Try each potential abbreviation from the first letter on a line
	// and expanding to the right.
	// We arbitrarily limit the length of an abbreviation (seems a reasonable value..),
	// and of course stop on the caret.
	while (abbrevLength > 0) {
		data = propsAbbrev.GetString(abbrev);
		if (!data.empty()) {
			break;	/* Found */
		}
		abbrev++;	// One more letter to the right
		abbrevLength--;
	}

	if (data.empty()) {
		WarnUser(kWarnNotFound);	// No need for a special warning
		return true; // returning if expanded abbreviation is empty
	}

	const std::string expbuf = UnSlashString(data.c_str());
	const size_t expbuflen = expbuf.length();

	int caret_pos = -1; // caret position
	int currentLineNumber = GetCurrentLineNumber();
	int indent = 0;
	const int indentSize = wEditor_.Call(SCI_GETINDENT);
	int indentExtra = 0;
	bool isIndent = true;
	const int eolMode = wEditor_.Call(SCI_GETEOLMODE);
	if (props.GetInt("indent.automatic")) {
		indent = GetLineIndentation(currentLineNumber);
	}

	wEditor_.Call(SCI_BEGINUNDOACTION);
	wEditor_.Call(SCI_SETSEL, position - abbrevLength, position);

	// add the abbreviation one character at a time
	for (size_t i = 0; i < expbuflen; i++) {
		const char c = expbuf[i];
		std::string abbrevText("");
		if (isIndent && c == '\t') {
			indentExtra++;
			SetLineIndentation(currentLineNumber, indent + indentSize * indentExtra);
		} else {
			switch (c) {
			case '|':
				// user may want to insert '|' instead of caret
				if (i < (expbuflen - 1) && expbuf[i + 1] == '|') {
					// put '|' into the line
					abbrevText += c;
					i++;
				} else if (caret_pos == -1) {
					if (i == 0) {
						// when caret is set at the first place in abbreviation
						caret_pos = wEditor_.Call(SCI_GETCURRENTPOS) - abbrevLength;
					} else {
						caret_pos = wEditor_.Call(SCI_GETCURRENTPOS);
					}
				}
				break;
			case '\n':
				if (eolMode == SC_EOL_CRLF || eolMode == SC_EOL_CR) {
					abbrevText += '\r';
				}
				if (eolMode == SC_EOL_CRLF || eolMode == SC_EOL_LF) {
					abbrevText += '\n';
				}
				break;
			default:
				abbrevText += c;
				break;
			}
			wEditor_.CallString(SCI_REPLACESEL, 0, abbrevText.c_str());
			if (c == '\n') {
				isIndent = true;
				indentExtra = 0;
				currentLineNumber++;
				SetLineIndentation(currentLineNumber, indent);
			} else {
				isIndent = false;
			}
		}
	}

	// set the caret to the desired position
	if (caret_pos != -1) {
		wEditor_.Call(SCI_GOTOPOS, caret_pos);
	}

	wEditor_.Call(SCI_ENDUNDOACTION);
	return true;
}

bool CuteTextBase::StartBlockComment() {
	std::string fileNameForExtension = ExtensionFileName();
	std::string lexerName = props.GetNewExpandString("lexer.", fileNameForExtension.c_str());
	std::string base("comment.block.");
	std::string comment_at_line_start("comment.block.at.line.start.");
	base += lexerName;
	comment_at_line_start += lexerName;
	const bool placeCommentsAtLineStart = props.GetInt(comment_at_line_start.c_str()) != 0;

	std::string comment = props.GetString(base.c_str());
	if (comment == "") { // user friendly error message box
		GUI::gui_string sBase = GUI::StringFromUTF8(base);
		GUI::gui_string error = LocaliseMessage(
		            "Block comment variable '^0' is not defined in SciTE *.properties!", sBase.c_str());
		WindowMessageBox(wCuteText_, error);
		return true;
	}
	std::string long_comment = comment;
	long_comment.append(" ");
	int selectionStart = wEditor_.Call(SCI_GETSELECTIONSTART);
	int selectionEnd = wEditor_.Call(SCI_GETSELECTIONEND);
	const int caretPosition = wEditor_.Call(SCI_GETCURRENTPOS);
	// checking if caret is located in _beginning_ of selected block
	const bool move_caret = caretPosition < selectionEnd;
	const int selStartLine = wEditor_.Call(SCI_LINEFROMPOSITION, selectionStart);
	int selEndLine = wEditor_.Call(SCI_LINEFROMPOSITION, selectionEnd);
	const int lines = selEndLine - selStartLine;
	const int firstSelLineStart = wEditor_.Call(SCI_POSITIONFROMLINE, selStartLine);
	// "caret return" is part of the last selected line
	if ((lines > 0) &&
	        (selectionEnd == wEditor_.Call(SCI_POSITIONFROMLINE, selEndLine)))
		selEndLine--;
	wEditor_.Call(SCI_BEGINUNDOACTION);
	for (int i = selStartLine; i <= selEndLine; i++) {
		const int lineStart = wEditor_.Call(SCI_POSITIONFROMLINE, i);
		int lineIndent = lineStart;
		const int lineEnd = wEditor_.Call(SCI_GETLINEENDPOSITION, i);
		if (!placeCommentsAtLineStart) {
			lineIndent = GetLineIndentPosition(i);
		}
		std::string linebuf = GetRangeString(wEditor_, lineIndent, lineEnd);
		// empty lines are not commented
		if (linebuf.length() < 1)
			continue;
		if (StartsWith(linebuf, comment.c_str())) {
			int commentLength = static_cast<int>(comment.length());
			if (StartsWith(linebuf, long_comment.c_str())) {
				// Removing comment with space after it.
				commentLength = static_cast<int>(long_comment.length());
			}
			wEditor_.Call(SCI_SETSEL, lineIndent, lineIndent + commentLength);
			wEditor_.CallString(SCI_REPLACESEL, 0, "");
			if (i == selStartLine) // is this the first selected line?
				selectionStart -= commentLength;
			selectionEnd -= commentLength; // every iteration
			continue;
		}
		if (i == selStartLine) // is this the first selected line?
			selectionStart += static_cast<int>(long_comment.length());
		selectionEnd += static_cast<int>(long_comment.length()); // every iteration
		wEditor_.CallString(SCI_INSERTTEXT, lineIndent, long_comment.c_str());
	}
	// after uncommenting selection may promote itself to the lines
	// before the first initially selected line;
	// another problem - if only comment symbol was selected;
	if (selectionStart < firstSelLineStart) {
		if (selectionStart >= selectionEnd - (static_cast<int>(long_comment.length()) - 1))
			selectionEnd = firstSelLineStart;
		selectionStart = firstSelLineStart;
	}
	if (move_caret) {
		// moving caret to the beginning of selected block
		wEditor_.Call(SCI_GOTOPOS, selectionEnd);
		wEditor_.Call(SCI_SETCURRENTPOS, selectionStart);
	} else {
		wEditor_.Call(SCI_SETSEL, selectionStart, selectionEnd);
	}
	wEditor_.Call(SCI_ENDUNDOACTION);
	return true;
}

static const char *LineEndString(int eolMode) {
	switch (eolMode) {
		case SC_EOL_CRLF:
			return "\r\n";
		case SC_EOL_CR:
			return "\r";
		case SC_EOL_LF:
		default:
			return "\n";
	}
}

bool CuteTextBase::StartBoxComment() {
	// Get start/middle/end comment strings from options file(s)
	std::string fileNameForExtension = ExtensionFileName();
	std::string lexerName = props.GetNewExpandString("lexer.", fileNameForExtension.c_str());
	std::string start_base("comment.box.start.");
	std::string middle_base("comment.box.middle.");
	std::string end_base("comment.box.end.");
	const std::string white_space(" ");
	const std::string eol(LineEndString(wEditor_.Call(SCI_GETEOLMODE)));
	start_base += lexerName;
	middle_base += lexerName;
	end_base += lexerName;
	std::string start_comment = props.GetString(start_base.c_str());
	std::string middle_comment = props.GetString(middle_base.c_str());
	std::string end_comment = props.GetString(end_base.c_str());
	if (start_comment == "" || middle_comment == "" || end_comment == "") {
		GUI::gui_string sStart = GUI::StringFromUTF8(start_base);
		GUI::gui_string sMiddle = GUI::StringFromUTF8(middle_base);
		GUI::gui_string sEnd = GUI::StringFromUTF8(end_base);
		GUI::gui_string error = LocaliseMessage(
		            "Box comment variables '^0', '^1' and '^2' are not defined in SciTE *.properties!",
		            sStart.c_str(), sMiddle.c_str(), sEnd.c_str());
		WindowMessageBox(wCuteText_, error);
		return true;
	}

	// Note selection and cursor location so that we can reselect text and reposition cursor after we insert comment strings
	size_t selectionStart = wEditor_.Call(SCI_GETSELECTIONSTART);
	size_t selectionEnd = wEditor_.Call(SCI_GETSELECTIONEND);
	const size_t caretPosition = wEditor_.Call(SCI_GETCURRENTPOS);
	const bool move_caret = caretPosition < selectionEnd;
	const size_t selStartLine = wEditor_.Call(SCI_LINEFROMPOSITION, selectionStart);
	size_t selEndLine = wEditor_.Call(SCI_LINEFROMPOSITION, selectionEnd);
	size_t lines = selEndLine - selStartLine + 1;

	// If selection ends at start of last selected line, fake it so that selection goes to end of second-last selected line
	if (lines > 1 && selectionEnd == static_cast<size_t>(wEditor_.Call(SCI_POSITIONFROMLINE, selEndLine))) {
		selEndLine--;
		lines--;
		selectionEnd = wEditor_.Call(SCI_GETLINEENDPOSITION, selEndLine);
	}

	// Pad comment strings with appropriate whitespace, then figure out their lengths (end_comment is a bit special-- see below)
	start_comment += white_space;
	middle_comment += white_space;
	const int start_comment_length = static_cast<int>(start_comment.length());
	const int middle_comment_length = static_cast<int>(middle_comment.length());
	const int end_comment_length = static_cast<int>(end_comment.length());

	wEditor_.Call(SCI_BEGINUNDOACTION);

	// Insert start_comment if needed
	int lineStart = wEditor_.Call(SCI_POSITIONFROMLINE, selStartLine);
	std::string tempString = GetRangeString(wEditor_, lineStart, lineStart + start_comment_length);
	if (start_comment != tempString) {
		wEditor_.CallString(SCI_INSERTTEXT, lineStart, start_comment.c_str());
		selectionStart += start_comment_length;
		selectionEnd += start_comment_length;
	}

	if (lines <= 1) {
		// Only a single line was selected, so just append whitespace + end-comment at end of line if needed
		const int lineEnd = wEditor_.Call(SCI_GETLINEENDPOSITION, selEndLine);
		tempString = GetRangeString(wEditor_, lineEnd - end_comment_length, lineEnd);
		if (end_comment != tempString) {
			end_comment.insert(0, white_space.c_str());
			wEditor_.CallString(SCI_INSERTTEXT, lineEnd, end_comment.c_str());
		}
	} else {
		// More than one line selected, so insert middle_comments where needed
		for (size_t i = selStartLine + 1; i < selEndLine; i++) {
			lineStart = wEditor_.Call(SCI_POSITIONFROMLINE, i);
			tempString = GetRangeString(wEditor_, lineStart, lineStart + middle_comment_length);
			if (middle_comment != tempString) {
				wEditor_.CallString(SCI_INSERTTEXT, lineStart, middle_comment.c_str());
				selectionEnd += middle_comment_length;
			}
		}

		// If last selected line is not middle-comment or end-comment, we need to insert
		// a middle-comment at the start of last selected line and possibly still insert
		// and end-comment tag after the last line (extra logic is necessary to
		// deal with the case that user selected the end-comment tag)
		lineStart = wEditor_.Call(SCI_POSITIONFROMLINE, selEndLine);
		GetRangeString(wEditor_, lineStart, lineStart + end_comment_length);
		if (end_comment != tempString) {
			tempString = GetRangeString(wEditor_, lineStart, lineStart + middle_comment_length);
			if (middle_comment != tempString) {
				wEditor_.CallString(SCI_INSERTTEXT, lineStart, middle_comment.c_str());
				selectionEnd += middle_comment_length;
			}

			// And since we didn't find the end-comment string yet, we need to check the *next* line
			//  to see if it's necessary to insert an end-comment string and a linefeed there....
			lineStart = wEditor_.Call(SCI_POSITIONFROMLINE, selEndLine + 1);
			tempString = GetRangeString(wEditor_, lineStart, lineStart + static_cast<int>(end_comment_length));
			if (end_comment != tempString) {
				end_comment += eol;
				wEditor_.CallString(SCI_INSERTTEXT, lineStart, end_comment.c_str());
			}
		}
	}

	if (move_caret) {
		// moving caret to the beginning of selected block
		wEditor_.Call(SCI_GOTOPOS, selectionEnd);
		wEditor_.Call(SCI_SETCURRENTPOS, selectionStart);
	} else {
		wEditor_.Call(SCI_SETSEL, selectionStart, selectionEnd);
	}

	wEditor_.Call(SCI_ENDUNDOACTION);

	return true;
}

bool CuteTextBase::StartStreamComment() {
	std::string fileNameForExtension = ExtensionFileName();
	const std::string lexerName = props.GetNewExpandString("lexer.", fileNameForExtension.c_str());
	std::string start_base("comment.stream.start.");
	std::string end_base("comment.stream.end.");
	std::string white_space(" ");
	start_base += lexerName;
	end_base += lexerName;
	std::string start_comment = props.GetString(start_base.c_str());
	std::string end_comment = props.GetString(end_base.c_str());
	if (start_comment == "" || end_comment == "") {
		GUI::gui_string sStart = GUI::StringFromUTF8(start_base);
		GUI::gui_string sEnd = GUI::StringFromUTF8(end_base);
		GUI::gui_string error = LocaliseMessage(
		            "Stream comment variables '^0' and '^1' are not defined in SciTE *.properties!",
		            sStart.c_str(), sEnd.c_str());
		WindowMessageBox(wCuteText_, error);
		return true;
	}
	start_comment += white_space;
	white_space += end_comment;
	end_comment = white_space;
	const int start_comment_length = static_cast<int>(start_comment.length());
	int selectionStart = wEditor_.Call(SCI_GETSELECTIONSTART);
	int selectionEnd = wEditor_.Call(SCI_GETSELECTIONEND);
	const int caretPosition = wEditor_.Call(SCI_GETCURRENTPOS);
	// checking if caret is located in _beginning_ of selected block
	const bool move_caret = caretPosition < selectionEnd;
	// if there is no selection?
	if (selectionStart == selectionEnd) {
		RangeExtend(wEditor_, selectionStart, selectionEnd,
			&CuteTextBase::islexerwordcharforsel);
		if (selectionStart == selectionEnd)
			return true; // caret is located _between_ words
	}
	wEditor_.Call(SCI_BEGINUNDOACTION);
	wEditor_.CallString(SCI_INSERTTEXT, selectionStart, start_comment.c_str());
	selectionEnd += start_comment_length;
	selectionStart += start_comment_length;
	wEditor_.CallString(SCI_INSERTTEXT, selectionEnd, end_comment.c_str());
	if (move_caret) {
		// moving caret to the beginning of selected block
		wEditor_.Call(SCI_GOTOPOS, selectionEnd);
		wEditor_.Call(SCI_SETCURRENTPOS, selectionStart);
	} else {
		wEditor_.Call(SCI_SETSEL, selectionStart, selectionEnd);
	}
	wEditor_.Call(SCI_ENDUNDOACTION);
	return true;
}

/**
 * Return the length of the given line, not counting the EOL.
 */
int CuteTextBase::GetLineLength(int line) {
	return wEditor_.Call(SCI_GETLINEENDPOSITION, line) - wEditor_.Call(SCI_POSITIONFROMLINE, line);
}

int CuteTextBase::GetCurrentLineNumber() {
	return wEditor_.Call(SCI_LINEFROMPOSITION,
	        wEditor_.Call(SCI_GETCURRENTPOS));
}

int CuteTextBase::GetCurrentColumnNumber() {
	const int mainSel = wEditor_.Call(SCI_GETMAINSELECTION, 0, 0);
	return wEditor_.Call(SCI_GETCOLUMN, wEditor_.Call(SCI_GETSELECTIONNCARET, mainSel, 0), 0) +
	        wEditor_.Call(SCI_GETSELECTIONNCARETVIRTUALSPACE, mainSel, 0);
}

int CuteTextBase::GetCurrentScrollPosition() {
	const int lineDisplayTop = wEditor_.Call(SCI_GETFIRSTVISIBLELINE);
	return wEditor_.Call(SCI_DOCLINEFROMVISIBLE, lineDisplayTop);
}

/**
 * Set up properties for ReadOnly, EOLMode, BufferLength, NbOfLines, SelLength, SelHeight.
 */
void CuteTextBase::SetTextProperties(
    PropSetFile &ps) {			///< Property set to update.

	std::string ro = GUI::UTF8FromString(localiser.Text("READ"));
	ps.Set("ReadOnly", CurrentBuffer()->isReadOnly ? ro.c_str() : "");

	const int eolMode = wEditor_.Call(SCI_GETEOLMODE);
	ps.Set("EOLMode", eolMode == SC_EOL_CRLF ? "CR+LF" : (eolMode == SC_EOL_LF ? "LF" : "CR"));

	ps.SetInteger("BufferLength", LengthDocument());

	ps.SetInteger("NbOfLines", wEditor_.Call(SCI_GETLINECOUNT));

	const Sci_CharacterRange crange = GetSelection();
	const int selFirstLine = wEditor_.Call(SCI_LINEFROMPOSITION, crange.cpMin);
	const int selLastLine = wEditor_.Call(SCI_LINEFROMPOSITION, crange.cpMax);
	long charCount = 0;
	if (wEditor_.Call(SCI_GETSELECTIONMODE) == SC_SEL_RECTANGLE) {
		for (int line = selFirstLine; line <= selLastLine; line++) {
			const int startPos = wEditor_.Call(SCI_GETLINESELSTARTPOSITION, line);
			const int endPos = wEditor_.Call(SCI_GETLINESELENDPOSITION, line);
			charCount += wEditor_.Call(SCI_COUNTCHARACTERS, startPos, endPos);
		}
	} else {
		charCount = wEditor_.Call(SCI_COUNTCHARACTERS, crange.cpMin, crange.cpMax);
	}
	ps.SetInteger("SelLength", static_cast<int>(charCount));
	const int caretPos = wEditor_.Call(SCI_GETCURRENTPOS);
	const int selAnchor = wEditor_.Call(SCI_GETANCHOR);
	int selHeight = selLastLine - selFirstLine + 1;
	if (0 == (crange.cpMax - crange.cpMin)) {
		selHeight = 0;
	} else if (selLastLine == selFirstLine) {
		selHeight = 1;
	} else if ((wEditor_.Call(SCI_GETCOLUMN, caretPos) == 0 && (selAnchor <= caretPos)) ||
	        ((wEditor_.Call(SCI_GETCOLUMN, selAnchor) == 0) && (selAnchor > caretPos ))) {
		selHeight = selLastLine - selFirstLine;
	}
	ps.SetInteger("SelHeight", selHeight);
}

void CuteTextBase::UpdateStatusBar(bool bUpdateSlowData) {
	if (sbVisible) {
		if (bUpdateSlowData) {
			SetFileProperties(propsStatus);
		}
		SetTextProperties(propsStatus);
		propsStatus.SetInteger("LineNumber", GetCurrentLineNumber() + 1);
		propsStatus.SetInteger("ColumnNumber", GetCurrentColumnNumber() + 1);
		propsStatus.Set("OverType", wEditor_.Call(SCI_GETOVERTYPE) ? "OVR" : "INS");

		char sbKey[32];
		sprintf(sbKey, "statusbar.text.%d", sbNum);
		std::string msg = propsStatus.GetExpandedString(sbKey);
		if (msg.size() && sbValue != msg) {	// To avoid flickering, update only if needed
			SetStatusBarText(msg.c_str());
			sbValue = msg;
		}
	} else {
		sbValue = "";
	}
}

void CuteTextBase::SetLineIndentation(int line, int indent) {
	if (indent < 0)
		return;
	Sci_CharacterRange crange = GetSelection();
	const Sci_CharacterRange crangeStart = crange;
	const int posBefore = GetLineIndentPosition(line);
	wEditor_.Call(SCI_SETLINEINDENTATION, line, indent);
	const int posAfter = GetLineIndentPosition(line);
	const int posDifference = posAfter - posBefore;
	if (posAfter > posBefore) {
		// Move selection on
		if (crange.cpMin >= posBefore) {
			crange.cpMin += posDifference;
		}
		if (crange.cpMax >= posBefore) {
			crange.cpMax += posDifference;
		}
	} else if (posAfter < posBefore) {
		// Move selection back
		if (crange.cpMin >= posAfter) {
			if (crange.cpMin >= posBefore)
				crange.cpMin += posDifference;
			else
				crange.cpMin = posAfter;
		}
		if (crange.cpMax >= posAfter) {
			if (crange.cpMax >= posBefore)
				crange.cpMax += posDifference;
			else
				crange.cpMax = posAfter;
		}
	}
	if ((crangeStart.cpMin != crange.cpMin) || (crangeStart.cpMax != crange.cpMax)) {
		SetSelection(static_cast<int>(crange.cpMin), static_cast<int>(crange.cpMax));
	}
}

int CuteTextBase::GetLineIndentation(int line) {
	return wEditor_.Call(SCI_GETLINEINDENTATION, line);
}

int CuteTextBase::GetLineIndentPosition(int line) {
	return wEditor_.Call(SCI_GETLINEINDENTPOSITION, line);
}

static std::string CreateIndentation(int indent, int tabSize, bool insertSpaces) {
	std::string indentation;
	if (!insertSpaces) {
		while (indent >= tabSize) {
			indentation.append("\t", 1);
			indent -= tabSize;
		}
	}
	while (indent > 0) {
		indentation.append(" ", 1);
		indent--;
	}
	return indentation;
}

void CuteTextBase::ConvertIndentation(int tabSize, int useTabs) {
	wEditor_.Call(SCI_BEGINUNDOACTION);
	const int maxLine = wEditor_.Call(SCI_GETLINECOUNT);
	for (int line = 0; line < maxLine; line++) {
		const int lineStart = wEditor_.Call(SCI_POSITIONFROMLINE, line);
		const int indent = GetLineIndentation(line);
		const int indentPos = GetLineIndentPosition(line);
		const int maxIndentation = 1000;
		if (indent < maxIndentation) {
			std::string indentationNow = GetRangeString(wEditor_, lineStart, indentPos);
			std::string indentationWanted = CreateIndentation(indent, tabSize, !useTabs);
			if (indentationNow != indentationWanted) {
				wEditor_.Call(SCI_SETTARGETSTART, lineStart);
				wEditor_.Call(SCI_SETTARGETEND, indentPos);
				wEditor_.CallString(SCI_REPLACETARGET, indentationWanted.length(),
					indentationWanted.c_str());
			}
		}
	}
	wEditor_.Call(SCI_ENDUNDOACTION);
}

bool CuteTextBase::RangeIsAllWhitespace(int start, int end) {
	TextReader acc(wEditor_);
	for (int i = start; i < end; i++) {
		if ((acc[i] != ' ') && (acc[i] != '\t'))
			return false;
	}
	return true;
}

std::vector<std::string> CuteTextBase::GetLinePartsInStyle(int line, const StyleAndWords &saw) {
	std::vector<std::string> sv;
	TextReader acc(wEditor_);
	std::string s;
	const bool separateCharacters = saw.IsSingleChar();
	const int thisLineStart = wEditor_.Call(SCI_POSITIONFROMLINE, line);
	const int nextLineStart = wEditor_.Call(SCI_POSITIONFROMLINE, line + 1);
	for (int pos = thisLineStart; pos < nextLineStart; pos++) {
		if (acc.StyleAt(pos) == saw.styleNumber) {
			if (separateCharacters) {
				// Add one character at a time, even if there is an adjacent character in the same style
				if (s.length() > 0) {
					sv.push_back(s);
				}
				s = "";
			}
			s += acc[pos];
		} else if (s.length() > 0) {
			sv.push_back(s);
			s = "";
		}
	}
	if (s.length() > 0) {
		sv.push_back(s);
	}
	return sv;
}

inline bool IsAlphabetic(unsigned int ch) {
	return ((ch >= 'A') && (ch <= 'Z')) || ((ch >= 'a') && (ch <= 'z'));
}

static bool includes(const StyleAndWords &symbols, const std::string &value) {
	if (symbols.words.length() == 0) {
		return false;
	} else if (IsAlphabetic(symbols.words[0])) {
		// Set of symbols separated by spaces
		const size_t lenVal = value.length();
		const char *symbol = symbols.words.c_str();
		while (symbol) {
			const char *symbolEnd = strchr(symbol, ' ');
			size_t lenSymbol = strlen(symbol);
			if (symbolEnd)
				lenSymbol = symbolEnd - symbol;
			if (lenSymbol == lenVal) {
				if (strncmp(symbol, value.c_str(), lenSymbol) == 0) {
					return true;
				}
			}
			symbol = symbolEnd;
			if (symbol)
				symbol++;
		}
	} else {
		// Set of individual characters. Only one character allowed for now
		const char ch = symbols.words[0];
		return strchr(value.c_str(), ch) != 0;
	}
	return false;
}

IndentationStatus CuteTextBase::GetIndentState(int line) {
	// C like language indentation defined by braces and keywords
	IndentationStatus indentState = kIsNone;
	const std::vector<std::string> controlIndents = GetLinePartsInStyle(line, statementIndent_);
	for (const std::string &sIndent : controlIndents) {
		if (includes(statementIndent_, sIndent))
			indentState = kIsKeyWordStart;
	}
	const std::vector<std::string> controlEnds = GetLinePartsInStyle(line, statementEnd_);
	for (const std::string &sEnd : controlEnds) {
		if (includes(statementEnd_, sEnd))
			indentState = kIsNone;
	}
	// Braces override keywords
	const std::vector<std::string> controlBlocks = GetLinePartsInStyle(line, blockEnd_);
	for (const std::string &sBlock : controlBlocks) {
		if (includes(blockEnd_, sBlock))
			indentState = kIsBlockEnd;
		if (includes(blockStart_, sBlock))
			indentState = kIsBlockStart;
	}
	return indentState;
}

int CuteTextBase::IndentOfBlock(int line) {
	if (line < 0)
		return 0;
	const int indentSize = wEditor_.Call(SCI_GETINDENT);
	int indentBlock = GetLineIndentation(line);
	int backLine = line;
	IndentationStatus indentState = kIsNone;
	if (statementIndent_.IsEmpty() && blockStart_.IsEmpty() && blockEnd_.IsEmpty())
		indentState = kIsBlockStart;	// Don't bother searching backwards

	int lineLimit = line - statementLookback_;
	if (lineLimit < 0)
		lineLimit = 0;
	while ((backLine >= lineLimit) && (indentState == 0)) {
		indentState = GetIndentState(backLine);
		if (indentState != 0) {
			indentBlock = GetLineIndentation(backLine);
			if (indentState == kIsBlockStart) {
				if (!indentOpening_)
					indentBlock += indentSize;
			}
			if (indentState == kIsBlockEnd) {
				if (indentClosing_)
					indentBlock -= indentSize;
				if (indentBlock < 0)
					indentBlock = 0;
			}
			if ((indentState == kIsKeyWordStart) && (backLine == line))
				indentBlock += indentSize;
		}
		backLine--;
	}
	return indentBlock;
}

void CuteTextBase::MaintainIndentation(char ch) {
	const int eolMode = wEditor_.Call(SCI_GETEOLMODE);
	const int curLine = GetCurrentLineNumber();
	int lastLine = curLine - 1;

	if (((eolMode == SC_EOL_CRLF || eolMode == SC_EOL_LF) && ch == '\n') ||
	        (eolMode == SC_EOL_CR && ch == '\r')) {
		if (props.GetInt("indent.automatic")) {
			while (lastLine >= 0 && GetLineLength(lastLine) == 0)
				lastLine--;
		}
		int indentAmount = 0;
		if (lastLine >= 0) {
			indentAmount = GetLineIndentation(lastLine);
		}
		if (indentAmount > 0) {
			SetLineIndentation(curLine, indentAmount);
		}
	}
}

void CuteTextBase::AutomaticIndentation(char ch) {
	const Sci_CharacterRange crange = GetSelection();
	const int selStart = static_cast<int>(crange.cpMin);
	const int curLine = GetCurrentLineNumber();
	const int thisLineStart = wEditor_.Call(SCI_POSITIONFROMLINE, curLine);
	const int indentSize = wEditor_.Call(SCI_GETINDENT);
	int indentBlock = IndentOfBlock(curLine - 1);

	if ((wEditor_.Call(SCI_GETLEXER) == SCLEX_PYTHON) &&
			(props.GetInt("indent.python.colon") == 1)) {
		const int eolMode = wEditor_.Call(SCI_GETEOLMODE);
		const int eolChar = (eolMode == SC_EOL_CR ? '\r' : '\n');
		const int eolChars = (eolMode == SC_EOL_CRLF ? 2 : 1);
		const int prevLineStart = wEditor_.Call(SCI_POSITIONFROMLINE, curLine - 1);
		const int prevIndentPos = GetLineIndentPosition(curLine - 1);
		const int indentExisting = GetLineIndentation(curLine);

		if (ch == eolChar) {
			// Find last noncomment, nonwhitespace character on previous line
			int character = 0;
			int style = 0;
			for (int p = selStart - eolChars - 1; p > prevLineStart; p--) {
				style = wEditor_.Call(SCI_GETSTYLEAT, p);
				if (style != SCE_P_DEFAULT && style != SCE_P_COMMENTLINE &&
						style != SCE_P_COMMENTBLOCK) {
					character = wEditor_.Call(SCI_GETCHARAT, p);
					break;
				}
			}
			indentBlock = GetLineIndentation(curLine - 1);
			if (style == SCE_P_OPERATOR && character == ':') {
				SetLineIndentation(curLine, indentBlock + indentSize);
			} else if (selStart == prevIndentPos + eolChars) {
				// Preserve the indentation of preexisting text beyond the caret
				SetLineIndentation(curLine, indentBlock + indentExisting);
			} else {
				SetLineIndentation(curLine, indentBlock);
			}
		}
		return;
	}

	if (blockEnd_.IsSingleChar() && ch == blockEnd_.words[0]) {	// Dedent maybe
		if (!indentClosing_) {
			if (RangeIsAllWhitespace(thisLineStart, selStart - 1)) {
				SetLineIndentation(curLine, indentBlock - indentSize);
			}
		}
	} else if (!blockEnd_.IsSingleChar() && (ch == ' ')) {	// Dedent maybe
		if (!indentClosing_ && (GetIndentState(curLine) == kIsBlockEnd)) {}
	} else if (blockStart_.IsSingleChar() && (ch == blockStart_.words[0])) {
		// Dedent maybe if first on line and previous line was starting keyword
		if (!indentOpening_ && (GetIndentState(curLine - 1) == kIsKeyWordStart)) {
			if (RangeIsAllWhitespace(thisLineStart, selStart - 1)) {
				SetLineIndentation(curLine, indentBlock - indentSize);
			}
		}
	} else if ((ch == '\r' || ch == '\n') && (selStart == thisLineStart)) {
		if (!indentClosing_ && !blockEnd_.IsSingleChar()) {	// Dedent previous line maybe
			const std::vector<std::string> controlWords = GetLinePartsInStyle(curLine - 1, blockEnd_);
			if (!controlWords.empty()) {
				if (includes(blockEnd_, controlWords[0])) {
					// Check if first keyword on line is an ender
					SetLineIndentation(curLine - 1, IndentOfBlock(curLine - 2) - indentSize);
					// Recalculate as may have changed previous line
					indentBlock = IndentOfBlock(curLine - 1);
				}
			}
		}
		SetLineIndentation(curLine, indentBlock);
	}
}

/**
 * Upon a character being added, SciTE may decide to perform some action
 * such as displaying a completion list or auto-indentation.
 */
void CuteTextBase::CharAdded(int utf32) {
	if (recording)
		return;
	const Sci_CharacterRange crange = GetSelection();
	const int selStart = static_cast<int>(crange.cpMin);
	const int selEnd = static_cast<int>(crange.cpMax);

	if (utf32 > 0XFF) { // MBCS, never let it go.
		if (imeAutoComplete) {
			if ((selEnd == selStart) && (selStart > 0)) {
				if (wEditor_.Call(SCI_CALLTIPACTIVE)) {
					ContinueCallTip();
				} else if (wEditor_.Call(SCI_AUTOCACTIVE)) {
					wEditor_.Call(SCI_AUTOCCANCEL);
					StartAutoComplete();
				} else {
					StartAutoComplete();
				}
			}
		}
		return;
	}

	// SBCS
	const char ch = static_cast<char>(utf32);
	if ((selEnd == selStart) && (selStart > 0)) {
		if (wEditor_.Call(SCI_CALLTIPACTIVE)) {
			if (Contains(calltipParametersEnd, ch)) {
				braceCount--;
				if (braceCount < 1)
					wEditor_.Call(SCI_CALLTIPCANCEL);
				else
					StartCallTip();
			} else if (Contains(calltipParametersStart, ch)) {
				braceCount++;
				StartCallTip();
			} else {
				ContinueCallTip();
			}
		} else if (wEditor_.Call(SCI_AUTOCACTIVE)) {
			if (Contains(calltipParametersStart, ch)) {
				braceCount++;
				StartCallTip();
			} else if (Contains(calltipParametersEnd, ch)) {
				braceCount--;
			} else if (!Contains(wordCharacters, ch)) {
				wEditor_.Call(SCI_AUTOCCANCEL);
				if (Contains(autoCompleteStartCharacters, ch)) {
					StartAutoComplete();
				}
			} else if (autoCCausedByOnlyOne) {
				StartAutoCompleteWord(true);
			}
		} else if (HandleXml(ch)) {
			// Handled in the routine
		} else {
			if (Contains(calltipParametersStart, ch)) {
				braceCount = 1;
				StartCallTip();
			} else {
				autoCCausedByOnlyOne = false;
				if (indentMaintain_)
					MaintainIndentation(ch);
				else if (props.GetInt("indent.automatic"))
					AutomaticIndentation(ch);
				if (Contains(autoCompleteStartCharacters, ch)) {
					StartAutoComplete();
				} else if (props.GetInt("autocompleteword.automatic") && Contains(wordCharacters, ch)) {
					StartAutoCompleteWord(true);
					autoCCausedByOnlyOne = wEditor_.Call(SCI_AUTOCACTIVE);
				}
			}
		}
	}
}

/**
 * Upon a character being added to the output, SciTE may decide to perform some action
 * such as displaying a completion list or running a shell command.
 */
void CuteTextBase::CharAddedOutput(int ch) {
	if (ch == '\n') {
		NewLineInOutput();
	} else if (ch == '(') {
		// Potential autocompletion of symbols when $( typed
		const int selStart = wOutput_.Call(SCI_GETSELECTIONSTART);
		if ((selStart > 1) && (wOutput_.Call(SCI_GETCHARAT, selStart - 2, 0) == '$')) {
			std::string symbols;
			const char *key = NULL;
			const char *val = NULL;
			bool b = props.GetFirst(key, val);
			while (b) {
				symbols.append(key);
				symbols.append(") ");
				b = props.GetNext(key, val);
			}
			StringList symList;
			symList.Set(symbols.c_str());
			std::string words = symList.GetNearestWords("", 0, true);
			if (words.length()) {
				wEditor_.Call(SCI_AUTOCSETSEPARATOR, ' ');
				wOutput_.CallString(SCI_AUTOCSHOW, 0, words.c_str());
			}
		}
	}
}

/**
 * This routine will auto complete XML or HTML tags that are still open by closing them
 * @param ch The character we are dealing with, currently only works with the '>' character
 * @return True if handled, false otherwise
 */
bool CuteTextBase::HandleXml(char ch) {
	// We're looking for this char
	// Quit quickly if not found
	if (ch != '>') {
		return false;
	}

	// This may make sense only in certain languages
	if (lexLanguage_ != SCLEX_HTML && lexLanguage_ != SCLEX_XML) {
		return false;
	}

	// If the user has turned us off, quit now.
	// Default is off
	const std::string value = props.GetExpandedString("xml.auto.close.tags");
	if ((value.length() == 0) || (value == "0")) {
		return false;
	}

	// Grab the last 512 characters or so
	const int nCaret = wEditor_.Call(SCI_GETCURRENTPOS);
	int nMin = nCaret - 512;
	if (nMin < 0) {
		nMin = 0;
	}

	if (nCaret - nMin < 3) {
		return false; // Smallest tag is 3 characters ex. <p>
	}
	std::string sel = GetRangeString(wEditor_, nMin, nCaret);

	if (sel[nCaret - nMin - 2] == '/') {
		// User typed something like "<br/>"
		return false;
	}

	if (sel[nCaret - nMin - 2] == '-') {
		// User typed something like "<a $this->"
		return false;
	}

	std::string strFound = FindOpenXmlTag(sel.c_str(), nCaret - nMin);

	if (strFound.length() > 0) {
		wEditor_.Call(SCI_BEGINUNDOACTION);
		std::string toInsert = "</";
		toInsert += strFound;
		toInsert += ">";
		wEditor_.CallString(SCI_REPLACESEL, 0, toInsert.c_str());
		SetSelection(nCaret, nCaret);
		wEditor_.Call(SCI_ENDUNDOACTION);
		return true;
	}

	return false;
}

/** Search backward through nSize bytes looking for a '<', then return the tag if any
 * @return The tag name
 */
std::string CuteTextBase::FindOpenXmlTag(const char sel[], int nSize) {
	std::string strRet = "";

	if (nSize < 3) {
		// Smallest tag is "<p>" which is 3 characters
		return strRet;
	}
	const char *pBegin = &sel[0];
	const char *pCur = &sel[nSize - 1];

	pCur--; // Skip past the >
	while (pCur > pBegin) {
		if (*pCur == '<') {
			break;
		} else if (*pCur == '>') {
			if (*(pCur - 1) != '-') {
				break;
			}
		}
		--pCur;
	}

	if (*pCur == '<') {
		pCur++;
		while (strchr(":_-.", *pCur) || isalnum(*pCur)) {
			strRet += *pCur;
			pCur++;
		}
	}

	// Return the tag name or ""
	return strRet;
}

void CuteTextBase::GoMatchingBrace(bool select) {
	int braceAtCaret = -1;
	int braceOpposite = -1;
	const bool isInside = FindMatchingBracePosition(pwFocussed == &wEditor_, braceAtCaret, braceOpposite, true);
	// Convert the character positions into caret positions based on whether
	// the caret position was inside or outside the braces.
	if (isInside) {
		if (braceOpposite > braceAtCaret) {
			braceAtCaret++;
		} else if (braceOpposite >= 0) {
			braceOpposite++;
		}
	} else {    // Outside
		if (braceOpposite > braceAtCaret) {
			braceOpposite++;
		} else {
			braceAtCaret++;
		}
	}
	if (braceOpposite >= 0) {
		EnsureRangeVisible(*pwFocussed, braceOpposite, braceOpposite);
		if (select) {
			pwFocussed->Call(SCI_SETSEL, braceAtCaret, braceOpposite);
		} else {
			pwFocussed->Call(SCI_SETSEL, braceOpposite, braceOpposite);
		}
	}
}

// Text	ConditionalUp	Ctrl+J	Finds the previous matching preprocessor condition
// Text	ConditionalDown	Ctrl+K	Finds the next matching preprocessor condition
void CuteTextBase::GoMatchingPreprocCond(int direction, bool select) {
	int mppcAtCaret = wEditor_.Call(SCI_GETCURRENTPOS);
	int mppcMatch = -1;
	const int forward = (direction == IDM_NEXTMATCHPPC);
	const bool isInside = FindMatchingPreprocCondPosition(forward, mppcAtCaret, mppcMatch);

	if (isInside && mppcMatch >= 0) {
		EnsureRangeVisible(wEditor_, mppcMatch, mppcMatch);
		if (select) {
			// Selection changes the rules a bit...
			const int selStart = wEditor_.Call(SCI_GETSELECTIONSTART);
			const int selEnd = wEditor_.Call(SCI_GETSELECTIONEND);
			// pivot isn't the caret position but the opposite (if there is a selection)
			const int pivot = (mppcAtCaret == selStart ? selEnd : selStart);
			if (forward) {
				// Caret goes one line beyond the target, to allow selecting the whole line
				const int lineNb = wEditor_.Call(SCI_LINEFROMPOSITION, mppcMatch);
				mppcMatch = wEditor_.Call(SCI_POSITIONFROMLINE, lineNb + 1);
			}
			SetSelection(pivot, mppcMatch);
		} else {
			SetSelection(mppcMatch, mppcMatch);
		}
	} else {
		WarnUser(kWarnNotFound);
	}
}

void CuteTextBase::AddCommand(const std::string &cmd, const std::string &dir, JobSubsystem jobType, const std::string &input, int flags) {
	// If no explicit directory, use the directory of the current file
	FilePath directoryRun;
	if (dir.length()) {
		FilePath directoryExplicit(GUI::StringFromUTF8(dir));
		if (directoryExplicit.IsAbsolute()) {
			directoryRun = directoryExplicit;
		} else {
			// Relative paths are relative to the current file
			directoryRun = FilePath(filePath_.Directory(), directoryExplicit).NormalizePath();
		}
	} else {
		directoryRun = filePath_.Directory();
	}
	jobQueue.AddCommand(cmd, directoryRun, jobType, input, flags);
}

int ControlIDOfCommand(unsigned long wParam) {
	return wParam & 0xffff;
}

void WindowSetFocus(GUI::ScintillaWindow &w) {
	w.Send(SCI_GRABFOCUS, 0, 0);
}

void CuteTextBase::SetLineNumberWidth() {
	if (lineNumbers) {
		int lineNumWidth = lineNumbersWidth;

		if (lineNumbersExpand) {
			// The margin size will be expanded if the current buffer's maximum
			// line number would overflow the margin.

			int lineCount = wEditor_.Call(SCI_GETLINECOUNT);

			lineNumWidth = 1;
			while (lineCount >= 10) {
				lineCount /= 10;
				++lineNumWidth;
			}

			if (lineNumWidth < lineNumbersWidth) {
				lineNumWidth = lineNumbersWidth;
			}
		}
		if (lineNumWidth < 0)
			lineNumWidth = 0;
		// The 4 here allows for spacing: 1 pixel on left and 3 on right.
		std::string nNines(lineNumWidth, '9');
		const int pixelWidth = 4 + wEditor_.CallString(SCI_TEXTWIDTH, STYLE_LINENUMBER, nNines.c_str());

		wEditor_.Call(SCI_SETMARGINWIDTHN, 0, pixelWidth);
	} else {
		wEditor_.Call(SCI_SETMARGINWIDTHN, 0, 0);
	}
}

void CuteTextBase::MenuCommand(int cmdID, int source) {
	switch (cmdID) {
	case IDM_NEW:
		// For the New command, the "are you sure" question is always asked as this gives
		// an opportunity to abandon the edits made to a file when are.you.sure is turned off.
		if (CanMakeRoom()) {
			New();
			ReadProperties();
			SetIndentSettings();
			SetEol();
			UpdateStatusBar(true);
			WindowSetFocus(wEditor_);
		}
		break;
	case IDM_OPEN:
		// No need to see if can make room as that will occur
		// when doing the opening. Must be done there as user
		// may decide to open multiple files so do not know yet
		// how much room needed.
		OpenDialog(filePath_.Directory(), GUI::StringFromUTF8(props.GetExpandedString("open.filter")).c_str());
		WindowSetFocus(wEditor_);
		break;
	case IDM_OPENSELECTED:
		if (OpenSelected())
			WindowSetFocus(wEditor_);
		break;
	case IDM_REVERT:
		Revert();
		WindowSetFocus(wEditor_);
		break;
	case IDM_CLOSE:
		if (SaveIfUnsure() != kSaveCancelled) {
			Close();
			WindowSetFocus(wEditor_);
		}
		break;
	case IDM_CLOSEALL:
		CloseAllBuffers();
		break;
	case IDM_SAVE:
		Save();
		WindowSetFocus(wEditor_);
		break;
	case IDM_SAVEALL:
		SaveAllBuffers(true);
		break;
	case IDM_SAVEAS:
		SaveAsDialog();
		WindowSetFocus(wEditor_);
		break;
	case IDM_SAVEACOPY:
		SaveACopy();
		WindowSetFocus(wEditor_);
		break;
	case IDM_COPYPATH:
		CopyPath();
		break;
	case IDM_SAVEASHTML:
		SaveAsHTML();
		WindowSetFocus(wEditor_);
		break;
	case IDM_SAVEASRTF:
		SaveAsRTF();
		WindowSetFocus(wEditor_);
		break;
	case IDM_SAVEASPDF:
		SaveAsPDF();
		WindowSetFocus(wEditor_);
		break;
	case IDM_SAVEASTEX:
		SaveAsTEX();
		WindowSetFocus(wEditor_);
		break;
	case IDM_SAVEASXML:
		SaveAsXML();
		WindowSetFocus(wEditor_);
		break;
	case IDM_PRINT:
		Print(true);
		break;
	case IDM_PRINTSETUP:
		PrintSetup();
		break;
	case IDM_LOADSESSION:
		LoadSessionDialog();
		WindowSetFocus(wEditor_);
		break;
	case IDM_SAVESESSION:
		SaveSessionDialog();
		WindowSetFocus(wEditor_);
		break;
	case IDM_ABOUT:
		AboutDialog();
		break;
	case IDM_QUIT:
		QuitProgram();
		break;
	case IDM_ENCODING_DEFAULT:
	case IDM_ENCODING_UCS2BE:
	case IDM_ENCODING_UCS2LE:
	case IDM_ENCODING_UTF8:
	case IDM_ENCODING_UCOOKIE:
		CurrentBuffer()->unicodeMode = static_cast<UniMode>(cmdID - IDM_ENCODING_DEFAULT);
		if (CurrentBuffer()->unicodeMode != uni8Bit) {
			// Override the code page if Unicode
			codePage_ = SC_CP_UTF8;
		} else {
			codePage_ = props.GetInt("code.page");
		}
		wEditor_.Call(SCI_SETCODEPAGE, codePage_);
		break;

	case IDM_NEXTFILESTACK:
		if (buffers.size() > 1 && props.GetInt("buffers.zorder.switching")) {
			NextInStack(); // next most recently selected buffer
			WindowSetFocus(wEditor_);
			break;
		}
		/* FALLTHRU */
		// else fall through and do NEXTFILE behaviour...
	case IDM_NEXTFILE:
		if (buffers.size() > 1) {
			Next(); // Use Next to tabs move left-to-right
			WindowSetFocus(wEditor_);
		} else {
			// Not using buffers - switch to next file on MRU
			StackMenuNext();
		}
		break;

	case IDM_PREVFILESTACK:
		if (buffers.size() > 1 && props.GetInt("buffers.zorder.switching")) {
			PrevInStack(); // next least recently selected buffer
			WindowSetFocus(wEditor_);
			break;
		}
		/* FALLTHRU */
		// else fall through and do PREVFILE behaviour...
	case IDM_PREVFILE:
		if (buffers.size() > 1) {
			Prev(); // Use Prev to tabs move right-to-left
			WindowSetFocus(wEditor_);
		} else {
			// Not using buffers - switch to previous file on MRU
			StackMenuPrev();
		}
		break;

	case IDM_MOVETABRIGHT:
		MoveTabRight();
		WindowSetFocus(wEditor_);
		break;
	case IDM_MOVETABLEFT:
		MoveTabLeft();
		WindowSetFocus(wEditor_);
		break;

	case IDM_UNDO:
		CallPane(source, SCI_UNDO);
		CheckMenus();
		break;
	case IDM_REDO:
		CallPane(source, SCI_REDO);
		CheckMenus();
		break;

	case IDM_CUT:
		if (!CallPane(source, SCI_GETSELECTIONEMPTY)) {
			CallPane(source, SCI_CUT);
		}
		break;
	case IDM_COPY:
		if (!CallPane(source, SCI_GETSELECTIONEMPTY)) {
			//fprintf(stderr, "Copy from %d\n", source);
			CallPane(source, SCI_COPY);
		}
		// does not trigger SCN_UPDATEUI, so do CheckMenusClipboard() here
		CheckMenusClipboard();
		break;
	case IDM_PASTE:
		CallPane(source, SCI_PASTE);
		break;
	case IDM_DUPLICATE:
		CallPane(source, SCI_SELECTIONDUPLICATE);
		break;
	case IDM_PASTEANDDOWN: {
			const int pos = CallFocused(SCI_GETCURRENTPOS);
			CallFocused(SCI_PASTE);
			CallFocused(SCI_SETCURRENTPOS, pos);
			CallFocused(SCI_CHARLEFT);
			CallFocused(SCI_LINEDOWN);
		}
		break;
	case IDM_CLEAR:
		CallPane(source, SCI_CLEAR);
		break;
	case IDM_SELECTALL:
		CallPane(source, SCI_SELECTALL);
		break;
	case IDM_COPYASRTF:
		CopyAsRTF();
		break;

	case IDM_FIND:
		Find();
		break;

	case IDM_INCSEARCH:
		IncrementSearchMode();
		break;

	case IDM_FINDNEXT:
		FindNext(reverseFind);
		break;

	case IDM_FINDNEXTBACK:
		FindNext(!reverseFind);
		break;

	case IDM_FINDNEXTSEL:
		SelectionIntoFind();
		FindNext(reverseFind, true, false);
		break;

	case IDM_ENTERSELECTION:
		SelectionIntoFind();
		break;

	case IDM_SELECTIONADDNEXT:
		SelectionAdd(kAddNext);
		break;

	case IDM_SELECTIONADDEACH:
		SelectionAdd(kAddEach);
		break;

	case IDM_FINDNEXTBACKSEL:
		SelectionIntoFind();
		FindNext(!reverseFind, true, false);
		break;

	case IDM_FINDINFILES:
		FindInFiles();
		break;

	case IDM_REPLACE:
		Replace();
		break;

	case IDM_GOTO:
		GoLineDialog();
		break;

	case IDM_MATCHBRACE:
		GoMatchingBrace(false);
		break;

	case IDM_SELECTTOBRACE:
		GoMatchingBrace(true);
		break;

	case IDM_PREVMATCHPPC:
		GoMatchingPreprocCond(IDM_PREVMATCHPPC, false);
		break;

	case IDM_SELECTTOPREVMATCHPPC:
		GoMatchingPreprocCond(IDM_PREVMATCHPPC, true);
		break;

	case IDM_NEXTMATCHPPC:
		GoMatchingPreprocCond(IDM_NEXTMATCHPPC, false);
		break;

	case IDM_SELECTTONEXTMATCHPPC:
		GoMatchingPreprocCond(IDM_NEXTMATCHPPC, true);
		break;
	case IDM_SHOWCALLTIP:
		if (wEditor_.Call(SCI_CALLTIPACTIVE)) {
			currentCallTip = (currentCallTip + 1 == maxCallTips) ? 0 : currentCallTip + 1;
			FillFunctionDefinition();
		} else {
			StartCallTip();
		}
		break;
	case IDM_COMPLETE:
		autoCCausedByOnlyOne = false;
		StartAutoComplete();
		break;

	case IDM_COMPLETEWORD:
		autoCCausedByOnlyOne = false;
		StartAutoCompleteWord(false);
		break;

	case IDM_ABBREV:
		wEditor_.Call(SCI_CANCEL);
		StartExpandAbbreviation();
		break;

	case IDM_INS_ABBREV:
		wEditor_.Call(SCI_CANCEL);
		StartInsertAbbreviation();
		break;

	case IDM_BLOCK_COMMENT:
		StartBlockComment();
		break;

	case IDM_BOX_COMMENT:
		StartBoxComment();
		break;

	case IDM_STREAM_COMMENT:
		StartStreamComment();
		break;

	case IDM_TOGGLE_FOLDALL:
		FoldAll();
		break;

	case IDM_UPRCASE:
		CallFocused(SCI_UPPERCASE);
		break;

	case IDM_LWRCASE:
		CallFocused(SCI_LOWERCASE);
		break;

	case IDM_LINEREVERSE:
		CallFocused(SCI_LINEREVERSE);
		break;

	case IDM_JOIN:
		CallFocused(SCI_TARGETFROMSELECTION);
		CallFocused(SCI_LINESJOIN);
		break;

	case IDM_SPLIT:
		CallFocused(SCI_TARGETFROMSELECTION);
		CallFocused(SCI_LINESSPLIT);
		break;

	case IDM_EXPAND:
		wEditor_.Call(SCI_TOGGLEFOLD, GetCurrentLineNumber());
		break;

	case IDM_TOGGLE_FOLDRECURSIVE: {
			const int line = GetCurrentLineNumber();
			const int level = wEditor_.Call(SCI_GETFOLDLEVEL, line);
			ToggleFoldRecursive(line, level);
		}
		break;

	case IDM_EXPAND_ENSURECHILDRENVISIBLE: {
			const int line = GetCurrentLineNumber();
			const int level = wEditor_.Call(SCI_GETFOLDLEVEL, line);
			EnsureAllChildrenVisible(line, level);
		}
		break;

	case IDM_SPLITVERTICAL:
		{
			const GUI::Rectangle rcClient = GetClientRectangle();
			const double doubleHeightOutput = heightOutput;
			const double doublePreviousHeightOutput = previousHeightOutput;
			heightOutput = static_cast<int>(splitVertical ?
				lround(doubleHeightOutput * rcClient.Height() / rcClient.Width()) :
				lround(doubleHeightOutput * rcClient.Width() / rcClient.Height()));
			previousHeightOutput = static_cast<int>(splitVertical ?
				lround(doublePreviousHeightOutput * rcClient.Height() / rcClient.Width()) :
				lround(doublePreviousHeightOutput * rcClient.Width() / rcClient.Height()));
		}
		splitVertical = !splitVertical;
		heightOutput = NormaliseSplit(heightOutput);
		SizeSubWindows();
		CheckMenus();
		Redraw();
		break;

	case IDM_LINENUMBERMARGIN:
		lineNumbers = !lineNumbers;
		SetLineNumberWidth();
		CheckMenus();
		break;

	case IDM_SELMARGIN:
		margin = !margin;
		wEditor_.Call(SCI_SETMARGINWIDTHN, 1, margin ? marginWidth : 0);
		CheckMenus();
		break;

	case IDM_FOLDMARGIN:
		foldMargin = !foldMargin;
		wEditor_.Call(SCI_SETMARGINWIDTHN, 2, foldMargin ? foldMarginWidth : 0);
		CheckMenus();
		break;

	case IDM_VIEWEOL:
		wEditor_.Call(SCI_SETVIEWEOL, !wEditor_.Call(SCI_GETVIEWEOL));
		CheckMenus();
		break;

	case IDM_VIEWTOOLBAR:
		tbVisible = !tbVisible;
		ShowToolBar();
		CheckMenus();
		break;

	case IDM_TOGGLEOUTPUT:
		ToggleOutputVisible();
		CheckMenus();
		break;

	case IDM_TOGGLEPARAMETERS:
		ParametersDialog(false);
		CheckMenus();
		break;

	case IDM_WRAP:
		wrap = !wrap;
		wEditor_.Call(SCI_SETWRAPMODE, wrap ? wrapStyle : SC_WRAP_NONE);
		CheckMenus();
		break;

	case IDM_WRAPOUTPUT:
		wrapOutput = !wrapOutput;
		wOutput_.Call(SCI_SETWRAPMODE, wrapOutput ? wrapStyle : SC_WRAP_NONE);
		CheckMenus();
		break;

	case IDM_READONLY:
		CurrentBuffer()->isReadOnly = !CurrentBuffer()->isReadOnly;
		wEditor_.Call(SCI_SETREADONLY, CurrentBuffer()->isReadOnly);
		UpdateStatusBar(true);
		CheckMenus();
		SetBuffersMenu();
		break;

	case IDM_VIEWTABBAR:
		tabVisible = !tabVisible;
		ShowTabBar();
		CheckMenus();
		break;

	case IDM_VIEWSTATUSBAR:
		sbVisible = !sbVisible;
		ShowStatusBar();
		UpdateStatusBar(true);
		CheckMenus();
		break;

	case IDM_CLEAROUTPUT:
		wOutput_.Call(SCI_CLEARALL);
		break;

	case IDM_SWITCHPANE:
		if (pwFocussed == &wEditor_)
			WindowSetFocus(wOutput_);
		else
			WindowSetFocus(wEditor_);
		break;

	case IDM_EOL_CRLF:
		wEditor_.Call(SCI_SETEOLMODE, SC_EOL_CRLF);
		CheckMenus();
		UpdateStatusBar(false);
		break;

	case IDM_EOL_CR:
		wEditor_.Call(SCI_SETEOLMODE, SC_EOL_CR);
		CheckMenus();
		UpdateStatusBar(false);
		break;
	case IDM_EOL_LF:
		wEditor_.Call(SCI_SETEOLMODE, SC_EOL_LF);
		CheckMenus();
		UpdateStatusBar(false);
		break;
	case IDM_EOL_CONVERT:
		wEditor_.Call(SCI_CONVERTEOLS, wEditor_.Call(SCI_GETEOLMODE));
		break;

	case IDM_VIEWSPACE:
		ViewWhitespace(!wEditor_.Call(SCI_GETVIEWWS));
		CheckMenus();
		Redraw();
		break;

	case IDM_VIEWGUIDES: {
			const bool viewIG = wEditor_.Call(SCI_GETINDENTATIONGUIDES, 0, 0) == 0;
			wEditor_.Call(SCI_SETINDENTATIONGUIDES, viewIG ? indentExamine : SC_IV_NONE);
			CheckMenus();
			Redraw();
		}
		break;

	case IDM_COMPILE: {
			if (SaveIfUnsureForBuilt() != kSaveCancelled) {
				SelectionIntoProperties();
				AddCommand(props.GetWild("command.compile.", FileNameExt().AsUTF8().c_str()), "",
				        SubsystemType("command.compile.subsystem."));
				if (jobQueue.HasCommandToRun())
					Execute();
			}
		}
		break;

	case IDM_BUILD: {
			if (SaveIfUnsureForBuilt() != kSaveCancelled) {
				SelectionIntoProperties();
				AddCommand(
				    props.GetWild("command.build.", FileNameExt().AsUTF8().c_str()),
				    props.GetNewExpandString("command.build.directory.", FileNameExt().AsUTF8().c_str()),
				    SubsystemType("command.build.subsystem."));
				if (jobQueue.HasCommandToRun()) {
					jobQueue.isBuilding = true;
					Execute();
				}
			}
		}
		break;

	case IDM_CLEAN: {
			if (SaveIfUnsureForBuilt() != kSaveCancelled) {
				SelectionIntoProperties();
				AddCommand(props.GetWild("command.clean.", FileNameExt().AsUTF8().c_str()), "",
				        SubsystemType("command.clean.subsystem."));
				if (jobQueue.HasCommandToRun())
					Execute();
			}
		}
		break;

	case IDM_GO: {
			if (SaveIfUnsureForBuilt() != kSaveCancelled) {
				SelectionIntoProperties();
				int flags = 0;

				if (!jobQueue.isBuilt) {
					std::string buildcmd = props.GetNewExpandString("command.go.needs.", FileNameExt().AsUTF8().c_str());
					AddCommand(buildcmd, "",
					        SubsystemType("command.go.needs.subsystem."));
					if (buildcmd.length() > 0) {
						jobQueue.isBuilding = true;
						flags |= jobForceQueue;
					}
				}
				AddCommand(props.GetWild("command.go.", FileNameExt().AsUTF8().c_str()), "",
				        SubsystemType("command.go.subsystem."), "", flags);
				if (jobQueue.HasCommandToRun())
					Execute();
			}
		}
		break;

	case IDM_STOPEXECUTE:
		StopExecute();
		break;

	case IDM_NEXTMSG:
		GoMessage(1);
		break;

	case IDM_PREVMSG:
		GoMessage(-1);
		break;

	case IDM_OPENLOCALPROPERTIES:
		OpenProperties(IDM_OPENLOCALPROPERTIES);
		WindowSetFocus(wEditor_);
		break;

	case IDM_OPENUSERPROPERTIES:
		OpenProperties(IDM_OPENUSERPROPERTIES);
		WindowSetFocus(wEditor_);
		break;

	case IDM_OPENGLOBALPROPERTIES:
		OpenProperties(IDM_OPENGLOBALPROPERTIES);
		WindowSetFocus(wEditor_);
		break;

	case IDM_OPENABBREVPROPERTIES:
		OpenProperties(IDM_OPENABBREVPROPERTIES);
		WindowSetFocus(wEditor_);
		break;

	case IDM_OPENLUAEXTERNALFILE:
		OpenProperties(IDM_OPENLUAEXTERNALFILE);
		WindowSetFocus(wEditor_);
		break;

	case IDM_OPENDIRECTORYPROPERTIES:
		OpenProperties(IDM_OPENDIRECTORYPROPERTIES);
		WindowSetFocus(wEditor_);
		break;

	case IDM_SRCWIN:
		break;

	case IDM_BOOKMARK_TOGGLE:
		BookmarkToggle();
		break;

	case IDM_BOOKMARK_NEXT:
		BookmarkNext(true);
		break;

	case IDM_BOOKMARK_PREV:
		BookmarkNext(false);
		break;

	case IDM_BOOKMARK_NEXT_SELECT:
		BookmarkNext(true, true);
		break;

	case IDM_BOOKMARK_PREV_SELECT:
		BookmarkNext(false, true);
		break;

	case IDM_BOOKMARK_CLEARALL:
		wEditor_.Call(SCI_MARKERDELETEALL, kMarkerBookmark);
		RemoveFindMarks();
		break;

	case IDM_BOOKMARK_SELECT_ALL:
		BookmarkSelectAll();
		break;

	case IDM_TABSIZE:
		TabSizeDialog();
		break;

	case IDM_MONOFONT:
		CurrentBuffer()->useMonoFont = !CurrentBuffer()->useMonoFont;
		ReadFontProperties();
		Redraw();
		break;

	case IDM_MACROLIST:
		AskMacroList();
		break;
	case IDM_MACROPLAY:
		StartPlayMacro();
		break;
	case IDM_MACRORECORD:
		StartRecordMacro();
		break;
	case IDM_MACROSTOPRECORD:
		StopRecordMacro();
		break;

	case IDM_HELP: {
			SelectionIntoProperties();
			AddCommand(props.GetWild("command.help.", FileNameExt().AsUTF8().c_str()), "",
			        SubsystemType("command.help.subsystem."));
			if (!jobQueue.IsExecuting() && jobQueue.HasCommandToRun()) {
				jobQueue.isBuilding = true;
				Execute();
			}
		}
		break;

	case IDM_HELP_SCITE: {
			SelectionIntoProperties();
			AddCommand(props.GetString("command.scite.help"), "",
			        SubsystemFromChar(props.GetString("command.scite.help.subsystem")[0]));
			if (!jobQueue.IsExecuting() && jobQueue.HasCommandToRun()) {
				jobQueue.isBuilding = true;
				Execute();
			}
		}
		break;

	default:
		if ((cmdID >= kBufferCmdID) &&
		        (cmdID < kBufferCmdID + buffers.size())) {
			SetDocumentAt(cmdID - kBufferCmdID);
			CheckReload();
		} else if ((cmdID >= kFileStackCmdID) &&
		        (cmdID < kFileStackCmdID + kFileStackMax)) {
			StackMenu(cmdID - kFileStackCmdID);
		} else if (cmdID >= kImportCmdID &&
		        (cmdID < kImportCmdID + kImportMax)) {
			ImportMenu(cmdID - kImportCmdID);
		} else if (cmdID >= IDM_TOOLS && cmdID < IDM_TOOLS + kToolMax) {
			ToolsMenu(cmdID - IDM_TOOLS);
		} else if (cmdID >= IDM_LANGUAGE && cmdID < IDM_LANGUAGE + 100) {
			SetOverrideLanguage(cmdID - IDM_LANGUAGE);
		}
		break;
	}
}

static int LevelNumber(int level) {
	return level & SC_FOLDLEVELNUMBERMASK;
}

void CuteTextBase::FoldChanged(int line, int levelNow, int levelPrev) {
	// Unfold any regions where the new fold structure makes that fold wrong.
	// Will only unfold and show lines and never fold or hide lines.
	if (levelNow & SC_FOLDLEVELHEADERFLAG) {
		if (!(levelPrev & SC_FOLDLEVELHEADERFLAG)) {
			// Adding a fold point.
			wEditor_.Call(SCI_SETFOLDEXPANDED, line, 1);
			if (!wEditor_.Call(SCI_GETALLLINESVISIBLE))
				ExpandFolds(line, true, levelPrev);
		}
	} else if (levelPrev & SC_FOLDLEVELHEADERFLAG) {
		const int prevLine = line - 1;
		const int levelPrevLine = wEditor_.Call(SCI_GETFOLDLEVEL, prevLine);

		// Combining two blocks where the first block is collapsed (e.g. by deleting the line(s) which separate(s) the two blocks)
		if ((LevelNumber(levelPrevLine) == LevelNumber(levelNow)) && !wEditor_.Call(SCI_GETLINEVISIBLE, prevLine)) {
			const int parentLine = wEditor_.Call(SCI_GETFOLDPARENT, prevLine);
			const int levelParentLine = wEditor_.Call(SCI_GETFOLDLEVEL, parentLine);
			wEditor_.Call(SCI_SETFOLDEXPANDED, parentLine, 1);
			ExpandFolds(parentLine, true, levelParentLine);
		}

		if (!wEditor_.Call(SCI_GETFOLDEXPANDED, line)) {
			// Removing the fold from one that has been contracted so should expand
			// otherwise lines are left invisible with no way to make them visible
			wEditor_.Call(SCI_SETFOLDEXPANDED, line, 1);
			if (!wEditor_.Call(SCI_GETALLLINESVISIBLE))
				// Combining two blocks where the second one is collapsed (e.g. by adding characters in the line which separates the two blocks)
				ExpandFolds(line, true, levelPrev);
		}
	}
	if (!(levelNow & SC_FOLDLEVELWHITEFLAG) &&
	        (LevelNumber(levelPrev) > LevelNumber(levelNow))) {
		if (!wEditor_.Call(SCI_GETALLLINESVISIBLE)) {
			// See if should still be hidden
			const int parentLine = wEditor_.Call(SCI_GETFOLDPARENT, line);
			if (parentLine < 0) {
				wEditor_.Call(SCI_SHOWLINES, line, line);
			} else if (wEditor_.Call(SCI_GETFOLDEXPANDED, parentLine) && wEditor_.Call(SCI_GETLINEVISIBLE, parentLine)) {
				wEditor_.Call(SCI_SHOWLINES, line, line);
			}
		}
	}
	// Combining two blocks where the first one is collapsed (e.g. by adding characters in the line which separates the two blocks)
	if (!(levelNow & SC_FOLDLEVELWHITEFLAG) && (LevelNumber(levelPrev) < LevelNumber(levelNow))) {
		if (!wEditor_.Call(SCI_GETALLLINESVISIBLE)) {
			const int parentLine = wEditor_.Call(SCI_GETFOLDPARENT, line);
			if (!wEditor_.Call(SCI_GETFOLDEXPANDED, parentLine) && wEditor_.Call(SCI_GETLINEVISIBLE, line)) {
				wEditor_.Call(SCI_SETFOLDEXPANDED, parentLine, 1);
				const int levelParentLine = wEditor_.Call(SCI_GETFOLDLEVEL, parentLine);
				ExpandFolds(parentLine, true, levelParentLine);
			}
		}
	}
}

void CuteTextBase::ExpandFolds(int line, bool expand, int level) {
	// Expand or contract line and all subordinates
	// level is the fold level of line
	const int lineMaxSubord = wEditor_.Call(SCI_GETLASTCHILD, line, LevelNumber(level));
	line++;
	wEditor_.Call(expand ? SCI_SHOWLINES : SCI_HIDELINES, line, lineMaxSubord);
	while (line <= lineMaxSubord) {
		const int levelLine = wEditor_.Call(SCI_GETFOLDLEVEL, line);
		if (levelLine & SC_FOLDLEVELHEADERFLAG) {
			wEditor_.Call(SCI_SETFOLDEXPANDED, line, expand ? 1 : 0);
		}
		line++;
	}
}

void CuteTextBase::FoldAll() {
	wEditor_.Call(SCI_COLOURISE, 0, -1);
	const int maxLine = wEditor_.Call(SCI_GETLINECOUNT);
	bool expanding = true;
	for (int lineSeek = 0; lineSeek < maxLine; lineSeek++) {
		if (wEditor_.Call(SCI_GETFOLDLEVEL, lineSeek) & SC_FOLDLEVELHEADERFLAG) {
			expanding = !wEditor_.Call(SCI_GETFOLDEXPANDED, lineSeek);
			break;
		}
	}
	for (int line = 0; line < maxLine; line++) {
		const int level = wEditor_.Call(SCI_GETFOLDLEVEL, line);
		if ((level & SC_FOLDLEVELHEADERFLAG) &&
		        (SC_FOLDLEVELBASE == LevelNumber(level))) {
			const int lineMaxSubord = wEditor_.Call(SCI_GETLASTCHILD, line, -1);
			if (expanding) {
				wEditor_.Call(SCI_SETFOLDEXPANDED, line, 1);
				ExpandFolds(line, true, level);
				line = lineMaxSubord;
			} else {
				wEditor_.Call(SCI_SETFOLDEXPANDED, line, 0);
				if (lineMaxSubord > line)
					wEditor_.Call(SCI_HIDELINES, line + 1, lineMaxSubord);
			}
		}
	}
}

void CuteTextBase::GotoLineEnsureVisible(int line) {
	wEditor_.Call(SCI_ENSUREVISIBLEENFORCEPOLICY, line);
	wEditor_.Call(SCI_GOTOLINE, line);
}

void CuteTextBase::EnsureRangeVisible(GUI::ScintillaWindow &win, int posStart, int posEnd, bool enforcePolicy) {
	const int lineStart = win.Call(SCI_LINEFROMPOSITION, Minimum(posStart, posEnd));
	const int lineEnd = win.Call(SCI_LINEFROMPOSITION, Maximum(posStart, posEnd));
	for (int line = lineStart; line <= lineEnd; line++) {
		win.Call(enforcePolicy ? SCI_ENSUREVISIBLEENFORCEPOLICY : SCI_ENSUREVISIBLE, line);
	}
}

bool CuteTextBase::MarginClick(int position, int modifiers) {
	const int lineClick = wEditor_.Call(SCI_LINEFROMPOSITION, position);
	if ((modifiers & SCMOD_SHIFT) && (modifiers & SCMOD_CTRL)) {
		FoldAll();
	} else {
		const int levelClick = wEditor_.Call(SCI_GETFOLDLEVEL, lineClick);
		if (levelClick & SC_FOLDLEVELHEADERFLAG) {
			if (modifiers & SCMOD_SHIFT) {
				EnsureAllChildrenVisible(lineClick, levelClick);
			} else if (modifiers & SCMOD_CTRL) {
				ToggleFoldRecursive(lineClick, levelClick);
			} else {
				// Toggle this line
				wEditor_.Call(SCI_TOGGLEFOLD, lineClick);
			}
		}
	}
	return true;
}

void CuteTextBase::ToggleFoldRecursive(int line, int level) {
	if (wEditor_.Call(SCI_GETFOLDEXPANDED, line)) {
		// This ensure fold structure created before the fold is expanded
		wEditor_.Call(SCI_GETLASTCHILD, line, LevelNumber(level));
		// Contract this line and all children
		wEditor_.Call(SCI_SETFOLDEXPANDED, line, 0);
		ExpandFolds(line, false, level);
	} else {
		// Expand this line and all children
		wEditor_.Call(SCI_SETFOLDEXPANDED, line, 1);
		ExpandFolds(line, true, level);
	}
}

void CuteTextBase::EnsureAllChildrenVisible(int line, int level) {
	// Ensure all children visible
	wEditor_.Call(SCI_SETFOLDEXPANDED, line, 1);
	ExpandFolds(line, true, level);
}

void CuteTextBase::NewLineInOutput() {
	if (jobQueue.IsExecuting())
		return;
	int line = wOutput_.Call(SCI_LINEFROMPOSITION,
	        wOutput_.Call(SCI_GETCURRENTPOS)) - 1;
	std::string cmd = GetLine(wOutput_, line);
	if (cmd == ">") {
		// Search output buffer for previous command
		line--;
		while (line >= 0) {
			cmd = GetLine(wOutput_, line);
			if (StartsWith(cmd, ">") && !StartsWith(cmd, ">Exit")) {
				cmd = cmd.substr(1);
				break;
			}
			line--;
		}
	} else if (StartsWith(cmd, ">")) {
		cmd = cmd.substr(1);
	}
	returnOutputToCommand = false;
	AddCommand(cmd.c_str(), "", jobCLI);
	Execute();
}

void CuteTextBase::Notify(SCNotification *notification) {
	bool handled = false;
	switch (notification->nmhdr.code) {
	case SCN_PAINTED:
		if ((notification->nmhdr.idFrom == IDM_SRCWIN) == (pwFocussed == &wEditor_)) {
			// Obly highlight focussed pane.
			// Manage delay before highlight when no user selection but there is word at the caret.
			// So the Delay is based on the blinking of caret, scroll...
			// If currentWordHighlight.statesOfDelay == currentWordHighlight.kDelay,
			// then there is word at the caret without selection, and need some delay.
			if (currentWordHighlight.statesOfDelay == currentWordHighlight.kDelay) {
				if (currentWordHighlight.elapsedTimes.Duration() >= 0.5) {
					currentWordHighlight.statesOfDelay = currentWordHighlight.kDelayJustEnded;
					HighlightCurrentWord(true);
					pwFocussed->InvalidateAll();
				}
			}
		}
		break;

	case SCN_FOCUSIN:
		SetPaneFocus(notification->nmhdr.idFrom == IDM_SRCWIN);
		CheckMenus();
		break;

	case SCN_FOCUSOUT:
		CheckMenus();
		break;

	case SCN_STYLENEEDED: {
			if (extender) {
				// Colourisation may be performed by script
				if ((notification->nmhdr.idFrom == IDM_SRCWIN) && (lexLanguage_ == SCLEX_CONTAINER)) {
					int endStyled = wEditor_.Call(SCI_GETENDSTYLED);
					const int lineEndStyled = wEditor_.Call(SCI_LINEFROMPOSITION, endStyled);
					endStyled = wEditor_.Call(SCI_POSITIONFROMLINE, lineEndStyled);
					StyleWriter styler(wEditor_);
					int styleStart = 0;
					if (endStyled > 0)
						styleStart = styler.StyleAt(endStyled - 1);
					styler.SetCodePage(codePage_);
					extender->OnStyle(endStyled, static_cast<int>(notification->position - endStyled),
					        styleStart, &styler);
					styler.Flush();
				}
			}
		}
		break;

	case SCN_CHARADDED:
		if (extender)
			handled = extender->OnChar(static_cast<char>(notification->ch));
		if (!handled) {
			if (notification->nmhdr.idFrom == IDM_SRCWIN) {
				CharAdded(notification->ch);
			} else {
				CharAddedOutput(notification->ch);
			}
		}
		break;

	case SCN_SAVEPOINTREACHED:
		if (notification->nmhdr.idFrom == IDM_SRCWIN) {
			if (extender)
				handled = extender->OnSavePointReached();
			if (!handled) {
				CurrentBuffer()->isDirty = false;
			}
		}
		CheckMenus();
		SetWindowName();
		SetBuffersMenu();
		break;

	case SCN_SAVEPOINTLEFT:
		if (notification->nmhdr.idFrom == IDM_SRCWIN) {
			if (extender)
				handled = extender->OnSavePointLeft();
			if (!handled) {
				CurrentBuffer()->isDirty = true;
				jobQueue.isBuilt = false;
			}
		}
		CheckMenus();
		SetWindowName();
		SetBuffersMenu();
		break;

	case SCN_DOUBLECLICK:
		if (extender)
			handled = extender->OnDoubleClick();
		if (!handled && notification->nmhdr.idFrom == IDM_RUNWIN) {
			GoMessage(0);
		}
		break;

	case SCN_UPDATEUI:
		if (extender)
			handled = extender->OnUpdateUI();
		if (!handled) {
			BraceMatch(notification->nmhdr.idFrom == IDM_SRCWIN);
			if (notification->nmhdr.idFrom == IDM_SRCWIN) {
				UpdateStatusBar(false);
			}
			CheckMenusClipboard();
		}
		if (CurrentBuffer()->findMarks == Buffer::kFmModified) {
			RemoveFindMarks();
		}
		if (notification->updated & (SC_UPDATE_SELECTION | SC_UPDATE_CONTENT)) {
			if ((notification->nmhdr.idFrom == IDM_SRCWIN) == (pwFocussed == &wEditor_)) {
				// Obly highlight focussed pane.
				if (notification->updated & SC_UPDATE_SELECTION) {
					currentWordHighlight.statesOfDelay = currentWordHighlight.kNoDelay; // Selection has just been updated, so delay is disabled.
					currentWordHighlight.textHasChanged = false;
					HighlightCurrentWord(true);
				} else if (currentWordHighlight.textHasChanged) {
					HighlightCurrentWord(false);
				}
				//	if (notification->updated & SC_UPDATE_SELECTION)
				//if (currentWordHighlight.statesOfDelay != currentWordHighlight.kDelayJustEnded)
				//else
				//	currentWordHighlight.statesOfDelay = currentWordHighlight.kDelayAlreadyElapsed;
			}
		}
		break;

	case SCN_MODIFIED:
		if (notification->nmhdr.idFrom == IDM_SRCWIN)
			CurrentBuffer()->DocumentModified();
		if (notification->modificationType & SC_LASTSTEPINUNDOREDO) {
			//when the user hits undo or redo, several normal insert/delete
			//notifications may fire, but we will end up here in the end
			EnableAMenuItem(IDM_UNDO, CallFocusedElseDefault(true, SCI_CANUNDO));
			EnableAMenuItem(IDM_REDO, CallFocusedElseDefault(true, SCI_CANREDO));
		} else if (notification->modificationType & (SC_MOD_INSERTTEXT | SC_MOD_DELETETEXT)) {
			if ((notification->nmhdr.idFrom == IDM_SRCWIN) == (pwFocussed == &wEditor_)) {
				currentWordHighlight.textHasChanged = true;
			}
			//this will be called a lot, and usually means "typing".
			EnableAMenuItem(IDM_UNDO, true);
			EnableAMenuItem(IDM_REDO, false);
			if (CurrentBuffer()->findMarks == Buffer::kFmMarked) {
				CurrentBuffer()->findMarks = Buffer::kFmModified;
			}
		}

		if (notification->linesAdded && lineNumbers && lineNumbersExpand)
			SetLineNumberWidth();

		if (0 != (notification->modificationType & SC_MOD_CHANGEFOLD)) {
			FoldChanged(static_cast<int>(notification->line),
			        notification->foldLevelNow, notification->foldLevelPrev);
		}
		break;

	case SCN_MARGINCLICK: {
			if (extender)
				handled = extender->OnMarginClick();
			if (!handled) {
				if (notification->margin == 2) {
					MarginClick(static_cast<int>(notification->position), notification->modifiers);
				}
			}
		}
		break;

	case SCN_NEEDSHOWN: {
			EnsureRangeVisible(wEditor_, static_cast<int>(notification->position), static_cast<int>(notification->position + notification->length), false);
		}
		break;

	case SCN_USERLISTSELECTION: {
			if (notification->wParam == 2)
				ContinueMacroList(notification->text);
			else if (extender && notification->wParam > 2)
				extender->OnUserListSelection(static_cast<int>(notification->wParam), notification->text);
		}
		break;

	case SCN_CALLTIPCLICK: {
			if (notification->position == 1 && currentCallTip > 0) {
				currentCallTip--;
				FillFunctionDefinition();
			} else if (notification->position == 2 && currentCallTip + 1 < maxCallTips) {
				currentCallTip++;
				FillFunctionDefinition();
			}
		}
		break;

	case SCN_MACRORECORD:
		RecordMacroCommand(notification);
		break;

	case SCN_URIDROPPED:
		OpenUriList(notification->text);
		break;

	case SCN_DWELLSTART:
		if (extender && (INVALID_POSITION != notification->position)) {
			int endWord = static_cast<int>(notification->position);
			int position = static_cast<int>(notification->position);
			std::string message =
				RangeExtendAndGrab(wEditor_,
					position, endWord, &CuteTextBase::iswordcharforsel);
			if (message.length()) {
				extender->OnDwellStart(position, message.c_str());
			}
		}
		break;

	case SCN_DWELLEND:
		if (extender) {
			extender->OnDwellStart(0,""); // flags end of calltip
		}
		break;

	case SCN_ZOOM:
		SetLineNumberWidth();
		break;

	case SCN_MODIFYATTEMPTRO:
		AbandonAutomaticSave();
		break;
	}
}

void CuteTextBase::CheckMenusClipboard() {
	const bool hasSelection = !CallFocusedElseDefault(false, SCI_GETSELECTIONEMPTY);
	EnableAMenuItem(IDM_CUT, hasSelection);
	EnableAMenuItem(IDM_COPY, hasSelection);
	EnableAMenuItem(IDM_CLEAR, hasSelection);
	EnableAMenuItem(IDM_PASTE, CallFocusedElseDefault(true, SCI_CANPASTE));
	EnableAMenuItem(IDM_SELECTALL, true);
}

void CuteTextBase::CheckMenus() {
	CheckMenusClipboard();
	EnableAMenuItem(IDM_UNDO, CallFocusedElseDefault(true, SCI_CANUNDO));
	EnableAMenuItem(IDM_REDO, CallFocusedElseDefault(true, SCI_CANREDO));
	EnableAMenuItem(IDM_DUPLICATE, CurrentBuffer()->isReadOnly);
	EnableAMenuItem(IDM_SHOWCALLTIP, apis_ != 0);
	EnableAMenuItem(IDM_COMPLETE, apis_ != 0);
	CheckAMenuItem(IDM_SPLITVERTICAL, splitVertical);
	EnableAMenuItem(IDM_OPENFILESHERE, props.GetInt("check.if.already.open") != 0);
	CheckAMenuItem(IDM_OPENFILESHERE, openFilesHere);
	CheckAMenuItem(IDM_WRAP, wrap);
	CheckAMenuItem(IDM_WRAPOUTPUT, wrapOutput);
	CheckAMenuItem(IDM_READONLY, CurrentBuffer()->isReadOnly);
	CheckAMenuItem(IDM_FULLSCREEN, fullScreen);
	CheckAMenuItem(IDM_VIEWTOOLBAR, tbVisible);
	CheckAMenuItem(IDM_VIEWTABBAR, tabVisible);
	CheckAMenuItem(IDM_VIEWSTATUSBAR, sbVisible);
	CheckAMenuItem(IDM_VIEWEOL, wEditor_.Call(SCI_GETVIEWEOL));
	CheckAMenuItem(IDM_VIEWSPACE, wEditor_.Call(SCI_GETVIEWWS));
	CheckAMenuItem(IDM_VIEWGUIDES, wEditor_.Call(SCI_GETINDENTATIONGUIDES));
	CheckAMenuItem(IDM_LINENUMBERMARGIN, lineNumbers);
	CheckAMenuItem(IDM_SELMARGIN, margin);
	CheckAMenuItem(IDM_FOLDMARGIN, foldMargin);
	CheckAMenuItem(IDM_TOGGLEOUTPUT, heightOutput > 0);
	CheckAMenuItem(IDM_TOGGLEPARAMETERS, ParametersOpen());
	CheckAMenuItem(IDM_MONOFONT, CurrentBuffer()->useMonoFont);
	EnableAMenuItem(IDM_COMPILE, !jobQueue.IsExecuting() &&
	        props.GetWild("command.compile.", FileNameExt().AsUTF8().c_str()).size() != 0);
	EnableAMenuItem(IDM_BUILD, !jobQueue.IsExecuting() &&
	        props.GetWild("command.build.", FileNameExt().AsUTF8().c_str()).size() != 0);
	EnableAMenuItem(IDM_CLEAN, !jobQueue.IsExecuting() &&
	        props.GetWild("command.clean.", FileNameExt().AsUTF8().c_str()).size() != 0);
	EnableAMenuItem(IDM_GO, !jobQueue.IsExecuting() &&
	        props.GetWild("command.go.", FileNameExt().AsUTF8().c_str()).size() != 0);
	EnableAMenuItem(IDM_OPENDIRECTORYPROPERTIES, props.GetInt("properties.directory.enable") != 0);
	for (int toolItem = 0; toolItem < kToolMax; toolItem++)
		EnableAMenuItem(IDM_TOOLS + toolItem, ToolIsImmediate(toolItem) || !jobQueue.IsExecuting());
	EnableAMenuItem(IDM_STOPEXECUTE, jobQueue.IsExecuting());
	if (buffers.size() > 0) {
		TabSelect(buffers.Current());
		for (int bufferItem = 0; bufferItem < buffers.lengthVisible; bufferItem++) {
			CheckAMenuItem(IDM_BUFFER + bufferItem, bufferItem == buffers.Current());
		}
	}
	EnableAMenuItem(IDM_MACROPLAY, !recording);
	EnableAMenuItem(IDM_MACRORECORD, !recording);
	EnableAMenuItem(IDM_MACROSTOPRECORD, recording);
}

void CuteTextBase::ContextMenu(GUI::ScintillaWindow &wSource, GUI::Point pt, GUI::Window wCmd) {
	const int currentPos = wSource.Call(SCI_GETCURRENTPOS);
	const int anchor = wSource.Call(SCI_GETANCHOR);
	popup.CreatePopUp();
	const bool writable = !wSource.Call(SCI_GETREADONLY);
	AddToPopUp("Undo", IDM_UNDO, writable && wSource.Call(SCI_CANUNDO));
	AddToPopUp("Redo", IDM_REDO, writable && wSource.Call(SCI_CANREDO));
	AddToPopUp("");
	AddToPopUp("Cut", IDM_CUT, writable && currentPos != anchor);
	AddToPopUp("Copy", IDM_COPY, currentPos != anchor);
	AddToPopUp("Paste", IDM_PASTE, writable && wSource.Call(SCI_CANPASTE));
	AddToPopUp("Delete", IDM_CLEAR, writable && currentPos != anchor);
	AddToPopUp("");
	AddToPopUp("Select All", IDM_SELECTALL);
	AddToPopUp("");
	if (wSource.GetID() == wOutput_.GetID()) {
		AddToPopUp("Hide", IDM_TOGGLEOUTPUT, true);
	} else {
		AddToPopUp("Close", IDM_CLOSE, true);
	}
	std::string userContextMenu = props.GetNewExpandString("user.context.menu");
	std::replace(userContextMenu.begin(), userContextMenu.end(), '|', '\0');
	const char *userContextItem = userContextMenu.c_str();
	const char *endDefinition = userContextItem + userContextMenu.length();
	while (userContextItem < endDefinition) {
		const char *caption = userContextItem;
		userContextItem += strlen(userContextItem) + 1;
		if (userContextItem < endDefinition) {
			const int cmd = GetMenuCommandAsInt(userContextItem);
			userContextItem += strlen(userContextItem) + 1;
			AddToPopUp(caption, cmd);
		}
	}
	popup.Show(pt, wCmd);
}

/**
 * Ensure that a splitter bar position is inside the main window.
 */
int CuteTextBase::NormaliseSplit(int splitPos) {
	const GUI::Rectangle rcClient = GetClientRectangle();
	const int w = rcClient.Width();
	const int h = rcClient.Height();
	if (splitPos < 20)
		splitPos = 0;
	if (splitVertical) {
		if (splitPos > w - heightBar - 20)
			splitPos = w - heightBar;
	} else {
		if (splitPos > h - heightBar - 20)
			splitPos = h - heightBar;
	}
	return splitPos;
}

void CuteTextBase::MoveSplit(GUI::Point ptNewDrag) {
	int newHeightOutput = heightOutputStartDrag + (ptStartDrag.y - ptNewDrag.y);
	if (splitVertical)
		newHeightOutput = heightOutputStartDrag + (ptStartDrag.x - ptNewDrag.x);
	newHeightOutput = NormaliseSplit(newHeightOutput);
	if (heightOutput != newHeightOutput) {
		heightOutput = newHeightOutput;
		SizeContentWindows();
		//Redraw();
	}

	previousHeightOutput = newHeightOutput;
}

void CuteTextBase::TimerStart(int /* mask */) {
}

void CuteTextBase::TimerEnd(int /* mask */) {
}

void CuteTextBase::OnTimer() {
	if (delayBeforeAutoSave && (0 == dialogsOnScreen)) {
		// First save the visible buffer to avoid any switching if not needed
		if (CurrentBuffer()->NeedsSave(delayBeforeAutoSave)) {
			Save(kSfNone);
		}
		// Then look through the other buffers to save any that need to be saved
		const int currentBuffer = buffers.Current();
		for (int i = 0; i < buffers.length; i++) {
			if (buffers.buffers[i].NeedsSave(delayBeforeAutoSave)) {
				SetDocumentAt(i);
				Save(kSfNone);
			}
		}
		SetDocumentAt(currentBuffer);
	}
}

void CuteTextBase::SetIdler(bool on) {
	needIdle_ = on;
}

void CuteTextBase::OnIdle() {
	if (!findMarker.Complete()) {
		findMarker.Continue();
		return;
	}
	if (!matchMarker.Complete()) {
		matchMarker.Continue();
		return;
	}
	SetIdler(false);
}

void CuteTextBase::SetHomeProperties() {
	FilePath homepath = GetSciteDefaultHome();
	props.Set("SciteDefaultHome", homepath.AsUTF8().c_str());
	homepath = GetSciteUserHome();
	props.Set("SciteUserHome", homepath.AsUTF8().c_str());
}

void CuteTextBase::UIAvailable() {
	SetImportMenu();
	if (extender) {
		SetHomeProperties();
		extender->Initialise(this);
	}
}

/**
 * Find the character following a name which is made up of characters from
 * the set [a-zA-Z.]
 */
static GUI::gui_char AfterName(const GUI::gui_char *s) {
	while (*s && ((*s == '.') ||
	        (*s >= 'a' && *s <= 'z') ||
	        (*s >= 'A' && *s <= 'Z')))
		s++;
	return *s;
}

void CuteTextBase::PerformOne(char *action) {
	const unsigned int len = UnSlash(action);
	char *arg = strchr(action, ':');
	if (arg) {
		arg++;
		if (isprefix(action, "askfilename:")) {
			extender->OnMacro("filename", filePath_.AsUTF8().c_str());
		} else if (isprefix(action, "askproperty:")) {
			PropertyToDirector(arg);
		} else if (isprefix(action, "close:")) {
			Close();
			WindowSetFocus(wEditor_);
		} else if (isprefix(action, "currentmacro:")) {
			currentMacro = arg;
		} else if (isprefix(action, "cwd:")) {
			FilePath dirTarget(GUI::StringFromUTF8(arg));
			if (!dirTarget.SetWorkingDirectory()) {
				GUI::gui_string msg = LocaliseMessage("Invalid directory '^0'.", dirTarget.AsInternal());
				WindowMessageBox(wCuteText_, msg);
			}
		} else if (isprefix(action, "enumproperties:")) {
			EnumProperties(arg);
		} else if (isprefix(action, "exportashtml:")) {
			SaveToHTML(GUI::StringFromUTF8(arg));
		} else if (isprefix(action, "exportasrtf:")) {
			SaveToRTF(GUI::StringFromUTF8(arg));
		} else if (isprefix(action, "exportaspdf:")) {
			SaveToPDF(GUI::StringFromUTF8(arg));
		} else if (isprefix(action, "exportaslatex:")) {
			SaveToTEX(GUI::StringFromUTF8(arg));
		} else if (isprefix(action, "exportasxml:")) {
			SaveToXML(GUI::StringFromUTF8(arg));
		} else if (isprefix(action, "find:") && wEditor_.Created()) {
			findWhat = arg;
			FindNext(false, false);
		} else if (isprefix(action, "goto:") && wEditor_.Created()) {
			const int line = atoi(arg) - 1;
			GotoLineEnsureVisible(line);
			// jump to column if given and greater than 0
			const char *colstr = strchr(arg, ',');
			if (colstr != NULL) {
				const int col = atoi(colstr + 1);
				if (col > 0) {
					const int pos = wEditor_.Call(SCI_GETCURRENTPOS) + col;
					// select the word you have found there
					const int wordStart = wEditor_.Call(SCI_WORDSTARTPOSITION, pos, true);
					const int wordEnd = wEditor_.Call(SCI_WORDENDPOSITION, pos, true);
					wEditor_.Call(SCI_SETSEL, wordStart, wordEnd);
				}
			}
		} else if (isprefix(action, "insert:") && wEditor_.Created()) {
			wEditor_.CallString(SCI_REPLACESEL, 0, arg);
		} else if (isprefix(action, "loadsession:")) {
			if (*arg) {
				LoadSessionFile(GUI::StringFromUTF8(arg).c_str());
				RestoreSession();
			}
		} else if (isprefix(action, "macrocommand:")) {
			ExecuteMacroCommand(arg);
		} else if (isprefix(action, "macroenable:")) {
			macrosEnabled = atoi(arg);
			SetToolsMenu();
		} else if (isprefix(action, "macrolist:")) {
			StartMacroList(arg);
		} else if (isprefix(action, "menucommand:")) {
			MenuCommand(atoi(arg));
		} else if (isprefix(action, "open:")) {
			Open(GUI::StringFromUTF8(arg), kOfSynchronous);
		} else if (isprefix(action, "output:") && wOutput_.Created()) {
			wOutput_.CallString(SCI_REPLACESEL, 0, arg);
		} else if (isprefix(action, "property:")) {
			PropertyFromDirector(arg);
		} else if (isprefix(action, "reloadproperties:")) {
			ReloadProperties();
		} else if (isprefix(action, "quit:")) {
			QuitProgram();
		} else if (isprefix(action, "replaceall:") && wEditor_.Created()) {
			if (len > strlen(action)) {
				const char *arg2 = arg + strlen(arg) + 1;
				findWhat = arg;
				replaceWhat = arg2;
				ReplaceAll(false);
			}
		} else if (isprefix(action, "saveas:")) {
			if (*arg) {
				SaveAs(GUI::StringFromUTF8(arg).c_str(), true);
			} else {
				SaveAsDialog();
			}
		} else if (isprefix(action, "savesession:")) {
			if (*arg) {
				SaveSessionFile(GUI::StringFromUTF8(arg).c_str());
			}
		} else if (isprefix(action, "extender:")) {
			extender->OnExecute(arg);
		} else if (isprefix(action, "focus:")) {
			ActivateWindow(arg);
		}
	}
}

static bool IsSwitchCharacter(GUI::gui_char ch) {
#ifdef __unix__
	return ch == '-';
#else
	return (ch == '-') || (ch == '/');
#endif
}

// Called by CuteTextBase::PerformOne when action="enumproperties:"
void CuteTextBase::EnumProperties(const char *propkind) {
	PropSetFile *pf = NULL;

	if (!extender)
		return;
	if (!strcmp(propkind, "dyn")) {
		SelectionIntoProperties(); // Refresh properties ...
		pf = &props;
	} else if (!strcmp(propkind, "local"))
		pf = &propsLocal;
	else if (!strcmp(propkind, "directory"))
		pf = &propsDirectory;
	else if (!strcmp(propkind, "user"))
		pf = &propsUser;
	else if (!strcmp(propkind, "base"))
		pf = &propsBase;
	else if (!strcmp(propkind, "embed"))
		pf = &propsEmbed;
	else if (!strcmp(propkind, "platform"))
		pf = &propsPlatform;
	else if (!strcmp(propkind, "abbrev"))
		pf = &propsAbbrev;

	if (pf != NULL) {
		const char *key = NULL;
		const char *val = NULL;
		bool b = pf->GetFirst(key, val);
		while (b) {
			SendOneProperty(propkind, key, val);
			b = pf->GetNext(key, val);
		}
	}
}

void CuteTextBase::SendOneProperty(const char *kind, const char *key, const char *val) {
	std::string m = kind;
	m += ":";
	m += key;
	m += "=";
	m += val;
	extender->SendProperty(m.c_str());
}

void CuteTextBase::PropertyFromDirector(const char *arg) {
	props.SetLine(arg);
}

void CuteTextBase::PropertyToDirector(const char *arg) {
	if (!extender)
		return;
	SelectionIntoProperties();
	const std::string gotprop = props.GetString(arg);
	extender->OnMacro("macro:stringinfo", gotprop.c_str());
}

/**
 * Menu/Toolbar command "Record".
 */
void CuteTextBase::StartRecordMacro() {
	recording = true;
	CheckMenus();
	wEditor_.Call(SCI_STARTRECORD);
}

/**
 * Received a SCN_MACRORECORD from Scintilla: send it to director.
 */
bool CuteTextBase::RecordMacroCommand(const SCNotification *notification) {
	if (extender) {
		std::string sMessage = StdStringFromInteger(notification->message);
		sMessage += ";";
		sMessage += StdStringFromSizeT(static_cast<size_t>(notification->wParam));
		sMessage += ";";
		const char *t = reinterpret_cast<const char *>(notification->lParam);
		if (t != NULL) {
			//format : "<message>;<wParam>;1;<text>"
			sMessage += "1;";
			sMessage += t;
		} else {
			//format : "<message>;<wParam>;0;"
			sMessage += "0;";
		}
		const bool handled = extender->OnMacro("macro:record", sMessage.c_str());
		return handled;
	}
	return true;
}

/**
 * Menu/Toolbar command "Stop recording".
 */
void CuteTextBase::StopRecordMacro() {
	wEditor_.Call(SCI_STOPRECORD);
	if (extender)
		extender->OnMacro("macro:stoprecord", "");
	recording = false;
	CheckMenus();
}

/**
 * Menu/Toolbar command "Play macro...": tell director to build list of Macro names
 * Through this call, user has access to all macros in Filerx.
 */
void CuteTextBase::AskMacroList() {
	if (extender)
		extender->OnMacro("macro:getlist", "");
}

/**
 * List of Macro names has been created. Ask Scintilla to show it.
 */
bool CuteTextBase::StartMacroList(const char *words) {
	if (words) {
		wEditor_.CallString(SCI_USERLISTSHOW, 2, words); //listtype=2
	}

	return true;
}

/**
 * User has chosen a macro in the list. Ask director to execute it.
 */
void CuteTextBase::ContinueMacroList(const char *stext) {
	if ((extender) && (*stext != '\0')) {
		currentMacro = stext;
		StartPlayMacro();
	}
}

/**
 * Menu/Toolbar command "Play current macro" (or called from ContinueMacroList).
 */
void CuteTextBase::StartPlayMacro() {
	if (extender)
		extender->OnMacro("macro:run", currentMacro.c_str());
}

/*
SciTE received a macro command from director : execute it.
If command needs answer (SCI_GETTEXTLENGTH ...) : give answer to director
*/

static unsigned int ReadNum(const char *&t) {
	const char *argend = strchr(t, ';');	// find ';'
	unsigned int v = 0;
	if (*t)
		v = atoi(t);					// read value
	t = (argend) ? (argend + 1) : nullptr;	// update pointer
	return v;						// return value
}

void CuteTextBase::ExecuteMacroCommand(const char *command) {
	const char *nextarg = command;
	uptr_t wParam;
	sptr_t lParam = 0;
	int rep = 0;				//Scintilla's answer
	const char *answercmd;
	int l;
	std::string string1;
	char params[4];
	// This code does not validate its input which may cause crashes when bad.
	// 'params' describes types of return values and of arguments.
	// There are exactly 3 characters: return type, wParam, lParam.
	// 0 : void or no param
	// I : integer
	// S : string
	// R : string (for wParam only)
	// For example, "4004;0RS;fruit;mango" performs SCI_SETPROPERTY("fruit","mango") with no return

	// Extract message, parameter specification, wParam, lParam

	const unsigned int message = ReadNum(nextarg);
	if (!nextarg) {
		Trace("Malformed macro command.\n");
		return;
	}
	strncpy(params, nextarg, 3);
	params[3] = '\0';
	nextarg += 4;
	if (*(params + 1) == 'R') {
		// in one function wParam is a string  : void SetProperty(string key,string name)
		const char *s1 = nextarg;
		while (*nextarg != ';')
			nextarg++;
		string1.assign(s1, nextarg - s1);
		wParam = UptrFromString(string1.c_str());
		nextarg++;
	} else {
		wParam = ReadNum(nextarg);
	}

	if (*(params + 2) == 'S')
		lParam = SptrFromString(nextarg);
	else if ((*(params + 2) == 'I') && nextarg)	// nextarg check avoids warning from clang analyze
		lParam = atoi(nextarg);

	if (*params == '0') {
		// no answer ...
		wEditor_.Call(message, wParam, lParam);
		return;
	}

	if (*params == 'S') {
		// string answer
		if (message == SCI_GETSELTEXT) {
			l = wEditor_.Call(SCI_GETSELTEXT, 0, 0);
			wParam = 0;
		} else if (message == SCI_GETCURLINE) {
			const int line = wEditor_.Call(SCI_LINEFROMPOSITION, wEditor_.Call(SCI_GETCURRENTPOS));
			l = wEditor_.Call(SCI_LINELENGTH, line);
			wParam = l;
		} else if (message == SCI_GETTEXT) {
			l = wEditor_.Call(SCI_GETLENGTH);
			wParam = l;
		} else if (message == SCI_GETLINE) {
			l = wEditor_.Call(SCI_LINELENGTH, wParam);
		} else {
			l = 0; //unsupported calls EM
		}
		answercmd = "stringinfo:";

	} else {
		//int answer
		answercmd = "intinfo:";
		l = 30;
	}

	std::string tbuff = answercmd;
	const size_t alen = strlen(answercmd);
	tbuff.resize(l + alen + 1);
	if (*params == 'S')
		lParam = SptrFromPointer(&tbuff[alen]);

	if (l > 0)
		rep = wEditor_.Call(message, wParam, lParam);
	if (*params == 'I')
		sprintf(&tbuff[alen], "%0d", rep);
	extender->OnMacro("macro", tbuff.c_str());
}

/**
 * Process all the command line arguments.
 * Arguments that start with '-' (also '/' on Windows) are switches or commands with
 * other arguments being file names which are opened. Commands are distinguished
 * from switches by containing a ':' after the command name.
 * The print switch /p is special cased.
 * Processing occurs in two phases to allow switches that occur before any file opens
 * to be evaluated before creating the UI.
 * Call twice, first with phase=0, then with phase=1 after creating UI.
 */
bool CuteTextBase::ProcessCommandLine(const GUI::gui_string &args, int phase) {
	bool performPrint = false;
	bool evaluate = phase == 0;
	std::vector<GUI::gui_string> wlArgs = ListFromString(args);
	// Convert args to vector
	for (size_t i = 0; i < wlArgs.size(); i++) {
		const GUI::gui_char *arg = wlArgs[i].c_str();
		if (IsSwitchCharacter(arg[0])) {
			arg++;
			if (arg[0] == '\0' || (arg[0] == '-' && arg[1] == '\0')) {
				if (phase == 1) {
					OpenFromStdin(arg[0] == '-');
				}
			} else if (arg[0] == '@') {
				if (phase == 1) {
					OpenFilesFromStdin();
				}
			} else if ((tolower(arg[0]) == 'p') && (arg[1] == 0)) {
				performPrint = true;
			} else if (GUI::gui_string(arg) == GUI_TEXT("grep") && (wlArgs.size() - i >= 4)) {
				// in form -grep [w~][c~][d~][b~] "<file-patterns>" "<search-string>"
				GrepFlags gf = kGrepStdOut;
				if (wlArgs[i+1][0] == 'w')
					gf = static_cast<GrepFlags>(gf | kGrepWholeWord);
				if (wlArgs[i+1][1] == 'c')
					gf = static_cast<GrepFlags>(gf | kGrepMatchCase);
				if (wlArgs[i+1][2] == 'd')
					gf = static_cast<GrepFlags>(gf | kGrepDot);
				if (wlArgs[i+1][3] == 'b')
					gf = static_cast<GrepFlags>(gf | kGrepBinary);
				std::string sSearch = GUI::UTF8FromString(wlArgs[i+3].c_str());
				std::string unquoted = UnSlashString(sSearch.c_str());
				sptr_t originalEnd = 0;
				InternalGrep(gf, FilePath::GetWorkingDirectory().AsInternal(), wlArgs[i+2].c_str(), unquoted.c_str(), originalEnd);
				exit(0);
			} else {
				if (AfterName(arg) == ':') {
					if (StartsWith(arg, GUI_TEXT("open:")) || StartsWith(arg, GUI_TEXT("loadsession:"))) {
						if (phase == 0)
							return performPrint;
						else
							evaluate = true;
					}
					if (evaluate) {
						const std::string sArg = GUI::UTF8FromString(arg);
						std::vector<char> vcArg(sArg.size() + 1);
						std::copy(sArg.begin(), sArg.end(), vcArg.begin());
						PerformOne(&vcArg[0]);
					}
				} else {
					if (evaluate) {
						props.ReadLine(GUI::UTF8FromString(arg).c_str(), PropSetFile::rlActive,
							FilePath::GetWorkingDirectory(), filter_, NULL, 0);
					}
				}
			}
		} else {	// Not a switch: it is a file name
			if (phase == 0)
				return performPrint;
			else
				evaluate = true;

			if (!buffers.initialised) {
				InitialiseBuffers();
				if (props.GetInt("save.recent"))
					RestoreRecentMenu();
			}

			if (!PreOpenCheck(arg))
				Open(arg, static_cast<OpenFlags>(kOfQuiet|kOfSynchronous));
		}
	}
	if (phase == 1) {
		// If we have finished with all args and no buffer is open
		// try to load session.
		if (!buffers.initialised) {
			InitialiseBuffers();
			if (props.GetInt("save.recent"))
				RestoreRecentMenu();
			if (props.GetInt("buffers") && props.GetInt("save.session"))
				RestoreSession();
		}
		// No open file after session load so create empty document.
		if (filePath_.IsUntitled() && buffers.length == 1 && !buffers.buffers[0].isDirty) {
			Open(GUI_TEXT(""));
		}
	}
	return performPrint;
}

// Implement ExtensionAPI methods
sptr_t CuteTextBase::Send(Pane p, unsigned int msg, uptr_t wParam, sptr_t lParam) {
	if (p == paneEditor)
		return wEditor_.Call(msg, wParam, lParam);
	else
		return wOutput_.Call(msg, wParam, lParam);
}
std::string CuteTextBase::Range(Pane p, int start, int end) {
	const int len = end - start;
	std::string s(len, '\0');
	if (p == paneEditor)
		GetRange(wEditor_, start, end, s.data());
	else
		GetRange(wOutput_, start, end, s.data());
	return s;
}
void CuteTextBase::Remove(Pane p, int start, int end) {
	if (p == paneEditor) {
		wEditor_.Call(SCI_DELETERANGE, start, end-start);
	} else {
		wOutput_.Call(SCI_DELETERANGE, start, end-start);
	}
}

void CuteTextBase::Insert(Pane p, int pos, const char *s) {
	if (p == paneEditor)
		wEditor_.CallString(SCI_INSERTTEXT, pos, s);
	else
		wOutput_.CallString(SCI_INSERTTEXT, pos, s);
}

void CuteTextBase::Trace(const char *s) {
	ShowOutputOnMainThread();
	OutputAppendStringSynchronised(s);
}

std::string CuteTextBase::Property(const char *key) {
	return props.GetExpandedString(key);
}

void CuteTextBase::SetProperty(const char *key, const char *val) {
	const std::string value = props.GetExpandedString(key);
	if (value != val) {
		props.Set(key, val);
		needReadProperties = true;
	}
}

void CuteTextBase::UnsetProperty(const char *key) {
	props.Unset(key);
	needReadProperties = true;
}

uptr_t CuteTextBase::GetInstance() {
	return 0;
}

void CuteTextBase::ShutDown() {
	QuitProgram();
}

void CuteTextBase::Perform(const char *actionList) {
	std::vector<char> vActions(actionList, actionList + strlen(actionList) + 1);
	char *actions = &vActions[0];
	char *nextAct;
	while ((nextAct = strchr(actions, '\n')) != NULL) {
		*nextAct = '\0';
		PerformOne(actions);
		actions = nextAct + 1;
	}
	PerformOne(actions);
}

void CuteTextBase::DoMenuCommand(int cmdID) {
	MenuCommand(cmdID, 0);
}
