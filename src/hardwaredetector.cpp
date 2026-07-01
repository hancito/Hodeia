#include "headers/hardwaredetector.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QSet>
#include <QSysInfo>

// ── Platform headers ────────────────────────────────────────────────
#ifdef Q_OS_LINUX
#include <sys/sysinfo.h>
#include <unistd.h>
#elif defined(Q_OS_MAC)
#include <sys/sysctl.h>
#include <sys/types.h>
#elif defined(Q_OS_WIN)
#include <windows.h>
#endif

// ── Private helpers ─────────────────────────────────────────────────

static QString readSysfs(const QString &path) {
#ifdef Q_OS_LINUX
  QFile f(path);
  if (f.open(QIODevice::ReadOnly | QIODevice::Text))
    return f.readAll().trimmed();
#endif
  return QString();
}

static QString runCommand(const QString &cmd, const QStringList &args = {}) {
  QProcess p;
  p.setProgram(cmd);
  p.setArguments(args);
  p.start();
  if (p.waitForFinished(5000)) {
    QString out = QString::fromUtf8(p.readAllStandardOutput()).trimmed();
    if (!out.isEmpty())
      return out;
  }
  return QString::fromUtf8(p.readAllStandardError()).trimmed();
}

// ── CPU ─────────────────────────────────────────────────────────────

static void detectCpu(int &threads, int &cores, QString &model) {
#ifdef Q_OS_LINUX
  threads = static_cast<int>(sysconf(_SC_NPROCESSORS_ONLN));
  cores = threads; // fallback

  QFile cpuinfo("/proc/cpuinfo");
  if (cpuinfo.open(QIODevice::ReadOnly | QIODevice::Text)) {
    QString content = cpuinfo.readAll();

    auto extractField = [&](const QString &key) -> QString {
      QString lineStart = key + ":";
      int pos = content.indexOf(lineStart);
      if (pos != -1) {
        int end = content.indexOf('\n', pos);
        if (end == -1)
          end = content.size();
        return content
            .mid(pos + lineStart.length(), end - pos - lineStart.length())
            .trimmed();
      }
      return QString();
    };

    model = extractField("model name");

    // Count unique core ids per physical id
    {
      QSet<int> coreIdsPerPhys;
      int lastPhysId = -1;
      for (const auto &line : content.split('\n', Qt::SkipEmptyParts)) {
        if (line.startsWith("physical id")) {
          lastPhysId = line.section(':', 1, 1).trimmed().toInt();
        } else if (line.startsWith("core id") && lastPhysId != -1) {
          coreIdsPerPhys.insert(line.section(':', 1, 1).trimmed().toInt());
        }
      }
      if (!coreIdsPerPhys.isEmpty())
        cores = coreIdsPerPhys.size();
      else
        cores = threads / 2;
    }
  }
  if (model.isEmpty())
    model = QSysInfo::currentCpuArchitecture();

#elif defined(Q_OS_MAC)
  {
    int64_t value = 0;
    size_t len = sizeof(value);
    int mib[] = {CTL_HW, HW_NCPU};
    if (sysctl(mib, 2, &value, &len, nullptr, 0) == 0)
      threads = static_cast<int>(value);
  }
  {
    int64_t value = 0;
    size_t len = sizeof(value);
    int mib[] = {CTL_HW, HW_AVAILCPU};
    if (sysctl(mib, 2, &value, &len, nullptr, 0) == 0)
      cores = static_cast<int>(value);
  }
  if (cores == 0)
    cores = threads;

  {
    size_t len = 0;
    int mib[] = {CTL_HW, HW_MODEL};
    sysctl(mib, 2, nullptr, &len, nullptr, 0);
    if (len > 0) {
      char buf[256];
      sysctl(mib, 2, buf, &len, nullptr, 0);
      model = QString::fromUtf8(buf);
    }
  }
  if (model.isEmpty())
    model = QSysInfo::currentCpuArchitecture();

#elif defined(Q_OS_WIN)
  SYSTEM_INFO si;
  GetSystemInfo(&si);
  threads = static_cast<int>(si.dwNumberOfProcessors);
  cores = threads; // WMI needed for true physical count
  model = QString("Windows CPU");
#endif
}

// ── RAM ─────────────────────────────────────────────────────────────

static qint64 detectRamBytes() {
#ifdef Q_OS_LINUX
  {
    QString content = readSysfs("/proc/meminfo");
    for (const auto &line : content.split('\n', Qt::SkipEmptyParts)) {
      if (line.startsWith("MemTotal:")) {
        QString value = line.section(':', 1, 1).trimmed();
        bool ok = false;
        double kb = value.section(' ', 0, 0).toDouble(&ok);
        if (ok)
          return static_cast<qint64>(kb * 1024.0);
      }
    }
  }
  // Fallback: sysinfo(2)
  struct sysinfo si;
  if (sysinfo(&si) == 0)
    return static_cast<qint64>(si.totalram) * static_cast<qint64>(si.mem_unit);
  return 0;

#elif defined(Q_OS_MAC)
  int64_t mem = 0;
  size_t len = sizeof(mem);
  int mib[] = {CTL_HW, HW_MEMSIZE};
  if (sysctl(mib, 2, &mem, &len, nullptr, 0) == 0)
    return mem;
  return 0;

#elif defined(Q_OS_WIN)
  MEMORYSTATUSEX ms;
  ms.dwLength = sizeof(ms);
  if (GlobalMemoryStatusEx(&ms))
    return static_cast<qint64>(ms.ullTotalPhys);
  return 0;
#endif
}

