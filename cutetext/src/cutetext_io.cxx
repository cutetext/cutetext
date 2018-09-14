// This file is part of CuteText project
// Copyright (C) 2018 by James Zeng <james.zeng@hotmail.com>
// The LICENSE file describes the conditions under which this software may be distributed.
/**
 * @file cutetext_io.cxx
 * @author James Zeng
 * @date 2018-08-12
 * @brief Manage input and output with the system.
 *
 * @see https://github.com/cutetext/cutetext
 */

#include <cstddef>
#include <cstdlib>
// Older versions of GNU stdint.h require this definition to be able to see INT32_MAX
#define __STDC_LIMIT_MACROS
#include <cstdint>
#include <cstring>
#include <cctype>
#include <cstdio>
#include <ctime>

#include <string>
#include <vector>
#include <map>
#include <set>
#include <algorithm>
#include <memory>

#include <fcntl.h>

#include "ILoader.h"
#include "Scintilla.h"

#include "GUI.h"
#include "ScintillaWindow.h"

#include "StringList.h"
#include "StringHelpers.h"
#include "FilePath.h"
#include "StyleDefinition.h"
#include "PropSetFile.h"
#include "StyleWriter.h"
#include "Extender.h"
#include "SciTE.h"
#include "Mutex.h"
#include "JobQueue.h"
#include "Cookie.h"
#include "Worker.h"
#include "FileWorker.h"
#include "MatchMarker.h"
#include "SciTEBase.h"
#include "Utf8_16.h"

#if defined(GTK)
const GUI::GUIChar propUserFileName[] = GUI_TEXT(".SciTEUser.properties");
#elif defined(__APPLE__)
const GUI::GUIChar propUserFileName[] = GUI_TEXT("SciTEUser.properties");
#else
// Windows
const GUI::GUIChar propUserFileName[] = GUI_TEXT("SciTEUser.properties");
#endif
const GUI::GUIChar propGlobalFileName[] = GUI_TEXT("SciTEGlobal.properties");
const GUI::GUIChar propAbbrevFileName[] = GUI_TEXT("abbrev.properties");

void SciTEBase::SetFileName(const FilePath &openName, bool fixCase) {
	if (openName.AsInternal()[0] == '\"') {
		// openName is surrounded by double quotes
		GUI::GUIString pathCopy = openName.AsInternal();
		pathCopy = pathCopy.substr(1, pathCopy.size() - 2);
		filePath_.Set(pathCopy);
	} else {
		filePath_.Set(openName);
	}

	// Break fullPath into directory and file name using working directory for relative paths
	if (!filePath_.IsAbsolute()) {
		// Relative path. Since we ran AbsolutePath, we probably are here because fullPath is empty.
		filePath_.SetDirectory(filePath_.Directory());
	}

	if (fixCase) {
		filePath_.FixName();
	}

	ReadLocalPropFile();

	props_.Set("FilePath", filePath_.AsUTF8().c_str());
	props_.Set("FileDir", filePath_.Directory().AsUTF8().c_str());
	props_.Set("FileName", filePath_.BaseName().AsUTF8().c_str());
	props_.Set("FileExt", filePath_.Extension().AsUTF8().c_str());
	props_.Set("FileNameExt", FileNameExt().AsUTF8().c_str());

	SetWindowName();
	if (buffers_.buffers_.size() > 0)
		CurrentBuffer()->file.Set(filePath_);
}

// See if path exists.
// If path is not absolute, it is combined with dir.
// If resultPath is not NULL, it receives the absolute path if it exists.
bool SciTEBase::Exists(const GUI::GUIChar *dir, const GUI::GUIChar *path, FilePath *resultPath) {
	FilePath copy(path);
	if (!copy.IsAbsolute() && dir) {
		copy.SetDirectory(dir);
	}
	if (!copy.Exists())
		return false;
	if (resultPath) {
		resultPath->Set(copy.AbsolutePath());
	}
	return true;
}

void SciTEBase::CountLineEnds(int &linesCR, int &linesLF, int &linesCRLF) {
	linesCR = 0;
	linesLF = 0;
	linesCRLF = 0;
	const int lengthDoc = LengthDocument();
	char chPrev = ' ';
	TextReader acc(wEditor_);
	char chNext = acc.SafeGetCharAt(0);
	for (int i = 0; i < lengthDoc; i++) {
		const char ch = chNext;
		chNext = acc.SafeGetCharAt(i + 1);
		if (ch == '\r') {
			if (chNext == '\n')
				linesCRLF++;
			else
				linesCR++;
		} else if (ch == '\n') {
			if (chPrev != '\r') {
				linesLF++;
			}
		} else if (i > 1000000) {
			return;
		}
		chPrev = ch;
	}
}

void SciTEBase::DiscoverEOLSetting() {
	SetEol();
	if (props_.GetInt("eol.auto")) {
		int linesCR;
		int linesLF;
		int linesCRLF;
		CountLineEnds(linesCR, linesLF, linesCRLF);
		if (((linesLF >= linesCR) && (linesLF > linesCRLF)) || ((linesLF > linesCR) && (linesLF >= linesCRLF)))
			wEditor_.Call(SCI_SETEOLMODE, SC_EOL_LF);
		else if (((linesCR >= linesLF) && (linesCR > linesCRLF)) || ((linesCR > linesLF) && (linesCR >= linesCRLF)))
			wEditor_.Call(SCI_SETEOLMODE, SC_EOL_CR);
		else if (((linesCRLF >= linesLF) && (linesCRLF > linesCR)) || ((linesCRLF > linesLF) && (linesCRLF >= linesCR)))
			wEditor_.Call(SCI_SETEOLMODE, SC_EOL_CRLF);
	}
}

// Look inside the first line for a #! clue regarding the language
std::string SciTEBase::DiscoverLanguage() {
	const int length = Minimum(LengthDocument(), 64 * 1024);
	std::string buf = GetRangeString(wEditor_, 0, length);
	std::string languageOverride = "";
	std::string l1 = ExtractLine(buf.c_str(), length);
	if (StartsWith(l1, "<?xml")) {
		languageOverride = "xml";
	} else if (StartsWith(l1, "#!")) {
		l1 = l1.substr(2);
		std::replace(l1.begin(), l1.end(), '\\', ' ');
		std::replace(l1.begin(), l1.end(), '/', ' ');
		std::replace(l1.begin(), l1.end(), '\t', ' ');
		Substitute(l1, "  ", " ");
		Substitute(l1, "  ", " ");
		Substitute(l1, "  ", " ");
		::Remove(l1, std::string("\r"));
		::Remove(l1, std::string("\n"));
		if (StartsWith(l1, " ")) {
			l1 = l1.substr(1);
		}
		std::replace(l1.begin(), l1.end(), ' ', '\0');
		l1.append(1, '\0');
		const char *word = l1.c_str();
		while (*word) {
			std::string propShBang("shbang.");
			propShBang.append(word);
			std::string langShBang = props_.GetExpandedString(propShBang.c_str());
			if (langShBang.length()) {
				languageOverride = langShBang;
			}
			word += strlen(word) + 1;
		}
	}
	if (languageOverride.length()) {
		languageOverride.insert(0, "x.");
	}
	return languageOverride;
}

