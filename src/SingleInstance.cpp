#include "SingleInstance.h"

#if defined(_WIN32)
#include <windows.h>
static HANDLE g_mutex = NULL;
SingleInstanceGuard::SingleInstanceGuard() {
    g_mutex = CreateMutexA(NULL, TRUE, "MotionCamPlayerSingletonMutex");
    if (!g_mutex || GetLastError() == ERROR_ALREADY_EXISTS) {
        acquired = false;
        if (g_mutex) {
            CloseHandle(g_mutex);
            g_mutex = NULL;
        }
    } else {
        acquired = true;
    }
}
SingleInstanceGuard::~SingleInstanceGuard() {
    if (g_mutex) {
        ReleaseMutex(g_mutex);
        CloseHandle(g_mutex);
        g_mutex = NULL;
    }
}

#elif defined(__APPLE__) || defined(__linux__)
#include <fcntl.h>
#include <unistd.h>
#include <sys/file.h>
static int g_lock_fd = -1;
SingleInstanceGuard::SingleInstanceGuard() {
    const char* lockPath = "/tmp/motioncam_player.lock";
    g_lock_fd = open(lockPath, O_RDWR | O_CREAT, 0666);
    if (g_lock_fd == -1) {
        acquired = false;
    } else if (flock(g_lock_fd, LOCK_EX | LOCK_NB) == -1) {
        close(g_lock_fd);
        g_lock_fd = -1;
        acquired = false;
    } else {
        acquired = true;
    }
}
SingleInstanceGuard::~SingleInstanceGuard() {
    if (g_lock_fd != -1) {
        flock(g_lock_fd, LOCK_UN);
        close(g_lock_fd);
        g_lock_fd = -1;
    }
}
#else
SingleInstanceGuard::SingleInstanceGuard() { acquired = true; }
SingleInstanceGuard::~SingleInstanceGuard() {}
#endif
