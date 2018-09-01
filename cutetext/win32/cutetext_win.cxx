// This file is part of CuteText project
// Copyright (C) 2018 by James Zeng <james.zeng@hotmail.com>
// The LICENSE file describes the conditions under which this software may be distributed.
/**
 * @file cute_frame_win.cxx
 * @author James Zeng
 * @date 2018-08-11
 * @brief Code of main frame for the Windows version of the editor.
 *
 * @see https://github.com/cutetext/cutetext
 */

#include "cute_frame_win.h"

CuteFrameWin::CuteFrameWin() : CuteFrameBase() {
    app_ = this;

    /*contents.SetSciTE(this);
    contents.SetLocalizer(&localiser_);
    backgroundStrip.SetLocalizer(&localiser_);
    searchStrip.SetLocalizer(&localiser_);
    searchStrip.SetSearcher(this);
    findStrip.SetLocalizer(&localiser_);
    findStrip.SetSearcher(this);
    replaceStrip.SetLocalizer(&localiser_);
    replaceStrip.SetSearcher(this);*/

    flatterUI_ = UIShouldBeFlat();

    cmdShow_ = 0;
    heightBar_ = 7;
    fontTabs_ = 0;
    wFocus_ = 0;

    winPlace = WINDOWPLACEMENT();
    winPlace.length = 0;
    rcWorkArea = RECT();

    openWhat[0] = '\0';
    tooltipText[0] = '\0';
    tbLarge = false;
    modalParameters = false;
    filterDefault = 1;
    staticBuild = false;
    menuSource = 0;

    hWriteSubProcess = NULL;
    subProcessGroupId = 0;

    pathAbbreviations_ = GetAbbrevPropertiesFileName();

    // System type properties are stored in the platform properties.
    propsPlatform_.Set("PLAT_WIN", "1");
    propsPlatform_.Set("PLAT_WINNT", "1");

    ReadEnvironment();

    ReadGlobalPropFile();

    SetScaleFactor(0);

    tbLarge = props_.GetInt("toolbar.large");
    /// Need to copy properties to variables before setting up window
    SetPropertiesInitial();
    ReadAbbrevPropFile();

    hDevMode = 0;
    hDevNames = 0;
    ::ZeroMemory(&pagesetupMargin, sizeof(pagesetupMargin));

    hHH = 0;
    hMM = 0;
    uniqueInstance.Init(this);

    hAccTable = ::LoadAccelerators(hInstance, TEXT("ACCELS")); // md

    cmdWorker.pSciTE = this;
}

CuteFrameWin::~CuteFrameWin() {
    if (hDevMode)
        ::GlobalFree(hDevMode);
    if (hDevNames)
        ::GlobalFree(hDevNames);
    if (hHH)
        ::FreeLibrary(hHH);
    if (hMM)
        ::FreeLibrary(hMM);
    if (fontTabs)
        ::DeleteObject(fontTabs);
    if (hAccTable)
        ::DestroyAcceleratorTable(hAccTable);
}

uptr_t CuteFrameWin::GetInstance() {
    return reinterpret_cast<uptr_t>(hInstance);
}

void CuteFrameWin::Register(HINSTANCE hInstance_) {
    const TCHAR resourceName[] = TEXT("SciTE");

    hInstance = hInstance_;

    WNDCLASS wndclass;

    // Register the frame window
    className = TEXT("SciTEWindow");
    wndclass.style = 0;
    wndclass.lpfnWndProc = SciTEWin::TWndProc;
    wndclass.cbClsExtra = 0;
    wndclass.cbWndExtra = sizeof(SciTEWin*);
    wndclass.hInstance = hInstance;
    wndclass.hIcon = ::LoadIcon(hInstance, resourceName);
    wndclass.hCursor = NULL;
    wndclass.hbrBackground = NULL;
    wndclass.lpszMenuName = resourceName;
    wndclass.lpszClassName = className;
    if (!::RegisterClass(&wndclass))
        exit(FALSE);

    // Register the window that holds the two Scintilla edit windows and the separator
    classNameInternal = TEXT("SciTEWindowContent");
    wndclass.lpfnWndProc = BaseWin::StWndProc;
    wndclass.lpszMenuName = 0;
    wndclass.lpszClassName = classNameInternal;
    if (!::RegisterClass(&wndclass))
        exit(FALSE);
}