void SciTEBase::DiscoverIndentSetting() {
	const int lengthDoc = std::min(LengthDocument(), 1000000);
	TextReader acc(wEditor_);
	bool newline = true;
	int indent = 0; // current line indentation
	int tabSizes[9] = { 0, 0, 0, 0, 0, 0, 0, 0, 0 }; // number of lines with corresponding indentation (index 0 - tab)
	int prevIndent = 0; // previous line indentation
	int prevTabSize = -1; // previous line tab size
	for (int i = 0; i < lengthDoc; i++) {
		const char ch = acc[i];
		if (ch == '\r' || ch == '\n') {
			indent = 0;
			newline = true;
		} else if (newline && ch == ' ') {
			indent++;
		} else if (newline) {
			if (indent) {
				if (indent == prevIndent && prevTabSize != -1) {
					tabSizes[prevTabSize]++;
				} else if (indent > prevIndent && prevIndent != -1) {
					if (indent - prevIndent <= 8) {
						prevTabSize = indent - prevIndent;
						tabSizes[prevTabSize]++;
					} else {
						prevTabSize = -1;
					}
				}
				prevIndent = indent;
			} else if (ch == '\t') {
				tabSizes[0]++;
				prevIndent = -1;
			} else {
				prevIndent = 0;
			}
			newline = false;
		}
	}
	// maximum non-zero indent
	int topTabSize = -1;
	for (int j = 0; j <= 8; j++) {
		if (tabSizes[j] && (topTabSize == -1 || tabSizes[j] > tabSizes[topTabSize])) {
			topTabSize = j;
		}
	}
	// set indentation
	if (topTabSize == 0) {
		wEditor_.Call(SCI_SETUSETABS, 1);
	} else if (topTabSize != -1) {
		wEditor_.Call(SCI_SETUSETABS, 0);
		wEditor_.Call(SCI_SETINDENT, topTabSize);
	}
}

void SciTEBase::OpenCurrentFile(long long fileSize, bool suppressMessage, bool asynchronous) {
	if (CurrentBuffer()->pFileWorker) {
		// Already performing an asynchronous load or save so do not restart load
		if (!suppressMessage) {
			GUI::GUIString msg = LocaliseMessage("Could not open file '^0'.", filePath_.AsInternal());
			WindowMessageBox(wCuteText_, msg);
		}
		return;
	}

	FILE *fp = filePath_.Open(fileRead);
	if (!fp) {
		if (!suppressMessage) {
			GUI::GUIString msg = LocaliseMessage("Could not open file '^0'.", filePath_.AsInternal());
			WindowMessageBox(wCuteText_, msg);
		}
		if (!wEditor_.Call(SCI_GETUNDOCOLLECTION)) {
			wEditor_.Call(SCI_SETUNDOCOLLECTION, 1);
		}
		return;
	}

	CurrentBuffer()->SetTimeFromFile();

	wEditor_.Call(SCI_BEGINUNDOACTION);	// Group together clear and insert
	wEditor_.Call(SCI_CLEARALL);

	CurrentBuffer()->lifeState = Buffer::kReading;
	if (asynchronous) {
		// Turn grey while loading
		wEditor_.Call(SCI_STYLESETBACK, STYLE_DEFAULT, 0xEEEEEE);
		wEditor_.Call(SCI_SETREADONLY, 1);
		assert(CurrentBufferConst()->pFileWorker == NULL);
		ILoader *pdocLoad;
		try {
			sptr_t docOptions = SC_DOCUMENTOPTION_DEFAULT;

			const long long sizeLarge = props_.GetLongLong("file.size.large");
			if (sizeLarge && (fileSize > sizeLarge))
				docOptions |= SC_DOCUMENTOPTION_TEXT_LARGE;

			const long long sizeNoStyles = props_.GetLongLong("file.size.no.styles");
			if (sizeNoStyles && (fileSize > sizeNoStyles))
				docOptions |= SC_DOCUMENTOPTION_STYLES_NONE;

			pdocLoad = reinterpret_cast<ILoader *>(
				wEditor_.CallReturnPointer(SCI_CREATELOADER, static_cast<uptr_t>(fileSize) + 1000,
					docOptions));
		} catch (...) {
			wEditor_.Call(SCI_SETSTATUS, 0);
			return;
		}
		CurrentBuffer()->pFileWorker = new FileLoader(this, pdocLoad, filePath_, static_cast<size_t>(fileSize), fp);
		CurrentBuffer()->pFileWorker->sleepTime = props_.GetInt("asynchronous.sleep");
		PerformOnNewThread(CurrentBuffer()->pFileWorker);
	} else {
		wEditor_.Call(SCI_ALLOCATE, static_cast<uptr_t>(fileSize) + 1000);

		Utf8_16_Read convert;
		std::vector<char> data(blockSize);
		size_t lenFile = fread(&data[0], 1, data.size(), fp);
		const UniMode umCodingCookie = CodingCookieValue(&data[0], lenFile);
		while (lenFile > 0) {
			lenFile = convert.convert(&data[0], lenFile);
			const char *dataBlock = convert.getNewBuf();
			wEditor_.CallString(SCI_ADDTEXT, lenFile, dataBlock);
			lenFile = fread(&data[0], 1, data.size(), fp);
			if (lenFile == 0) {
				// Handle case where convert is holding a lead surrogate but no more data
				const size_t lenFileTrail = convert.convert(NULL, lenFile);
				if (lenFileTrail) {
					const char *dataTrail = convert.getNewBuf();
					wEditor_.CallString(SCI_ADDTEXT, lenFileTrail, dataTrail);
				}
			}
		}
		fclose(fp);
		wEditor_.Call(SCI_ENDUNDOACTION);

		CurrentBuffer()->unicodeMode = static_cast<UniMode>(
			    static_cast<int>(convert.getEncoding()));
		// Check the first two lines for coding cookies
		if (CurrentBuffer()->unicodeMode == uni8Bit) {
			CurrentBuffer()->unicodeMode = umCodingCookie;
		}

		CompleteOpen(kOcSynchronous);
	}
}

void SciTEBase::TextRead(FileWorker *pFileWorker) {
	FileLoader *pFileLoader = static_cast<FileLoader *>(pFileWorker);
	const int iBuffer = buffers_.GetDocumentByWorker(pFileLoader);
	// May not be found if load cancelled
	if (iBuffer >= 0) {
		buffers_.buffers_[iBuffer].unicodeMode = pFileLoader->unicodeMode;
		buffers_.buffers_[iBuffer].lifeState = Buffer::kReadAll;
		if (pFileLoader->err) {
			GUI::GUIString msg = LocaliseMessage("Could not open file '^0'.", pFileLoader->path.AsInternal());
			WindowMessageBox(wCuteText_, msg);
			// Should refuse to save when failure occurs
			buffers_.buffers_[iBuffer].lifeState = Buffer::kEmpty;
		}
		// Switch documents
		const sptr_t pdocLoading = reinterpret_cast<sptr_t>(pFileLoader->pLoader->ConvertToDocument());
		pFileLoader->pLoader = 0;
		SwitchDocumentAt(iBuffer, pdocLoading);
		if (iBuffer == buffers_.Current()) {
			CompleteOpen(kOcCompleteCurrent);
			if (extender_)
				extender_->OnOpen(buffers_.buffers_[iBuffer].file.AsUTF8().c_str());
			RestoreState(buffers_.buffers_[iBuffer], true);
			DisplayAround(buffers_.buffers_[iBuffer].file);
			wEditor_.Call(SCI_SCROLLCARET);
		}
	}
}

void SciTEBase::PerformDeferredTasks() {
	if (buffers_.buffers_[buffers_.Current()].futureDo & Buffer::kFdFinishSave) {
		wEditor_.Call(SCI_SETSAVEPOINT);
		wEditor_.Call(SCI_SETREADONLY, CurrentBuffer()->isReadOnly);
		buffers_.FinishedFuture(buffers_.Current(), Buffer::kFdFinishSave);
	}
}

