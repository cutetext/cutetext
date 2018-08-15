// This file is part of CuteText project
// Copyright (C) 2018 by James Zeng <james.zeng@hotmail.com>
// The LICENSE file describes the conditions under which this software may be distributed.
/**
 * @file cutetext_base.h
 * @author James Zeng
 * @date 2018-08-12
 * @brief Definition of platform independent base class of editor.
 *
 * @see https://github.com/cutetext/cutetext
 */

extern const GUI::GUIChar appName[];

extern const GUI::GUIChar propUserFileName[];
extern const GUI::GUIChar propGlobalFileName[];
extern const GUI::GUIChar propAbbrevFileName[];

#ifdef _WIN32
#ifdef _MSC_VER
// Shut up level 4 warning:
// warning C4800: forcing value to bool 'true' or 'false' (performance warning)
#pragma warning(disable: 4800)
#endif
#endif

inline int Minimum(int a, int b) {
    return (a < b) ? a : b;
}

inline int Maximum(int a, int b) {
    return (a > b) ? a : b;
}

inline long LongFromTwoShorts(short a,short b) {
    return (a) | ((b) << 16);
}

/**
 * The order of menus on Windows - the Buffers menu may not be present
 * and there is a Help menu at the end.
 */
enum {
    kMenuFile = 0, kMenuEdit = 1, kMenuSearch = 2, kMenuView = 3,
    kMenuTools = 4, kMenuOptions = 5, kMenuLanguage = 6, kMenuBuffers = 7,
    kMenuHelp = 8
};

struct SelectedRange {
    int position_;
    int anchor_;
    SelectedRange(int position= INVALID_POSITION, int anchor= INVALID_POSITION) :
        position_(position), anchor_(anchor) {
    }
};

class RecentFile : public FilePath {
public:
    SelectedRange selection_;
    int scrollPosition_;
    RecentFile() {
        scrollPosition_ = 0;
    }
    RecentFile(const FilePath &path, SelectedRange selection, int scrollPosition) :
        FilePath(path), selection_(selection), scrollPosition_(scrollPosition) {
    }
    RecentFile(RecentFile const &) = default;
    RecentFile(RecentFile &&) = default;
    RecentFile &operator=(RecentFile const &) = default;
    RecentFile &operator=(RecentFile &&) = default;
    ~RecentFile() override = default;
    void Init() override {
        FilePath::Init();
        selection_.position_ = INVALID_POSITION;
        selection_.anchor_ = INVALID_POSITION;
        scrollPosition_ = 0;
    }
};

struct BufferState {
public:
    RecentFile file_;
    std::vector<int> foldState_;
    std::vector<int> bookmarks_;
};

class Session {
public:
    FilePath pathActive_;
    std::vector<BufferState> buffers_;
};

struct FileWorker;

class Buffer {
public:
    RecentFile file_;
    sptr_t doc_;
    bool isDirty_;
    bool isReadOnly_;
    bool failedSave_;
    bool useMonoFont_;
    enum { kEmpty, kReading, kReadAll, kOpen } lifeState_;
    UniMode unicodeMode_;
    time_t fileModTime_;
    time_t fileModLastAsk_;
    time_t documentModTime_;
    enum { kFmNone, kFmTemporary, kFmMarked, kFmModified} findMarks_;
    std::string overrideExtension_; ///< User has chosen to use a particular language
    std::vector<int> foldState_;
    std::vector<int> bookmarks_;
    FileWorker *pFileWorker_;
    PropSetFile props_;
    enum FutureDo { kFdNone=0, kFdFinishSave=1 } futureDo_;
    Buffer() :
            file_(), doc_(0), isDirty_(false), isReadOnly_(false), failedSave_(false), useMonoFont_(false), lifeState_(kEmpty),
            unicodeMode_(kUni8Bit), fileModTime_(0), fileModLastAsk_(0), documentModTime_(0),
            findMarks_(kFmNone), pFileWorker_(0), futureDo_(kFdNone) {}

    ~Buffer() = default;
    void Init() {
        file_.Init();
        isDirty_ = false;
        isReadOnly_ = false;
        failedSave_ = false;
        useMonoFont_ = false;
        lifeState_ = kEmpty;
        unicodeMode_ = kUni8Bit;
        fileModTime_ = 0;
        fileModLastAsk_ = 0;
        documentModTime_ = 0;
        findMarks_ = kFmNone;
        overrideExtension_ = "";
        foldState_.clear();
        bookmarks_.clear();
        pFileWorker_ = 0;
        futureDo_ = kFdNone;
    }

    void SetTimeFromFile() {
        fileModTime_ = file_.ModifiedTime();
        fileModLastAsk_ = fileModTime_;
        documentModTime_ = fileModTime_;
        failedSave_ = false;
    }

    void DocumentModified();
    bool NeedsSave(int delayBeforeSave) const;

    void CompleteLoading();
    void CompleteStoring();
    void AbandonAutomaticSave();

    bool ShouldNotSave() const {
        return lifeState_ != kOpen;
    }

    void CancelLoad();
};

