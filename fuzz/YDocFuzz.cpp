// libFuzzer harness for the sync document reader (src/sync):
// arbitrary bytes as a Yjs update must be refused or read, and read
// documents mapped onto a layout and written back, or refused, never crash.
// The same bytes cut in two (where the first byte says) are also applied
// one after the other, as a document and then an update to it.
// Seed: fixtures/sync/*.ydoc.

#include "DocJson.h"
#include "SyncDoc.h"
#include "WebModel.h"
#include "YDocSummary.h"

#include "core/Map.h"

#include <cstddef>
#include <cstdint>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    const QByteArray update(reinterpret_cast<const char*>(data), static_cast<qsizetype>(size));
    (void)bld::sync::summarizeDoc(update);
    if (size > 1) {
        const qsizetype cut = 1 + data[0] % static_cast<qsizetype>(size - 1);
        bld::sync::SyncDoc twoStep;
        (void)twoStep.applyUpdate(update.mid(1, cut - 1));
        (void)twoStep.applyUpdate(update.mid(cut));
    }
    bld::sync::SyncDoc doc;
    if (!doc.applyUpdate(update)) return 0;
    // Map what was read, then write it back: the writer compares against
    // whatever odd shapes the document holds.
    if (const auto map = bld::sync::mapFromDocJson(doc.toJson())) (void)doc.writeMap(*map);
    return 0;
}
