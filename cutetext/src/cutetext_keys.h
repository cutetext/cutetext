// This file is part of CuteText project
// Copyright (C) 2018 by James Zeng <james.zeng@hotmail.com>
// The LICENSE file describes the conditions under which this software may be distributed.
/**
 * @file cutetext_keys.h
 * @author James Zeng
 * @date 2018-08-12
 * @brief CuteText keyboard shortcut facilities.
 *
 * @see https://github.com/cutetext/cutetext
 */

#ifndef SCITEKEYS_H
#define SCITEKEYS_H

class SciTEKeys {
public:
	static long ParseKeyCode(const char *mnemonic);
	static bool MatchKeyCode(long parsedKeyCode, int keyval, int modifiers);
};

#endif