struct BackgroundActivities {
    int loaders_;
    int storers_;
    size_t totalWork_;
    size_t totalProgress_;
    GUI::GUIString fileNameLast_;
};

class BufferList {
protected:
    int current_;
    int stackcurrent_;
    std::vector<int> stack_;
public:
    std::vector<Buffer> buffers_;
    int length_;
    int lengthVisible_;
    bool initialised_;

    BufferList();
    ~BufferList();
    int size() const {
        return static_cast<int>(buffers_.size());
    }
    void Allocate(int maxSize);
    int Add();
    int GetDocumentByWorker(const FileWorker *pFileWorker) const;
    int GetDocumentByName(const FilePath &filename, bool excludeCurrent=false);
    void RemoveInvisible(int index);
    void RemoveCurrent();
    int Current() const;
    Buffer *CurrentBuffer();
    const Buffer *CurrentBufferConst() const;
    void SetCurrent(int index);
    int StackNext();
    int StackPrev();
    void CommitStackSelection();
    void MoveToStackTop(int index);
    void ShiftTo(int indexFrom, int indexTo);
    void Swap(int indexA, int indexB);
    bool SingleBuffer() const;
    BackgroundActivities CountBackgroundActivities() const;
    bool SavingInBackground() const;
    bool GetVisible(int index) const;
    void SetVisible(int index, bool visible);
    void AddFuture(int index, Buffer::FutureDo fd);
    void FinishedFuture(int index, Buffer::FutureDo fd);
private:
    void PopStack();
};

// class to hold user defined keyboard shortcuts
class ShortcutItem {
public:
    std::string menuKey_; // the keyboard shortcut
    std::string menuCommand_; // the menu command to be passed to "CuteTextBase::MenuCommand"
};

class LanguageMenuItem {
public:
    std::string menuItem_;
    std::string menuKey_;
    std::string extension_;
};

enum {
    kHeightTools = 24,
    kHeightToolsBig = 32,
    kHeightTab = 24,
    kHeightStatus = 20,
    kStatusPosWidth = 256
};

/// Warning IDs.
enum {
    kWarnFindWrapped = 1,
    kWarnNotFound,
    kWarnNoOtherBookmark,
    kWarnWrongFile,
    kWarnExecuteOK,
    kWarnExecuteKO
};

/// Codes representing the effect a line has on indentation.
enum IndentationStatus {
    kIsNone,        // no effect on indentation
    kIsBlockStart,  // indentation block begin such as "{" or VB "function"
    kIsBlockEnd,    // indentation end indicator such as "}" or VB "end"
    kIsKeyWordStart // Keywords that cause indentation
};

struct StyleAndWords {
    int styleNumber_;
    std::string words_;
    StyleAndWords() : styleNumber_(0) {
    }
    bool IsEmpty() const { return words_.length() == 0; }
    bool IsSingleChar() const { return words_.length() == 1; }
};

struct CurrentWordHighlight {
    enum {
        kNoDelay,            // No delay, and no word at the caret.
        kDelay,              // Delay before to highlight the word at the caret.
        kDelayJustEnded,     // Delay has just ended. This state allows to ignore next HighlightCurrentWord (SCN_UPDATEUI and SC_UPDATE_CONTENT for setting indicators).
        kDelayAlreadyElapsed // Delay has already elapsed, word at the caret and occurrences are (or have to be) highlighted.
    } statesOfDelay_;
    bool isEnabled_;
    bool textHasChanged_;
    GUI::ElapsedTime elapsedTimes_;
    bool isOnlyWithSameStyle_;

    CurrentWordHighlight() {
        statesOfDelay_ = kNoDelay;
        isEnabled_ = false;
        isOnlyWithSameStyle_ = false;
        textHasChanged_ = false;
    }
};

class Localization : public PropSetFile, public ILocalize {
    std::string missing_;
public:
    bool read_;
    Localization() : PropSetFile(true), read_(false) {
    }
    GUI::GUIString Text(const char *s, bool retainIfNotFound=true) override;
    void SetMissing(const std::string &missing) {
        missing_ = missing;
    }
};

// Interface between CuteText and dialogs and strips for find and replace
class Searcher {
public:
    std::string findWhat_;
    std::string replaceWhat_;

    bool wholeWord_;
    bool matchCase_;
    bool regExp_;
    bool unSlash_;
    bool wrapFind_;
    bool reverseFind_;

    int searchStartPosition_;
    bool replacing_;
    bool havefound_;
    bool failedfind_;
    bool findInStyle_;
    int findStyle_;
    enum class CloseFind { kClosePrevent, kCloseAlways, kCloseOnMatch } closeFind_;
    ComboMemory memFinds_;
    ComboMemory memReplaces_;

    bool focusOnReplace_;

    Searcher();

    virtual void SetFindText(const char *sFind) = 0;
    virtual void SetFind(const char *sFind) = 0;
    virtual bool FindHasText() const = 0;
    void InsertFindInMemory();
    virtual void SetReplace(const char *sReplace) = 0;
    virtual void SetCaretAsStart() = 0;
    virtual void MoveBack() = 0;
    virtual void ScrollEditorIfNeeded() = 0;

