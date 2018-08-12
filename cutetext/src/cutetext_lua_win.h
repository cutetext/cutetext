// This file is part of CuteText project
// Copyright (C) 2018 by James Zeng <james.zeng@hotmail.com>
// The LICENSE file describes the conditions under which this software may be distributed.
/**
 * @file cutetext_lua_win.h
 * @author James Zeng
 * @date 2018-08-12
 * @brief CuteText Lua scripting interface.
 *
 * @see https://github.com/cutetext/cutetext
 */

/* Modifications for Windows to allow UTF-8 file names and command lines */
/*
Imported into Lua build with -DLUA_USER_H=\"scite_lua_win.h\"
Redirect fopen and _popen to functions that treat their arguments as UTF-8.
If UTF-8 does not work then retry with the original strings as may be in locale characters.
*/
#if defined(LUA_USE_WINDOWS)
#include <stdio.h>
FILE *scite_lua_fopen(const char *filename, const char *mode);
#define fopen scite_lua_fopen
FILE *scite_lua_popen(const char *filename, const char *mode);
#define _popen scite_lua_popen
#endif
