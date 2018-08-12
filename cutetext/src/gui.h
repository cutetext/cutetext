// This file is part of CuteText project
// Copyright (C) 2018 by James Zeng <james.zeng@hotmail.com>
// The LICENSE file describes the conditions under which this software may be distributed.
/**
 * @file gui.h
 * @author James Zeng
 * @date 2018-08-09
 * @brief Interface to platform GUI facilities.
 *
 * Split off from Scintilla's Platform.h to avoid CuteText depending on implementation of Scintilla.
 * Implementation in win32/gui_win.cxx for Windows and gtk/gui_gtk.cxx for GTK+.
 * @see https://github.com/cutetext/cutetext
 */


#ifndef GUI_H
#define GUI_H

namespace GUI {

class Point {
public:
    int x_;
    int y_;

    explicit Point(int x=0, int y=0) noexcept : x_(x), y_(y) {
    }
};

class Rectangle {
public:
    int left_;
    int top_;
    int right_;
    int bottom_;

    Rectangle(int left=0, int top=0, int right=0, int bottom = 0) noexcept :
        left_(left), top_(top), right_(right), bottom_(bottom) {
    }
    bool Contains(Point pt) const noexcept {
        return (pt.x_ >= left_) && (pt.x_ <= right_) &&
            (pt.y_ >= top_) && (pt.y_ <= bottom_);
    }
    int Width() const noexcept { return right_ - left_; }
    int Height() const noexcept { return bottom_ - top_; }
    bool operator==(const Rectangle &other) const noexcept {
        return (left_ == other.left_) &&
            (top_ == other.top_) &&
            (right_ == other.right_) &&
            (bottom_ == other.bottom_);
    }
};

#if defined(GTK) || defined(__APPLE__)

// On GTK+ and OS X use UTF-8 char strings

typedef char GUIChar;
typedef std::string GUIString;
typedef std::string_view GUIStringView;

#define GUI_TEXT(q) q

#else

// On Win32 use UTF-16 wide char strings

typedef wchar_t GUIChar;
typedef std::wstring GUIString;
typedef std::wstring_view GUIStringView;

#define GUI_TEXT(q) L##q

#endif

GUIString StringFromUTF8(const char *s);
GUIString StringFromUTF8(const std::string &s);
std::string UTF8FromString(const GUIString &s);
GUIString StringFromInteger(long i);
GUIString StringFromLongLong(long long i);
GUIString HexStringFromInteger(long i);

std::string LowerCaseUTF8(std::string_view sv);

typedef void *WindowID;
class Window {
protected:
    WindowID wid_;
public:
    Window() noexcept : wid_(0) {
    }
    Window(Window const &) = default;
    Window(Window &&) = default;
    Window &operator=(Window const &) = default;
    Window &operator=(Window &&) = default;
    Window &operator=(WindowID wid) noexcept {
        wid_ = wid;
        return *this;
    }
    virtual ~Window() = default;

    WindowID GetID() const noexcept {
        return wid_;
    }
    void SetID(WindowID wid) noexcept {
        wid_ = wid;
    }
    bool Created() const noexcept {
        return wid_ != 0;
    }
    void Destroy();
    bool HasFocus();
    Rectangle GetPosition();
    void SetPosition(Rectangle rc);
    Rectangle GetClientPosition();
    void Show(bool show=true);
    void InvalidateAll();
    void SetTitle(const GUIChar *s);
};

typedef void *MenuID;
class Menu {
    MenuID mid_;
public:
    Menu() noexcept : mid_(0) {
    }
    MenuID GetID() const noexcept {
        return mid_;
    }
    void CreatePopUp();
    void Destroy();
    void Show(Point pt, Window &w);
};

class ElapsedTime {
    long bigBit_;
    long littleBit_;
public:
    ElapsedTime();
    double Duration(bool reset=false);
};

class ScintillaPrimitive : public Window {
public:
    // Send is the basic method and can be used between threads on Win32
    sptr_t Send(unsigned int msg, uptr_t wParam=0, sptr_t lParam=0);
};

bool IsDBCSLeadByte(int codePage, char ch);

}

#endif
