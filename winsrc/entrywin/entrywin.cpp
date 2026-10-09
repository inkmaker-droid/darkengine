///////////////////////////////////////////////////////////////////////////////
// $Source: x:/prj/tech/winsrc/entrywin/RCS/entrywin.cpp $
// $Author: TOML $
// $Date: 1996/12/12 15:44:01 $
// $Revision: 1.4 $
//
//
// Main entry point for windowed Windows applications
//

#ifdef _WIN32

#include <windows.h>
#include <shlobj.h>
#include <dbghelp.h>
#include <stdio.h>
#include <string.h>
#include <lg.h>

#pragma comment(lib, "dbghelp.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")

EXTERN int LGAPI _AppMain(int argc, const char* argv[]);
EXTERN int LGAPI HeapInit();
extern "C" void* g_pMalloc;

extern "C"
{
	int _g_referenceEntryPoint = 0;
}

static BOOL UsesThief2Data(const char* executable)
{
    const char* name = executable;
    const char* slash;

    if (!executable)
        return FALSE;

    slash = strrchr(executable, '\\');
    if (slash)
        name = slash + 1;
    slash = strrchr(name, '/');
    if (slash)
        name = slash + 1;

    return _strnicmp(name, "thief2", 6) == 0 ||
           _strnicmp(name, "dromed", 6) == 0;
}

