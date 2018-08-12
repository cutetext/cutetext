// This file is part of CuteText project
// Copyright (C) 2018 by James Zeng <james.zeng@hotmail.com>
// The LICENSE file describes the conditions under which this software may be distributed.
/**
 * @file mutex.h
 * @author James Zeng
 * @date 2018-08-12
 * @brief Define mutex
 *
 * @see https://github.com/cutetext/cutetext
 */

// TODO: see http://www.codeproject.com/threads/cppsyncstm.asp

#ifndef MUTEX_H
#define MUTEX_H

class Mutex {
public:
	virtual void Lock() = 0;
	virtual void Unlock() = 0;
	virtual ~Mutex() {}
	static Mutex *Create();
};

class Lock {
	Mutex *mute;
public:
	explicit Lock(Mutex *mute_) : mute(mute_) {
		mute->Lock();
	}
	// Deleted so Lock objects can not be copied.
	Lock(const Lock &) = delete;
	void operator=(const Lock &) = delete;
	~Lock() {
		mute->Unlock();
	}
};

#endif