void SciTEBase::CompleteOpen(OpenCompletion oc) {
	wEditor_.Call(SCI_SETREADONLY, CurrentBuffer()->isReadOnly);

	if (oc != kOcSynchronous) {
		ReadProperties();
	}

	if (language_ == "") {
		std::string languageOverride = DiscoverLanguage();
		if (languageOverride.length()) {
			CurrentBuffer()->overrideExtension = languageOverride;
			CurrentBuffer()->lifeState = Buffer::kOpen;
			ReadProperties();
			SetIndentSettings();
		}
	}

	if (oc != kOcSynchronous) {
		SetIndentSettings();
		SetEol();
		UpdateBuffersCurrent();
		SizeSubWindows();
	}

	if (CurrentBuffer()->unicodeMode != uni8Bit) {
		// Override the code page if Unicode
		codePage_ = SC_CP_UTF8;
	} else {
		codePage_ = props_.GetInt("code.page");
	}
	wEditor_.Call(SCI_SETCODEPAGE, codePage_);

	DiscoverEOLSetting();

	if (props_.GetInt("indent.auto")) {
		DiscoverIndentSetting();
	}

	if (!wEditor_.Call(SCI_GETUNDOCOLLECTION)) {
		wEditor_.Call(SCI_SETUNDOCOLLECTION, 1);
	}
	wEditor_.Call(SCI_SETSAVEPOINT);
	if (props_.GetInt("fold.on.open") > 0) {
		FoldAll();
	}
	wEditor_.Call(SCI_GOTOPOS, 0);

	CurrentBuffer()->CompleteLoading();

	Redraw();
}

void SciTEBase::TextWritten(FileWorker *pFileWorker) {
	const FileStorer *pFileStorer = static_cast<const FileStorer *>(pFileWorker);
	const int iBuffer = buffers_.GetDocumentByWorker(pFileStorer);

	FilePath pathSaved = pFileStorer->path;
	const int errSaved = pFileStorer->err;
	const bool cancelledSaved = pFileStorer->Cancelling();

	// May not be found if save cancelled or buffer closed
	if (iBuffer >= 0) {
		// Complete and release
		buffers_.buffers_[iBuffer].CompleteStoring();
		if (errSaved || cancelledSaved) {
			// Background save failed (possibly out-of-space) so resurrect the
			// buffer so can be saved to another disk or retried after making room.
			buffers_.SetVisible(iBuffer, true);
			SetBuffersMenu();
			if (iBuffer == buffers_.Current()) {
				wEditor_.Call(SCI_SETREADONLY, CurrentBuffer()->isReadOnly);
			}
		} else {
			if (!buffers_.GetVisible(iBuffer)) {
				buffers_.RemoveInvisible(iBuffer);
			}
			if (iBuffer == buffers_.Current()) {
				wEditor_.Call(SCI_SETREADONLY, CurrentBuffer()->isReadOnly);
				if (pathSaved.SameNameAs(CurrentBuffer()->file)) {
					wEditor_.Call(SCI_SETSAVEPOINT);
				}
				if (extender_)
					extender_->OnSave(buffers_.buffers_[iBuffer].file.AsUTF8().c_str());
			} else {
				buffers_.buffers_[iBuffer].isDirty = false;
				buffers_.buffers_[iBuffer].failedSave = false;
				// Need to make writable and set save point when next receive focus.
				buffers_.AddFuture(iBuffer, Buffer::kFdFinishSave);
				SetBuffersMenu();
			}
		}
	} else {
		GUI::GUIString msg = LocaliseMessage("Could not find buffer '^0'.", pathSaved.AsInternal());
		WindowMessageBox(wCuteText_, msg);
	}

	if (errSaved) {
		FailedSaveMessageBox(pathSaved);
	}

	if (IsPropertiesFile(pathSaved)) {
		ReloadProperties();
	}
	UpdateStatusBar(true);
	if (!jobQueue_.executing && (jobQueue_.HasCommandToRun())) {
		Execute();
	}
	if (quitting_ && !buffers_.SavingInBackground()) {
		QuitProgram();
	}
}

void SciTEBase::UpdateProgress(Worker *) {
	GUI::GUIString prog;
	BackgroundActivities bgActivities = buffers_.CountBackgroundActivities();
	const int countBoth = bgActivities.loaders + bgActivities.storers;
	if (countBoth == 0) {
		// Should hide UI
		ShowBackgroundProgress(GUI_TEXT(""), 0, 0);
	} else {
		if (countBoth == 1) {
			prog += LocaliseMessage(bgActivities.loaders ? "Opening '^0'" : "Saving '^0'",
				bgActivities.fileNameLast.c_str());
		} else {
			if (bgActivities.loaders) {
				prog += LocaliseMessage("Opening ^0 files ", GUI::StringFromInteger(bgActivities.loaders).c_str());
			}
			if (bgActivities.storers) {
				prog += LocaliseMessage("Saving ^0 files ", GUI::StringFromInteger(bgActivities.storers).c_str());
			}
		}
		ShowBackgroundProgress(prog, bgActivities.totalWork, bgActivities.totalProgress);
	}
}

bool SciTEBase::PreOpenCheck(const GUI::GUIChar *) {
	return false;
}

