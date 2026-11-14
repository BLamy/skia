/*
 * Copyright 2011 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <algorithm>
#include "src/core/SkRecordReplay.h"
#include "src/core/SkTypefaceCache.h"

#include "include/core/SkFontStyle.h"
#include "include/core/SkGraphics.h"
#include "include/core/SkString.h"
#include "include/private/SkAssert.h"
#include "include/private/SkDebug.h"
#include "include/private/SkMutex.h"

#include <atomic>
#include <cstdint>
#include <utility>

SkTypefaceCache::SkTypefaceCache() {}

void SkTypefaceCache::add(sk_sp<SkTypeface> face) {
    SkASSERT_RELEASE(face);
    const auto limit = SkGraphics::GetTypefaceCacheCountLimit();

    if (fTypefaces.size() >= limit) {
        this->purge(limit >> 2);
    }
    if (limit > 0) {
        fTypefaces.emplace_back(std::move(face));
    }
}

sk_sp<SkTypeface> SkTypefaceCache::findByProcAndRef(FindProc proc, void* ctx) const {
    SkRecordReplayAssert("[RUN-2612-2639] SkTypefaceCache::findByProcAndRef A %d", (int)fTypefaces.size());
    for (const sk_sp<SkTypeface>& typeface : fTypefaces) {
        if (proc(typeface.get(), ctx)) {
            SkRecordReplayAssert("[RUN-2612-2639] SkTypefaceCache::findByProcAndRef B %u",
                                 typeface->uniqueID());
            return typeface;
        }
    }
    return nullptr;
}

void SkTypefaceCache::purge(int numToPurge) {
    int count = fTypefaces.size();
    int i = 0;
    SkRecordReplayAssert("[RUN-2612-2639] SkTypefaceCache::purge %d %d", count, numToPurge);
    // Replay: whether the cache holds the only reference to a typeface depends on what the
    // collector has freed, in a Chromium whose font data is collected (108's was reference
    // counted), and that isn't the same on a replay. The cache then differs, and with it whether a
    // font finds its typeface here or has one made, which asks the system. So the oldest entries
    // go, whoever else holds them: a typeface still in use is only made again if asked for.
    if (SkRecordReplayIsRecordingOrReplaying()) {
        int purged = std::min(numToPurge, count);
        if (purged <= 0) {
            return;
        }
        skia_private::TArray<sk_sp<SkTypeface>> kept;
        kept.reserve(count - purged);
        for (int j = purged; j < count; j++) {
            kept.push_back(std::move(fTypefaces[j]));
        }
        fTypefaces = std::move(kept);
        return;
    }
    while (i < count) {
        if (fTypefaces[i]->unique()) {
            fTypefaces.removeShuffle(i);
            --count;
            if (--numToPurge == 0) {
                return;
            }
        } else {
            ++i;
        }
    }
}

void SkTypefaceCache::purgeAll() {
    this->purge(fTypefaces.size());
}

///////////////////////////////////////////////////////////////////////////////

SkTypefaceCache& SkTypefaceCache::Get() {
    static SkTypefaceCache gCache;
    return gCache;
}

SkTypefaceID SkTypefaceCache::NewTypefaceID() {
    static std::atomic<int32_t> nextID{1};
    return nextID.fetch_add(1, std::memory_order_relaxed);
}

static SkMutex& typeface_cache_mutex() {
    static SkMutex& mutex = *(new SkMutex);
    return mutex;
}

void SkRecordReplayInitTypefaceCache() { (void)typeface_cache_mutex(); }

void SkTypefaceCache::Add(sk_sp<SkTypeface> face) {
    SkAutoMutexExclusive ama(typeface_cache_mutex());
    Get().add(std::move(face));
}

sk_sp<SkTypeface> SkTypefaceCache::FindByProcAndRef(FindProc proc, void* ctx) {
    SkAutoMutexExclusive ama(typeface_cache_mutex());
    return Get().findByProcAndRef(proc, ctx);
}

void SkTypefaceCache::PurgeAll() {
    SkAutoMutexExclusive ama(typeface_cache_mutex());
    Get().purgeAll();
}

///////////////////////////////////////////////////////////////////////////////

#ifdef SK_DEBUG
static bool DumpProc(SkTypeface* face, void* ctx) {
    SkString n;
    face->getFamilyName(&n);
    SkFontStyle s = face->fontStyle();
    SkTypefaceID id = face->uniqueID();
    SkDebugf("SkTypefaceCache: face %p typefaceID %u weight %d width %d style %d name %s\n",
             face, id, s.weight(), s.width(), s.slant(), n.c_str());
    return false;
}
#endif

void SkTypefaceCache::Dump() {
#ifdef SK_DEBUG
    (void)Get().findByProcAndRef(DumpProc, nullptr);
#endif
}
