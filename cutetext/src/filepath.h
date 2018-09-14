// This file is part of CuteText project
// Copyright (C) 2018 by James Zeng <james.zeng@hotmail.com>
// The LICENSE file describes the conditions under which this software may be distributed.
/**
 * @file filepath.h
 * @author James Zeng
 * @date 2018-08-12
 * @brief Encapsulate a file path.
 *
 * @see https://github.com/cutetext/cutetext
 */

extern const GUI::GUIChar pathSepString[];
extern const GUI::GUIChar pathSepChar;
extern const GUI::GUIChar listSepString[];
extern const GUI::GUIChar configFileVisibilityString[];
extern const GUI::GUIChar fileRead[];
extern const GUI::GUIChar fileWrite[];

#if defined(__unix__)
#include <climits>
#ifdef PATH_MAX
#define MAX_PATH PATH_MAX
#else
#define MAX_PATH 260
#endif
#endif

class FilePath;

typedef std::vector<FilePath> FilePathSet;

class FilePath {
	GUI::GUIString fileName_;
public:
	FilePath(const GUI::GUIChar *fileName = GUI_TEXT(""));
	FilePath(const GUI::GUIString &fileName);
	FilePath(FilePath const &directory, FilePath const &name);
	FilePath(FilePath const &) = default;
	FilePath(FilePath &&) = default;
	FilePath &operator=(FilePath const &) = default;
	FilePath &operator=(FilePath &&) = default;
	virtual ~FilePath() = default;
	void Set(const GUI::GUIChar *fileName);
	void Set(FilePath const &other);
	void Set(FilePath const &directory, FilePath const &name);
	void SetDirectory(FilePath const &directory);
	virtual void Init();
	bool SameNameAs(const GUI::GUIChar *other) const;
	bool SameNameAs(const FilePath &other) const;
	bool operator==(const FilePath &other) const;
	bool operator<(const FilePath &other) const;
	bool IsSet() const;
	bool IsUntitled() const;
	bool IsAbsolute() const;
	bool IsRoot() const;
	static int RootLength();
	const GUI::GUIChar *AsInternal() const;
	std::string AsUTF8() const;
	FilePath Name() const;
	FilePath BaseName() const;
	FilePath Extension() const;
	FilePath Directory() const;
	void FixName();
	FilePath AbsolutePath() const;
	FilePath NormalizePath() const;
	static FilePath GetWorkingDirectory();
	bool SetWorkingDirectory() const;
	void List(FilePathSet &directories, FilePathSet &files) const;
	FILE *Open(const GUI::GUIChar *mode) const;
	std::string Read() const;
	void Remove() const;
	time_t ModifiedTime() const;
	long long GetFileLength() const;
	bool Exists() const;
	bool IsDirectory() const;
	bool Matches(const GUI::GUIChar *pattern) const;
	static bool CaseSensitive();
};

std::string CommandExecute(const GUI::GUIChar *command, const GUI::GUIChar *directoryForRun);
