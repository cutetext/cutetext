// This file is part of CuteText project
// Copyright (C) 2018 by James Zeng <james.zeng@hotmail.com>
// The LICENSE file describes the conditions under which this software may be distributed.
/**
 * @file cute_frame_win.h
 * @author James Zeng
 * @date 2018-08-10
 * @brief Header of main frame for the Windows version of the editor.
 *
 * @see https://github.com/cutetext/cutetext
 */

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <string>
#include <vector>
#include <deque>
#include <set>
#include <map>
#include <algorithm>
#include <memory>
#include <sstream>
#include <iomanip>
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <richedit.h>
#include <shlwapi.h>
// need this header for SHBrowseForFolder
#include <shlobj.h>
#include <io.h>
#include <process.h>
#include <mmsystem.h>
#include <commctrl.h>

#include "Scintilla.h"
#include "ILoader.h"

#include "gui.h"

struct Band {
    bool visible_;
    int height_;
    bool expands_;
    GUI::Window win_;
    Band(bool visible, int height, bool expands, GUI::Window win) :
        visible_(visible),
        height_(height),
        expands_(expands),
        win_(win) {
    }
};

/**
 * @brief Class that contains the Windows platform specific stuff of the main frame.
 */
class CuteFrameWin : public CuteFrameBase {
    /*friend class ContentWin;
    friend class Strip;
    friend class SearchStrip;
    friend class FindStrip;
    friend class ReplaceStrip;
    friend class UserStrip;*/

protected:

    bool flatterUI_;
    int cmdShow_;
    static HINSTANCE hInstance_;
    static const TCHAR *className_;
    static const TCHAR *classNameInternal_;
    static CuteFrameWin *app_;
    WINDOWPLACEMENT winPlace_;
    RECT rcWorkArea_;
    GUI::GUIChar openWhat_[200];
    GUI::GUIChar tooltipText_[MAX_PATH * 2 + 1];
    bool tbLarge_;
    bool modalParameters_;
    int filterDefault_;
    bool staticBuild_;
    int menuSource_;
    std::deque<GUI::GUIString> dropFilesQueue_;

    // Fields also used in tool execution thread
    /*CommandWorker cmdWorker_;*/
    HANDLE hWriteSubProcess_;
    DWORD subProcessGroupId_;

    HACCEL hAccTable_;

    GUI::Rectangle pagesetupMargin_;
    HGLOBAL hDevMode_;
    HGLOBAL hDevNames_;

    /*UniqueInstance uniqueInstance_;*/

    /// HTMLHelp module
    HMODULE hHH_;
    /// Multimedia (sound) module
    HMODULE hMM_;

    // Tab Bar
    HFONT fontTabs_;

    /// Preserve focus during deactivation
    HWND wFocus_;

    GUI::Window wFindInFiles_;
    GUI::Window wFindReplace_;
    GUI::Window wParameters_;

    /*ContentWin contents_;
    BackgroundStrip backgroundStrip_;
    UserStrip userStrip_;
    SearchStrip searchStrip_;
    FindStrip findStrip_;
    ReplaceStrip replaceStrip_;*/

    enum { bandTool, bandTab, bandContents, bandUser, bandBackground, bandSearch, bandFind, bandReplace, bandStatus };
    std::vector<Band> bands;

