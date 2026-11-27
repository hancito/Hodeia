#include "updatechecker.h"

#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUrl>

namespace {

constexpr auto kReleasesUrl =
    "https://api.github.com/repos/hancito/Hodeia/releases?per_page=30";
constexpr int kTimeoutMs = 15000;

struct ParsedVersion {
  bool valid = false;
  QList<int> numbers;
  int stage = 3; // alpha 0, beta 1, rc 2, final 3
  int stageNumber = 0;
};

ParsedVersion parse(const QString &text) {
  static const QRegularExpression re(
      QStringLiteral("^v?(\\d+(?:\\.\\d+)*)(?:[-.]?(alpha|beta|rc)(\\d*))?$"),
      QRegularExpression::CaseInsensitiveOption);
  ParsedVersion v;
  const auto m = re.match(text.trimmed());
  if (!m.hasMatch())
    return v;
  for (const QString &part : m.captured(1).split(u'.'))
    v.numbers.append(part.toInt());
  const QString stage = m.captured(2).toLower();
  if (stage == u"alpha")
    v.stage = 0;
  else if (stage == u"beta")
    v.stage = 1;
  else if (stage == u"rc")
    v.stage = 2;
  v.stageNumber = m.captured(3).toInt();
  v.valid = true;
  return v;
}

} // namespace

UpdateChecker::UpdateChecker(const QString &currentVersion, QObject *parent)
    : QObject(parent), m_currentVersion(currentVersion),
      m_network(new QNetworkAccessManager(this)) {}

UpdateChecker::~UpdateChecker() {
  // No onFinished() (nor signal) from a destructor.
  if (m_reply) {
    m_reply->disconnect(this);
    m_reply->abort();
  }
}

int UpdateChecker::compareVersions(const QString &a, const QString &b) {
  const ParsedVersion va = parse(a), vb = parse(b);
  if (va.valid != vb.valid)
    return va.valid ? 1 : -1;
  if (!va.valid)
    return 0;
  const auto n = std::max(va.numbers.size(), vb.numbers.size());
  for (qsizetype i = 0; i < n; ++i) {
    const int x = i < va.numbers.size() ? va.numbers[i] : 0;
    const int y = i < vb.numbers.size() ? vb.numbers[i] : 0;
    if (x != y)
      return x < y ? -1 : 1;
  }
  if (va.stage != vb.stage)
    return va.stage < vb.stage ? -1 : 1;
  if (va.stageNumber != vb.stageNumber)
    return va.stageNumber < vb.stageNumber ? -1 : 1;
  return 0;
}

bool UpdateChecker::isValidVersion(const QString &version) {
  return parse(version).valid;
}

bool UpdateChecker::isPreRelease(const QString &version) {
  const ParsedVersion v = parse(version);
  return v.valid && v.stage < 3;
}

UpdateChecker::Release
UpdateChecker::newestRelease(const QJsonArray &releases,
                             const QString &current) {
  const bool acceptPre = isPreRelease(current);
  Release best;
  for (const QJsonValue &value : releases) {
    const QJsonObject r = value.toObject();
    if (r.value("draft").toBool())
      continue;
    QString version = r.value("tag_name").toString();
    if (version.startsWith(u'v') || version.startsWith(u'V'))
      version.remove(0, 1);
    if (!isValidVersion(version))
      continue;
    const bool pre = r.value("prerelease").toBool() || isPreRelease(version);
    if (pre && !acceptPre)
      continue;
    if (compareVersions(version, current) <= 0)
      continue;
    if (!best.version.isEmpty() && compareVersions(version, best.version) <= 0)
      continue;
    best.version = version;
    best.url = r.value("html_url").toString();
    best.notes = r.value("body").toString();
  }
  return best;
}

void UpdateChecker::check(bool silent) {
  if (m_reply)
    return;
  QNetworkRequest request{QUrl(QString::fromLatin1(kReleasesUrl))};
  request.setRawHeader("Accept", "application/vnd.github+json");
  request.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
  request.setHeader(QNetworkRequest::UserAgentHeader,
                    QStringLiteral("HodeIA/%1").arg(m_currentVersion));
  request.setTransferTimeout(kTimeoutMs);
  QNetworkReply *reply = m_network->get(request);
  m_reply = reply;
  connect(reply, &QNetworkReply::finished, this,
          [this, reply, silent]() { onFinished(reply, silent); });
  emit checkingChanged();
}

void UpdateChecker::onFinished(QNetworkReply *reply, bool silent) {
  reply->deleteLater();
  m_reply = nullptr;
  emit checkingChanged();

  const int status =
      reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
  if (reply->error() != QNetworkReply::NoError || status != 200) {
    qWarning() << "[Update] check failed:" << status << reply->errorString();
    if (!silent)
      emit checkFailed(
          status == 403 || status == 429
              ? tr("GitHub refused the request (too many requests). Try "
                   "again later.")
              : tr("Could not reach GitHub: %1").arg(reply->errorString()));
    return;
  }

  QJsonParseError parseError;
  const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll(), &parseError);
  if (parseError.error != QJsonParseError::NoError || !doc.isArray()) {
    qWarning() << "[Update] unexpected answer:" << parseError.errorString();
    if (!silent)
      emit checkFailed(tr("GitHub sent an unexpected answer."));
    return;
  }

  const Release newest = newestRelease(doc.array(), m_currentVersion);
  if (!newest.version.isEmpty()) {
    qInfo() << "[Update] new version available:" << newest.version;
    emit updateAvailable(newest.version, newest.url, newest.notes, silent);
  } else if (!silent) {
    emit upToDate(m_currentVersion);
  }
}
