#include "systemmemory.h"

#include <algorithm>

#if defined(Q_OS_WIN)
#include <windows.h>
#elif defined(Q_OS_MACOS)
#include <mach/mach.h>
#else
#include <QFile>
#endif

#if defined(__GLIBC__)
#include <malloc.h>
#endif

namespace SystemMemory {

qint64 availableBytes() {
#if defined(Q_OS_WIN)
  MEMORYSTATUSEX status;
  status.dwLength = sizeof(status);
  if (!GlobalMemoryStatusEx(&status))
    return -1;
  return qint64(status.ullAvailPhys);
#elif defined(Q_OS_MACOS)
  // Free, inactive and purgeable pages: what macOS hands out without
  // compressing or swapping.
  vm_statistics64_data_t stats;
  mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
  if (host_statistics64(mach_host_self(), HOST_VM_INFO64,
                        reinterpret_cast<host_info64_t>(&stats),
                        &count) != KERN_SUCCESS)
    return -1;
  return qint64(stats.free_count + stats.inactive_count +
                stats.purgeable_count) *
         qint64(vm_page_size);
#else
  // MemAvailable, not MemFree: Linux keeps most of the free memory as a
  // page cache it gives back on demand.
  // readAll(), not atEnd(): /proc files report a size of 0.
  QFile meminfo(QStringLiteral("/proc/meminfo"));
  if (!meminfo.open(QIODevice::ReadOnly | QIODevice::Text))
    return -1;
  for (const QByteArray &line : meminfo.readAll().split('\n')) {
    if (!line.startsWith("MemAvailable:"))
      continue;
    bool ok = false;
    const qint64 kb =
        line.mid(13).trimmed().split(' ').value(0).toLongLong(&ok);
    return ok ? kb * 1024 : -1;
  }
  return -1;
#endif
}

void releaseFreed() {
#if defined(__GLIBC__)
  malloc_trim(0);
#endif
}

int tasksThatFit(qint64 availableBytes, qint64 perTaskBytes, double share,
                 int maxTasks, int fallback) {
  maxTasks = std::max(1, maxTasks);
  if (availableBytes < 0 || perTaskBytes <= 0)
    return std::clamp(fallback, 1, maxTasks);
  const qint64 fit = qint64(double(availableBytes) * share) / perTaskBytes;
  return int(std::clamp<qint64>(fit, 1, maxTasks));
}

} // namespace SystemMemory
