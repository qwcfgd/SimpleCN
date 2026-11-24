#pragma once
#include <QCoreApplication>
#include <QDir>
#include <QString>
#include <QMutex>
#include <QMutexLocker>
#include <QSet>
#include <windows.h>
namespace communication {
inline HINSTANCE loadPeakLibrary(const QString &name, QString &path, DWORD &error, bool keepLoadedUntilExit=false)
{
    path = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath("dll/" + name);
    HINSTANCE dll = LoadLibraryExW(reinterpret_cast<LPCWSTR>(path.utf16()), nullptr,
                                  LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!dll) { error=GetLastError(); return nullptr; }
    if (keepLoadedUntilExit) {
        // PLIN-API 3.1.x raises STATUS_INVALID_HANDLE during dynamic DLL detach under
        // a Windows debugger. Keep its module mapped until process exit. Driver
        // clients, schedules and hardware connections are still released normally.
        static QMutex mutex;
        static QSet<QString> pinnedPaths;
        QMutexLocker lock(&mutex);
        if (!pinnedPaths.contains(path)) {
            HMODULE pinned=nullptr;
            if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN,
                                   reinterpret_cast<LPCWSTR>(path.utf16()),&pinned)) {
                error=GetLastError(); FreeLibrary(dll); return nullptr;
            }
            pinnedPaths.insert(path);
        }
    }
    error=ERROR_SUCCESS;
    return dll;
}
}