bool SciTEBase::Open(const FilePath &file, OpenFlags of) {
	InitialiseBuffers();

	FilePath absPath = file.AbsolutePath();
	if (!absPath.IsUntitled() && absPath.IsDirectory()) {
		GUI::GUIString msg = LocaliseMessage("Path '^0' is a directory so can not be opened.",
			absPath.AsInternal());
		WindowMessageBox(wCuteText_, msg);
		return false;
	}

	const int index = buffers_.GetDocumentByName(absPath);
	if (index >= 0) {
		buffers_.SetVisible(index, true);
		SetDocumentAt(index);
		RemoveFileFromStack(absPath);
		DeleteFileStackMenu();
		SetFileStackMenu();
		// If not forcing reload or currently busy with load or save, just rotate into view
		if ((!(of & kOfForceLoad)) || (CurrentBufferConst()->pFileWorker))
			return true;
	}
	// See if we can have a buffer for the file to open
	if (!CanMakeRoom(!(of & kOfNoSaveIfDirty))) {
		return false;
	}

	const long long fileSize = absPath.IsUntitled() ? 0 : absPath.GetFileLength();
	if (fileSize > INTPTR_MAX) {
		const GUI::GUIString sSize = GUI::StringFromLongLong(fileSize);
		const GUI::GUIString msg = LocaliseMessage("File '^0' is ^1 bytes long, "
			"larger than 2GB which is the largest SciTE can open.",
			absPath.AsInternal(), sSize.c_str());
		WindowMessageBox(wCuteText_, msg, kMbsIconWarning);
		return false;
	}
	if (fileSize > 0) {
		// Real file, not empty buffer
		const long long maxSize = props_.GetLongLong("max.file.size", 2000000000LL);
		if (maxSize > 0 && fileSize > maxSize) {
			const GUI::GUIString sSize = GUI::StringFromLongLong(fileSize);
			const GUI::GUIString sMaxSize = GUI::StringFromLongLong(maxSize);
			const GUI::GUIString msg = LocaliseMessage("File '^0' is ^1 bytes long,\n"
			        "larger than the ^2 bytes limit set in the properties.\n"
			        "Do you still want to open it?",
			        absPath.AsInternal(), sSize.c_str(), sMaxSize.c_str());
			const MessageBoxChoice answer = WindowMessageBox(wCuteText_, msg, kMbsYesNo | kMbsIconWarning);
			if (answer != kMbcYes) {
				return false;
			}
		}
	}

	if (buffers_.size() == buffers_.length) {
		AddFileToStack(RecentFile(filePath_, GetSelectedRange(), GetCurrentScrollPosition()));
		ClearDocument();
		CurrentBuffer()->lifeState = Buffer::kOpen;
		if (extender_)
			extender_->InitBuffer(buffers_.Current());
	} else {
		if (index < 0 || !(of & kOfForceLoad)) { // No new buffer, already opened
			New();
		}
	}

	assert(CurrentBufferConst()->pFileWorker == NULL);
	SetFileName(absPath);

	propsDiscovered_.Clear();
	std::string discoveryScript = props_.GetExpandedString("command.discover.properties");
	if (discoveryScript.length()) {
		std::string propertiesText = CommandExecute(GUI::StringFromUTF8(discoveryScript).c_str(),
			absPath.Directory().AsInternal());
		if (propertiesText.size()) {
			propsDiscovered_.ReadFromMemory(propertiesText.c_str(), propertiesText.size(), absPath.Directory(), filter_, NULL, 0);
		}
	}
	CurrentBuffer()->props_ = propsDiscovered_;
	CurrentBuffer()->overrideExtension = "";
	ReadProperties();
	SetIndentSettings();
	SetEol();
	UpdateBuffersCurrent();
	SizeSubWindows();
	SetBuffersMenu();

	bool asynchronous = false;
	if (!filePath_.IsUntitled()) {
		wEditor_.Call(SCI_SETREADONLY, 0);
		wEditor_.Call(SCI_CANCEL);
		if (of & kOfPreserveUndo) {
			wEditor_.Call(SCI_BEGINUNDOACTION);
		} else {
			wEditor_.Call(SCI_SETUNDOCOLLECTION, 0);
		}

		asynchronous = (fileSize > props_.GetInt("background.open.size", -1)) &&
			!(of & (kOfPreserveUndo|kOfSynchronous));
		OpenCurrentFile(fileSize, of & kOfQuiet, asynchronous);

		if (of & kOfPreserveUndo) {
			wEditor_.Call(SCI_ENDUNDOACTION);
		} else {
			wEditor_.Call(SCI_EMPTYUNDOBUFFER);
		}
		CurrentBuffer()->isReadOnly = props_.GetInt("read.only");
		wEditor_.Call(SCI_SETREADONLY, CurrentBuffer()->isReadOnly);
	}
	RemoveFileFromStack(filePath_);
	DeleteFileStackMenu();
	SetFileStackMenu();
	SetWindowName();
	if (lineNumbers_ && lineNumbersExpand_)
		SetLineNumberWidth();
	UpdateStatusBar(true);
	if (extender_ && !asynchronous)
		extender_->OnOpen(filePath_.AsUTF8().c_str());
	return true;
}

// Returns true if editor should get the focus
bool SciTEBase::OpenSelected() {
	std::string selName = SelectionFilename();
	if (selName.length() == 0) {
		WarnUser(kWarnWrongFile);
		return false;	// No selection
	}

#if !defined(GTK)
	if (StartsWith(selName, "http:") ||
		StartsWith(selName, "https:") ||
		StartsWith(selName, "ftp:") ||
		StartsWith(selName, "ftps:") ||
		StartsWith(selName, "news:") ||
		StartsWith(selName, "mailto:")) {
		std::string cmd = selName;
		AddCommand(cmd, "", jobShell);
		return false;	// Job is done
	}
#endif

	if (StartsWith(selName, "file://")) {
		selName.erase(0, 7);
		if (selName[0] == '/' && selName[2] == ':') { // file:///C:/filename.ext
			selName.erase(0, 1);
		}
	}

	std::string fileNameForExtension = ExtensionFileName();
	std::string openSuffix = props_.GetNewExpandString("open.suffix.", fileNameForExtension.c_str());
	selName += openSuffix;

	if (EqualCaseInsensitive(selName.c_str(), FileNameExt().AsUTF8().c_str()) || EqualCaseInsensitive(selName.c_str(), filePath_.AsUTF8().c_str())) {
		WarnUser(kWarnWrongFile);
		return true;	// Do not open if it is the current file!
	}

	std::string cTag;
	unsigned long lineNumber = 0;
	if (IsPropertiesFile(filePath_) &&
	        (selName.find('.') == std::string::npos)) {
		// We are in a properties file and try to open a file without extension,
		// we suppose we want to open an imported .properties file
		// So we append the correct extension to open the included file.
		// Maybe we should check if the filename is preceded by "import"...
		selName += PROPERTIES_EXTENSION;
	} else {
		// Check if we have a line number (error message or grep result)
		// A bit of duplicate work with DecodeMessage, but we don't know
		// here the format of the line, so we do guess work.
		// Can't do much for space separated line numbers anyway...
		size_t endPath = selName.find('(');
		if (endPath != std::string::npos) {	// Visual Studio error message: F:\scite\src\SciTEBase.h(312):	bool Exists(
			lineNumber = atol(selName.c_str() + endPath + 1);
		} else {
			endPath = selName.find(':', 2);	// Skip Windows' drive separator
			if (endPath != std::string::npos) {	// grep -n line, perhaps gcc too: F:\scite\src\SciTEBase.h:312:	bool Exists(
				lineNumber = atol(selName.c_str() + endPath + 1);
			}
		}
		if (lineNumber > 0) {
			selName.erase(endPath);
		}

		// Support the ctags format

		if (lineNumber == 0) {
			cTag = GetCTag();
		}
	}

	FilePath path;
	// Don't load the path of the current file if the selected
	// filename is an absolute pathname
	GUI::GUIString selFN = GUI::StringFromUTF8(selName);
	if (!FilePath(selFN).IsAbsolute()) {
		path = filePath_.Directory();
		// If not there, look in openpath
		if (!Exists(path.AsInternal(), selFN.c_str(), NULL)) {
			GUI::GUIString openPath = GUI::StringFromUTF8(props_.GetNewExpandString(
			            "openpath.", fileNameForExtension.c_str()));
			while (openPath.length()) {
				GUI::GUIString tryPath(openPath);
				const size_t sepIndex = tryPath.find(listSepString);
				if ((sepIndex != GUI::GUIString::npos) && (sepIndex != 0)) {
					tryPath.erase(sepIndex);
					openPath.erase(0, sepIndex + 1);
				} else {
					openPath.erase();
				}
				if (Exists(tryPath.c_str(), selFN.c_str(), NULL)) {
					path.Set(tryPath.c_str());
					break;
				}
			}
		}
	}
	FilePath pathReturned;
	if (Exists(path.AsInternal(), selFN.c_str(), &pathReturned)) {
		// Open synchronously if want to seek line number or search tag
		const OpenFlags of = ((lineNumber > 0) || (cTag.length() != 0)) ? kOfSynchronous : kOfNone;
		if (Open(pathReturned, of)) {
			if (lineNumber > 0) {
				wEditor_.Call(SCI_GOTOLINE, lineNumber - 1);
			} else if (cTag.length() != 0) {
				if (atoi(cTag.c_str()) > 0) {
					wEditor_.Call(SCI_GOTOLINE, atoi(cTag.c_str()) - 1);
				} else {
					findWhat = cTag;
					FindNext(false);
				}
			}
			return true;
		}
	} else {
		WarnUser(kWarnWrongFile);
	}
	return false;
}

