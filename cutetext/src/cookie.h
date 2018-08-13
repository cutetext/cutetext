// This file is part of CuteText project
// Copyright (C) 2018 by James Zeng <james.zeng@hotmail.com>
// The LICENSE file describes the conditions under which this software may be distributed.
/**
 * @file cookie.h
 * @author James Zeng
 * @date 2018-08-12
 * @brief Examine start of files for coding cookies and type information.
 *
 * @see https://github.com/cutetext/cutetext
 */

// Related to Utf8_16::encodingType but with additional values at end
enum UniMode {
    kUni8Bit = 0, kUni16BE = 1, kUni16LE = 2, kUniUTF8 = 3,
    kUniCookie = 4
};

std::string ExtractLine(const char *buf, size_t length);
UniMode CodingCookieValue(const char *buf, size_t length);
