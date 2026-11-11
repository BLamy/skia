/*
 * Copyright 2015 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/private/SkSemaphore.h"

#include "include/private/SkFeatures.h" // IWYU pragma: keep
#include "include/private/SkMutex.h"

#if defined(SK_BUILD_FOR_MAC) || defined(SK_BUILD_FOR_UNIX)
#include <dlfcn.h>

#include <atomic>

namespace {

// Replay's driver, found by name: it's loaded before this library in a process that's recorded,
// and the engine answers for it in one that's replayed. Any other process has none.
struct ReplayDriver {
    int (*fCreateOrderedLock)(const char*);
    void (*fOrderedLock)(int);
    void (*fOrderedUnlock)(int);
    bool (*fAreEventsDisallowed)();
    bool (*fAreEventsPassedThrough)();
};
ReplayDriver gReplayDriver;
// 0 before the first look, 1 when there's no driver, 2 when there is.
std::atomic<int> gReplayDriverState{0};

bool has_replay_driver() {
    int state = gReplayDriverState.load(std::memory_order_acquire);
    if (state == 0) {
        ReplayDriver driver;
        driver.fCreateOrderedLock = reinterpret_cast<int (*)(const char*)>(
                dlsym(RTLD_DEFAULT, "RecordReplayCreateOrderedLock"));
        driver.fOrderedLock =
                reinterpret_cast<void (*)(int)>(dlsym(RTLD_DEFAULT, "RecordReplayOrderedLock"));
        driver.fOrderedUnlock =
                reinterpret_cast<void (*)(int)>(dlsym(RTLD_DEFAULT, "RecordReplayOrderedUnlock"));
        driver.fAreEventsDisallowed = reinterpret_cast<bool (*)()>(
                dlsym(RTLD_DEFAULT, "RecordReplayAreEventsDisallowed"));
        driver.fAreEventsPassedThrough = reinterpret_cast<bool (*)()>(
                dlsym(RTLD_DEFAULT, "RecordReplayAreEventsPassedThrough"));
        bool found = dlsym(RTLD_DEFAULT, "RecordReplayAttach") && driver.fCreateOrderedLock &&
                     driver.fOrderedLock && driver.fOrderedUnlock &&
                     driver.fAreEventsDisallowed && driver.fAreEventsPassedThrough;
        // Every thread that gets here finds the same thing, so whichever writes last is right.
        if (found) {
            gReplayDriver = driver;
        }
        state = found ? 2 : 1;
        gReplayDriverState.store(state, std::memory_order_release);
    }
    return state == 2;
}

}  // namespace

bool SkRecordReplayIsRecordingOrReplaying() { return has_replay_driver(); }

int SkRecordReplayNewMutexId() {
    if (!has_replay_driver() || gReplayDriver.fAreEventsDisallowed() ||
        gReplayDriver.fAreEventsPassedThrough()) {
        return 0;
    }
    return gReplayDriver.fCreateOrderedLock("SkMutex");
}

void SkRecordReplayOrderedLock(int lock) { gReplayDriver.fOrderedLock(lock); }

void SkRecordReplayOrderedUnlock(int lock) { gReplayDriver.fOrderedUnlock(lock); }
#else
bool SkRecordReplayIsRecordingOrReplaying() { return false; }
int SkRecordReplayNewMutexId() { return 0; }
void SkRecordReplayOrderedLock(int) {}
void SkRecordReplayOrderedUnlock(int) {}
#endif

#if defined(SK_BUILD_FOR_MAC) || defined(SK_BUILD_FOR_IOS)
    #include <dispatch/dispatch.h>

    struct SkSemaphore::OSSemaphore {
        dispatch_semaphore_t fSemaphore;

        OSSemaphore()  { fSemaphore = dispatch_semaphore_create(0/*initial count*/); }
        ~OSSemaphore() { dispatch_release(fSemaphore); }

        void signal(int n) { while (n --> 0) { dispatch_semaphore_signal(fSemaphore); } }
        void wait() { dispatch_semaphore_wait(fSemaphore, DISPATCH_TIME_FOREVER); }
    };
#elif defined(SK_BUILD_FOR_WIN)
#include "src/core/SkLeanWindows.h"

    struct SkSemaphore::OSSemaphore {
        HANDLE fSemaphore;

        OSSemaphore()  {
            fSemaphore = CreateSemaphore(nullptr    /*security attributes, optional*/,
                                         0       /*initial count*/,
                                         MAXLONG /*max count*/,
                                         nullptr    /*name, optional*/);
        }
        ~OSSemaphore() { CloseHandle(fSemaphore); }

        void signal(int n) {
            ReleaseSemaphore(fSemaphore, n, nullptr/*returns previous count, optional*/);
        }
        void wait() { WaitForSingleObject(fSemaphore, INFINITE/*timeout in ms*/); }
    };
#else
    // It's important we test for Mach before this.  This code will compile but not work there.
    #include <errno.h>
    #include <semaphore.h>
    struct SkSemaphore::OSSemaphore {
        sem_t fSemaphore;

        OSSemaphore()  { sem_init(&fSemaphore, 0/*cross process?*/, 0/*initial count*/); }
        ~OSSemaphore() { sem_destroy(&fSemaphore); }

        void signal(int n) { while (n --> 0) { sem_post(&fSemaphore); } }
        void wait() {
            // Try until we're not interrupted.
            while(sem_wait(&fSemaphore) == -1 && errno == EINTR);
        }
    };
#endif

///////////////////////////////////////////////////////////////////////////////

SkSemaphore::~SkSemaphore() {
    delete fOSSemaphore;
}

void SkSemaphore::osSignal(int n) {
    fOSSemaphoreOnce([this] { fOSSemaphore = new OSSemaphore; });
    fOSSemaphore->signal(n);
}

void SkSemaphore::osWait() {
    fOSSemaphoreOnce([this] { fOSSemaphore = new OSSemaphore; });
    fOSSemaphore->wait();
}

bool SkSemaphore::try_wait() {
    int count = fCount.load(std::memory_order_relaxed);
    if (count > 0) {
        return fCount.compare_exchange_weak(count, count-1, std::memory_order_acquire);
    }
    return false;
}
