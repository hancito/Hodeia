#include "helpers.h"
#include "anthropicapi.h"
#include "configmanager.h"
#include "database.h"
#include "metadataresolver.h"
#include "pdfexport.h"
#include "summaryformat.h"
#include "zoterorepository.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextList>
#include <QThreadPool>
#include <QUrl>

/* ── Constructor / Destructor ────────────────────────────────── */

Helpers::Helpers(ConfigManager *config, QObject *parent)
    : QObject(parent), m_config(config) {
  m_networkManager = new QNetworkAccessManager(this);

  QObject::connect(m_config, &ConfigManager::activeBackendChanged, this,
                   &Helpers::loadActiveBackendModels);

  refreshLocalModelList();
  loadActiveBackendModels();
}

Helpers::~Helpers() = default;

/* ── Summaries ───────────────────────────────────────────────── */

namespace {

/// Zotero's metadata by paper hash, or nothing when they do not apply.
QHash<QString, ZoteroMeta> zoteroMetaFor(DatabaseManager *database,
                                         bool zoteroPriority) {
  if (!zoteroPriority || !database || !database->isOpen())
    return {};
  return ZoteroRepository(database->db()).metaByHash();
}

} // namespace

QString Helpers::summaryToHtml(const QString &json, const QString &paperHash,
                               bool zoteroPriority) const {
  PaperSummary summary = SummaryFormat::parseJson(json);
  const auto zotero = zoteroMetaFor(m_database, zoteroPriority && !paperHash.isEmpty());
  if (const auto it = zotero.constFind(paperHash); it != zotero.constEnd())
    MetadataResolver::applyZotero(summary, *it);
  return SummaryFormat::toHtml(summary);
}

QString Helpers::markdownToHtml(const QString &markdown) const {
  QTextDocument doc;
  doc.setMarkdown(markdown);

  // Spacing in px.  Paragraph/heading margins collapse with the next block's
  // top margin, so each block only needs one side set.
  constexpr qreal paragraphGap = 10;
  constexpr qreal headingTop = 18;
  constexpr qreal headingBottom = 6;
  constexpr qreal listItemGap = 4;
  constexpr qreal lineHeight = 120; // percent

  QTextCursor cursor(&doc);
  for (QTextBlock block = doc.begin(); block.isValid(); block = block.next()) {
    QTextBlockFormat fmt = block.blockFormat();
    const bool inList = block.textList() != nullptr;
    const bool nextInList =
        block.next().isValid() && block.next().textList() != nullptr;
    if (fmt.headingLevel() > 0) {
      fmt.setTopMargin(headingTop);
      fmt.setBottomMargin(headingBottom);
    } else if (inList) {
      fmt.setBottomMargin(nextInList ? listItemGap : paragraphGap);
    } else if (!fmt.hasProperty(QTextFormat::BlockCodeFence)) {
      fmt.setBottomMargin(paragraphGap);
    }
    fmt.setLineHeight(lineHeight, QTextBlockFormat::ProportionalHeight);
    cursor.setPosition(block.position());
    cursor.setBlockFormat(fmt);
  }

  // toHtml() writes the document's default font on <body>, which would
  // override the view's own font: drop it so the TextEdit's font applies.
  QString html = doc.toHtml();
  static const QRegularExpression bodyStyle(
      QStringLiteral("<body style=\"[^\"]*\">"));
  html.replace(bodyStyle, QStringLiteral("<body>"));
  return html;
}

QString Helpers::exportPdf(const QStringList &jsonList, const QString &destPath,
                           const QStringList &paperHashes,
                           bool zoteroPriority) const {
  const auto zotero = zoteroMetaFor(m_database, zoteroPriority);
  QList<PaperSummary> papers;
  for (qsizetype i = 0; i < jsonList.size(); ++i) {
    if (jsonList[i].isEmpty())
      continue;
    PaperSummary summary = SummaryFormat::parseJson(jsonList[i]);
    if (const auto it = zotero.constFind(paperHashes.value(i));
        it != zotero.constEnd())
      MetadataResolver::applyZotero(summary, *it);
    papers.append(summary);
  }
  if (papers.isEmpty())
    return QString();
  return PdfExport::writeHtml(SummaryFormat::reportToHtml(papers), destPath);
}

QStringList Helpers::getImagesForHash(const QString &paperHash,
                                      const QString &imgPath) const {
  QDir imgDir(imgPath);
  if (!imgDir.exists())
    return {};
  imgDir.setNameFilters({paperHash + "_page_*.png"});
  QStringList paths;
  for (const QFileInfo &fi : imgDir.entryInfoList())
    paths.append(fi.absoluteFilePath());
  paths.sort();
  return paths;
}

QString Helpers::localFileUrl(const QString &path) const {
  return QUrl::fromLocalFile(path).toString();
}

/* ── Local models ────────────────────────────────────────────── */

