// This file is part of CuteText project
// Copyright (C) 2018 by James Zeng <james.zeng@hotmail.com>
// The LICENSE file describes the conditions under which this software may be distributed.
/**
 * @file string_list.h
 * @author James Zeng
 * @date 2018-08-12
 * @brief Definition of class holding a list of strings.
 *
 * @see https://github.com/cutetext/cutetext
 */

class StringList {
	// Text pointed into by words and wordsNoCase
	std::string listText;
	// Each word contains at least one character.
	std::vector<char *> words;
	std::vector<char *> wordsNoCase;
	bool onlyLineEnds;	///< Delimited by any white space or only line ends
	bool sorted;
	bool sortedNoCase;
	void SetFromListText();
	void SortIfNeeded(bool ignoreCase);
public:
	explicit StringList(bool onlyLineEnds_ = false) :
		words(0), wordsNoCase(0), onlyLineEnds(onlyLineEnds_),
		sorted(false), sortedNoCase(false) {}
	~StringList() { Clear(); }
	size_t Length() const { return words.size(); }
	operator bool() const { return !words.empty(); }
	char *operator[](size_t ind) { return words[ind]; }
	void Clear();
	void Set(const char *s);
	void Set(const std::vector<char> &data);
	std::string GetNearestWord(const char *wordStart, size_t searchLen,
		bool ignoreCase, const std::string &wordCharacters, int wordIndex);
	std::string GetNearestWords(const char *wordStart, size_t searchLen,
		bool ignoreCase, char otherSeparator='\0', bool exactLen=false);
};
