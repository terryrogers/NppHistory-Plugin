#include <array>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>
#include <windows.h>
#include <bcrypt.h>
#include <shellapi.h>

namespace fs = std::filesystem;

namespace
{
std::wstring sha256File(const fs::path& file)
{
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD objectSize = 0, transferred = 0;
    std::vector<unsigned char> object;
    std::array<unsigned char, 32> digest{};
    std::array<char, 64 * 1024> buffer{};
    std::ifstream stream(file, std::ios::binary);
    if (!stream || BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM,
        nullptr, 0) < 0
        || BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
            reinterpret_cast<PUCHAR>(&objectSize), sizeof(objectSize), &transferred, 0) < 0)
        goto cleanup;
    object.resize(objectSize);
    if (BCryptCreateHash(algorithm, &hash, object.data(), objectSize, nullptr, 0, 0) < 0)
        goto cleanup;
    while (stream)
    {
        stream.read(buffer.data(), buffer.size());
        const std::streamsize read = stream.gcount();
        if (read > 0 && BCryptHashData(hash, reinterpret_cast<PUCHAR>(buffer.data()),
            static_cast<ULONG>(read), 0) < 0)
            goto cleanup;
    }
    if (!stream.eof() || BCryptFinishHash(hash, digest.data(),
        static_cast<ULONG>(digest.size()), 0) < 0)
        goto cleanup;
    {
        constexpr wchar_t hex[] = L"0123456789abcdef";
        std::wstring result;
        result.reserve(64);
        for (const unsigned char value : digest)
        {
            result.push_back(hex[value >> 4]);
            result.push_back(hex[value & 0x0F]);
        }
        if (hash) BCryptDestroyHash(hash);
        if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
        return result;
    }
cleanup:
    if (hash) BCryptDestroyHash(hash);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    return {};
}

void writeResult(const fs::path& file, const wchar_t* status,
    const std::wstring& detail, const std::wstring& version)
{
    WritePrivateProfileStringW(L"Update", L"Status", status, file.c_str());
    WritePrivateProfileStringW(L"Update", L"Detail", detail.c_str(), file.c_str());
    WritePrivateProfileStringW(L"Update", L"Version", version.c_str(), file.c_str());
}

int fail(const fs::path& result, const std::wstring& detail, const std::wstring& version,
    int exitCode)
{
    if (!result.empty())
        writeResult(result, L"failure", detail, version);
    return exitCode;
}
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    int count = 0;
    wchar_t** values = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!values)
        return 2;
    std::map<std::wstring, std::wstring> arguments;
    for (int index = 1; index + 1 < count; index += 2)
        arguments[values[index]] = values[index + 1];
    LocalFree(values);
    const fs::path source = arguments[L"--source"];
    const fs::path target = arguments[L"--target"];
    const fs::path restart = arguments[L"--restart"];
    const fs::path result = arguments[L"--result"];
    const std::wstring version = arguments[L"--version"];
    const std::wstring expectedHash = arguments[L"--sha256"];
    const std::wstring pidText = arguments[L"--wait-pid"];
    if (source.empty() || target.empty() || restart.empty() || result.empty()
        || version.empty() || pidText.empty() || expectedHash.size() != 64
        || target.filename() != L"NppHistory.dll")
        return fail(result, L"The updater received incomplete or invalid arguments.", version, 2);
    wchar_t* end = nullptr;
    const unsigned long pid = wcstoul(pidText.c_str(), &end, 10);
    if (!end || *end != L'\0' || pid == 0)
        return fail(result, L"The Notepad++ process identifier was invalid.", version, 2);

    HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (process)
    {
        const DWORD wait = WaitForSingleObject(process, 10U * 60U * 1000U);
        CloseHandle(process);
        if (wait != WAIT_OBJECT_0)
            return fail(result,
                wait == WAIT_TIMEOUT ? L"Notepad++ did not close within ten minutes. The update was cancelled."
                    : L"Windows could not wait for Notepad++ to close.", version, 3);
    }
    else if (GetLastError() != ERROR_INVALID_PARAMETER)
        return fail(result, L"The updater could not monitor the running Notepad++ process.", version, 3);

    if (!fs::is_regular_file(source) || !fs::is_regular_file(target)
        || !fs::is_regular_file(restart))
        return fail(result, L"An update source or installation file is missing.", version, 4);
    const fs::path backup = source.parent_path() / L"NppHistory.previous.dll";
    const fs::path replacement = target.parent_path() / L"NppHistory.update-new.dll";
    DeleteFileW(backup.c_str());
    DeleteFileW(replacement.c_str());
    if (!CopyFileW(target.c_str(), backup.c_str(), FALSE))
        return fail(result, L"The existing plugin could not be backed up. Windows error "
            + std::to_wstring(GetLastError()) + L".", version, 5);
    if (!CopyFileW(source.c_str(), replacement.c_str(), FALSE))
        return fail(result, L"The verified update could not be copied into the plugin folder. Windows error "
            + std::to_wstring(GetLastError()) + L".", version, 6);
    if (sha256File(replacement) != expectedHash)
    {
        DeleteFileW(replacement.c_str());
        return fail(result,
            L"The staged plugin failed its final SHA-256 verification. The previous DLL remains installed.",
            version, 7);
    }
    if (!MoveFileExW(replacement.c_str(), target.c_str(),
        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        const DWORD error = GetLastError();
        DeleteFileW(replacement.c_str());
        return fail(result, L"The plugin DLL could not be replaced. Windows error "
            + std::to_wstring(error) + L". The previous DLL remains installed.", version, 7);
    }
    writeResult(result, L"success", L"NppHistory was updated successfully.", version);
    DeleteFileW(source.c_str());
    const auto launched = reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open",
        restart.c_str(), nullptr, restart.parent_path().c_str(), SW_SHOWNORMAL));
    if (launched <= 32)
    {
        writeResult(result, L"installed_restart_failed",
            L"The update was installed, but Notepad++ could not be restarted automatically.", version);
        return 8;
    }
    return 0;
}
