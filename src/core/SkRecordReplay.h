/*
 * Copyright 2019 Record Replay Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkRecordReplay_DEFINED
#define SkRecordReplay_DEFINED

#include <cstdint>

// Replay's driver, for a process that's being recorded or replayed. The driver is found by name
// where the process has it, and every function here does nothing in a process that has none.

extern void SkRecordReplayPrint(const char* format, ...);
extern void SkRecordReplayWarning(const char* format, ...);
extern void SkRecordReplayAssert(const char* format, ...);
extern void SkRecordReplayDiagnostic(const char* format, ...);
extern void SkRecordReplayRegisterPointer(const void* ptr);
extern void SkRecordReplayUnregisterPointer(const void* ptr);

extern bool SkRecordReplayFeatureEnabled(const char* feature, const char* subfeature);
extern bool SkRecordReplayIsRecordingOrReplaying(const char* feature = nullptr,
                                                 const char* subfeature = nullptr);
extern bool SkRecordReplayAreEventsDisallowed(const char* why = nullptr);

// Whether something is leaked instead of being released, where the release would happen at a
// point that isn't the same when replaying. Says so in what a crash report carries.
inline bool SkRecordReplayEnterLeakMemory(const char* label) {
    if (!SkRecordReplayIsRecordingOrReplaying("leak-references", label)) {
        return false;
    }
    SkRecordReplayDiagnostic("LeakMemory %s", label);
    return true;
}

extern void SkRecordReplayBeginPassThroughEvents();
extern void SkRecordReplayEndPassThroughEvents();
extern bool SkRecordReplayIsReplaying();
extern bool SkRecordReplayHasDivergedFromRecording();
extern bool SkRecordReplayAreEventsUnavailable(const char* why);
extern uintptr_t SkRecordReplayValue(const char* why, uintptr_t v);

#endif
