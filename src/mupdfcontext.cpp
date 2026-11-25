#include "mupdfcontext.h"

#include <mupdf/fitz.h>

#include <mutex>

namespace {

/// Shared by every context: guards OpenJPEG's global context (and FreeType).
std::mutex g_freetypeLock;

} // namespace

struct MuPdfContext::Locks {
  std::mutex perContext[FZ_LOCK_MAX];

  std::mutex &get(int lock) {
    return lock == FZ_LOCK_FREETYPE ? g_freetypeLock : perContext[lock];
  }
  static void lock(void *user, int lock) {
    static_cast<Locks *>(user)->get(lock).lock();
  }
  static void unlock(void *user, int lock) {
    static_cast<Locks *>(user)->get(lock).unlock();
  }
};

MuPdfContext::MuPdfContext(std::size_t maxStore)
    : m_locks(std::make_unique<Locks>()) {
  // MuPDF copies the handlers: the Locks they point to live as long as the
  // context (dropped first in the destructor).
  const fz_locks_context locks{m_locks.get(), &Locks::lock, &Locks::unlock};
  m_ctx = fz_new_context(nullptr, &locks, maxStore);
}

MuPdfContext::~MuPdfContext() {
  if (m_ctx)
    fz_drop_context(m_ctx);
}