    virtual int FindNext(bool reverseDirection, bool showWarnings=true, bool allowRegExp=true) = 0;
    virtual void HideMatch() = 0;
    enum MarkPurpose { kMarkWithBookMarks, kMarkIncremental };
    virtual void MarkAll(MarkPurpose purpose=kMarkWithBookMarks) = 0;
    virtual int ReplaceAll(bool inSelection) = 0;
    virtual void ReplaceOnce(bool showWarnings=true) = 0;
    virtual void UIClosed() = 0;
    virtual void UIHasFocus() = 0;
    bool &FlagFromCmd(int cmd);
    bool ShouldClose(bool found) const {
        return (closeFind_ == CloseFind::kCloseAlways) || (found && (closeFind_ == CloseFind::kCloseOnMatch));
    }
};

// User interface for search options implemented as both buttons and popup menu items
struct SearchOption {
    enum { kTWord, kTCase, kTRegExp, kTBackslash, kTWrap, kTUp };
    const char *label_;
    int cmd_;    // Menu item
    int id_; // Control in dialog
};

class SearchUI {
protected:
    Searcher *pSearcher_;
public:
    SearchUI() : pSearcher_(0) {
    }
    void SetSearcher(Searcher *pSearcher) {
        pSearcher_ = pSearcher;
    }
};

class IEditorConfig;

class CuteTextBase : public ExtensionAPI, public Searcher, public WorkerListener {
protected:
    bool needIdle_;
    GUI::GUIString windowName_;
    FilePath filePath_;
    FilePath dirNameAtExecute_;
    FilePath dirNameForExecute_;

    enum { kFileStackMax = 10 };
    RecentFile recentFileStack_[kFileStackMax];
    enum { kFileStackCmdID = IDM_MRUFILE, kBufferCmdID = IDM_BUFFER };

    enum { kImportMax = 50 };
    FilePathSet importFiles_;
    enum { kImportCmdID = IDM_IMPORT };
    ImportFilter filter_;

    enum { kIndicatorMatch = INDIC_CONTAINER,
        kIndicatorHighlightCurrentWord,
        kIndicatorSpellingMistake,
        kIndicatorSentinel };
    enum { kMarkerBookmark = 1 };
    ComboMemory memFiles_;
    ComboMemory memDirectory_;
    std::string parameterisedCommand_;
    std::string abbrevInsert_;

    enum { kLanguageCmdID = IDM_LANGUAGE };
    std::vector<LanguageMenuItem> languageMenu_;

    // an array of short cut items that are defined by the user in the properties file.
    std::vector<ShortcutItem> shortCutItemList_;

    int codePage_;
    int characterSet_;
    std::string language_;
    int lexLanguage_;
    std::vector<char> subStyleBases_;
    int lexLPeg_;
    StringList apis_;
    std::string apisFileNames_;
    std::string functionDefinition_;

    int diagnosticStyleStart_;
    enum { kDiagnosticStyles=4};

    bool stripTrailingSpaces_;
    bool ensureFinalLineEnd_;
    bool ensureConsistentLineEnds_;

    bool indentOpening_;
    bool indentClosing_;
    bool indentMaintain_;
    int statementLookback_;
    StyleAndWords statementIndent_;
    StyleAndWords statementEnd_;
    StyleAndWords blockStart_;
    StyleAndWords blockEnd_;
    enum PreProcKind { kPpcNone, kPpcStart, kPpcMiddle, kPpcEnd, kPpcDummy };    ///< Indicate the kind of preprocessor condition line
    char preprocessorSymbol_;    ///< Preprocessor symbol (in C, #)
    std::map<std::string, PreProcKind> preprocOfString_; ///< Map preprocessor keywords to positions
    /// In C, if ifdef ifndef : start, else elif : middle, endif : end.

    GUI::Window wCuteText_;  ///< Contains wToolBar, wTabBar, wContent, and wStatusBar
    GUI::Window wContent_;    ///< Contains wEditor and wOutput
    GUI::ScintillaWindow wEditor_;
    GUI::ScintillaWindow wOutput_;
    GUI::ScintillaWindow *pwFocussed_;
    GUI::Window wIncrement_;
    GUI::Window wToolBar_;
    GUI::Window wStatusBar_;
    GUI::Window wTabBar_;
    GUI::Menu popup_;
    bool tbVisible_;
    bool tabVisible_;
    bool tabHideOne_; // Hide tab bar if one buffer is opened only
    bool tabMultiLine_;
    bool sbVisible_; ///< @c true if status bar is visible.
    std::string sbValue_;    ///< Status bar text.
    int sbNum_;  ///< Number of the currently displayed status bar information.
    int visHeightTools_;
    int visHeightTab_;
    int visHeightStatus_;
    int visHeightEditor_;
    int heightBar_;
    // Prevent automatic load dialog appearing at the same time as
    // other dialogs as this can leads to reentry errors.
    int dialogsOnScreen_;
    bool topMost_;
    bool wrap_;
    bool wrapOutput_;
    int wrapStyle_;
    int alphaIndicator_;
    bool underIndicator_;
    std::string foldColour_;
    std::string foldHiliteColour_;
    bool openFilesHere_;
    bool fullScreen_;
    enum { kToolMax = 50 };
    Extension *extender_;
    bool needReadProperties_;
    bool quitting_;