void SciTEBase::Revert() {
	if (filePath_.IsUntitled()) {
		wEditor_.Call(SCI_CLEARALL);
	} else {
		RecentFile rf = GetFilePosition();
		OpenCurrentFile(filePath_.GetFileLength(), false, false);
		DisplayAround(rf);
	}
}

void SciTEBase::CheckReload() {
	if (props_.GetInt("load.on.activate")) {
		// Make a copy of fullPath as otherwise it gets aliased in Open
		const time_t newModTime = filePath_.ModifiedTime();
		if ((newModTime != 0) && (newModTime != CurrentBuffer()->fileModTime)) {
			RecentFile rf = GetFilePosition();
			const OpenFlags of = props_.GetInt("reload.preserves.undo") ? kOfPreserveUndo : kOfNone;
			if (CurrentBuffer()->isDirty || props_.GetInt("are.you.sure.on.reload") != 0) {
				if ((0 == dialogsOnScreen_) && (newModTime != CurrentBuffer()->fileModLastAsk)) {
					GUI::GUIString msg;
					if (CurrentBuffer()->isDirty) {
						msg = LocaliseMessage(
						          "The file '^0' has been modified. Should it be reloaded?",
						          filePath_.AsInternal());
					} else {
						msg = LocaliseMessage(
						          "The file '^0' has been modified outside SciTE. Should it be reloaded?",
						          FileNameExt().AsInternal());
					}
					const MessageBoxChoice decision = WindowMessageBox(wCuteText_, msg, kMbsYesNo | kMbsIconQuestion);
					if (decision == kMbcYes) {
						Open(filePath_, static_cast<OpenFlags>(of | kOfForceLoad));
						DisplayAround(rf);
					}
					CurrentBuffer()->fileModLastAsk = newModTime;
				}
			} else {
				Open(filePath_, static_cast<OpenFlags>(of | kOfForceLoad));
				DisplayAround(rf);
			}
		}  else if (newModTime == 0 && CurrentBuffer()->fileModTime != 0)  {
			// Check if the file is deleted
			CurrentBuffer()->fileModTime = 0;
			CurrentBuffer()->fileModLastAsk = 0;
			CurrentBuffer()->isDirty = true;
			CheckMenus();
			SetWindowName();
			SetBuffersMenu();
			GUI::GUIString msg = LocaliseMessage(
						      "The file '^0' has been deleted.",
						      filePath_.AsInternal());
			WindowMessageBox(wCuteText_, msg, kMbsOK);
		}
	}
}

void SciTEBase::Activate(bool activeApp) {
	if (activeApp) {
		CheckReload();
	} else {
		if (props_.GetInt("save.on.deactivate")) {
			SaveTitledBuffers();
		}
	}
}

FilePath SciTEBase::SaveName(const char *ext) const {
	GUI::GUIString savePath = filePath_.AsInternal();
	if (ext) {
		int dot = static_cast<int>(savePath.length() - 1);
		while ((dot >= 0) && (savePath[dot] != '.')) {
			dot--;
		}
		if (dot >= 0) {
			const int keepExt = props_.GetInt("export.keep.ext");
			if (keepExt == 0) {
				savePath.erase(dot);
			} else if (keepExt == 2) {
				savePath[dot] = '_';
			}
		}
		savePath += GUI::StringFromUTF8(ext);
	}
	//~ fprintf(stderr, "SaveName <%s> <%s> <%s>\n", filePath_.AsInternal(), savePath.c_str(), ext);
	return FilePath(savePath.c_str());
}

SciTEBase::SaveResult SciTEBase::SaveIfUnsure(bool forceQuestion, SaveFlags sf) {
	CurrentBuffer()->failedSave = false;
	if (CurrentBuffer()->pFileWorker) {
		if (CurrentBuffer()->pFileWorker->IsLoading())
			// In semi-loaded state so refuse to save
			return kSaveCancelled;
		else
			return kSaveCompleted;
	}
	if ((CurrentBuffer()->isDirty) && (LengthDocument() || !filePath_.IsUntitled() || forceQuestion)) {
		if (props_.GetInt("are.you.sure", 1) ||
		        filePath_.IsUntitled() ||
		        forceQuestion) {
					GUI::GUIString msg;
			if (!filePath_.IsUntitled()) {
				msg = LocaliseMessage("Save changes to '^0'?", filePath_.AsInternal());
			} else {
				msg = LocaliseMessage("Save changes to (Untitled)?");
			}
			const MessageBoxChoice decision = WindowMessageBox(wCuteText_, msg, kMbsYesNoCancel | kMbsIconQuestion);
			if (decision == kMbcYes) {
				if (!Save(sf))
					return kSaveCancelled;
			}
			return (decision == kMbcCancel) ? kSaveCancelled : kSaveCompleted;
		} else {
			if (!Save(sf))
				return kSaveCancelled;
		}
	}
	return kSaveCompleted;
}

SciTEBase::SaveResult SciTEBase::SaveIfUnsureAll() {
	if (SaveAllBuffers(false) == kSaveCancelled) {
		return kSaveCancelled;
	}
	if (props_.GetInt("save.recent")) {
		for (int i = 0; i < buffers_.lengthVisible; ++i) {
			Buffer buff = buffers_.buffers_[i];
			AddFileToStack(buff.file);
		}
	}
	if (props_.GetInt("save.session") || props_.GetInt("save.position") || props_.GetInt("save.recent")) {
		SaveSessionFile(GUI_TEXT(""));
	}

	if (extender_ && extender_->NeedsOnClose()) {
		// Ensure extender_ is told about each buffer closing
		for (int k = 0; k < buffers_.lengthVisible; k++) {
			SetDocumentAt(k);
			extender_->OnClose(filePath_.AsUTF8().c_str());
		}
	}

	// Definitely going to exit now, so delete all documents
	// Set editor back to initial document
	if (buffers_.lengthVisible > 0) {
		wEditor_.Call(SCI_SETDOCPOINTER, 0, buffers_.buffers_[0].doc);
	}
	// Release all the extra documents
	for (int j = 0; j < buffers_.size(); j++) {
		if (buffers_.buffers_[j].doc && !buffers_.buffers_[j].pFileWorker) {
			wEditor_.Call(SCI_RELEASEDOCUMENT, 0, buffers_.buffers_[j].doc);
			buffers_.buffers_[j].doc = 0;
		}
	}
	// Initial document will be deleted when editor deleted
	return kSaveCompleted;
}

SciTEBase::SaveResult SciTEBase::SaveIfUnsureForBuilt() {
	if (props_.GetInt("save.all.for.build")) {
		return SaveAllBuffers(!props_.GetInt("are.you.sure.for.build"));
	}
	if (CurrentBuffer()->isDirty) {
		if (props_.GetInt("are.you.sure.for.build"))
			return SaveIfUnsure(true);

		Save();
	}
	return kSaveCompleted;
}
/**
	Selection saver and restorer.

	If virtual space is disabled, the class does nothing.

	If virtual space is enabled, constructor saves all selections using (line, column) coordinates,
	destructor restores all the saved selections.
**/
class SelectionKeeper {
public:
	explicit SelectionKeeper(GUI::ScintillaWindow &editor) : wEditor_(editor) {
		const int mask = SCVS_RECTANGULARSELECTION | SCVS_USERACCESSIBLE;
		if (wEditor_.Call(SCI_GETVIRTUALSPACEOPTIONS, 0, 0) & mask) {
			const int n = wEditor_.Call(SCI_GETSELECTIONS, 0, 0);
			for (int i = 0; i < n; ++i) {
				selections.push_back(LocFromPos(GetSelection(i)));
			}
		}
	}

