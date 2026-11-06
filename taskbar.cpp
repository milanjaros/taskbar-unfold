// SPDX-License-Identifier: GPL-3.0-only
// HasLabel vtable technique adapted from m417z's taskbar-labels v1.5.1.
#define UNICODE
#define _UNICODE
#ifndef _M_X64
#error Taskbar Unfold requires an x64 build.
#endif
#include <windows.h>
#include <tlhelp32.h>
#include <dbghelp.h>
#include <cstdio>
#include <string>
#include <filesystem>
#include <fstream>
#include <cstring>
#include <vector>
#include <stdexcept>
#ifndef BUILD_DLL
#include <urlmon.h>
#endif

struct Config
{
    DWORD magic = 0x54424931, stamp = 0, size = 0;
    DWORD rva[4]{};
};
static std::wstring Directory(HMODULE m = nullptr)
{
    wchar_t p[32768];
    GetModuleFileNameW(m, p, 32768);
    return std::filesystem::path(p).parent_path().wstring();
}
static IMAGE_NT_HEADERS *Header(HMODULE m)
{
    return reinterpret_cast<IMAGE_NT_HEADERS *>(reinterpret_cast<BYTE *>(m) + reinterpret_cast<IMAGE_DOS_HEADER *>(m)->e_lfanew);
}
#ifdef BUILD_DLL
static HMODULE self;
static void **slots[2]{};
static void *originals[2]{};
static HRESULT WINAPI NoLabel(void *, bool *value)
{
    if (!value)
        return E_POINTER;
    *value = false;
    return S_OK;
}
static bool Swap(void **slot, void *from, void *to)
{
    DWORD old;
    if (!VirtualProtect(slot, sizeof(void *), PAGE_READWRITE, &old))
        return false;
    bool ok = InterlockedCompareExchangePointer(slot, to, from) == from;
    DWORD ignored;
    VirtualProtect(slot, sizeof(void *), old, &ignored);
    return ok;
}
static DWORD Change(bool stop)
{
    if (stop)
    {
        for (int i = 0; i < 2; i++)
            if (slots[i])
            {
                if (!Swap(slots[i], reinterpret_cast<void *>(NoLabel), originals[i]))
                    return 6;
                slots[i] = nullptr;
            }
        slots[0] = slots[1] = nullptr;
        return 0;
    }
    if (slots[0])
        return 0;
    Config c;
    std::ifstream f(std::filesystem::path(Directory(self)) / L"symbols.bin", std::ios::binary);
    if (!f.read(reinterpret_cast<char *>(&c), sizeof(c)) || c.magic != 0x54424931)
        return 1;
    HMODULE m = GetModuleHandleW(L"Taskbar.View.dll");
    if (!m)
        return 2;
    auto h = Header(m);
    if (c.stamp != h->FileHeader.TimeDateStamp || c.size != h->OptionalHeader.SizeOfImage)
        return 3;
    for (auto r : c.rva)
        if (!r || r >= c.size - 64 * sizeof(void *))
            return 3;
    auto base = reinterpret_cast<BYTE *>(m);
    auto w = reinterpret_cast<void **>(base + c.rva[0]);
    auto g = reinterpret_cast<void **>(base + c.rva[1]);
    for (int i = 6; i < 64; i++)
        if (g[i] == base + c.rva[3] && w[i] == base + c.rva[2])
        {
            HMODULE pinned;
            if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN, reinterpret_cast<LPCWSTR>(self), &pinned))
                return 4;
            if (!Swap(w + i, base + c.rva[2], reinterpret_cast<void *>(NoLabel)))
                return 5;
            if (!Swap(g + i, base + c.rva[3], reinterpret_cast<void *>(NoLabel)))
            {
                Swap(w + i, reinterpret_cast<void *>(NoLabel), base + c.rva[2]);
                return 5;
            }
            slots[0] = w + i;
            slots[1] = g + i;
            originals[0] = base + c.rva[2];
            originals[1] = base + c.rva[3];
            return 0;
        }
    return 7;
}
extern "C" __declspec(dllexport) LRESULT CALLBACK TaskbarHook(int code, WPARAM wp, LPARAM lp)
{
    if (code >= 0)
    {
        auto msg = reinterpret_cast<MSG *>(lp);
        if (msg->message == RegisterWindowMessageW(L"LocalTaskbarUnfold.Change.v1"))
        {
            DWORD result = Change(msg->wParam != 0);
            std::ofstream f(std::filesystem::path(Directory(self)) / L"result.txt");
            f << result;
            f.close();
            std::wstring name = L"Local\\TaskbarUnfold.Done." + std::to_wstring(GetCurrentProcessId());
            HANDLE e = OpenEventW(EVENT_MODIFY_STATE, FALSE, name.c_str());
            if (e)
            {
                SetEvent(e);
                CloseHandle(e);
            }
        }
    }
    return CallNextHookEx(nullptr, code, wp, lp);
}
BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        self = h;
        DisableThreadLibraryCalls(h);
    }
    return TRUE;
}
#else
static std::wstring StateDirectory() { return Directory() + L"\\.taskbar-unfold"; }
static std::vector<char> Resource(int id)
{
    auto resource = FindResourceW(nullptr, MAKEINTRESOURCEW(id), RT_RCDATA);
    if (!resource)
        throw std::runtime_error("Embedded resource is missing.");
    auto p = static_cast<const char *>(LockResource(LoadResource(nullptr, resource)));
    return {p, p + SizeofResource(nullptr, resource)};
}
static std::wstring ExtractDll()
{
    auto bytes = Resource(101);
    unsigned long long hash = 14695981039346656037ull;
    for (unsigned char b : bytes)
    {
        hash ^= b;
        hash *= 1099511628211ull;
    }
    auto path = StateDirectory() + L"\\hook-" + std::to_wstring(hash) + L".dll";
    std::ifstream old(path, std::ios::binary);
    std::vector<char> existing((std::istreambuf_iterator<char>(old)), {});
    old.close();
    if (existing != bytes)
    {
        std::ofstream f(path, std::ios::binary);
        if (!f.write(bytes.data(), bytes.size()))
            throw std::runtime_error("Cannot extract the embedded DLL.");
    }
    return path;
}
struct Backup
{
    DWORD magic = 0x54425231;
    DWORD present[2]{};
    DWORD value[2]{};
};
static constexpr const wchar_t *SettingNames[] = {L"TaskbarGlomLevel", L"MMTaskbarGlomLevel"};
static void RefreshTaskbar()
{
    DWORD_PTR result;
    // A null lParam triggers TrayUI::_HandleSettingChange. TraySettings alone
    // doesn't force existing view models to reevaluate their label bindings.
    HWND taskbar = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (taskbar)
        SendMessageTimeoutW(taskbar, WM_SETTINGCHANGE, 0, 0, SMTO_ABORTIFHUNG, 3000, &result);
}
static void Settings(bool restore)
{
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced", 0, KEY_QUERY_VALUE | KEY_SET_VALUE, &key) != ERROR_SUCCESS)
        throw std::runtime_error("Cannot open taskbar settings.");
    auto path = StateDirectory() + L"\\settings-backup.bin";
    Backup b;
    if (restore || std::filesystem::exists(path))
    {
        std::ifstream f(path, std::ios::binary);
        if (!f.read(reinterpret_cast<char *>(&b), sizeof(b)) || b.magic != 0x54425231)
        {
            RegCloseKey(key);
            throw std::runtime_error("Settings backup is missing or invalid.");
        }
    }
    else
    {
        for (int i = 0; i < 2; i++)
        {
            DWORD size = sizeof(DWORD), type = 0;
            auto error = RegQueryValueExW(key, SettingNames[i], nullptr, &type, reinterpret_cast<BYTE *>(&b.value[i]), &size);
            if (error != ERROR_FILE_NOT_FOUND && (error != ERROR_SUCCESS || type != REG_DWORD))
            {
                RegCloseKey(key);
                throw std::runtime_error("Unexpected taskbar setting type.");
            }
            b.present[i] = error == ERROR_SUCCESS;
        }
        std::ofstream f(path, std::ios::binary);
        if (!f.write(reinterpret_cast<char *>(&b), sizeof(b)))
        {
            RegCloseKey(key);
            throw std::runtime_error("Cannot save taskbar settings backup.");
        }
    }
    // Force a grouping transition even when the registry already contains the
    // requested values (for example after Explorer restarts). This invalidates
    // the cached HasLabel value on existing buttons with the hooks installed.
    bool ok = true;
    for (int i = 0; i < 2; i++)
    {
        DWORD target = restore ? (b.present[i] ? b.value[i] : 0) : 2;
        DWORD temporary = target == 2 ? 0 : 2;
        auto error = RegSetValueExW(key, SettingNames[i], 0, REG_DWORD,
                                   reinterpret_cast<BYTE *>(&temporary), sizeof(temporary));
        ok = ok && error == ERROR_SUCCESS;
    }
    RefreshTaskbar();
    Sleep(400);
    for (int i = 0; i < 2; i++)
    {
        DWORD value = restore ? b.value[i] : 2;
        LSTATUS error;
        if (restore && !b.present[i])
        {
            error = RegDeleteValueW(key, SettingNames[i]);
            if (error == ERROR_FILE_NOT_FOUND)
                error = ERROR_SUCCESS;
        }
        else
            error = RegSetValueExW(key, SettingNames[i], 0, REG_DWORD, reinterpret_cast<BYTE *>(&value), sizeof(value));
        ok = ok && error == ERROR_SUCCESS;
    }
    RegCloseKey(key);
    if (!ok)
        throw std::runtime_error("Cannot update all taskbar settings. The backup has been retained.");
    if (restore)
        std::filesystem::remove(path);
    DWORD_PTR result;
    SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, reinterpret_cast<LPARAM>(L"TraySettings"), SMTO_ABORTIFHUNG, 3000, &result);
    RefreshTaskbar();
}
struct Symbols
{
    DWORD64 base;
    Config c;
    bool duplicate = false;
};
static BOOL CALLBACK Found(PSYMBOL_INFO s, ULONG, void *ctx)
{
    auto &data = *static_cast<Symbols *>(ctx);
    char demangled[8192];
    const char *name = s->Name;
    if (UnDecorateSymbolName(name, demangled, sizeof(demangled), UNDNAME_COMPLETE))
        name = demangled;
    bool window = strstr(name, "produce<struct winrt::Taskbar::implementation::TaskListWindowViewModel,struct winrt::Taskbar::ITaskbarAppItemViewModel>") != nullptr;
    bool group = strstr(name, "produce<struct winrt::Taskbar::implementation::TaskListGroupViewModel,struct winrt::Taskbar::ITaskbarAppItemViewModel>") != nullptr;
    if (!window && !group)
        return TRUE;
    int index = -1;
    if (strstr(name, "`vftable'"))
        index = group ? 1 : 0;
    if (strstr(name, "::get_HasLabel("))
        index = group ? 3 : 2;
    if (index >= 0)
    {
        DWORD r = static_cast<DWORD>(s->Address - data.base);
        if (data.c.rva[index] && data.c.rva[index] != r)
            data.duplicate = true;
        data.c.rva[index] = r;
        printf("%d: %08lx %s\n", index, r, name);
    }
    return TRUE;
}
static int Run(int argc, wchar_t **argv)
{
    const wchar_t *command = argc == 1 ? L"start" : argv[1];
    if (argc > 2 || (wcscmp(command, L"start") && wcscmp(command, L"stop") && wcscmp(command, L"probe")))
    {
        puts("Usage: taskbar-unfold.exe [start | stop | probe]");
        return 1;
    }
    bool stop = wcscmp(command, L"stop") == 0;
    bool probe = wcscmp(command, L"probe") == 0;
    std::filesystem::create_directories(StateDirectory());
    HANDLE mutex = CreateMutexW(nullptr, FALSE, L"Local\\TaskbarUnfold.Launcher.v2");
    if (!mutex || WaitForSingleObject(mutex, 0) != WAIT_OBJECT_0)
    {
        puts("Another taskbar-unfold command is running.");
        return 1;
    }
    HWND taskbar = FindWindowW(L"Shell_TrayWnd", nullptr);
    DWORD pid = 0;
    DWORD thread = GetWindowThreadProcessId(taskbar, &pid);
    if (!thread)
    {
        puts("Taskbar not found.");
        return 1;
    }
    auto dir = StateDirectory();
    if (!stop)
    {
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pid);
        MODULEENTRY32W entry{sizeof(entry)};
        std::wstring path;
        if (snap != INVALID_HANDLE_VALUE)
        {
            if (Module32FirstW(snap, &entry))
                do
                {
                    if (_wcsicmp(entry.szModule, L"Taskbar.View.dll") == 0)
                        path = entry.szExePath;
                } while (Module32NextW(snap, &entry));
            CloseHandle(snap);
        }
        if (path.empty())
        {
            puts("Taskbar.View.dll not found.");
            return 2;
        }
        HMODULE image = LoadLibraryExW(path.c_str(), nullptr, DONT_RESOLVE_DLL_REFERENCES);
        if (!image)
            return 3;
        Symbols data{};
        auto h = Header(image);
        data.c.stamp = h->FileHeader.TimeDateStamp;
        data.c.size = h->OptionalHeader.SizeOfImage;
        Config cached{};
        std::ifstream cache(dir + L"\\symbols.bin", std::ios::binary);
        cache.read(reinterpret_cast<char *>(&cached), sizeof(cached));
        cache.close();
        if (cached.magic != 0x54424931 || cached.stamp != data.c.stamp || cached.size != data.c.size)
        {
            if (FindResourceW(nullptr, MAKEINTRESOURCEW(102), RT_RCDATA))
            {
                auto embedded = Resource(102);
                if (embedded.size() == sizeof(cached))
                    memcpy(&cached, embedded.data(), sizeof(cached));
            }
        }
        bool ready = cached.magic == 0x54424931 && cached.stamp == data.c.stamp && cached.size == data.c.size;
        for (auto r : cached.rva)
            ready = ready && r > 0 && r < data.c.size;
        if (ready)
        {
            std::ofstream f(dir + L"\\symbols.bin", std::ios::binary);
            if (!f.write(reinterpret_cast<char *>(&cached), sizeof(cached)))
                throw std::runtime_error("Cannot save symbol configuration.");
            FreeLibrary(image);
            puts("Symbol configuration matches this Windows build.");
        }
        else
        {
            std::filesystem::create_directories(std::filesystem::path(dir) / L"symbols");
            auto debug = h->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG];
            auto records = reinterpret_cast<IMAGE_DEBUG_DIRECTORY *>(reinterpret_cast<BYTE *>(image) + debug.VirtualAddress);
            for (DWORD i = 0; i < debug.Size / sizeof(*records); i++)
                if (records[i].Type == IMAGE_DEBUG_TYPE_CODEVIEW)
                {
                    auto cv = reinterpret_cast<BYTE *>(image) + records[i].AddressOfRawData;
                    if (memcmp(cv, "RSDS", 4) != 0)
                        continue;
                    GUID guid;
                    memcpy(&guid, cv + 4, 16);
                    DWORD age;
                    memcpy(&age, cv + 20, 4);
                    char key[64];
                    sprintf_s(key, "%08lX%04X%04X%02X%02X%02X%02X%02X%02X%02X%02X%lX", guid.Data1, guid.Data2, guid.Data3, guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3], guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7], age);
                    auto pdb = std::filesystem::path(reinterpret_cast<char *>(cv + 24)).filename().string();
                    std::ofstream url(std::filesystem::path(dir) / L"pdb-url.txt");
                    url << "https://msdl.microsoft.com/download/symbols/" << pdb << "/" << key << "/" << pdb;
                    printf("PDB: %s / %s\n", pdb.c_str(), key);
                    std::string address = "https://msdl.microsoft.com/download/symbols/" + pdb + "/" + key + "/" + pdb;
                    std::wstring wide(address.begin(), address.end());
                    auto destination = std::filesystem::path(dir) / L"symbols" / std::filesystem::path(pdb);
                    puts("Downloading matching symbols from Microsoft...");
                    if (FAILED(URLDownloadToFileW(nullptr, wide.c_str(), destination.c_str(), 0, nullptr)))
                    {
                        FreeLibrary(image);
                        throw std::runtime_error("Symbol download failed. Check your internet connection and retry.");
                    }
                }
            auto search = dir + L"\\symbols";
            SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_FAIL_CRITICAL_ERRORS | SYMOPT_EXACT_SYMBOLS);
            HANDLE process = GetCurrentProcess();
            if (!SymInitializeW(process, search.c_str(), FALSE))
                return 4;
            data.base = SymLoadModuleExW(process, nullptr, path.c_str(), nullptr, 0, 0, nullptr, 0);
            if (!data.base || !SymEnumSymbols(process, data.base, nullptr, Found, &data))
                printf("Symbol error: %lu\n", GetLastError());
            SymCleanup(process);
            FreeLibrary(image);
            if (data.duplicate || !data.c.rva[0] || !data.c.rva[1] || !data.c.rva[2] || !data.c.rva[3])
            {
                puts("Required unique symbols unavailable; no changes made.");
                return 5;
            }
            std::ofstream f(std::filesystem::path(dir) / L"symbols.bin", std::ios::binary);
            f.write(reinterpret_cast<char *>(&data.c), sizeof(data.c));
            if (!f)
                throw std::runtime_error("Cannot save symbol configuration.");
            puts("Symbols verified and saved.");
        }
        if (probe)
        {
            puts("No Explorer changes made.");
            return 0;
        }
    }
    auto dllPath = ExtractDll();
    std::wstring eventName = L"Local\\TaskbarUnfold.Done." + std::to_wstring(pid);
    HANDLE event = CreateEventW(nullptr, TRUE, FALSE, eventName.c_str());
    if (!event)
        return 7;
    ResetEvent(event);
    HMODULE dll = LoadLibraryW(dllPath.c_str());
    if (!dll)
    {
        puts("Cannot load the embedded DLL.");
        return 8;
    }
    auto hook = SetWindowsHookExW(WH_GETMESSAGE, reinterpret_cast<HOOKPROC>(GetProcAddress(dll, "TaskbarHook")), dll, thread);
    if (!hook)
    {
        printf("Hook error: %lu\n", GetLastError());
        return 9;
    }
    PostThreadMessageW(thread, RegisterWindowMessageW(L"LocalTaskbarUnfold.Change.v1"), stop, 0);
    DWORD wait = WaitForSingleObject(event, 15000);
    UnhookWindowsHookEx(hook);
    CloseHandle(event);
    FreeLibrary(dll);
    if (wait != WAIT_OBJECT_0)
    {
        puts("Explorer did not respond.");
        return 10;
    }
    int result = -1;
    std::ifstream(std::filesystem::path(dir) / L"result.txt") >> result;
    if (result != 0)
    {
        printf("Explorer could not update label hooks (error %d).\n", result);
        return 11;
    }
    if (stop)
    {
        if (std::filesystem::exists(dir + L"\\settings-backup.bin"))
            Settings(true);
        puts("Stopped. Original taskbar settings restored.");
    }
    else
    {
        Settings(false);
        puts("Started. Taskbar buttons are separate and labels are hidden.");
    }
    return 0;
}
int wmain(int argc, wchar_t **argv)
{
    try
    {
        return Run(argc, argv);
    }
    catch (const std::exception &e)
    {
        printf("Error: %s\n", e.what());
        return 1;
    }
}
#endif
