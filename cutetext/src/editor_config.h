// This file is part of CuteText project
// Copyright (C) 2018 by James Zeng <james.zeng@hotmail.com>
// The LICENSE file describes the conditions under which this software may be distributed.
/**
 * @file editor_config.h
 * @author James Zeng
 * @date 2018-08-12
 * @brief Read and interpret settings files in the EditorConfig format.
 *
 * @see https://github.com/cutetext/cutetext
 * @see http://editorconfig.org/
 */

class FilePath;

class IEditorConfig {
public:
	virtual ~IEditorConfig() = default;
	virtual void ReadFromDirectory(const FilePath &dirStart) = 0;
	virtual std::map<std::string, std::string> MapFromAbsolutePath(const FilePath &absolutePath) const = 0;
	virtual void Clear() = 0;
	static std::unique_ptr<IEditorConfig> Create();
};