	~SelectionKeeper() {
		int i = 0;
		for (auto const &sel : selections) {
			SetSelection(i, PosFromLoc(sel));
			++i;
		}
	}

private:
	struct Position {
		Position(int pos_, int virt_ = 0) : pos(pos_), virt(virt_) {};
		int pos;
		int virt;
	};

	struct Location {
		Location(int line_, int col_) : line(line_), col(col_) {};
		int line;
		int col;
	};

	Position GetAnchor(int i) {
		const int pos  = wEditor_.Call(SCI_GETSELECTIONNANCHOR, i, 0);
		const int virt = wEditor_.Call(SCI_GETSELECTIONNANCHORVIRTUALSPACE, i, 0);
		return Position(pos, virt);
	}

	Position GetCaret(int i) {
		const int pos  = wEditor_.Call(SCI_GETSELECTIONNCARET, i, 0);
		const int virt = wEditor_.Call(SCI_GETSELECTIONNCARETVIRTUALSPACE, i, 0);
		return Position(pos, virt);
	}

	std::pair<Position, Position> GetSelection(int i) {
		return {GetAnchor(i), GetCaret(i)};
	};

	Location LocFromPos(Position const &pos) {
		const int line = wEditor_.Call(SCI_LINEFROMPOSITION, pos.pos, 0);
		const int col  = wEditor_.Call(SCI_GETCOLUMN, pos.pos, 0) + pos.virt;
		return Location(line, col);
	}

	std::pair<Location, Location> LocFromPos(std::pair<Position, Position> const &pos) {
		return {LocFromPos(pos.first), LocFromPos(pos.second)};
	}

	Position PosFromLoc(Location const &loc) {
		const int pos = wEditor_.Call(SCI_FINDCOLUMN, loc.line, loc.col);
		const int col = wEditor_.Call(SCI_GETCOLUMN, pos, 0);
		return Position(pos, loc.col - col);
	}

	std::pair<Position, Position> PosFromLoc(std::pair<Location, Location> const &loc) {
		return {PosFromLoc(loc.first), PosFromLoc(loc.second)};
	}

	void SetAnchor(int i, Position const &pos) {
		wEditor_.Call(SCI_SETSELECTIONNANCHOR, i, pos.pos);
		wEditor_.Call(SCI_SETSELECTIONNANCHORVIRTUALSPACE, i, pos.virt);
	};

	void SetCaret(int i, Position const &pos) {
		wEditor_.Call(SCI_SETSELECTIONNCARET, i, pos.pos);
		wEditor_.Call(SCI_SETSELECTIONNCARETVIRTUALSPACE, i, pos.virt);
	}

	void SetSelection(int i, std::pair<Position, Position> const &pos) {
		SetAnchor(i, pos.first);
		SetCaret(i, pos.second);
	}

	GUI::ScintillaWindow &wEditor_;
	std::vector<std::pair<Location, Location>> selections;
};

void SciTEBase::StripTrailingSpaces() {
	const int maxLines = wEditor_.Call(SCI_GETLINECOUNT);
	SelectionKeeper keeper(wEditor_);
	for (int line = 0; line < maxLines; line++) {
		const int lineStart = wEditor_.Call(SCI_POSITIONFROMLINE, line);
		const int lineEnd = wEditor_.Call(SCI_GETLINEENDPOSITION, line);
		int i = lineEnd - 1;
		char ch = static_cast<char>(wEditor_.Call(SCI_GETCHARAT, i));
		while ((i >= lineStart) && ((ch == ' ') || (ch == '\t'))) {
			i--;
			ch = static_cast<char>(wEditor_.Call(SCI_GETCHARAT, i));
		}
		if (i < (lineEnd - 1)) {
			wEditor_.Call(SCI_SETTARGETSTART, i + 1);
			wEditor_.Call(SCI_SETTARGETEND, lineEnd);
			wEditor_.CallString(SCI_REPLACETARGET, 0, "");
		}
	}
}

void SciTEBase::EnsureFinalNewLine() {
	const int maxLines = wEditor_.Call(SCI_GETLINECOUNT);
	bool appendNewLine = maxLines == 1;
	const int endDocument = wEditor_.Call(SCI_POSITIONFROMLINE, maxLines);
	if (maxLines > 1) {
		appendNewLine = endDocument > wEditor_.Call(SCI_POSITIONFROMLINE, maxLines - 1);
	}
	if (appendNewLine) {
		const char *eol = "\n";
		switch (wEditor_.Call(SCI_GETEOLMODE)) {
		case SC_EOL_CRLF:
			eol = "\r\n";
			break;
		case SC_EOL_CR:
			eol = "\r";
			break;
		}
		wEditor_.CallString(SCI_INSERTTEXT, endDocument, eol);
	}
}

// Perform any changes needed before saving such as normalizing spaces and line ends.
bool SciTEBase::PrepareBufferForSave(const FilePath &saveName) {
	bool retVal = false;
	// Perform clean ups on text before saving
	wEditor_.Call(SCI_BEGINUNDOACTION);
	if (stripTrailingSpaces_)
		StripTrailingSpaces();
	if (ensureFinalLineEnd_)
		EnsureFinalNewLine();
	if (ensureConsistentLineEnds_)
		wEditor_.Call(SCI_CONVERTEOLS, wEditor_.Call(SCI_GETEOLMODE));

	if (extender_)
		retVal = extender_->OnBeforeSave(saveName.AsUTF8().c_str());

	wEditor_.Call(SCI_ENDUNDOACTION);

	return retVal;
}

/**
 * Writes the buffer to the given filename.
 */
bool SciTEBase::SaveBuffer(const FilePath &saveName, SaveFlags sf) {
	bool retVal = PrepareBufferForSave(saveName);

	if (!retVal) {

		FILE *fp = saveName.Open(fileWrite);
		if (fp) {
			const size_t lengthDoc = LengthDocument();
			if (!(sf & kSfSynchronous)) {
				wEditor_.Call(SCI_SETREADONLY, 1);
				const char *documentBytes = reinterpret_cast<const char *>(wEditor_.CallReturnPointer(SCI_GETCHARACTERPOINTER));
				CurrentBuffer()->pFileWorker = new FileStorer(this, documentBytes, saveName, lengthDoc, fp, CurrentBuffer()->unicodeMode, (sf & kSfProgressVisible));
				CurrentBuffer()->pFileWorker->sleepTime = props_.GetInt("asynchronous.sleep");
				if (PerformOnNewThread(CurrentBuffer()->pFileWorker)) {
					retVal = true;
				} else {
					GUI::GUIString msg = LocaliseMessage("Failed to save file '^0' as thread could not be started.", saveName.AsInternal());
					WindowMessageBox(wCuteText_, msg);
				}
			} else {
				Utf8_16_Write convert;
				if (CurrentBuffer()->unicodeMode != uniCookie) {	// Save file with cookie without BOM.
					convert.setEncoding(static_cast<Utf8_16::encodingType>(
							static_cast<int>(CurrentBuffer()->unicodeMode)));
				}
				convert.setfile(fp);
				std::vector<char> data(blockSize + 1);
				retVal = true;
				size_t grabSize;
				for (size_t i = 0; i < lengthDoc; i += grabSize) {
					grabSize = lengthDoc - i;
					if (grabSize > blockSize)
						grabSize = blockSize;
					// Round down so only whole characters retrieved.
					grabSize = wEditor_.Call(SCI_POSITIONBEFORE, i + grabSize + 1) - i;
					GetRange(wEditor_, static_cast<int>(i), static_cast<int>(i + grabSize), &data[0]);
					const size_t written = convert.fwrite(&data[0], grabSize);
					if (written == 0) {
						retVal = false;
						break;
					}
				}
				if (convert.fclose() != 0) {
					retVal = false;
				}
			}
		}
	}

	if (retVal && extender_ && (sf & kSfSynchronous)) {
		extender_->OnSave(saveName.AsUTF8().c_str());
	}
	UpdateStatusBar(true);
	return retVal;
}

