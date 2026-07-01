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
      for (const auto &line : content.split('\n', Qt::SkipEmptyParts)) {
        // /proc/cpuinfo uses "key\t: value" — split on ':' to be robust
        QString beforeColon = line.section(':', 0, 0).trimmed();
        if (beforeColon == key) {
          return line.section(':', 1).trimmed();
        }
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
  // Check system-wide Vulkan availability first
  bool systemHasVulkan = false;
  {
    QProcess vkTest;
    vkTest.setProgram("vulkaninfo");
    vkTest.setArguments({"--summary"});
    vkTest.start();
    if (vkTest.waitForFinished(5000)) {
      QString out = QString::fromUtf8(vkTest.readAllStandardOutput()).trimmed();
      systemHasVulkan = (vkTest.exitCode() == 0 && !out.isEmpty());
    }
  }

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
        gpu.has_cuda = true;
        gpu.has_vulkan = systemHasVulkan;
        gpus.append(gpu);
      }
    }
  }

  // Strategy 2: /sys/class/drm for AMD / Intel (non-NVIDIA)
  {
    QDir drm("/sys/class/drm");
    if (drm.exists()) {
      QSet<QString> seenDevices;
      for (const auto &entry :
           drm.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (!entry.startsWith("card") || entry.mid(4).isEmpty() ||
            !entry.mid(4, 1).at(0).isDigit())
          continue;
        QString devicePath = drm.absolutePath() + "/" + entry + "/device";
        QString vendorId = readSysfs(devicePath + "/vendor");
        QString deviceId = readSysfs(devicePath + "/device");

        QString pciKey = vendorId + ":" + deviceId;
        if (seenDevices.contains(pciKey))
          continue;
        seenDevices.insert(pciKey);

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

        // Skip if already reported by nvidia-smi
        bool alreadyReported = false;
        for (const auto &existing : gpus) {
          if (existing.vendor == vendor) {
            alreadyReported = true;
            break;
          }
        }
        if (alreadyReported)
          continue;

        GpuInfo gpu;
        gpu.name = vendor.toUpper() + " GPU (" + deviceId + ")";
        gpu.vram_bytes = 0;
        gpu.vendor = vendor;
        gpu.has_vulkan = systemHasVulkan;
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
    gpu.vram_bytes = detectRamBytes();
    gpu.has_metal = true;
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
        if (nl.contains("nvidia")) {
          gpu.vendor = "nvidia";
          gpu.has_cuda = true;
        } else if (nl.contains("amd") || nl.contains("radeon")) {
          gpu.vendor = "amd";
        } else if (nl.contains("intel")) {
          gpu.vendor = "intel";
        } else {
          gpu.vendor = "unknown";
        }
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

// ── NUMA ────────────────────────────────────────────────────────────

static QList<NumaNodeInfo> detectNuma() {
  QList<NumaNodeInfo> nodes;

#ifdef Q_OS_LINUX
  QDir nodeDir("/sys/devices/system/node");
  if (!nodeDir.exists())
    return nodes; // no NUMA exposed by kernel

  for (const auto &entry :
       nodeDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
    if (!entry.startsWith("node"))
      continue;

    int nodeId = entry.mid(4).toInt();
    QString nodePath = nodeDir.absolutePath() + "/" + entry;

    NumaNodeInfo node;
    node.node_id = nodeId;

    // cpulist
    QString cpulist = readSysfs(nodePath + "/cpulist");
    if (!cpulist.isEmpty()) {
      // e.g. "0-15,32-47" or "0"
      for (const auto &range : cpulist.split(',')) {
        if (range.contains('-')) {
          QStringList bounds = range.split('-');
          int start = bounds[0].toInt();
          int end = bounds[1].toInt();
          for (int c = start; c <= end; ++c)
            node.cpus.append(c);
        } else {
          node.cpus.append(range.toInt());
        }
      }
    }

    // memory (in kB)
    QString memStr = readSysfs(nodePath + "/meminfo");
    for (const auto &line : memStr.split('\n', Qt::SkipEmptyParts)) {
      if (line.startsWith("NodeMemTotal:")) {
        QString value = line.section(':', 1, 1).trimmed();
        bool ok = false;
        double kb = value.section(' ', 0, 0).toDouble(&ok);
        if (ok)
          node.memory_bytes = static_cast<qint64>(kb * 1024.0);
        break;
      }
    }

    nodes.append(node);
  }

#elif defined(Q_OS_MAC)
  // macOS treats all memory as unified — single NUMA node
  NumaNodeInfo node;
  node.node_id = 0;
  int nc = static_cast<int>(sysconf(_SC_NPROCESSORS_ONLN));
  for (int c = 0; c < nc; ++c)
    node.cpus.append(c);
  node.memory_bytes = detectRamBytes();
  nodes.append(node);

#elif defined(Q_OS_WIN)
  // Windows: query via PowerShell / WMI
  QString psOut = runCommand("powershell.exe",
                             {"-NoProfile", "-Command",
                              "Get-CimInstance Win32_NUMANode | "
                              "ForEach-Object { '{0},{1},{2}' -f "
                              "$_.NodeNumber,$_.Memory,$_.ProcessorMask }"});
  if (!psOut.isEmpty()) {
    for (const auto &line : psOut.split('\n', Qt::SkipEmptyParts)) {
      QStringList parts = line.split(',');
      if (parts.size() >= 3) {
        NumaNodeInfo node;
        node.node_id = parts[0].toInt();
        node.memory_bytes = parts[1].toLongLong() * 1024; // WMI returns kB

        // ProcessorMask is a bitmask (decimal string)
        QString maskStr = parts[2].trimmed();
        quint64 mask = maskStr.toULongLong();
        int bit = 0;
        while (mask > 0) {
          if (mask & 1)
            node.cpus.append(bit);
          mask >>= 1;
          ++bit;
        }
        nodes.append(node);
      }
    }
  }
#endif

  return nodes;
}

// ── Public entry point ──────────────────────────────────────────────

HardwareInfo detectHardware() {
  HardwareInfo info;
  detectCpu(info.cpu_threads, info.cpu_cores, info.cpu_model);
  info.total_ram_bytes = detectRamBytes();
  info.gpus = detectGpus();
  info.has_npu = detectNpu();
  info.numa_nodes = detectNuma();
  info.detection_time = QDateTime::currentDateTime().toString(Qt::ISODate);
  return info;
}