    /*void ReadLocalization() override;
    void GetWindowPosition(int *left, int *top, int *width, int *height, int *maximize) override;
    void SetScaleFactor(int scale);

    void ReadEmbeddedProperties() override;
    void ReadPropertiesInitial() override;
    void ReadProperties() override;

    void TimerStart(int mask) override;
    void TimerEnd(int mask) override;

    void ShowOutputOnMainThread() override;
    void SizeContentWindows() override;
    void SizeSubWindows() override;

    void SetMenuItem(int menuNumber, int position, int itemID,
        const GUI::GUIChar *text, const GUI::GUIChar *mnemonic = 0) override;
    void RedrawMenu() override;
    void DestroyMenuItem(int menuNumber, int itemID) override;
    void CheckAMenuItem(int wIDCheckItem, bool val) override;
    void EnableAMenuItem(int wIDCheckItem, bool val) override;
    void CheckMenus() override;

    void LocaliseMenu(HMENU hmenu);
    void LocaliseMenus();
    void LocaliseControl(HWND w);
    void LocaliseDialog(HWND wDialog);

    int DoDialog(const TCHAR *resName, DLGPROC lpProc);
    HWND CreateParameterisedDialog(LPCWSTR lpTemplateName, DLGPROC lpProc);
    GUI::GUIString DialogFilterFromProperty(const GUI::GUIChar *filterProperty);
    void CheckCommonDialogError();
    bool OpenDialog(const FilePath &directory, const GUI::GUIChar *filesFilter) override;
    FilePath ChooseSaveName(const FilePath &directory, const char *title, const GUI::GUIChar *filesFilter = 0, const char *ext = 0);
    bool SaveAsDialog() override;
    void SaveACopy() override;
    void SaveAsHTML() override;
    void SaveAsRTF() override;
    void SaveAsPDF() override;
    void SaveAsTEX() override;
    void SaveAsXML() override;
    void LoadSessionDialog() override;
    void SaveSessionDialog() override;
    bool PreOpenCheck(const GUI::GUIChar *arg) override;
    bool IsStdinBlocked() override;

    /// Print the current buffer.
    void Print(bool showDialog) override;
    /// Handle default print setup values and ask the user its preferences.
    void PrintSetup() override;

    BOOL HandleReplaceCommand(int cmd, bool reverseDirection = false);

    MessageBoxChoice WindowMessageBox(GUI::Window &w, const GUI::GUIString &msg, MessageBoxStyle style = kMbsIconWarning) override;
    void FindMessageBox(const std::string &msg, const std::string *findItem = 0) override;
    void AboutDialog() override;
    void DropFiles(HDROP hdrop);
    void MinimizeToTray();
    void RestoreFromTray();
    static GUI::GUIString ProcessArgs(const GUI::GUIChar *cmdLine);
    void QuitProgram() override;

    FilePath GetDefaultDirectory() override;
    FilePath GetSciteDefaultHome() override;
    FilePath GetSciteUserHome() override;

    void SetFileProperties(PropSetFile &ps) override;
    void SetStatusBarText(const char *s) override;

    void TabInsert(int index, const GUI::GUIChar *title) override;
    void TabSelect(int index) override;
    void RemoveAllTabs() override;

    /// Warn the user, by means defined in its properties.
    void WarnUser(int warnID) override;

    void Notify(SCNotification *notification) override;
    void ShowToolBar() override;
    void ShowTabBar() override;
    void ShowStatusBar() override;
    void ActivateWindow(const char *timestamp) override;
    void ExecuteHelp(const char *cmd);
    void ExecuteOtherHelp(const char *cmd);
    void CopyAsRTF() override;
    void CopyPath() override;
    void FullScreenToggle();
    void Command(WPARAM wParam, LPARAM lParam);
    HWND MainHWND();

    void UserStripShow(const char *description) override;
    void UserStripSet(int control, const char *value) override;
    void UserStripSetList(int control, const char *value) override;
    std::string UserStripValue(int control) override;
    void UserStripClosed();
    void ShowBackgroundProgress(const GUI::GUIString &explanation, size_t size, size_t progress) override;
    BOOL FindMessage(HWND hDlg, UINT message, WPARAM wParam);
    static INT_PTR CALLBACK FindDlg(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
    BOOL ReplaceMessage(HWND hDlg, UINT message, WPARAM wParam);
    static INT_PTR CALLBACK ReplaceDlg(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
    void UIClosed() override;
    void PerformGrep();
    void FillCombos(Dialog &dlg);
    void FillCombosForGrep(Dialog &dlg);
    BOOL GrepMessage(HWND hDlg, UINT message, WPARAM wParam);
    static INT_PTR CALLBACK GrepDlg(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
    void FindIncrement() override;
    void Find() override;
    void FindInFiles() override;
    void Replace() override;
    void FindReplace(bool replace) override;
    void DestroyFindReplace() override;

    BOOL GoLineMessage(HWND hDlg, UINT message, WPARAM wParam);
    static INT_PTR CALLBACK GoLineDlg(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
    void GoLineDialog() override;

    BOOL AbbrevMessage(HWND hDlg, UINT message, WPARAM wParam);
    static INT_PTR CALLBACK AbbrevDlg(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
    bool AbbrevDialog() override;

    BOOL TabSizeMessage(HWND hDlg, UINT message, WPARAM wParam);
    static INT_PTR CALLBACK TabSizeDlg(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
    void TabSizeDialog() override;

    bool ParametersOpen() override;
    void ParamGrab() override;
    bool ParametersDialog(bool modal) override;
    BOOL ParametersMessage(HWND hDlg, UINT message, WPARAM wParam);
    static INT_PTR CALLBACK ParametersDlg(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);

    BOOL AboutMessage(HWND hDlg, UINT message, WPARAM wParam);
    static INT_PTR CALLBACK AboutDlg(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
    void AboutDialogWithBuild(int staticBuild_);

    void RestorePosition();*/

public:

    explicit CuteFrameWin();
    ~CuteFrameWin();

    static bool DialogHandled(GUI::WindowID id, MSG *pmsg);
    bool ModelessHandler(MSG *pmsg);

    void CreateUI();
    /// Management of the command line parameters.
    void Run(const GUI::GUIChar *cmdLine);
    uptr_t EventLoop();
    void OutputAppendEncodedStringSynchronised(const GUI::GUIString &s, int codePageDocument);
    void ResetExecution();
    void ExecuteNext();
    DWORD ExecuteOne(const Job &jobToRun);
    void ProcessExecute();
    void ShellExec(const std::string &cmd, const char *dir);
    void Execute() override;
    void StopExecute() override;
    void AddCommand(const std::string &cmd, const std::string &dir, JobSubsystem jobType, const std::string &input = "", int flags = 0) override;

    bool PerformOnNewThread(Worker *pWorker) override;
    void PostOnMainThread(int cmd, Worker *pWorker) override;
    void WorkerCommand(int cmd, Worker *pWorker) override;

    void Creation();
    LRESULT KeyDown(WPARAM wParam);
    LRESULT KeyUp(WPARAM wParam);
    void AddToPopUp(const char *label, int cmd = 0, bool enabled = true) override;
    LRESULT ContextMenuMessage(UINT iMessage, WPARAM wParam, LPARAM lParam);
    void CheckForScintillaFailure(int statusFailure);
    LRESULT WndProc(UINT iMessage, WPARAM wParam, LPARAM lParam);

    std::string EncodeString(const std::string &s) override;
    std::string GetRangeInUIEncoding(GUI::ScintillaWindow &win, int selStart, int selEnd) override;

    HACCEL GetAcceleratorTable() {
        return hAccTable;
    }

    uptr_t GetInstance() override;
    static void Register(HINSTANCE hInstance_);
    static LRESULT PASCAL TWndProc(
        HWND hWnd, UINT iMessage, WPARAM wParam, LPARAM lParam);

    friend class UniqueInstance;
};