static BOOL FileExistsInDirectory(const char* directory, const char* name)
{
    char path[MAX_PATH];
    DWORD attributes;

    if (!directory || !directory[0])
        return FALSE;

    _snprintf(path, sizeof(path) - 1, "%s\\%s", directory, name);
    path[sizeof(path) - 1] = '\0';
    attributes = GetFileAttributesA(path);
    return attributes != INVALID_FILE_ATTRIBUTES &&
           !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

static BOOL IsThief2DataDirectory(const char* directory)
{
    return FileExistsInDirectory(directory, "cam.cfg") &&
           (FileExistsInDirectory(directory, "DARK.GAM") ||
            FileExistsInDirectory(directory, "MISS1.MIS"));
}

static BOOL LoadThief2DataDirectory(char* directory, DWORD directorySize)
{
    HKEY key;
    DWORD type = REG_SZ;
    DWORD size = directorySize;
    LONG result;

    result = RegOpenKeyExA(HKEY_CURRENT_USER,
                           "Software\\OpenDarkEngine\\Thief2", 0,
                           KEY_QUERY_VALUE, &key);
    if (result != ERROR_SUCCESS)
        return FALSE;

    result = RegQueryValueExA(key, "GameDataPath", NULL, &type,
                              (BYTE*)directory, &size);
    RegCloseKey(key);
    directory[directorySize - 1] = '\0';
    return result == ERROR_SUCCESS && type == REG_SZ &&
           IsThief2DataDirectory(directory);
}

static void SaveThief2DataDirectory(const char* directory)
{
    HKEY key;
    DWORD disposition;

    if (RegCreateKeyExA(HKEY_CURRENT_USER,
                        "Software\\OpenDarkEngine\\Thief2", 0, NULL, 0,
                        KEY_SET_VALUE, NULL, &key, &disposition) == ERROR_SUCCESS)
    {
        RegSetValueExA(key, "GameDataPath", 0, REG_SZ,
                       (const BYTE*)directory, (DWORD)strlen(directory) + 1);
        RegCloseKey(key);
    }
}

static BOOL BrowseForThief2DataDirectory(char* directory, DWORD directorySize)
{
    BROWSEINFOA browse = { 0 };
    LPITEMIDLIST item;
    HRESULT comResult = CoInitialize(NULL);

    browse.hwndOwner = GetConsoleWindow();
    browse.lpszTitle = "Select your Thief 2 installation folder (the folder containing cam.cfg).";
    browse.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;

    item = SHBrowseForFolderA(&browse);
    if (!item)
    {
        if (SUCCEEDED(comResult))
            CoUninitialize();
        return FALSE;
    }

    directory[0] = '\0';
    SHGetPathFromIDListA(item, directory);
    CoTaskMemFree(item);
    if (SUCCEEDED(comResult))
        CoUninitialize();

    directory[directorySize - 1] = '\0';
    return IsThief2DataDirectory(directory);
}

static BOOL PrepareThief2DataDirectory(void)
{
    char directory[MAX_PATH];

    if (GetCurrentDirectoryA(sizeof(directory), directory) &&
        IsThief2DataDirectory(directory))
    {
        SaveThief2DataDirectory(directory);
        return TRUE;
    }

    if (LoadThief2DataDirectory(directory, sizeof(directory)) ||
        BrowseForThief2DataDirectory(directory, sizeof(directory)))
    {
        SaveThief2DataDirectory(directory);
        if (SetCurrentDirectoryA(directory))
            return TRUE;
    }

    MessageBoxA(NULL,
                "That folder does not contain Thief 2 game data. The folder must contain cam.cfg and DARK.GAM or MISS1.MIS.",
                "Thief 2 game data required", MB_OK | MB_ICONERROR);
    return FALSE;
}

static void WriteCrashLine(HANDLE output, const char* text)
{
    DWORD written;
    if (output && output != INVALID_HANDLE_VALUE)
        WriteFile(output, text, (DWORD)strlen(text), &written, NULL);
}

static LONG WINAPI DarkUnhandledExceptionFilter(EXCEPTION_POINTERS* exceptionInfo)
{
    HANDLE process = GetCurrentProcess();
    HANDLE output = GetStdHandle(STD_ERROR_HANDLE);
    HANDLE log = CreateFileA("darkengine-crash.log", FILE_APPEND_DATA,
                             FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                             OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    char line[1024];

    SYSTEMTIME now;
    GetLocalTime(&now);
    wsprintfA(line, "\r\n[%04u-%02u-%02u %02u:%02u:%02u] ",
              now.wYear, now.wMonth, now.wDay,
              now.wHour, now.wMinute, now.wSecond);
    WriteCrashLine(output, line);
    WriteCrashLine(log, line);

    wsprintfA(line, "Unhandled exception %08X at %p\r\n",
              exceptionInfo->ExceptionRecord->ExceptionCode,
              exceptionInfo->ExceptionRecord->ExceptionAddress);
    WriteCrashLine(output, line);
    WriteCrashLine(log, line);
    if (exceptionInfo->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION &&
        exceptionInfo->ExceptionRecord->NumberParameters >= 2)
    {
#if defined(_M_X64)
        wsprintfA(line, "  access=%s address=%p rax=%p rcx=%p rdx=%p rsp=%p rbp=%p\r\n",
                  exceptionInfo->ExceptionRecord->ExceptionInformation[0] ? "write" : "read",
                  (void *)exceptionInfo->ExceptionRecord->ExceptionInformation[1],
                  (void *)exceptionInfo->ContextRecord->Rax,
                  (void *)exceptionInfo->ContextRecord->Rcx,
                  (void *)exceptionInfo->ContextRecord->Rdx,
                  (void *)exceptionInfo->ContextRecord->Rsp,
                  (void *)exceptionInfo->ContextRecord->Rbp);
#else
        wsprintfA(line, "  access=%s address=%p eax=%08X ecx=%08X edx=%08X esp=%08X ebp=%08X\r\n",
                  exceptionInfo->ExceptionRecord->ExceptionInformation[0] ? "write" : "read",
                  (void *)exceptionInfo->ExceptionRecord->ExceptionInformation[1],
                  exceptionInfo->ContextRecord->Eax,
                  exceptionInfo->ContextRecord->Ecx,
                  exceptionInfo->ContextRecord->Edx,
                  exceptionInfo->ContextRecord->Esp,
                  exceptionInfo->ContextRecord->Ebp);
#endif
        WriteCrashLine(output, line);
        WriteCrashLine(log, line);
    }

    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);
    if (SymInitialize(process, NULL, TRUE))
    {
        CONTEXT context = *exceptionInfo->ContextRecord;
        STACKFRAME64 frame = {};
#if defined(_M_X64)
        const DWORD machineType = IMAGE_FILE_MACHINE_AMD64;
        DWORD64 &programCounter = context.Rip;
        DWORD64 &stackPointer = context.Rsp;
        DWORD64 &framePointer = context.Rbp;
#else
        const DWORD machineType = IMAGE_FILE_MACHINE_I386;
        DWORD &programCounter = context.Eip;
        DWORD &stackPointer = context.Esp;
        DWORD &framePointer = context.Ebp;
#endif
        if (!programCounter && stackPointer)
        {
            // A call through a null function pointer leaves the caller's
            // return address at the top of the stack. Start there so the
            // crash report still identifies the owning call site.
            programCounter = *reinterpret_cast<ULONG_PTR*>(stackPointer);
            stackPointer += sizeof(ULONG_PTR);
        }
        frame.AddrPC.Offset = programCounter;
        frame.AddrPC.Mode = AddrModeFlat;
        frame.AddrFrame.Offset = framePointer;
        frame.AddrFrame.Mode = AddrModeFlat;
        frame.AddrStack.Offset = stackPointer;
        frame.AddrStack.Mode = AddrModeFlat;

        for (int depth = 0; depth < 64 && frame.AddrPC.Offset; ++depth)
        {
            char symbolStorage[sizeof(SYMBOL_INFO) + MAX_SYM_NAME] = {};
            SYMBOL_INFO* symbol = reinterpret_cast<SYMBOL_INFO*>(symbolStorage);
            symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
            symbol->MaxNameLen = MAX_SYM_NAME;
            DWORD64 displacement = 0;

            if (SymFromAddr(process, frame.AddrPC.Offset, &displacement, symbol))
                wsprintfA(line, "  #%02d %p %s+0x%p\r\n", depth,
                          (void *)frame.AddrPC.Offset, symbol->Name, (void *)displacement);
            else
                wsprintfA(line, "  #%02d %p\r\n", depth, (void *)frame.AddrPC.Offset);
            WriteCrashLine(output, line);
            WriteCrashLine(log, line);

            if (!StackWalk64(machineType, process, GetCurrentThread(),
                             &frame, &context, NULL, SymFunctionTableAccess64,
                             SymGetModuleBase64, NULL))
                break;
        }
        SymCleanup(process);
    }

    if (log != INVALID_HANDLE_VALUE)
        CloseHandle(log);

    return EXCEPTION_CONTINUE_SEARCH;
}

int main(int argc, const char* argv[])
{
    SetUnhandledExceptionFilter(DarkUnhandledExceptionFilter);
    if (!g_pMalloc && !HeapInit())
        return 1;

    if (UsesThief2Data(argc > 0 ? argv[0] : NULL) &&
        !PrepareThief2DataDirectory())
        return 0;

	return _AppMain(argc, argv);
}

/*
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR pszCmdLine, int fShow)
{
	// Turn Windows command line into classic arguments    @TBD (toml 04-24-96):  won't do quoted arguments correctly
	int argc = 1;
	const char* argv[32];

	argv[0] = "";
	argv[1] = strtok(pszCmdLine, " ");
	while (argv[argc] != NULL && argc < 31)
		argv[++argc] = strtok(NULL, " ");

	return _AppMain(argc, argv);
}
*/

#endif
