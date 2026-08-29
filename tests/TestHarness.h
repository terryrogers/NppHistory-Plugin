#pragma once

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>
#include <windows.h>

struct TestContext
{
    int checks = 0;
    int failures = 0;
    std::vector<std::string> failureMessages;

    void expect(bool condition, const std::string& message)
    {
        ++checks;
        if (condition)
            return;
        ++failures;
        failureMessages.push_back(message);
        std::cerr << "FAIL: " << message << '\n';
    }
};

class TestDirectory
{
public:
    explicit TestDirectory(const wchar_t* label)
    {
        _path = std::filesystem::temp_directory_path()
            / (std::wstring(L"NppHistory-") + label + L"-"
                + std::to_wstring(GetCurrentProcessId()) + L"-"
                + std::to_wstring(GetTickCount64()));
        std::error_code error;
        std::filesystem::create_directories(_path, error);
    }

    ~TestDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(_path, error);
    }

    const std::filesystem::path& path() const noexcept { return _path; }

private:
    std::filesystem::path _path;
};

void runUtilityTests(TestContext& context);
void runSettingsTests(TestContext& context);
void runDiffTests(TestContext& context);
void runHistoryStoreTests(TestContext& context);
void runHistoryCatalogTests(TestContext& context);
