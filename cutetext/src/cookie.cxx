// This file is part of CuteText project
// Copyright (C) 2018 by James Zeng <james.zeng@hotmail.com>
// The LICENSE file describes the conditions under which this software may be distributed.
/**
 * @file cookie.cxx
 * @author James Zeng
 * @date 2018-08-12
 * @brief Examine start of files for coding cookies and type information.
 *
 * @see https://github.com/cutetext/cutetext
 */

#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "scintilla.h"
#include "gui.h"
#include "string_helpers.h"
#include "cookie.h"

static const char kCodingCookie[] = "coding";

static bool IsEncodingChar(char ch) {
    return (ch == '_') || (ch == '-') || (ch == '.') ||
           (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
           (ch >= '0' && ch <= '9');
}

static bool IsSpaceChar(char ch) {
    return (ch == ' ') || (ch == '\t');
}

static UniMode CookieValue(const std::string &s) {
    size_t posCoding = s.find(kCodingCookie);
    if (posCoding != std::string::npos) {
        posCoding += strlen(kCodingCookie);
        if ((s[posCoding] == ':') || (s[posCoding] == '=')) {
            posCoding++;
            if ((s[posCoding] == '\"') || (s[posCoding] == '\'')) {
                posCoding++;
            }
            while ((posCoding < s.length()) &&
                    (IsSpaceChar(s[posCoding]))) {
                posCoding++;
            }
            size_t endCoding = posCoding;
            while ((endCoding < s.length()) &&
                    (IsEncodingChar(s[endCoding]))) {
                endCoding++;
            }
            std::string code(s, posCoding, endCoding-posCoding);
            LowerCaseAZ(code);
            if (code == "utf-8") {
                return kUniCookie;
            }
        }
    }
    return kUni8Bit;
}

/**
 * @brief  Extract a line from a C-string buffer
 *
 * @param  buf is a char array containing a proper C-string
 * @param  length is the length of the above buf
 * @return a line string
 */
std::string ExtractLine(const char *buf, size_t length) {
    unsigned int endl = 0;
    if (length > 0) {
        while ((endl < length) && (buf[endl] != '\r') && (buf[endl] != '\n')) {
            endl++;
        }
        if (((endl + 1) < length) && (buf[endl] == '\r') && (buf[endl+1] == '\n')) {
            endl++;
        }
        if (endl < length) {
            endl++;
        }
    }
    return std::string(buf, endl);
}

UniMode CodingCookieValue(const char *buf, size_t length) {
    std::string l1 = ExtractLine(buf, length);
    UniMode unicodeMode = CookieValue(l1);
    if (unicodeMode == kUni8Bit) {
        std::string l2 = ExtractLine(buf + l1.length(), length - l1.length());
        unicodeMode = CookieValue(l2);
    }
    return unicodeMode;
}

