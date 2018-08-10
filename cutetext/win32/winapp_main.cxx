// This file is part of CuteText project
// Copyright (C) 2018 by James Zeng <james.zeng@hotmail.com>
// The LICENSE file describes the conditions under which this software may be distributed.
/**
 * @file winapp_main.cxx
 * @author James Zeng
 * @date 2018-08-09
 * @brief Main code for the Windows version of the editor.
 *
 * @see https://github.com/cutetext/cutetext
 */

#include <time.h>

#include "cute_frame.h"


#ifdef STATIC_BUILD
const GUI::GUIChar appName[] = GUI_TEXT("CuteTextOne");
#else
const GUI::GUIChar appName[] = GUI_TEXT("CuteText");
#endif

#ifndef LOAD_LIBRARY_SEARCH_APPLICATION_DIR
#define LOAD_LIBRARY_SEARCH_APPLICATION_DIR 0x200
#endif
#ifndef LOAD_LIBRARY_SEARCH_SYSTEM32
#define LOAD_LIBRARY_SEARCH_SYSTEM32 0x800
#endif

static void RestrictDLLPath() {
	// Try to limit the locations where DLLs will be loaded from to prevent binary planting.
	// That is where a bad DLL is placed in the current directory or in the PATH.
	typedef BOOL(WINAPI *SetDefaultDllDirectoriesSig)(DWORD DirectoryFlags);
	typedef BOOL(WINAPI *SetDllDirectorySig)(LPCTSTR lpPathName);
	HMODULE kernel32 = ::GetModuleHandle(TEXT("kernel32.dll"));
	if (kernel32) {
		// SetDefaultDllDirectories is stronger, limiting search path to just the application and
		// system directories but is only available on Windows 8+
		SetDefaultDllDirectoriesSig SetDefaultDllDirectoriesFn =
			reinterpret_cast<SetDefaultDllDirectoriesSig>(::GetProcAddress(
				kernel32, "SetDefaultDllDirectories"));
		if (SetDefaultDllDirectoriesFn) {
			SetDefaultDllDirectoriesFn(LOAD_LIBRARY_SEARCH_APPLICATION_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
		}
		else {
			SetDllDirectorySig SetDllDirectoryFn =
				reinterpret_cast<SetDllDirectorySig>(::GetProcAddress(
					kernel32, "SetDllDirectoryW"));
			if (SetDllDirectoryFn) {
				// For security, remove current directory from the DLL search path
				SetDllDirectoryFn(TEXT(""));
			}
		}
	}
}

int PASCAL WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {

	RestrictDLLPath();

#ifndef NO_EXTENSIONS
	MultiplexExtension multiExtender;

#ifndef NO_LUA
	multiExtender.RegisterExtension(LuaExtension::Instance());
#endif

#ifndef NO_FILER
	multiExtender.RegisterExtension(DirectorExtension::Instance());
#endif
#endif

	SciTEWin::Register(hInstance);
#ifdef STATIC_BUILD

	Scintilla_LinkLexers();
	Scintilla_RegisterClasses(hInstance);
#else

	HMODULE hmod = ::LoadLibrary(TEXT("SciLexer.DLL"));
	if (hmod == NULL)
		::MessageBox(NULL, TEXT("The Scintilla DLL could not be loaded.  SciTE will now close"),
			TEXT("Error loading Scintilla"), MB_OK | MB_ICONERROR);
#endif

	uptr_t result = 0;
	{
#ifdef NO_EXTENSIONS
		Extension *extender = 0;
#else
		Extension *extender = &multiExtender;
#endif
		SciTEWin MainWind(extender);
		LPTSTR lptszCmdLine = GetCommandLine();
		if (*lptszCmdLine == '\"') {
			lptszCmdLine++;
			while (*lptszCmdLine && (*lptszCmdLine != '\"'))
				lptszCmdLine++;
			if (*lptszCmdLine == '\"')
				lptszCmdLine++;
		}
		else {
			while (*lptszCmdLine && (*lptszCmdLine != ' '))
				lptszCmdLine++;
		}
		while (*lptszCmdLine == ' ')
			lptszCmdLine++;
		try {
			MainWind.Run(lptszCmdLine);
			result = MainWind.EventLoop();
		}
		catch (GUI::ScintillaFailure &sf) {
			MainWind.CheckForScintillaFailure(static_cast<int>(sf.status));
		}
		MainWind.Finalise();
	}

#ifdef STATIC_BUILD
	Scintilla_ReleaseResources();
#else

	::FreeLibrary(hmod);
#endif

	return static_cast<int>(result);
}