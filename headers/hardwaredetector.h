#pragma once
#include <QList>
#include <QSet>
#include <QString>

/// Describes a single GPU discovered on the system.
struct GpuInfo {
  QString name;
  qint64 vram_bytes = 0;
  QString vendor; // "nvidia" | "amd" | "intel" | "apple" | "unknown"
  bool has_vulkan = false;
  bool has_metal = false;
  bool has_cuda = false;
};

/// Describes a single NUMA node.
struct NumaNodeInfo {
  int node_id = -1;
  QList<int> cpus;         // logical thread IDs bound to this node
  qint64 memory_bytes = 0; // local memory on this node
};

/// Flat snapshot of hardware capabilities relevant for LLM inference.
struct HardwareInfo {
  int cpu_threads = 0;
  int cpu_cores = 0;
  QString cpu_model;
  qint64 total_ram_bytes = 0;
  QList<GpuInfo> gpus;
  bool has_npu = false;
  QList<NumaNodeInfo> numa_nodes;
  QString detection_time;
};

/// Populate `info` by probing the host system.
/// Cross-platform: Linux, macOS, Windows.
HardwareInfo detectHardware();
