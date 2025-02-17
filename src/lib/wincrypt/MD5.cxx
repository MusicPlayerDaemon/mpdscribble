// SPDX-License-Identifier: BSD-2-Clause
// author: Max Kellermann <max.kellermann@gmail.com>

#include "MD5.hxx"
#include "system/Error.hxx"
#include "util/ScopeExit.hxx"

#include <minwinbase.h> // for PSYSTEMTIME (needed by wincrypt.h)
#include <windef.h> // for HWND (needed by dpapi.h, included by wincrypt.h)
#include <wincrypt.h>

namespace WinCrypt {

std::array<std::byte, 16>
MD5(std::span<const std::byte> input)
{
    HCRYPTPROV hProv = 0;
    if (!CryptAcquireContext(&hProv, nullptr, nullptr, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT))
	    throw MakeLastError("CryptAcquireContext failed");

    AtScopeExit(hProv) { CryptReleaseContext(hProv, 0); };

    HCRYPTHASH hHash = 0;
    if (!CryptCreateHash(hProv, CALG_MD5, 0, 0, &hHash))
	    throw MakeLastError("CryptCreateHash() failed");

    AtScopeExit(hHash) { CryptDestroyHash(hHash); };

    if (!CryptHashData(hHash, (const BYTE *)input.data(), input.size(), 0))
	    throw MakeLastError("CryptHashData() failed");

    std::array<std::byte, 16> result;
    DWORD size = result.size();
    if (!CryptGetHashParam(hHash, HP_HASHVAL, (BYTE *)result.data(), &size, 0))
	    throw MakeLastError("CryptGetHashParam() failed");

    if (size != result.size())
	    throw std::runtime_error("CryptGetHashParam() returned an unexpected size");

    return result;
}

} // namespace WinCrypt
