// SPDX-License-Identifier: BSD-2-Clause
// author: Max Kellermann <max.kellermann@gmail.com>

#include "FileReader.hxx"
#include "lib/fmt/SystemError.hxx"
#include "io/Open.hxx"

#include <cassert>

#ifdef _WIN32

FileReader::FileReader(const char *path)
	:handle(CreateFile(path, GENERIC_READ, FILE_SHARE_READ,
			   nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
			   nullptr))
{
	if (handle == INVALID_HANDLE_VALUE)
		throw FmtLastError("Failed to open {}", path);
}

std::size_t
FileReader::Read(std::span<std::byte> dest)
{
	assert(IsDefined());

	DWORD nbytes;
	if (!ReadFile(handle, dest.data(), dest.size(), &nbytes, nullptr))
		throw MakeLastError("Failed to read from file");

	return nbytes;
}

#else

FileReader::FileReader(const char *path)
	:fd(OpenReadOnly(path))
{
}

std::size_t
FileReader::Read(std::span<std::byte> dest)
{
	assert(IsDefined());

	ssize_t nbytes = fd.Read(dest);
	if (nbytes < 0)
		throw MakeErrno("Failed to read from file");

	return nbytes;
}

#endif
