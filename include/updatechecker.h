#pragma once

#include <QJsonArray>
#include <QObject>
#include <QPointer>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

/// Looks for a newer HodeIA release on GitHub (hancito/Hodeia releases).
///
/// The releases are tagged v<version> (CMakeLists.txt: project VERSION +
/// HODEIA_VERSION_SUFFIX, e.g. v0.99rc1); "rc"/"beta" tags are published as
/// pre-releases.  A stable build is only offered stable releases; a
/// pre-release build is offered pre-releases too (it was installed to test
/// them).  Drafts are ignored.
///
/// Nothing is downloaded nor installed: the user gets the version, its notes
/// and the release page (installer, Linux packages), opened in the browser.
///
/// check(silent): a silent check (at startup) only reports an available
/// update, never "up to date" nor a network failure.  Exposed to QML as
/// "updateChecker"; lives in the main thread.
class UpdateChecker : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool checking READ checking NOTIFY checkingChanged)
  Q_PROPERTY(QString currentVersion READ currentVersion CONSTANT)

public:
  /// A release found on GitHub.
  struct Release {
    QString version; // tag without the leading "v"
    QString url;     // release page
    QString notes;   // body, Markdown
  };

  explicit UpdateChecker(const QString &currentVersion,
                         QObject *parent = nullptr);
  ~UpdateChecker() override;

  bool checking() const { return !m_reply.isNull(); }
  QString currentVersion() const { return m_currentVersion; }

  /// Ask GitHub for the releases.  Ignored while a check is running.
  Q_INVOKABLE void check(bool silent = false);

  /// <0, 0, >0 like strcmp.  Versions are "1.2.3" with an optional
  /// "alpha"/"beta"/"rc" suffix and number ("0.99rc1"), a leading "v"
  /// allowed: 0.99beta2 < 0.99rc1 < 0.99rc2 < 0.99 < 0.99.1 < 1.0.
  /// An unparsable version sorts before any valid one.
  static int compareVersions(const QString &a, const QString &b);
  static bool isValidVersion(const QString &version);
  static bool isPreRelease(const QString &version);

  /// The newest release of the GitHub API answer (/releases) that is newer
  /// than @p current, or an empty Release (version empty).  Pure function.
  static Release newestRelease(const QJsonArray &releases,
                               const QString &current);

signals:
  void checkingChanged();
  /// A newer release exists (silent or not).
  void updateAvailable(const QString &version, const QString &url,
                       const QString &notes, bool silent);
  /// Manual check only: no newer release.
  void upToDate(const QString &version);
  /// Manual check only: GitHub could not be reached or answered nonsense.
  void checkFailed(const QString &error);

private:
  void onFinished(QNetworkReply *reply, bool silent);

  QString m_currentVersion;
  QNetworkAccessManager *m_network = nullptr;
  QPointer<QNetworkReply> m_reply;
};
