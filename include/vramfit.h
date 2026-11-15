#pragma once

#include "localprompt.h"

#include <QString>
#include <cstdint>
#include <vector>

struct common_params;
struct ggml_backend_device;

/// GPU memory fitting for the local llama.cpp backend: probe a model without
/// allocating it, estimate what each configuration needs (weights, KV cache,
/// compute buffers, projector) and pick the parameters that fit the GPU.
/// Pure decision code — nothing here loads a model for inference.
/// Measurements behind the estimates: docs/NOTES-VRAM-FITTING.md.
namespace VramFit {

/// KV cache shape of one model layer.  k_dim / v_dim = n_head_kv × head
/// size; 0 for a layer without its own cache (recurrent layer of a hybrid
/// model, or a layer reusing another layer's KV).
struct KvLayer {
  uint32_t k_dim = 0;
  uint32_t v_dim = 0;
  bool swa = false; ///< sliding-window layer: holds at most n_swa tokens
};

/// What the no-alloc probe learns about a model: structure for the memory
/// estimate, exact prompt token count, projector size.
struct ProbeInfo {
  int32_t n_layers = 0;
  int32_t n_embd = 0;
  int32_t n_head = 0;
  std::vector<KvLayer> kv_layers; // one per model layer
  uint32_t n_swa = 0;              // sliding window size (0: none)
  int32_t n_ctx_train = 0;
  uint64_t model_size = 0;
  uint64_t mmproj_size = 0;   // mmproj weights + compute, all devices
  bool has_mmproj = false;
  bool is_hybrid = false;     // recurrent + attention layers
  uint32_t prompt_tokens = 0; // text + image tokens
  uint32_t image_tokens = 0;  // image share of prompt_tokens (exact or estimate)
};

/// Per-device memory snapshot.
struct DeviceMem {
  ggml_backend_device *dev = nullptr;
  QString name;
  QString description;
  uint64_t memory_free = 0;
  uint64_t memory_total = 0;
  bool is_gpu = false;
  bool is_integrated = false; // true for iGPU / shared-memory GPU
};

/// Flash attention for hybrid models (recurrent + attention).  They used to
/// be kept off it (older llama.cpp, Jamba/Granite); validated on Qwen3.5-9B
/// with a recent llama.cpp: correct output, and without FA the KQ matrix
/// costs n_ctx × n_ubatch × n_head × 4 bytes of VRAM (5.4 GiB at 43k ctx,
/// docs/NOTES-VRAM-FITTING.md).  Set to false if a hybrid model misbehaves.
inline constexpr bool kHybridFlashAttn = true;

/// Flash attention requested for a model (AUTO lets llama.cpp turn it off
/// on a backend that does not support it).
inline bool usesFlashAttn(bool is_hybrid) {
  return !is_hybrid || kHybridFlashAttn;
}

/// Round up to the nearest multiple of 256 (llama.cpp pads n_ctx to 256).
uint32_t pad256(uint32_t v);

/// Probe a GGUF model without allocating GPU memory (no-alloc load): layer
/// structure and KV layout, sizes, projector memory.  With a payload, also
/// counts the exact prompt tokens, images included.  n_layers == 0 on
/// failure.
ProbeInfo probeModel(const QString &modelPath, const QString &mmprojPath,
                     const PromptPayload *payload);

/// GPU devices with their free memory (the CPU device is not listed).
std::vector<DeviceMem> discoverDevices();

/// The single GPU the model is loaded on: discrete over integrated, CUDA
/// over other backends, then the most free memory.  nullptr without GPU.
const DeviceMem *pickGpu(const std::vector<DeviceMem> &gpuDevs);

/// Adaptive fitting loop: parameters that fit the chosen GPU, applying the
/// cheapest reductions first.  n_gpu_layers == 0 means "load on the CPU"
/// (no GPU, or nothing fits); otherwise it includes the output layer, as
/// llama.cpp counts it (> probe.n_layers: everything on the GPU).
common_params fitParams(const ProbeInfo &probe,
                        const std::vector<DeviceMem> &gpuDevs,
                        const QString &mmprojPath, uint32_t desiredCtx,
                        uint32_t desiredUbatch, uint32_t minCtx);

/// Silences fitParams' logging for its lifetime (repeated estimates).
class QuietScope {
public:
  QuietScope();
  ~QuietScope();
  QuietScope(const QuietScope &) = delete;
  QuietScope &operator=(const QuietScope &) = delete;

private:
  bool m_previous;
};

} // namespace VramFit
