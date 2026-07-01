#pragma once
#include <QList>
#include <QString>

/// Describes a single GPU discovered on the system.
struct GpuInfo {
  QString name;
  qint64 vram_bytes = 0;
  QString vendor; // "nvidia" | "amd" | "intel" | "apple" | "unknown"
};

/// Flat snapshot of hardware capabilities relevant for LLM inference.
struct HardwareInfo {
  int cpu_threads = 0;
  int cpu_cores = 0;
  QString cpu_model;
  qint64 total_ram_bytes = 0;
  QList<GpuInfo> gpus;
  bool has_npu = false;
  QString detection_time;
};

/// Populate `info` by probing the host system.
/// Cross-platform: Linux, macOS, Windows.
HardwareInfo detectHardware();
