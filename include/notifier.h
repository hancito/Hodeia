#pragma once

#include <QMutex>
#include <QObject>
#include <QSet>
#include <QString>
#include <QVariantList>

/// App-wide channel for problems the user must hear about but that happen
/// outside a user action: at startup (before the window exists), or in a
/// background thread (OCR, OpenAlex, saving the settings).
///
/// post() is thread-safe.  Messages posted before QML is ready are kept and
/// fetched once with takePending(); later ones are emitted as notification().
/// Each @p key is shown at most once per session, so a failure that repeats
/// (e.g. every settings save) opens a single window.  Exposed to QML as
/// "notifier".
class Notifier : public QObject {
  Q_OBJECT

public:
  static Notifier *instance();

  /// Report a problem (or information) to the user, once per @p key.
  static void post(const QString &key, const QString &title,
                   const QString &message, bool isError = true);
  /// Allow @p key to be shown again (e.g. after the user changed the
  /// setting that caused it).
  static void forget(const QString &key);

  /// Messages posted before the UI was ready, as {title, message, isError}
  /// maps.  After this call, new messages are emitted as notification().
  Q_INVOKABLE QVariantList takePending();

signals:
  void notification(const QString &title, const QString &message,
                    bool isError);

private:
  explicit Notifier(QObject *parent = nullptr);
  void deliver(const QString &title, const QString &message, bool isError);

  QMutex m_mutex; // guards m_shownKeys
  QSet<QString> m_shownKeys;
  // Main-thread only (deliver() and takePending() run there).
  bool m_uiReady = false;
  QVariantList m_pending;
};