void SciTEBase::ReloadProperties() {
	ReadGlobalPropFile();
	SetImportMenu();
	ReadLocalPropFile();
	ReadAbbrevPropFile();
	ReadProperties();
	SetWindowName();
	BuffersMenu();
	Redraw();
}

// Returns false if cancelled or failed to save
bool SciTEBase::Save(SaveFlags sf) {
	if (!filePath_.IsUntitled()) {
		GUI::GUIString msg;
		if (CurrentBuffer()->ShouldNotSave()) {
			msg = LocaliseMessage(
				"The file '^0' has not yet been loaded entirely, so it can not be saved right now. Please retry in a while.",
				filePath_.AsInternal());
			WindowMessageBox(wCuteText_, msg);
			// It is OK to not save this file
			return true;
		}

		if (CurrentBuffer()->pFileWorker) {
			msg = LocaliseMessage(
				"The file '^0' is already being saved.",
				filePath_.AsInternal());
			WindowMessageBox(wCuteText_, msg);
			// It is OK to not save this file
			return true;
		}

		if (props_.GetInt("save.deletes.first")) {
			filePath_.Remove();
		} else if (props_.GetInt("save.check.modified.time")) {
			const time_t newModTime = filePath_.ModifiedTime();
			if ((newModTime != 0) && (CurrentBuffer()->fileModTime != 0) &&
				(newModTime != CurrentBuffer()->fileModTime)) {
				msg = LocaliseMessage("The file '^0' has been modified outside SciTE. Should it be saved?",
					filePath_.AsInternal());
				const MessageBoxChoice decision = WindowMessageBox(wCuteText_, msg, kMbsYesNo | kMbsIconQuestion);
				if (decision == kMbcNo) {
					return false;
				}
			}
		}

		if ((LengthDocument() <= props_.GetInt("background.save.size", -1)) ||
			(buffers_.SingleBuffer()))
			sf = static_cast<SaveFlags>(sf | kSfSynchronous);
		if (SaveBuffer(filePath_, sf)) {
			CurrentBuffer()->SetTimeFromFile();
			if (sf & kSfSynchronous) {
				wEditor_.Call(SCI_SETSAVEPOINT);
				if (IsPropertiesFile(filePath_)) {
					ReloadProperties();
				}
			}
		} else {
			if (!CurrentBuffer()->failedSave) {
				CurrentBuffer()->failedSave = true;
				msg = LocaliseMessage(
					"Could not save file '^0'. Save under a different name?", filePath_.AsInternal());
				const MessageBoxChoice decision = WindowMessageBox(wCuteText_, msg, kMbsYesNo | kMbsIconWarning);
				if (decision == kMbcYes) {
					return SaveAsDialog();
				}
			}
			return false;
		}
		return true;
	} else {
		if (props_.GetString("save.path.suggestion").length()) {
			const time_t t = time(NULL);
			char timeBuff[15];
			strftime(timeBuff, sizeof(timeBuff), "%Y%m%d%H%M%S",  localtime(&t));
			PropSetFile propsSuggestion;
			propsSuggestion.superPS = &props_;  // Allow access to other settings
			propsSuggestion.Set("TimeStamp", timeBuff);
			propsSuggestion.Set("SciteUserHome", GetSciteUserHome().AsUTF8().c_str());
			std::string savePathSuggestion = propsSuggestion.GetExpandedString("save.path.suggestion");
			std::replace(savePathSuggestion.begin(), savePathSuggestion.end(), '\\', '/');  // To accept "\" on Unix
			if (savePathSuggestion.size() > 0) {
				filePath_ = FilePath(GUI::StringFromUTF8(savePathSuggestion)).NormalizePath();
			}
		}
		const bool ret = SaveAsDialog();
		if (!ret)
			filePath_.Set(GUI_TEXT(""));
		return ret;
	}
}

void SciTEBase::SaveAs(const GUI::GUIChar *file, bool fixCase) {
	SetFileName(file, fixCase);
	Save();
	ReadProperties();
	wEditor_.Call(SCI_CLEARDOCUMENTSTYLE);
	wEditor_.Call(SCI_COLOURISE, 0, wEditor_.Call(SCI_POSITIONFROMLINE, 1));
	Redraw();
	SetWindowName();
	BuffersMenu();
	if (extender_)
		extender_->OnSave(filePath_.AsUTF8().c_str());
}

bool SciTEBase::SaveIfNotOpen(const FilePath &destFile, bool fixCase) {
	FilePath absPath = destFile.AbsolutePath();
	const int index = buffers_.GetDocumentByName(absPath, true /* excludeCurrent */);
	if (index >= 0) {
		GUI::GUIString msg = LocaliseMessage(
			    "File '^0' is already open in another buffer.", destFile.AsInternal());
		WindowMessageBox(wCuteText_, msg);
		return false;
	} else {
		SaveAs(absPath.AsInternal(), fixCase);
		return true;
	}
}

void SciTEBase::AbandonAutomaticSave() {
	CurrentBuffer()->AbandonAutomaticSave();
}

bool SciTEBase::IsStdinBlocked() {
	return false; /* always default to blocked */
}

void SciTEBase::OpenFromStdin(bool UseOutputPane) {
	Utf8_16_Read convert;
	std::vector<char> data(blockSize);

	/* if stdin is blocked, do not execute this method */
	if (IsStdinBlocked())
		return;

	Open(GUI_TEXT(""));
	if (UseOutputPane) {
		wOutput_.Call(SCI_CLEARALL);
	} else {
		wEditor_.Call(SCI_BEGINUNDOACTION);	// Group together clear and insert
		wEditor_.Call(SCI_CLEARALL);
	}
	size_t lenFile = fread(&data[0], 1, data.size(), stdin);
	const UniMode umCodingCookie = CodingCookieValue(&data[0], lenFile);
	while (lenFile > 0) {
		lenFile = convert.convert(&data[0], lenFile);
		if (UseOutputPane) {
			wOutput_.CallString(SCI_ADDTEXT, lenFile, convert.getNewBuf());
		} else {
			wEditor_.CallString(SCI_ADDTEXT, lenFile, convert.getNewBuf());
		}
		lenFile = fread(&data[0], 1, data.size(), stdin);
	}
	if (UseOutputPane) {
		if (props_.GetInt("split.vertical") == 0) {
			heightOutput_ = 2000;
		} else {
			heightOutput_ = 500;
		}
		SizeSubWindows();
	} else {
		wEditor_.Call(SCI_ENDUNDOACTION);
	}
	CurrentBuffer()->unicodeMode = static_cast<UniMode>(
	            static_cast<int>(convert.getEncoding()));
	// Check the first two lines for coding cookies
	if (CurrentBuffer()->unicodeMode == uni8Bit) {
		CurrentBuffer()->unicodeMode = umCodingCookie;
	}
	if (CurrentBuffer()->unicodeMode != uni8Bit) {
		// Override the code page if Unicode
		codePage_ = SC_CP_UTF8;
	} else {
		codePage_ = props_.GetInt("code.page");
	}
	if (UseOutputPane) {
		wOutput_.Call(SCI_SETSEL, 0, 0);
	} else {
		wEditor_.Call(SCI_SETCODEPAGE, codePage_);

		// Zero all the style bytes
		wEditor_.Call(SCI_CLEARDOCUMENTSTYLE);

		CurrentBuffer()->overrideExtension = "x.txt";
		ReadProperties();
		SetIndentSettings();
		wEditor_.Call(SCI_COLOURISE, 0, -1);
		Redraw();

		wEditor_.Call(SCI_SETSEL, 0, 0);
	}
}