    int timerMask_;
    enum { kTimerAutoSave = 1 };
    int delayBeforeAutoSave_;

    int heightOutput_;
    int heightOutputStartDrag_;
    GUI::Point ptStartDrag_;
    bool capturedMouse_;
    int previousHeightOutput_;
    bool firstPropertiesRead_;
    bool splitVertical_; ///< @c true if the split bar between editor and output is vertical.
    bool bufferedDraw_;
    bool bracesCheck_;
    bool bracesSloppy_;
    int bracesStyle_;
    int braceCount_;

    int indentationWSVisible_;
    int indentExamine_;
    bool autoCompleteIgnoreCase_;
    bool imeAutoComplete_;
    bool callTipUseEscapes_;
    bool callTipIgnoreCase_;
    bool autoCCausedByOnlyOne_;
    std::string calltipWordCharacters_;
    std::string calltipParametersStart_;
    std::string calltipParametersEnd_;
    std::string calltipParametersSeparators_;
    std::string calltipEndDefinition_;
    std::string autoCompleteStartCharacters_;
    std::string autoCompleteFillUpCharacters_;
    std::string autoCompleteTypeSeparator_;
    std::string wordCharacters_;
    std::string whitespaceCharacters_;
    int startCalltipWord_;
    int currentCallTip_;
    int maxCallTips_;
    std::string currentCallTipWord_;
    int lastPosCallTip_;

    bool margin_;
    int marginWidth_;
    enum { kMarginWidthDefault = 20};

    bool foldMargin_;
    int foldMarginWidth_;
    enum { kFoldMarginWidthDefault = 14};

    bool lineNumbers_;
    int lineNumbersWidth_;
    enum { kLineNumbersWidthDefault = 4 };
    bool lineNumbersExpand_;

    bool allowMenuActions_;
    int scrollOutput_;
    bool returnOutputToCommand_;
    JobQueue jobQueue_;

    bool macrosEnabled_;
    std::string currentMacro_;
    bool recording_;

    PropSetFile propsPlatform_;
    PropSetFile propsEmbed_;
    PropSetFile propsBase_;
    PropSetFile propsUser_;
    PropSetFile propsDirectory_;
    PropSetFile propsLocal_;
    PropSetFile propsDiscovered_;
    PropSetFile props_;

    PropSetFile propsAbbrev_;

    PropSetFile propsSession_;

    FilePath pathAbbreviations_;

    Localization localiser_;

    PropSetFile propsStatus_;    // Not attached to a file but need SetInteger method.

    std::unique_ptr<IEditorConfig> editorConfig_;

    enum { kBufferMax = IDM_IMPORT - IDM_BUFFER };
    BufferList buffers_;

    // Handle buffers
    sptr_t GetDocumentAt(int index);
    void SwitchDocumentAt(int index, sptr_t pdoc);
    void UpdateBuffersCurrent();
    bool IsBufferAvailable() const;
    bool CanMakeRoom(bool maySaveIfDirty = true);
    void SetDocumentAt(int index, bool updateStack = true);
    Buffer *CurrentBuffer() {
        return buffers_.CurrentBuffer();
    }
    const Buffer *CurrentBufferConst() const {
        return buffers_.CurrentBufferConst();
    }
    void SetBuffersMenu();
    void BuffersMenu();
    void Next();
    void Prev();
    void NextInStack();
    void PrevInStack();
    void EndStackedTabbing();

    virtual void TabInsert(int index, const GUI::GUIChar *title) = 0;
    virtual void TabSelect(int index) = 0;
    virtual void RemoveAllTabs() = 0;
    void ShiftTab(int indexFrom, int indexTo);
    void MoveTabRight();
    void MoveTabLeft();

    virtual void ReadEmbeddedProperties();
    void ReadEnvironment();
    void ReadGlobalPropFile();
    void ReadAbbrevPropFile();
    void ReadLocalPropFile();
    void ReadDirectoryPropFile();

    void SetPaneFocus(bool editPane);
    int CallFocused(unsigned int msg, uptr_t wParam = 0, sptr_t lParam = 0);
    int CallFocusedElseDefault(int defaultValue, unsigned int msg, uptr_t wParam = 0, sptr_t lParam = 0);
    sptr_t CallPane(int destination, unsigned int msg, uptr_t wParam = 0, sptr_t lParam = 0);
    void CallChildren(unsigned int msg, uptr_t wParam = 0, sptr_t lParam = 0);
    std::string GetTranslationToAbout(const char * const propname, bool retainIfNotFound = true);
    int LengthDocument();
    int GetCaretInLine();
    void GetLine(char *text, int sizeText, int line = -1);
    std::string GetCurrentLine();
    static void GetRange(GUI::ScintillaWindow &win, int start, int end, char *text);
    int IsLinePreprocessorCondition(char *line);
    bool FindMatchingPreprocessorCondition(int &curLine, int direction, int condEnd1, int condEnd2);
    bool FindMatchingPreprocCondPosition(bool isForward, int &mppcAtCaret, int &mppcMatch);
    bool FindMatchingBracePosition(bool editor, int &braceAtCaret, int &braceOpposite, bool sloppy);
    void BraceMatch(bool editor);

