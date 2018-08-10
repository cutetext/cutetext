// This file is part of CuteText project
// Copyright (C) 2018 by James Zeng <james.zeng@hotmail.com>
// The LICENSE file describes the conditions under which this software may be distributed.
/**
 * @file cute_frame.h
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
