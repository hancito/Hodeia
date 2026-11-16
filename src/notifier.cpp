#include "notifier.h"

#include <QCoreApplication>
#include <QMetaObject>
#include <QMutexLocker>
#include <QVariantMap>

Notifier::Notifier(QObject *parent) : QObject(parent) {}

Notifier *Notifier::instance() {
  // Created on first use; moved to the GUI thread so deliver() always runs
  // there whatever thread posts first.
  static Notifier *s_instance = [] {
    auto *n = new Notifier();
    if (QCoreApplication::instance())
      n->moveToThread(QCoreApplication::instance()->thread());
    return n;
  }();
  return s_instance;
}

void Notifier::post(const QString &key, const QString &title,
                    const QString &message, bool isError) {
  Notifier *self = instance();
  {
    const QMutexLocker locker(&self->m_mutex);
    if (self->m_shownKeys.contains(key))
      return;
    self->m_shownKeys.insert(key);
  }
  qWarning().noquote() << "[Notifier]" << title << "—" << message;
  QMetaObject::invokeMethod(
      self, [self, title, message, isError] { self->deliver(title, message, isError); },
      Qt::QueuedConnection);
}

void Notifier::forget(const QString &key) {
  Notifier *self = instance();
  const QMutexLocker locker(&self->m_mutex);
  self->m_shownKeys.remove(key);
}

void Notifier::deliver(const QString &title, const QString &message,
                       bool isError) {
  if (!m_uiReady) {
    m_pending.append(QVariantMap{
        {"title", title}, {"message", message}, {"isError", isError}});
    return;
  }
  emit notification(title, message, isError);
}

QVariantList Notifier::takePending() {
  m_uiReady = true;
  QVariantList pending;
  pending.swap(m_pending);
  return pending;
}
