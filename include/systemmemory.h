#pragma once

#include <QtGlobal>

/// The machine's memory, to size work that runs in parallel.
namespace SystemMemory {

/// Memory the system can give without swapping (Linux MemAvailable: free
/// memory plus the reclaimable cache), in bytes; -1 if unknown.
qint64 availableBytes();

/// Give the memory freed by the application back to the system.  glibc's
/// allocator keeps what threads free in per-thread arenas: after a
/// multi-threaded job (the PDF extraction) the process keeps it otherwise.
/// A no-op where the allocator returns memory by itself.
void releaseFreed();

/// How many tasks of `perTaskBytes` each fit in `share` of `availableBytes`,
/// between 1 and `maxTasks`.  An unknown memory (< 0) gives `fallback`
/// (also bounded by maxTasks).
int tasksThatFit(qint64 availableBytes, qint64 perTaskBytes, double share,
                 int maxTasks, int fallback);

} // namespace SystemMemory
