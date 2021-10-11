// Diablo II Botting System Core

#include <shlwapi.h>
#include <io.h>
#include <fcntl.h>

#include "dde.h"
#include "Offset.h"
#include "ScriptEngine.h"
#include "Helpers.h"
#include "D2Handlers.h"
#include "Console.h"
#include "D2BS.h"
#include "D2Ptrs.h"
#include "CommandLine.h"

#ifdef _MSVC_DEBUG
#include "D2Loader.h"
#endif
// D2线程Id
static HANDLE hD2Thread = INVALID_HANDLE_VALUE;
// 事件线程Id
static HANDLE hEventThread = INVALID_HANDLE_VALUE;
BOOL WINAPI DllMain(HINSTANCE hDll, DWORD dwReason, LPVOID lpReserved) {
    switch (dwReason) {
    case DLL_PROCESS_ATTACH: {
        // 1. 防止DLL嵌套加载
        DisableThreadLibraryCalls(hDll);
        if (lpReserved != NULL) {
            // 模块内存加载
            Vars.pModule = (Module*)lpReserved;

            if (!Vars.pModule)
                return FALSE;
            // 直接从module里面拷贝文件路劲
            wcscpy_s(Vars.szPath, MAX_PATH, Vars.pModule->szPath);
            // 这个变量 分辨pModule是怎么加载的
            Vars.bLoadedWithCGuard = TRUE;
            //TODO 这里是不是还需要把模块句柄也设置进去
        } else {
            // 模块句柄
            Vars.hModule = hDll;
            // 通过API来获取文件路劲
            GetModuleFileNameW(hDll, Vars.szPath, MAX_PATH);
            PathRemoveFileSpecW(Vars.szPath);
            wcscat_s(Vars.szPath, MAX_PATH, L"\\");
            Vars.bLoadedWithCGuard = FALSE;
        }
        // 2. 设置日志路径并创建目录
        swprintf_s(Vars.szLogPath, _countof(Vars.szLogPath), L"%slogs\\", Vars.szPath);
        CreateDirectoryW(Vars.szLogPath, NULL);
        // 3. 解析命令行
        InitCommandLine();
        ParseCommandLine(Vars.szCommandLine);
        // 4. 初始化设置
        InitSettings();
        sLine* command = NULL;
        Vars.bUseRawCDKey = FALSE;
        // 5. 获取一堆参数得值，来覆盖默认变量
        if (command = GetCommand(L"-title")) { // 如果有设置 就 复制值
            int len = wcslen((wchar_t*)command->szText);
            wcsncat_s(Vars.szTitle, (wchar_t*)command->szText, len);
        }

        if (GetCommand(L"-sleepy"))
            Vars.bSleepy = TRUE;

        if (GetCommand(L"-cachefix"))
            Vars.bCacheFix = TRUE;

        if (GetCommand(L"-multi"))
            Vars.bMulti = TRUE;

        if (GetCommand(L"-ftj"))
            Vars.bReduceFTJ = TRUE;

        if (command = GetCommand(L"-d2c")) { // 设置cdkey
            Vars.bUseRawCDKey = TRUE;
            const char* keys = UnicodeToAnsi(command->szText);
            strncat_s(Vars.szClassic, keys, strlen(keys));
            delete[] keys;
        }

        if (command = GetCommand(L"-d2x")) {
            const char* keys = UnicodeToAnsi(command->szText);
            strncat_s(Vars.szLod, keys, strlen(keys));
            delete[] keys;
        }

#if 0
		char errlog[516] = "";
		sprintf_s(errlog, 516, "%sd2bs.log", Vars.szPath);
		AllocConsole();
		int handle = _open_osfhandle((long)GetStdHandle(STD_ERROR_HANDLE), _O_TEXT);
		FILE* f = _fdopen(handle, "wt");
		*stderr = *f;
		setvbuf(stderr, NULL, _IONBF, 0);
		freopen_s(&f, errlog, "a+t", f);
#endif

        Vars.bShutdownFromDllMain = FALSE;
        SetUnhandledExceptionFilter(ExceptionHandler);
        // 6. 启动主要得逻辑
        if (!Startup())
            return FALSE;
    } break;
    case DLL_PROCESS_DETACH:
        if (Vars.bNeedShutdown) {
            Vars.bShutdownFromDllMain = TRUE;
            Shutdown();
        }
        break;
    }

    return TRUE;
}
/**
* 主业务函数
*/ 
BOOL Startup(void) {
    InitializeCriticalSection(&Vars.cEventSection);
    InitializeCriticalSection(&Vars.cRoomSection);
    InitializeCriticalSection(&Vars.cMiscSection);
    InitializeCriticalSection(&Vars.cScreenhookSection);
    InitializeCriticalSection(&Vars.cPrintSection);
    InitializeCriticalSection(&Vars.cBoxHookSection);
    InitializeCriticalSection(&Vars.cFrameHookSection);
    InitializeCriticalSection(&Vars.cLineHookSection);
    InitializeCriticalSection(&Vars.cImageHookSection);
    InitializeCriticalSection(&Vars.cTextHookSection);
    InitializeCriticalSection(&Vars.cFlushCacheSection);
    InitializeCriticalSection(&Vars.cConsoleSection);
    InitializeCriticalSection(&Vars.cGameLoopSection);
    InitializeCriticalSection(&Vars.cUnitListSection);
    InitializeCriticalSection(&Vars.cFileSection);

    Vars.bNeedShutdown = TRUE;
    Vars.bChangedAct = FALSE;
    Vars.bGameLoopEntered = FALSE;
    Vars.SectionCount = 0;

    // MessageBox(NULL, "qwe", "qwe", MB_OK);
    Genhook::Initialize();
    DefineOffsets();
    InstallPatches();
    InstallConditional();
    CreateDdeServer();

    if ((hD2Thread = CreateThread(NULL, NULL, D2Thread, NULL, NULL, NULL)) == NULL)
        return FALSE;
    //	hEventThread = CreateThread(0, 0, EventThread, NULL, 0, 0);
    return TRUE;
}
/**
* 退出的逻辑
*/
void Shutdown(void) {
    if (!Vars.bNeedShutdown)
        return;

    Vars.bActive = FALSE;
    if (!Vars.bShutdownFromDllMain) // 等待线程执行完
        WaitForSingleObject(hD2Thread, INFINITE);

    SetWindowLong(D2GFX_GetHwnd(), GWL_WNDPROC, (LONG)Vars.oldWNDPROC);

    RemovePatches();
    Genhook::Destroy();
    ShutdownDdeServer();

    KillTimer(D2GFX_GetHwnd(), Vars.uTimer);

    UnhookWindowsHookEx(Vars.hMouseHook);
    UnhookWindowsHookEx(Vars.hKeybHook);

    DeleteCriticalSection(&Vars.cRoomSection);
    DeleteCriticalSection(&Vars.cMiscSection);
    DeleteCriticalSection(&Vars.cScreenhookSection);
    DeleteCriticalSection(&Vars.cPrintSection);
    DeleteCriticalSection(&Vars.cBoxHookSection);
    DeleteCriticalSection(&Vars.cFrameHookSection);
    DeleteCriticalSection(&Vars.cLineHookSection);
    DeleteCriticalSection(&Vars.cImageHookSection);
    DeleteCriticalSection(&Vars.cTextHookSection);
    DeleteCriticalSection(&Vars.cFlushCacheSection);
    DeleteCriticalSection(&Vars.cConsoleSection);
    DeleteCriticalSection(&Vars.cGameLoopSection);
    DeleteCriticalSection(&Vars.cUnitListSection);
    DeleteCriticalSection(&Vars.cFileSection);

    Log(L"D2BS Shutdown complete.");
    Vars.bNeedShutdown = false;
}