// ── GPUs ────────────────────────────────────────────────────────────

static QList<GpuInfo> detectGpus() {
  QList<GpuInfo> gpus;

#ifdef Q_OS_LINUX
  // Strategy 1: nvidia-smi (most reliable for NVIDIA VRAM)
  QString nvidiaOut =
      runCommand("nvidia-smi", {"--query-gpu=name,memory.total",
                                "--format=csv,noheader,nounits"});
  if (!nvidiaOut.isEmpty()) {
    for (const auto &line : nvidiaOut.split('\n', Qt::SkipEmptyParts)) {
      QStringList parts = line.split(',');
      if (parts.size() >= 2) {
        GpuInfo gpu;
        gpu.name = parts[0].trimmed();
        QString memStr = parts[1].trimmed();
        if (memStr.contains("MiB")) {
          gpu.vram_bytes = static_cast<qint64>(
              memStr.replace("MiB", "").trimmed().toDouble() * 1024.0 * 1024.0);
        } else if (memStr.contains("GiB")) {
          gpu.vram_bytes = static_cast<qint64>(
              memStr.replace("GiB", "").trimmed().toDouble() * 1024.0 * 1024.0 *
              1024.0);
        }
        gpu.vendor = "nvidia";
        gpus.append(gpu);
      }
    }
  }

  // Strategy 2: /sys/class/drm for AMD / Intel
  if (gpus.isEmpty()) {
    QDir drm("/sys/class/drm");
    if (drm.exists()) {
      for (const auto &entry :
           drm.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (!entry.startsWith("card"))
          continue;
        QString devicePath = drm.absolutePath() + "/" + entry + "/device";
        QString vendorId = readSysfs(devicePath + "/vendor");
        QString deviceId = readSysfs(devicePath + "/device");

        QString vendor;
        if (vendorId.contains("0x10de")) {
          vendor = "nvidia";
        } else if (vendorId.contains("0x1002")) {
          vendor = "amd";
        } else if (vendorId.contains("0x8086")) {
          vendor = "intel";
        } else {
          vendor = "unknown";
        }

        GpuInfo gpu;
        gpu.name = vendor.toUpper() + " GPU (" + deviceId + ")";
        gpu.vram_bytes = 0;
        gpu.vendor = vendor;
        gpus.append(gpu);
      }
    }
  }

#elif defined(Q_OS_MAC)
  // Apple Silicon – unified memory, GPU is part of the SoC
  {
    QString spOut = runCommand("system_profiler", {"SPDisplaysDataType"});
    GpuInfo gpu;
    gpu.vendor = "apple";
    gpu.vram_bytes = detectRamBytes(); // shared unified memory
    if (!spOut.isEmpty()) {
      for (const auto &line : spOut.split('\n')) {
        if (line.contains("Chipset Model:") || line.contains("GPU Name:")) {
          gpu.name = line.section(':', 1, 1).trimmed();
          break;
        }
      }
    }
    if (gpu.name.isEmpty())
      gpu.name = "Apple Silicon GPU";
    gpus.append(gpu);
  }

#elif defined(Q_OS_WIN)
  // PowerShell WMI query
  QString psOut =
      runCommand("powershell.exe", {"-NoProfile", "-Command",
                                    "Get-CimInstance Win32_VideoController | "
                                    "ForEach-Object { '{0},{1}' -f "
                                    "($_.Name),($_.AdapterRAM) }"});
  if (!psOut.isEmpty()) {
    for (const auto &line : psOut.split('\n', Qt::SkipEmptyParts)) {
      int comma = line.indexOf(',');
      if (comma > 0) {
        GpuInfo gpu;
        gpu.name = line.left(comma);
        gpu.vram_bytes = line.mid(comma + 1).trimmed().toLongLong();
        QString nl = gpu.name.toLower();
        if (nl.contains("nvidia"))
          gpu.vendor = "nvidia";
        else if (nl.contains("amd") || nl.contains("radeon"))
          gpu.vendor = "amd";
        else if (nl.contains("intel"))
          gpu.vendor = "intel";
        else
          gpu.vendor = "unknown";
        gpus.append(gpu);
      }
    }
  }
#endif

  return gpus;
}

// ── NPU ─────────────────────────────────────────────────────────────

static bool detectNpu() {
#ifdef Q_OS_LINUX
  QDir npuDir("/sys/bus/msm_device");
  if (npuDir.exists())
    return true;
  return false;
#elif defined(Q_OS_MAC)
  // Apple Silicon has Neural Engine
  return true;
#elif defined(Q_OS_WIN)
  QString out = runCommand("powershell.exe",
                           {"-NoProfile", "-Command",
                            "Get-CimInstance Win32_PnPEntity | "
                            "Where-Object { $_.Name -match 'NPU|Neural' }"});
  return !out.isEmpty();
#endif
  return false;
}

// ── Public entry point ──────────────────────────────────────────────

HardwareInfo detectHardware() {
  HardwareInfo info;
  detectCpu(info.cpu_threads, info.cpu_cores, info.cpu_model);
  info.total_ram_bytes = detectRamBytes();
  info.gpus = detectGpus();
  info.has_npu = detectNpu();
  info.detection_time = QDateTime::currentDateTime().toString(Qt::ISODate);
  return info;
}