    virtual void WarnUser(int warnID) = 0;
    void SetWindowName();
    void SetFileName(const FilePath &openName, bool fixCase = true);
    FilePath FileNameExt() const {
        return filePath_.Name();
    }
    void ClearDocument();
    void CreateBuffers();
    void InitialiseBuffers();
    FilePath UserFilePath(const GUI::GUIChar *name);
    void LoadSessionFile(const GUI::GUIChar *sessionName);
    void RestoreRecentMenu();
    void RestoreFromSession(const Session &session);
    void RestoreSession();
    void SaveSessionFile(const GUI::GUIChar *sessionName);
    virtual void GetWindowPosition(int *left, int *top, int *width, int *height, int *maximize) = 0;
    void SetIndentSettings();
    void SetEol();
    void New();
    void RestoreState(const Buffer &buffer, bool restoreBookmarks);
    void Close(bool updateUI = true, bool loadingSession = false, bool makingRoomForNew = false);
    static bool Exists(const GUI::GUIChar *dir, const GUI::GUIChar *path, FilePath *resultPath);
    void DiscoverEOLSetting();
    void DiscoverIndentSetting();
    std::string DiscoverLanguage();
    void OpenCurrentFile(long long fileSize, bool suppressMessage, bool asynchronous);
    virtual void OpenUriList(const char *) {}
    virtual bool OpenDialog(const FilePath &directory, const GUI::GUIChar *filesFilter) = 0;
    virtual bool SaveAsDialog() = 0;
    virtual void LoadSessionDialog() {}
    virtual void SaveSessionDialog() {}
    void CountLineEnds(int &linesCR, int &linesLF, int &linesCRLF);
    enum OpenFlags {
        kOfNone = 0,         // Default
        kOfNoSaveIfDirty = 1,    // Suppress check for unsaved changes
        kOfForceLoad = 2,    // Reload file even if already in a buffer
        kOfPreserveUndo = 4, // Do not delete undo history
        kOfQuiet = 8,        // Avoid "Could not open file" message
        kOfSynchronous = 16  // Force synchronous read
    };
    void TextRead(FileWorker *pFileWorker);
    void TextWritten(FileWorker *pFileWorker);
    void UpdateProgress(Worker *pWorker);
    void PerformDeferredTasks();
    enum OpenCompletion { kOcSynchronous, kOcCompleteCurrent, kOcCompleteSwitch };
    void CompleteOpen(OpenCompletion oc);
    virtual bool PreOpenCheck(const GUI::GUIChar *file);
    bool Open(const FilePath &file, OpenFlags of = kOfNone);
    bool OpenSelected();
    void Revert();
    FilePath SaveName(const char *ext) const;
    enum SaveFlags {
        kSfNone = 0,         // Default
        kSfProgressVisible = 1,  // Show in background save strip
        kSfSynchronous = 16  // Write synchronously blocking UI
    };
    enum SaveResult {
        kSaveCompleted,
        kSaveCancelled
    };
    SaveResult SaveIfUnsure(bool forceQuestion = false, SaveFlags sf = kSfProgressVisible);
    SaveResult SaveIfUnsureAll();
    SaveResult SaveIfUnsureForBuilt();
    bool SaveIfNotOpen(const FilePath &destFile, bool fixCase);
    void AbandonAutomaticSave();
    bool Save(SaveFlags sf = kSfProgressVisible);
    void SaveAs(const GUI::GUIChar *file, bool fixCase);
    virtual void SaveACopy() = 0;
    void SaveToHTML(const FilePath &saveName);
    void StripTrailingSpaces();
    void EnsureFinalNewLine();
    bool PrepareBufferForSave(const FilePath &saveName);
    bool SaveBuffer(const FilePath &saveName, SaveFlags sf);
    virtual void SaveAsHTML() = 0;
    void SaveToStreamRTF(std::ostream &os, int start = 0, int end = -1);
    void SaveToRTF(const FilePath &saveName, int start = 0, int end = -1);
    virtual void SaveAsRTF() = 0;
    void SaveToPDF(const FilePath &saveName);
    virtual void SaveAsPDF() = 0;
    void SaveToTEX(const FilePath &saveName);
    virtual void SaveAsTEX() = 0;
    void SaveToXML(const FilePath &saveName);
    virtual void SaveAsXML() = 0;
    virtual FilePath GetDefaultDirectory() = 0;
    virtual FilePath GetSciteDefaultHome() = 0;
    virtual FilePath GetSciteUserHome() = 0;
    FilePath GetDefaultPropertiesFileName();
    FilePath GetUserPropertiesFileName();
    FilePath GetDirectoryPropertiesFileName();
    FilePath GetLocalPropertiesFileName();
    FilePath GetAbbrevPropertiesFileName();
    void OpenProperties(int propsFile);
    static int GetMenuCommandAsInt(std::string commandName);
    virtual void Print(bool) {}
    virtual void PrintSetup() {}
    void UserStripShow(const char * /* description */) override {}
    void UserStripSet(int /* control */, const char * /* value */) override {}
    void UserStripSetList(int /* control */, const char * /* value */) override {}
    std::string UserStripValue(int /* control */) override { return std::string(); }
    virtual void ShowBackgroundProgress(const GUI::GUIString & /* explanation */, size_t /* size */, size_t /* progress */) {}
    Sci_CharacterRange GetSelection();
    SelectedRange GetSelectedRange();
    void SetSelection(int anchor, int currentPos);
    std::string GetCTag();
    static std::string GetRangeString(GUI::ScintillaWindow &win, int selStart, int selEnd);
    virtual std::string GetRangeInUIEncoding(GUI::ScintillaWindow &win, int selStart, int selEnd);
    static std::string GetLine(GUI::ScintillaWindow &win, int line);
    void RangeExtend(GUI::ScintillaWindow &wCurrent, int &selStart, int &selEnd,
        bool (CuteTextBase::*ischarforsel)(char ch));
    std::string RangeExtendAndGrab(GUI::ScintillaWindow &wCurrent, int &selStart, int &selEnd,
        bool (CuteTextBase::*ischarforsel)(char ch), bool stripEol = true);
    std::string SelectionExtend(bool (CuteTextBase::*ischarforsel)(char ch), bool stripEol = true);
    std::string SelectionWord(bool stripEol = true);
    std::string SelectionFilename();
    void SelectionIntoProperties();
    void SelectionIntoFind(bool stripEol = true);
    enum AddSelection { kAddNext, kAddEach };
    void SelectionAdd(AddSelection add);
    virtual std::string EncodeString(const std::string &s);
    virtual void Find() = 0;
    enum MessageBoxChoice {
        kMbcOK,
        kMbcCancel,
        kMbcYes,
        kMbcNo
    };
    typedef int MessageBoxStyle;
    enum {
        // Same as Win32 MB_*
        kMbsOK = 0,
        kMbsYesNo = 4,
        kMbsYesNoCancel = 3,
        kMbsIconQuestion = 0x20,
        kMbsIconWarning = 0x30
    };
    virtual MessageBoxChoice WindowMessageBox(GUI::Window &w, const GUI::GUIString &msg, MessageBoxStyle style = kMbsIconWarning) = 0;
    void FailedSaveMessageBox(const FilePath &filePathSaving);
    virtual void FindMessageBox(const std::string &msg, const std::string *findItem = 0) = 0;
    bool FindReplaceAdvanced() const;
    int FindInTarget(const std::string &findWhatText, int startPosition, int endPosition);
    // Implement Searcher
    void SetFindText(const char *sFind) override;
    void SetFind(const char *sFind) override;
    bool FindHasText() const override;
    void SetReplace(const char *sReplace) override;
    void SetCaretAsStart() override;
    void MoveBack() override;
    void ScrollEditorIfNeeded() override;
    int FindNext(bool reverseDirection, bool showWarnings=true, bool allowRegExp=true) override;
    void HideMatch() override;
    virtual void FindIncrement() = 0;
    int IncrementSearchMode();
    virtual void FindInFiles() = 0;
    virtual void Replace() = 0;
    void ReplaceOnce(bool showWarnings=true) override;
    int DoReplaceAll(bool inSelection); // returns number of replacements or negative value if error
    int ReplaceAll(bool inSelection) override;
    int ReplaceInBuffers();
    void UIClosed() override;
    void UIHasFocus() override;
    virtual void DestroyFindReplace() = 0;
    virtual void GoLineDialog() = 0;
    virtual bool AbbrevDialog() = 0;
    virtual void TabSizeDialog() = 0;
    virtual bool ParametersOpen() = 0;
    virtual void ParamGrab() = 0;
    virtual bool ParametersDialog(bool modal) = 0;
    bool HandleXml(char ch);
    static std::string FindOpenXmlTag(const char sel[], int nSize);
    void GoMatchingBrace(bool select);
    void GoMatchingPreprocCond(int direction, bool select);
    virtual void FindReplace(bool replace) = 0;
    void OutputAppendString(const char *s, int len = -1);
    virtual void OutputAppendStringSynchronised(const char *s, int len = -1);
    virtual void Execute();
    virtual void StopExecute() = 0;
    void ShowMessages(int line);
    void GoMessage(int dir);
    virtual bool StartCallTip();
    std::string GetNearestWords(const char *wordStart, size_t searchLen,
        const char *separators, bool ignoreCase=false, bool exactLen=false);
    virtual void FillFunctionDefinition(int pos = -1);
    void ContinueCallTip();
    virtual void EliminateDuplicateWords(std::string &words);
    virtual bool StartAutoComplete();
    virtual bool StartAutoCompleteWord(bool onlyOneWord);
    virtual bool StartExpandAbbreviation();
    bool PerformInsertAbbreviation();
    virtual bool StartInsertAbbreviation();
    virtual bool StartBlockComment();
    virtual bool StartBoxComment();
    virtual bool StartStreamComment();
    std::vector<std::string> GetLinePartsInStyle(int line, const StyleAndWords &saw);
    void SetLineIndentation(int line, int indent);
    int GetLineIndentation(int line);
    int GetLineIndentPosition(int line);
    void ConvertIndentation(int tabSize, int useTabs);
    bool RangeIsAllWhitespace(int start, int end);
    IndentationStatus GetIndentState(int line);
    int IndentOfBlock(int line);
    void MaintainIndentation(char ch);
    void AutomaticIndentation(char ch);
    void CharAdded(int utf32);
    void CharAddedOutput(int ch);
    void SetTextProperties(PropSetFile &ps);
    virtual void SetFileProperties(PropSetFile &ps) = 0;
    void UpdateStatusBar(bool bUpdateSlowData) override;
    int GetLineLength(int line);
    int GetCurrentLineNumber();
    int GetCurrentColumnNumber();
    int GetCurrentScrollPosition();
    virtual void AddCommand(const std::string &cmd, const std::string &dir,
            JobSubsystem jobType, const std::string &input = "",
            int flags = 0);
    virtual void AboutDialog() = 0;
    virtual void QuitProgram() = 0;
    void CloseTab(int tab);
    void CloseAllBuffers(bool loadingSession = false);
    SaveResult SaveAllBuffers(bool alwaysYes);
    void SaveTitledBuffers();
    virtual void CopyAsRTF() {}
    virtual void CopyPath() {}
    void SetLineNumberWidth();
    void MenuCommand(int cmdID, int source = 0);
    void FoldChanged(int line, int levelNow, int levelPrev);
    void ExpandFolds(int line, bool expand, int level);
    void FoldAll();
    void ToggleFoldRecursive(int line, int level);
    void EnsureAllChildrenVisible(int line, int level);
    static void EnsureRangeVisible(GUI::ScintillaWindow &win, int posStart, int posEnd, bool enforcePolicy = true);
    void GotoLineEnsureVisible(int line);
    bool MarginClick(int position, int modifiers);
    void NewLineInOutput();
    virtual void SetStatusBarText(const char *s) = 0;
    virtual void Notify(SCNotification *notification);
    virtual void ShowToolBar() = 0;
    virtual void ShowTabBar() = 0;
    virtual void ShowStatusBar() = 0;
    virtual void ActivateWindow(const char *timestamp) = 0;

