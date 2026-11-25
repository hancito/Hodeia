#pragma once

#include <cstddef>
#include <memory>

typedef struct fz_context fz_context;

/// A MuPDF context with the lock handlers MuPDF requires once several
/// contexts run at the same time (the extraction threads, the PDF export).
///
/// MuPDF's JPEG 2000 decoder (OpenJPEG) keeps the current context in a
/// global, guarded by FZ_LOCK_FREETYPE: that lock must be shared by every
/// context, or two threads decoding JPX images overwrite each other's
/// context (a crash in fz_free).  The other locks (FZ_LOCK_ALLOC is taken
/// on every allocation) stay per context, so the extraction threads still
/// run in parallel.
///
/// Created before any fz_try of its owner and dropped by the destructor.
class MuPdfContext {
public:
  explicit MuPdfContext(std::size_t maxStore);
  ~MuPdfContext();
  MuPdfContext(const MuPdfContext &) = delete;
  MuPdfContext &operator=(const MuPdfContext &) = delete;

  /// Null when MuPDF could not create the context.
  fz_context *get() const { return m_ctx; }

private:
  struct Locks;
  std::unique_ptr<Locks> m_locks;
  fz_context *m_ctx = nullptr;
};
