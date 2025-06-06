#pragma once

#ifdef _WIN32
#include <windows.h> // For HANDLE, CreateMutexW, ERROR_ALREADY_EXISTS, CloseHandle, DWORD

// Ensures that only one instance of the application can run at a time on Windows.
// Uses a named mutex to detect other instances.
class SingleInstanceGuard {
public:
    // Attempts to create a named mutex.
    // `name`: A unique name for the mutex.
    explicit SingleInstanceGuard(const wchar_t* name)
        : _mutex(CreateMutexW(nullptr, TRUE, name)) { // Attempt to create/own the mutex
        _lastError = GetLastError(); // Store error immediately
        _alreadyRunning = (_lastError == ERROR_ALREADY_EXISTS);
    }

    // Releases the mutex handle.
    ~SingleInstanceGuard() {
        if (_mutex) {
            // If this instance created the mutex (i.e., it wasn't already running),
            // it should release ownership before closing the handle.
            // If it just opened an existing mutex, ReleaseMutex is not strictly necessary
            // as it didn't increment the ownership count beyond the initial open.
            // However, a single ReleaseMutex is safe if it's the owner.
            if (!_alreadyRunning) { // Only release if we successfully created and owned it
                ReleaseMutex(_mutex);
            }
            CloseHandle(_mutex);
            _mutex = nullptr;
        }
    }

    // Returns true if another instance of the application is already running.
    bool alreadyRunning() const {
        return _alreadyRunning;
    }

    // ADD THESE METHODS for debugging
    HANDLE getMutexHandle() const { return _mutex; }
    DWORD getLastErrorAfterCreation() const { return _lastError; } // Renamed for clarity

private:
    SingleInstanceGuard(const SingleInstanceGuard&) = delete;
    SingleInstanceGuard& operator=(const SingleInstanceGuard&) = delete;

    HANDLE _mutex{};
    bool   _alreadyRunning{};
    DWORD  _lastError{}; // Stores result of GetLastError() after CreateMutexW
};

#endif // _WIN32