// This file is part of CuteText project
// Copyright (C) 2018 by James Zeng <james.zeng@hotmail.com>
// The LICENSE file describes the conditions under which this software may be distributed.
/**
 * @file fileworker.h
 * @author James Zeng
 * @date 2018-08-12
 * @brief Definition of classes to perform background file tasks as threads.
 *
 * @see https://github.com/cutetext/cutetext
 */

/// Base size of file I/O operations.
const size_t blockSize = 131072;

struct FileWorker : public Worker {
	WorkerListener *pListener;
	FilePath path;
	size_t size;
	int err;
	FILE *fp;
	GUI::ElapsedTime et;
	int sleepTime;
	double nextProgress;

	FileWorker(WorkerListener *pListener_, const FilePath &path_, size_t size_, FILE *fp_);
	~FileWorker() override;
	virtual double Duration();
	void Cancel() override {
		Worker::Cancel();
	}
	virtual bool IsLoading() const = 0;
};

class FileLoader : public FileWorker {
public:
	ILoader *pLoader;
	size_t readSoFar;
	UniMode unicodeMode;

	FileLoader(WorkerListener *pListener_, ILoader *pLoader_, const FilePath &path_, size_t size_, FILE *fp_);
	~FileLoader() override;
	void Execute() override;
	void Cancel() override;
	bool IsLoading() const override {
		return true;
	}
};

class FileStorer : public FileWorker {
public:
	const char *documentBytes;
	size_t writtenSoFar;
	UniMode unicodeMode;
	bool visibleProgress;

	FileStorer(WorkerListener *pListener_, const char *documentBytes_, const FilePath &path_,
		size_t size_, FILE *fp_, UniMode unicodeMode_, bool visibleProgress_);
	~FileStorer() override;
	void Execute() override;
	void Cancel() override;
	bool IsLoading() const override {
		return false;
	}
};

enum {
	kWorkFileRead = 1,
	kWorkFileWritten = 2,
	kWorkFileProgress = 3,
	kWorkPlatform = 100
};