    void RemoveFindMarks();
    int SearchFlags(bool regularExpressions) const;
    void MarkAll(MarkPurpose purpose=kMarkWithBookMarks) override;
    void BookmarkAdd(int lineno = -1);
    void BookmarkDelete(int lineno = -1);
    bool BookmarkPresent(int lineno = -1);
    void BookmarkToggle(int lineno = -1);
    void BookmarkNext(bool forwardScan = true, bool select = false);
    void BookmarkSelectAll();
    void SetOutputVisibility(bool show);
    virtual void ShowOutputOnMainThread();
    void ToggleOutputVisible();
    virtual void SizeContentWindows() = 0;
    virtual void SizeSubWindows() = 0;

    virtual void SetMenuItem(int menuNumber, int position, int itemID,
        const GUI::GUIChar *text, const GUI::GUIChar *mnemonic = 0) = 0;
    virtual void RedrawMenu() {}
    virtual void DestroyMenuItem(int menuNumber, int itemID) = 0;
    virtual void CheckAMenuItem(int wIDCheckItem, bool val) = 0;
    virtual void EnableAMenuItem(int wIDCheckItem, bool val) = 0;
    virtual void CheckMenusClipboard();
    virtual void CheckMenus();
    virtual void AddToPopUp(const char *label, int cmd = 0, bool enabled = true) = 0;
    void ContextMenu(GUI::ScintillaWindow &wSource, GUI::Point pt, GUI::Window wCmd);