// Summarizers sub-folders holding a model .gguf (an mmproj alone is not a
// usable model).
static QStringList scanLocalModels(const QString &summarizersPath) {
  QDir d(summarizersPath);
  QStringList models;
  for (const QString &subdir : d.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
    const QStringList ggufs =
        QDir(d.filePath(subdir))
            .entryList(QStringList{"*.gguf"}, QDir::Files | QDir::NoSymLinks);
    for (const QString &f : ggufs) {
      if (!f.contains("mmproj", Qt::CaseInsensitive)) {
        models.append(subdir);
        break;
      }
    }
  }
  return models;
}

void Helpers::refreshLocalModelList() {
  if (!m_config)
    return;
  const QStringList list = scanLocalModels(m_config->summarizersPath());
  if (list != m_localModelList) {
    m_localModelList = list;
    emit localModelListChanged();
  }
}

QStringList Helpers::localModelList() const { return m_localModelList; }

QString Helpers::deleteLocalModels(const QStringList &folderNames) {
  if (!m_config)
    return tr("No configuration available");
  QStringList failed;
  for (const QString &name : folderNames) {
    if (name.trimmed().isEmpty())
      continue;
    const QString folder = m_config->summarizersPath() + "/" + name.trimmed();
    if (QDir(folder).removeRecursively()) {
      qDebug() << "[Helpers] Local model removed:" << folder;
    } else {
      qWarning() << "[Helpers] Could not fully remove" << folder;
      failed << name.trimmed();
    }
  }
  refreshLocalModelList(); // the active backend may be remote
  loadActiveBackendModels();
  if (!failed.isEmpty())
    return tr("These models could not be fully deleted (a file may still be "
              "in use, e.g. by a running summary or query):") +
           "\n\n• " + failed.join("\n• ");
  return {};
}

// A path from QML may be a file: URL (FileDialog::selectedFile) or a native
// path; QUrl::fromUserInput maps both to the local path on every platform.
static QString normalizeLocalPath(const QString &in) {
  if (in.isEmpty())
    return {};
  if (in.startsWith(QLatin1String("file:"), Qt::CaseInsensitive))
    return QUrl(in).toLocalFile();
  return QUrl::fromUserInput(in).toLocalFile();
}

// Copy @p srcPath into @p destDir. Runs on a worker thread: progress is
// queued to the GUI thread, once per percent. Progress is reported over the
// whole operation: @p doneBefore bytes were already copied out of
// @p grandTotal (model + mmproj share one bar). Returns an error message, or
// an empty string on success.
static QString copyFileToDir(const QString &srcPath, const QString &destDir,
                             Helpers *helper, qint64 doneBefore,
                             qint64 grandTotal) {
  const QString fileName = QFileInfo(srcPath).fileName();
  QFile src(srcPath);
  if (!src.open(QIODevice::ReadOnly))
    return Helpers::tr("Cannot read %1: %2").arg(srcPath, src.errorString());
  const qint64 total = src.size();
  if (total <= 0)
    return Helpers::tr("%1 is empty").arg(fileName);

  const QString destPath = destDir + "/" + fileName;
  QFile dest(destPath);
  if (!dest.open(QIODevice::WriteOnly | QIODevice::Truncate))
    return Helpers::tr("Cannot write %1: %2").arg(destPath, dest.errorString());

  // Heap-allocated buffer: this runs on a QThreadPool worker whose default
  // stack is only 1 MB on Windows; a 1 MB stack array overflowed it.
  QByteArray buf(1024 * 1024, Qt::Uninitialized);
  qint64 copied = 0;
  int lastPct = -1;
  while (copied < total) {
    const qint64 n = src.read(buf.data(), buf.size());
    if (n <= 0)
      return Helpers::tr("Cannot read %1: %2").arg(srcPath, src.errorString());
    if (dest.write(buf.constData(), n) != n)
      return Helpers::tr("Cannot write %1: %2")
          .arg(destPath, dest.errorString());
    copied += n;
    const qint64 denom = grandTotal > 0 ? grandTotal : total;
    const int pct = static_cast<int>((doneBefore + copied) * 100 / denom);
    if (pct != lastPct) {
      lastPct = pct;
      QMetaObject::invokeMethod(
          helper,
          [helper, fileName, pct] {
            Q_EMIT helper->modelCopyProgress(pct, fileName);
          },
          Qt::QueuedConnection);
    }
  }
  if (!dest.flush())
    return Helpers::tr("Cannot write %1: %2").arg(destPath, dest.errorString());
  return QString();
}

