/*
 * Copyright 2019 Record Replay Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/core/SkRecordReplay.h"

#include "include/private/SkOnce.h"

#ifndef _WIN32
#include <dlfcn.h>
#else
#include <windows.h>
#endif

#include <stdarg.h>
#include <string.h>

static bool gRecordingOrReplaying;
static bool gHasDisabledFeatures;

static void (*gRecordReplayPrint)(const char* format, va_list args);
static void (*gRecordReplayWarning)(const char* format, va_list args);
static void (*gRecordReplayAssert)(const char*, va_list);
static void (*gRecordReplayDiagnostic)(const char*, va_list);
static void (*gRecordReplayRegisterPointer)(const void* ptr);
static void (*gRecordReplayUnregisterPointer)(const void* ptr);

static bool (*gRecordReplayHasDisabledFeatures)();
static bool (*gRecordReplayFeatureEnabled)(const char* feature, const char* subfeature);
static bool (*gRecordReplayAreEventsDisallowed)();
static void (*gRecordReplayBeginPassThroughEvents)();
static void (*gRecordReplayEndPassThroughEvents)();
static bool (*gRecordReplayIsReplaying)(void);
static bool (*gRecordReplayHasDivergedFromRecording)(void);
static uintptr_t (*gRecordReplayValue)(const char* why, uintptr_t v);

template <typename T>
static void RecordReplayLoadSymbol(const char* name, T& function) {
#ifndef _WIN32
    void* sym = dlsym(RTLD_DEFAULT, name);
#else
    HMODULE module = GetModuleHandleA("windows-recordreplay.dll");
    void* sym = module ? (void*)GetProcAddress(module, name) : nullptr;
#endif
    if (sym) {
        static_assert(sizeof(T) == sizeof(void*), "bad size");
        memcpy((void*)&function, (const void*)&sym, sizeof(void*));
    }
}

// Whether the process has a driver.
static inline bool EnsureInitialized() {
    static SkOnce once;
    once([] {
        RecordReplayLoadSymbol("RecordReplayPrint", gRecordReplayPrint);
        RecordReplayLoadSymbol("RecordReplayWarning", gRecordReplayWarning);
        RecordReplayLoadSymbol("RecordReplayAssert", gRecordReplayAssert);
        RecordReplayLoadSymbol("RecordReplayDiagnostic", gRecordReplayDiagnostic);
        RecordReplayLoadSymbol("RecordReplayRegisterPointer", gRecordReplayRegisterPointer);
        RecordReplayLoadSymbol("RecordReplayUnregisterPointer", gRecordReplayUnregisterPointer);

        RecordReplayLoadSymbol("RecordReplayHasDisabledFeatures",
                               gRecordReplayHasDisabledFeatures);
        RecordReplayLoadSymbol("RecordReplayFeatureEnabled", gRecordReplayFeatureEnabled);
        RecordReplayLoadSymbol("RecordReplayAreEventsDisallowed",
                               gRecordReplayAreEventsDisallowed);
        RecordReplayLoadSymbol("RecordReplayBeginPassThroughEvents",
                               gRecordReplayBeginPassThroughEvents);
        RecordReplayLoadSymbol("RecordReplayEndPassThroughEvents",
                               gRecordReplayEndPassThroughEvents);
        RecordReplayLoadSymbol("RecordReplayIsReplaying", gRecordReplayIsReplaying);
        RecordReplayLoadSymbol("RecordReplayHasDivergedFromRecording",
                               gRecordReplayHasDivergedFromRecording);
        RecordReplayLoadSymbol("RecordReplayValue", gRecordReplayValue);

        // The driver is in a process that's being recorded, loaded before anything else, and
        // the engine answers for it in one that's being replayed.
        void* attach = nullptr;
        RecordReplayLoadSymbol("RecordReplayAttach", attach);
        gRecordingOrReplaying = attach && gRecordReplayAssert;
        gHasDisabledFeatures = gRecordingOrReplaying && gRecordReplayHasDisabledFeatures &&
                               gRecordReplayHasDisabledFeatures();
    });
    return gRecordingOrReplaying;
}

void SkRecordReplayPrint(const char* format, ...) {
    if (SkRecordReplayIsRecordingOrReplaying() && gRecordReplayPrint) {
        va_list args;
        va_start(args, format);
        gRecordReplayPrint(format, args);
        va_end(args);
    }
}

void SkRecordReplayWarning(const char* format, ...) {
    if (EnsureInitialized() && gRecordReplayWarning) {
        va_list ap;
        va_start(ap, format);
        gRecordReplayWarning(format, ap);
        va_end(ap);
    }
}

void SkRecordReplayAssert(const char* format, ...) {
    if (EnsureInitialized()) {
        va_list ap;
        va_start(ap, format);
        gRecordReplayAssert(format, ap);
        va_end(ap);
    }
}

void SkRecordReplayDiagnostic(const char* format, ...) {
    if (EnsureInitialized() && gRecordReplayDiagnostic) {
        va_list ap;
        va_start(ap, format);
        gRecordReplayDiagnostic(format, ap);
        va_end(ap);
    }
}

void SkRecordReplayRegisterPointer(const void* ptr) {
    if (EnsureInitialized() && gRecordReplayRegisterPointer) {
        gRecordReplayRegisterPointer(ptr);
    }
}

void SkRecordReplayUnregisterPointer(const void* ptr) {
    if (EnsureInitialized() && gRecordReplayUnregisterPointer) {
        gRecordReplayUnregisterPointer(ptr);
    }
}

bool SkRecordReplayFeatureEnabled(const char* feature, const char* subfeature) {
    if (EnsureInitialized()) {
        if (!gHasDisabledFeatures) {
            return true;
        }
        return gRecordReplayFeatureEnabled(feature, subfeature);
    }
    return true;
}

bool SkRecordReplayIsRecordingOrReplaying(const char* feature, const char* subfeature) {
    return EnsureInitialized() && gRecordingOrReplaying &&
           (!feature || SkRecordReplayFeatureEnabled(feature, subfeature));
}

bool SkRecordReplayAreEventsDisallowed(const char* why) {
    if (EnsureInitialized() && gRecordReplayAreEventsDisallowed) {
        if (SkRecordReplayIsRecordingOrReplaying("disallow-events", why)) {
            return gRecordReplayAreEventsDisallowed();
        }
    }
    return false;
}

void SkRecordReplayBeginPassThroughEvents() {
    if (EnsureInitialized() && gRecordReplayBeginPassThroughEvents) {
        gRecordReplayBeginPassThroughEvents();
    }
}

void SkRecordReplayEndPassThroughEvents() {
    if (EnsureInitialized() && gRecordReplayEndPassThroughEvents) {
        gRecordReplayEndPassThroughEvents();
    }
}

bool SkRecordReplayIsReplaying(void) {
    return EnsureInitialized() && gRecordReplayIsReplaying && gRecordReplayIsReplaying();
}

bool SkRecordReplayHasDivergedFromRecording(void) {
    return SkRecordReplayIsReplaying() && gRecordReplayHasDivergedFromRecording &&
           gRecordReplayHasDivergedFromRecording();
}

bool SkRecordReplayAreEventsUnavailable(const char* why) {
    return SkRecordReplayAreEventsDisallowed(why) || SkRecordReplayHasDivergedFromRecording();
}

uintptr_t SkRecordReplayValue(const char* why, uintptr_t v) {
    if (SkRecordReplayIsRecordingOrReplaying("values", why) && gRecordReplayValue) {
        return gRecordReplayValue(why, v);
    }
    return v;
}