    void DeleteFileStackMenu();
    void SetFileStackMenu();
    bool AddFileToBuffer(const BufferState &bufferState);
    void AddFileToStack(const RecentFile &file);
    void RemoveFileFromStack(const FilePath &file);
    RecentFile GetFilePosition();
    void DisplayAround(const RecentFile &rf);
    void StackMenu(int pos);
    void StackMenuNext();
    void StackMenuPrev();

    void RemoveToolsMenu();
    void SetMenuItemLocalised(int menuNumber, int position, int itemID,
            const char *text, const char *mnemonic);
    bool ToolIsImmediate(int item);
    void SetToolsMenu();
    JobSubsystem SubsystemType(const char *cmd);
    void ToolsMenu(int item);

    void AssignKey(int key, int mods, int cmd);
    void ViewWhitespace(bool view);
    void SetAboutMessage(GUI::ScintillaWindow &wsci, const char *appTitle);
    void SetImportMenu();
    void ImportMenu(int pos);
    void SetLanguageMenu();
    void SetPropertiesInitial();
    GUI::GUIString LocaliseMessage(const char *s,
        const GUI::GUIChar *param0 = 0, const GUI::GUIChar *param1 = 0, const GUI::GUIChar *param2 = 0);
    virtual void ReadLocalization();
    std::string GetFileNameProperty(const char *name);
    virtual void ReadPropertiesInitial();
    void ReadFontProperties();
    void SetOverrideLanguage(int cmdID);
    StyleAndWords GetStyleAndWords(const char *base);
    std::string ExtensionFileName() const;
    static const char *GetNextPropItem(const char *pStart, char *pPropItem, int maxLen);
    void ForwardPropertyToEditor(const char *key);
    void DefineMarker(int marker, int markerType, Colour fore, Colour back, Colour backSelected);
    void ReadAPI(const std::string &fileNameForExtension);
    std::string FindLanguageProperty(const char *pattern, const char *defaultValue = "");
    virtual void ReadProperties();
    std::string StyleString(const char *lang, int style) const;
    StyleDefinition StyleDefinitionFor(int style);
    void SetOneStyle(GUI::ScintillaWindow &win, int style, const StyleDefinition &sd);
    void SetStyleBlock(GUI::ScintillaWindow &win, const char *lang, int start, int last);
    void SetStyleFor(GUI::ScintillaWindow &win, const char *lang);
    static void SetOneIndicator(GUI::ScintillaWindow &win, int indicator, const IndicatorDefinition &ind);
    void ReloadProperties();