void Helpers::addLocalSummarizer(const QString &modelPath,
                                 const QString &mmprojPath) {
  if (!m_config)
    return;

  const QString modelSrc = normalizeLocalPath(modelPath);
  const QString mmprojSrc = normalizeLocalPath(mmprojPath);
  if (!QFile::exists(modelSrc)) {
    emit modelCopyFinished(false, QString(),
                           tr("Model file does not exist: %1").arg(modelSrc));
    return;
  }
  if (!mmprojSrc.isEmpty() && !QFile::exists(mmprojSrc)) {
    emit modelCopyFinished(false, QString(),
                           tr("MMProj file does not exist: %1").arg(mmprojSrc));
    return;
  }

  const QString folder = m_config->summarizersPath() + "/" +
                         QFileInfo(modelSrc).completeBaseName();
  if (!QDir().mkpath(folder)) {
    emit modelCopyFinished(false, QString(),
                           tr("Cannot create folder %1").arg(folder));
    return;
  }

  // One task, files copied one after the other (both go to the same disk:
  // copying them in parallel gains nothing).
  QThreadPool::globalInstance()->start([this, modelSrc, mmprojSrc, folder] {
    const qint64 modelSize = QFileInfo(modelSrc).size();
    const qint64 grandTotal =
        modelSize + (mmprojSrc.isEmpty() ? 0 : QFileInfo(mmprojSrc).size());
    QString error = copyFileToDir(modelSrc, folder, this, 0, grandTotal);
    if (error.isEmpty() && !mmprojSrc.isEmpty())
      error = copyFileToDir(mmprojSrc, folder, this, modelSize, grandTotal);
    if (!error.isEmpty())
      QDir(folder).removeRecursively(); // don't leave a half-copied model

    QMetaObject::invokeMethod(
        this,
        [this, folder, error] {
          const bool ok = error.isEmpty();
          emit modelCopyFinished(ok, ok ? folder : QString(), error);
          if (ok)
            loadActiveBackendModels(); // also rescans the local models
        },
        Qt::QueuedConnection);
  });
}

/* ── Backends ────────────────────────────────────────────────── */

void Helpers::checkRemoteModels(const QString &name, const QString &apiUrl,
                                const QString &apiKey, const QString &api) {
  if (apiUrl.isEmpty()) {
    emit modelsChecked(false, {}, tr("URL is empty"));
    return;
  }
  fetchRemoteModels(apiUrl, apiKey, api, name);
}

void Helpers::loadActiveBackendModels() {
  if (!m_config)
    return;
  const QVariantMap cfg = m_config->backendConfig(m_config->activeBackend());
  const QString url = cfg.value("url").toString();
  if (!url.isEmpty()) {
    fetchRemoteModels(url, cfg.value("api_key").toString(),
                      cfg.value("api").toString(), QString());
    return;
  }
  // Local backend: a pending remote request must not overwrite the list.
  cancelPendingReply();
  refreshLocalModelList();
  // Success even when empty: the scan worked, there is just no model left
  // (the combobox must then be cleared, not left showing a deleted model).
  emit modelsChecked(true, m_localModelList, QString());
}

// Drop a pending /models request silently: disconnected first, its abort()
// must not report a spurious "Operation canceled" failure.
void Helpers::cancelPendingReply() {
  if (!m_currentReply)
    return;
  m_currentReply->disconnect(this);
  m_currentReply->abort();
  m_currentReply->deleteLater();
  m_currentReply = nullptr;
}

void Helpers::fetchRemoteModels(const QString &apiUrl, const QString &apiKey,
                                const QString &api, const QString &saveAs) {
  cancelPendingReply();

  QNetworkRequest req;
  if (api == QLatin1String("anthropic")) {
    // GET /v1/models: same {"data":[{"id":...}]} shape as OpenAI's list.
    req.setUrl(AnthropicApi::endpointUrl(apiUrl, QStringLiteral("models")));
    req.setRawHeader("x-api-key", apiKey.toUtf8());
    req.setRawHeader("anthropic-version", AnthropicApi::kApiVersion);
  } else {
    QUrl url(apiUrl);
    QString path = url.path();
    while (path.endsWith('/'))
      path.chop(1);
    url.setPath(path + "/models");
    req.setUrl(url);
    if (!apiKey.isEmpty())
      req.setRawHeader("Authorization", ("Bearer " + apiKey).toUtf8());
  }

  QNetworkReply *reply = m_networkManager->get(req);
  m_currentReply = reply;
  QObject::connect(reply, &QNetworkReply::finished, this,
                   [this, reply, apiUrl, apiKey, api, saveAs] {
                     if (m_currentReply == reply)
                       m_currentReply = nullptr;
                     reply->deleteLater();
                     if (handleModelsReply(reply) && !saveAs.isEmpty() &&
                         m_config) {
                       m_config->setBackend(saveAs, apiUrl, apiKey,
                                            api.isEmpty() ? QStringLiteral("openai")
                                                          : api);
                       m_config->setActiveBackend(saveAs);
                     }
                   });
}

bool Helpers::handleModelsReply(QNetworkReply *reply) {
  if (reply->error() != QNetworkReply::NoError) {
    emit modelsChecked(false, {}, reply->errorString());
    return false;
  }
  const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
  if (!doc.isObject()) {
    emit modelsChecked(false, {}, tr("Invalid JSON"));
    return false;
  }
  QStringList models;
  for (const auto &e : doc.object().value("data").toArray()) {
    const QString id = e.toObject().value("id").toString();
    if (!id.isEmpty())
      models.append(id);
  }
  qDebug() << "[Helpers] Received" << models.size() << "model(s) from server";
  emit modelsChecked(true, models, QString());
  return true;
}
