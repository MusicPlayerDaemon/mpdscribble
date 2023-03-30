// SPDX-License-Identifier: BSD-2-Clause
// author: Max Kellermann <max.kellermann@gmail.com>

#pragma once

#include <cstddef>
#include <utility>

#ifndef _WIN32
#include <unistd.h>
#endif
#include <sys/types.h>

#ifdef _WIN32
#include <io.h>
#include <wchar.h>
typedef int mode_t;
#if defined(_WIN64)
 typedef __int64 ssize_t; 
#else
 typedef long ssize_t;
#endif
#endif

class UniqueFileDescriptor;

/**
 * An OO wrapper for a UNIX file descriptor.
 *
 * This class is unmanaged and trivial; for a managed version, see
 * #UniqueFileDescriptor.
 */
class FileDescriptor {
protected:
	int fd;

public:
	FileDescriptor() = default;
	explicit constexpr FileDescriptor(int _fd) noexcept:fd(_fd) {}

	constexpr bool operator==(FileDescriptor other) const noexcept {
		return fd == other.fd;
	}

	constexpr bool operator!=(FileDescriptor other) const noexcept {
		return !(*this == other);
	}

	constexpr bool IsDefined() const noexcept {
		return fd >= 0;
	}

#ifndef _WIN32
	/**
	 * Ask the kernel whether this is a valid file descriptor.
	 */
	[[gnu::pure]]
	bool IsValid() const noexcept;

	/**
	 * Ask the kernel whether this is a regular file.
	 */
	[[gnu::pure]]
	bool IsRegularFile() const noexcept;

	/**
	 * Ask the kernel whether this is a pipe.
	 */
	[[gnu::pure]]
	bool IsPipe() const noexcept;

	/**
	 * Ask the kernel whether this is a socket descriptor.
	 */
	[[gnu::pure]]
	bool IsSocket() const noexcept;
#endif

	/**
	 * Returns the file descriptor.  This may only be called if
	 * IsDefined() returns true.
	 */
	constexpr int Get() const noexcept {
		return fd;
	}

	void Set(int _fd) noexcept {
		fd = _fd;
	}

	int Steal() noexcept {
		return std::exchange(fd, -1);
	}

	void SetUndefined() noexcept {
		fd = -1;
	}

	static constexpr FileDescriptor Undefined() noexcept {
		return FileDescriptor(-1);
	}

#ifdef __linux__
	bool Open(FileDescriptor dir, const char *pathname,
		  int flags, mode_t mode=0666) noexcept;
#endif

	bool Open(const char *pathname, int flags, mode_t mode=0666) noexcept;

#ifdef _WIN32
	bool Open(const wchar_t *pathname, int flags, mode_t mode=0666) noexcept;
#endif

	bool OpenReadOnly(const char *pathname) noexcept;

#ifdef __linux__
	bool OpenReadOnly(FileDescriptor dir,
			  const char *pathname) noexcept;
#endif

#ifndef _WIN32
	bool OpenNonBlocking(const char *pathname) noexcept;
#endif

#ifdef __linux__
	static bool CreatePipe(FileDescriptor &r, FileDescriptor &w,
			       int flags) noexcept;
#endif

	static bool CreatePipe(FileDescriptor &r, FileDescriptor &w) noexcept;

#ifdef _WIN32
	void EnableCloseOnExec() noexcept {}
	void DisableCloseOnExec() noexcept {}
#else
	static bool CreatePipeNonBlock(FileDescriptor &r,
				       FileDescriptor &w) noexcept;

	/**
	 * Enable non-blocking mode on this file descriptor.
	 */
	void SetNonBlocking() noexcept;

	/**
	 * Enable blocking mode on this file descriptor.
	 */
	void SetBlocking() noexcept;

	/**
	 * Auto-close this file descriptor when a new program is
	 * executed.
	 */
	void EnableCloseOnExec() noexcept;

	/**
	 * Do not auto-close this file descriptor when a new program
	 * is executed.
	 */
	void DisableCloseOnExec() noexcept;

	/**
	 * Duplicate this file descriptor.
	 *
	 * @return the new file descriptor or UniqueFileDescriptor{}
	 * on error
	 */
	UniqueFileDescriptor Duplicate() const noexcept;

	/**
	 * Duplicate the file descriptor onto the given file descriptor.
	 */
	bool Duplicate(FileDescriptor new_fd) const noexcept {
		return ::dup2(Get(), new_fd.Get()) != -1;
	}

	/**
	 * Similar to Duplicate(), but if destination and source file
	 * descriptor are equal, clear the close-on-exec flag.  Use
	 * this method to inject file descriptors into a new child
	 * process, to be used by a newly executed program.
	 */
	bool CheckDuplicate(FileDescriptor new_fd) noexcept;
#endif

	/**
	 * Close the file descriptor.  It should not be called on an
	 * "undefined" object.  After this call, IsDefined() is guaranteed
	 * to return false, and this object may be reused.
	 */
	bool Close() noexcept {
		return ::close(Steal()) == 0;
	}

	/**
	 * Rewind the pointer to the beginning of the file.
	 */
	bool Rewind() noexcept;

	off_t Seek(off_t offset) noexcept {
		#ifdef _WIN32
		return _lseek(Get(), offset, SEEK_SET);
		#else
		return lseek(Get(), offset, SEEK_SET);
		#endif
	}

	off_t Skip(off_t offset) noexcept {
		#ifdef _WIN32
		return _lseek(Get(), offset, SEEK_CUR);
		#else
		return lseek(Get(), offset, SEEK_CUR);
		#endif
	}

	[[gnu::pure]]
	off_t Tell() const noexcept {
		#ifdef _WIN32
		return _lseek(Get(), 0, SEEK_CUR);
		#else
		return lseek(Get(), 0, SEEK_CUR);
		#endif
	}

	/**
	 * Returns the size of the file in bytes, or -1 on error.
	 */
	[[gnu::pure]]
	off_t GetSize() const noexcept;

	ssize_t Read(void *buffer, std::size_t length) noexcept {
		return ::read(fd, buffer, length);
	}

	/**
	 * Read until all of the given buffer has been filled.  Throws
	 * on error.
	 */
	void FullRead(void *buffer, std::size_t length);

	ssize_t Write(const void *buffer, std::size_t length) noexcept {
		return ::write(fd, buffer, length);
	}

	/**
	 * Write until all of the given buffer has been written.
	 * Throws on error.
	 */
	void FullWrite(const void *buffer, std::size_t length);

#ifndef _WIN32
	int Poll(short events, int timeout) const noexcept;

	int WaitReadable(int timeout) const noexcept;
	int WaitWritable(int timeout) const noexcept;

	[[gnu::pure]]
	bool IsReadyForWriting() const noexcept;
#endif
};