    void CheckReload();
    void Activate(bool activeApp);
    GUI::Rectangle GetClientRectangle();
    void Redraw();
    int NormaliseSplit(int splitPos);
    void MoveSplit(GUI::Point ptNewDrag);

    virtual void TimerStart(int mask);
    virtual void TimerEnd(int mask);
    void OnTimer();
    virtual void SetIdler(bool on);
    void OnIdle();

    void SetHomeProperties();
    void UIAvailable();
    void PerformOne(char *action);
    void StartRecordMacro();
    void StopRecordMacro();
    void StartPlayMacro();
    bool RecordMacroCommand(const SCNotification *notification);
    void ExecuteMacroCommand(const char *command);
    void AskMacroList();
    bool StartMacroList(const char *words);
    void ContinueMacroList(const char *stext);
    bool ProcessCommandLine(const GUI::GUIString &args, int phase);
    virtual bool IsStdinBlocked();
    void OpenFromStdin(bool UseOutputPane);
    void OpenFilesFromStdin();
    enum GrepFlags {
        kGrepNone = 0, kGrepWholeWord = 1, kGrepMatchCase = 2, kGrepStdOut = 4,
        kGrepDot = 8, kGrepBinary = 16, kGrepScroll = 32
    };
    virtual bool GrepIntoDirectory(const FilePath &directory);
    void GrepRecursive(GrepFlags gf, const FilePath &baseDir, const char *searchString, const GUI::GUIChar *fileTypes);
    void InternalGrep(GrepFlags gf, const GUI::GUIChar *directory, const GUI::GUIChar *fileTypes,
              const char *search, sptr_t &originalEnd);
    void EnumProperties(const char *propkind);
    void SendOneProperty(const char *kind, const char *key, const char *val);
    void PropertyFromDirector(const char *arg);
    void PropertyToDirector(const char *arg);
    // ExtensionAPI
    sptr_t Send(Pane p, unsigned int msg, uptr_t wParam = 0, sptr_t lParam = 0) override;
    std::string Range(Pane p, int start, int end) override;
    void Remove(Pane p, int start, int end) override;
    void Insert(Pane p, int pos, const char *s) override;
    void Trace(const char *s) override;
    std::string Property(const char *key) override;
    void SetProperty(const char *key, const char *val) override;
    void UnsetProperty(const char *key) override;
    uptr_t GetInstance() override;
    void ShutDown() override;
    void Perform(const char *actionList) override;
    void DoMenuCommand(int cmdID) override;

    // Valid CurrentWord characters
    bool iswordcharforsel(char ch);
    bool isfilenamecharforsel(char ch);
    bool islexerwordcharforsel(char ch);

    CurrentWordHighlight currentWordHighlight_;
    void HighlightCurrentWord(bool highlight);
    MatchMarker matchMarker_;
    MatchMarker findMarker_;
public:

    enum { kMaxParam = 4 };

    explicit CuteTextBase(Extension *ext = 0);
    // Deleted copy-constructor and assignment operator.
    CuteTextBase(const CuteTextBase &) = delete;
    CuteTextBase(CuteTextBase &&) = delete;
    void operator=(const CuteTextBase &) = delete;
    void operator=(CuteTextBase &&) = delete;
    ~CuteTextBase() override;

    void Finalise();

    GUI::WindowID GetID() const { return wCuteText_.GetID(); }

    virtual bool PerformOnNewThread(Worker *pWorker) = 0;
    // WorkerListener
    void PostOnMainThread(int cmd, Worker *pWorker) override = 0;
    virtual void WorkerCommand(int cmd, Worker *pWorker);
};

int ControlIDOfCommand(unsigned long);
long ColourOfProperty(const PropSetFile &props, const char *key, Colour colourDefault);
void WindowSetFocus(GUI::ScintillaWindow &w);

inline bool isspacechar(unsigned char ch) {
    return (ch == ' ') || ((ch >= 0x09) && (ch <= 0x0d));
}
