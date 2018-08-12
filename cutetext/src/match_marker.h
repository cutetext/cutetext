// This file is part of CuteText project
// Copyright (C) 2018 by James Zeng <james.zeng@hotmail.com>
// The LICENSE file describes the conditions under which this software may be distributed.
/**
 * @file match_marker.h
 * @author James Zeng
 * @date 2018-08-12
 * @brief Mark all the matches of a string.
 *
 * @see https://github.com/cutetext/cutetext
 */

struct LineRange {
	int lineStart;
	int lineEnd;
	LineRange(int lineStart_, int lineEnd_) : lineStart(lineStart_), lineEnd(lineEnd_) {}
};

std::vector<LineRange> LinesBreak(GUI::ScintillaWindow *pSci);

class MatchMarker {
	GUI::ScintillaWindow *pSci;
	std::string textMatch;
	int styleMatch;
	int flagsMatch;
	int indicator;
	int bookMark;
	std::vector<LineRange> lineRanges;
public:
	MatchMarker();
	~MatchMarker();
	void StartMatch(GUI::ScintillaWindow *pSci_,
		const std::string &textMatch_, int flagsMatch_, int styleMatch_,
		int indicator_, int bookMark_);
	bool Complete() const;
	void Continue();
	void Stop();
};
