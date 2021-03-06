// This file is part of CuteText project
// Copyright (C) 2018 by James Zeng <james.zeng@hotmail.com>
// The LICENSE file describes the conditions under which this software may be distributed.
/**
 * @file style_writer.h
 * @author James Zeng
 * @date 2018-08-12
 * @brief Simple buffered interface to the text and styles of a document held by Scintilla.
 *
 * @see https://github.com/cutetext/cutetext
 */

#ifndef STYLEWRITER_H
#define STYLEWRITER_H

// Read only access to a document, its styles and other data
class TextReader {
protected:
	enum {extremePosition=0x7FFFFFFF};
	/** @a bufferSize is a trade off between time taken to copy the characters
	 * and retrieval overhead.
	 * @a slopSize positions the buffer before the desired position
	 * in case there is some backtracking. */
	enum {bufferSize=4000, slopSize=bufferSize/8};
	char buf[bufferSize+1];
	int startPos;
	int endPos;
	int codePage_;

	GUI::ScintillaWindow &sw;
	int lenDoc;

	bool InternalIsLeadByte(char ch) const;
	void Fill(int position);
public:
	explicit TextReader(GUI::ScintillaWindow &sw_);
	// Deleted so TextReader objects can not be copied.
	TextReader(const TextReader &source) = delete;
	TextReader &operator=(const TextReader &) = delete;
	char operator[](int position) {
		if (position < startPos || position >= endPos) {
			Fill(position);
		}
		return buf[position - startPos];
	}
	/** Safe version of operator[], returning a defined value for invalid position. */
	char SafeGetCharAt(int position, char chDefault=' ') {
		if (position < startPos || position >= endPos) {
			Fill(position);
			if (position < startPos || position >= endPos) {
				// Position is outside range of document
				return chDefault;
			}
		}
		return buf[position - startPos];
	}
	bool IsLeadByte(char ch) const {
		return codePage_ && InternalIsLeadByte(ch);
	}
	void SetCodePage(int codePage) {
		codePage_ = codePage;
	}
	bool Match(int pos, const char *s);
	int StyleAt(int position);
	int GetLine(int position);
	int LineStart(int line);
	int LevelAt(int line);
	int Length();
	int GetLineState(int line);
};

// Adds methods needed to write styles and folding
class StyleWriter : public TextReader {
protected:
	char styleBuf[bufferSize];
	int validLen;
	unsigned int startSeg;
public:
	explicit StyleWriter(GUI::ScintillaWindow &sw_);
	// Deleted so StyleWriter objects can not be copied.
	StyleWriter(const StyleWriter &source) = delete;
	StyleWriter &operator=(const StyleWriter &) = delete;
	void Flush();
	int SetLineState(int line, int state);

	void StartAt(unsigned int start, char chMask=31);
	unsigned int GetStartSegment() const { return startSeg; }
	void StartSegment(unsigned int pos);
	void ColourTo(unsigned int pos, int chAttr);
	void SetLevel(int line, int level);
};

#endif