void SciTEBase::OpenFilesFromStdin() {
	char data[8 * 1024];

	/* if stdin is blocked, do not execute this method */
	if (IsStdinBlocked())
		return;

	while (fgets(data, sizeof(data) - 1, stdin)) {
		char *pNL;
		if ((pNL = strchr(data, '\n')) != NULL)
			* pNL = '\0';
		Open(GUI::StringFromUTF8(data).c_str(), kOfQuiet);
	}
	if (buffers_.lengthVisible == 0)
		Open(GUI_TEXT(""));
}

class BufferedFile {
	FILE *fp;
	bool readAll;
	bool exhausted;
	enum {bufLen = 64 * 1024};
	char buffer[bufLen];
	size_t pos;
	size_t valid;
	void EnsureData() {
		if (pos >= valid) {
			if (readAll || !fp) {
				exhausted = true;
			} else {
				valid = fread(buffer, 1, bufLen, fp);
				if (valid < bufLen) {
					readAll = true;
				}
				pos = 0;
			}
		}
	}
public:
	explicit BufferedFile(const FilePath &fPath) {
		fp = fPath.Open(fileRead);
		readAll = false;
		exhausted = fp == NULL;
		buffer[0] = 0;
		pos = 0;
		valid = 0;
	}
	~BufferedFile() {
		if (fp) {
			fclose(fp);
		}
		fp = NULL;
	}
	bool Exhausted() const {
		return exhausted;
	}
	int NextByte() {
		EnsureData();
		if (pos >= valid) {
			return 0;
		}
		return buffer[pos++];
	}
	bool BufferContainsNull() {
		EnsureData();
		for (size_t i = 0;i < valid;i++) {
			if (buffer[i] == '\0')
				return true;
		}
		return false;
	}
};

class FileReader {
	std::unique_ptr<BufferedFile> bf;
	int lineNum;
	bool lastWasCR;
	std::string lineToCompare;
	std::string lineToShow;
	bool caseSensitive;
public:
	FileReader(const FilePath &fPath, bool caseSensitive_) : bf(std::make_unique<BufferedFile>(fPath)) {
		lineNum = 0;
		lastWasCR = false;
		caseSensitive = caseSensitive_;
	}
	// Deleted so FileReader objects can not be copied.
	FileReader(const FileReader &) = delete;
	~FileReader() {
	}
	const char *Next() {
		if (bf->Exhausted()) {
			return NULL;
		}
		lineToShow.clear();
		while (!bf->Exhausted()) {
			const int ch = bf->NextByte();
			if (lastWasCR && ch == '\n' && lineToShow.empty()) {
				lastWasCR = false;
			} else if (ch == '\r' || ch == '\n') {
				lastWasCR = ch == '\r';
				break;
			} else {
				lineToShow.push_back(static_cast<char>(ch));
			}
		}
		lineNum++;
		lineToCompare = lineToShow;
		if (!caseSensitive) {
			LowerCaseAZ(lineToCompare);
		}
		return lineToCompare.c_str();
	}
	int LineNumber() const {
		return lineNum;
	}
	const char *Original() const {
		return lineToShow.c_str();
	}
	bool BufferContainsNull() {
		return bf->BufferContainsNull();
	}
};

static bool IsWordCharacter(int ch) {
	return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z')  || (ch >= '0' && ch <= '9')  || (ch == '_');
}

bool SciTEBase::GrepIntoDirectory(const FilePath &directory) {
    const GUI::GUIChar *sDirectory = directory.AsInternal();
#ifdef __APPLE__
    if (strcmp(sDirectory, "build") == 0)
        return false;
#endif
    return sDirectory[0] != '.';
}

void SciTEBase::GrepRecursive(GrepFlags gf, const FilePath &baseDir, const char *searchString, const GUI::GUIChar *fileTypes) {
	const int checkAfterLines = 10'000;
	FilePathSet directories;
	FilePathSet files;
	baseDir.List(directories, files);
	const size_t searchLength = strlen(searchString);
	std::string os;
	for (const FilePath &fPath : files) {
		if (jobQueue_.Cancelled())
			return;
		if (*fileTypes == '\0' || fPath.Matches(fileTypes)) {
			//OutputAppendStringSynchronised(i->AsInternal());
			//OutputAppendStringSynchronised("\n");
			FileReader fr(fPath, gf & kGrepMatchCase);
			if ((gf & kGrepBinary) || !fr.BufferContainsNull()) {
				while (const char *line = fr.Next()) {
					if (((fr.LineNumber() % checkAfterLines) == 0) && jobQueue_.Cancelled())
						return;
					const char *match = strstr(line, searchString);
					if (match) {
						if (gf & kGrepWholeWord) {
							const char *lineEnd = line + strlen(line);
							while (match) {
								if (((match == line) || !IsWordCharacter(match[-1])) &&
								        ((match + searchLength == (lineEnd)) || !IsWordCharacter(match[searchLength]))) {
									break;
								}
								match = strstr(match + 1, searchString);
							}
						}
						if (match) {
							os.append(fPath.AsUTF8().c_str());
							os.append(":");
							std::string lNumber = StdStringFromInteger(fr.LineNumber());
							os.append(lNumber.c_str());
							os.append(":");
							os.append(fr.Original());
							os.append("\n");
						}
					}
				}
			}
		}
	}
	if (os.length()) {
		if (gf & kGrepStdOut) {
			fwrite(os.c_str(), os.length(), 1, stdout);
		} else {
			OutputAppendStringSynchronised(os.c_str());
		}
	}
	for (const FilePath &fPath : directories) {
		if ((gf & kGrepDot) || GrepIntoDirectory(fPath.Name())) {
			GrepRecursive(gf, fPath, searchString, fileTypes);
		}
	}
}

void SciTEBase::InternalGrep(GrepFlags gf, const GUI::GUIChar *directory, const GUI::GUIChar *fileTypes, const char *search, sptr_t &originalEnd) {
	GUI::ElapsedTime commandTime;
	if (!(gf & kGrepStdOut)) {
		std::string os;
		os.append(">Internal search for \"");
		os.append(search);
		os.append("\" in \"");
		os.append(GUI::UTF8FromString(fileTypes).c_str());
		os.append("\"\n");
		OutputAppendStringSynchronised(os.c_str());
		ShowOutput_OnMainThread();
		originalEnd += os.length();
	}
	std::string searchString(search);
	if (!(gf & kGrepMatchCase)) {
		LowerCaseAZ(searchString);
	}
	GrepRecursive(gf, FilePath(directory), searchString.c_str(), fileTypes);
	if (!(gf & kGrepStdOut)) {
		std::string sExitMessage(">");
		if (jobQueue_.TimeCommands()) {
			sExitMessage += "    Time: ";
			sExitMessage += StdStringFromDouble(commandTime.Duration(), 3);
		}
		sExitMessage += "\n";
		OutputAppendStringSynchronised(sExitMessage.c_str());
	}
}

