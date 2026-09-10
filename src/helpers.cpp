#include "helpers.h"
#include "configmanager.h"

#include <QAtomicInt>
#include <QDir>
#include <QFile>
#include <QFileDevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QThreadPool>
#include <QTimer>
#include <QUrl>

/* ── Summary parsing ─────────────────────────────────────────── */

namespace SummaryHelpers {

PaperSummary parseSummaryJson(const QString &raw) {
  PaperSummary summary;

  QString text = raw;

  // 1. Strip markdown code fences FIRST ( ```json ... ```  or  ``` ... ``` )
  {
    int firstFence = text.indexOf("```");
    if (firstFence >= 0) {
      text = text.mid(firstFence + 3);
      // Skip the language tag line (e.g. "json\n")
      int nl = text.indexOf('\n');
      if (nl >= 0 && nl < 10) {
        text = text.mid(nl + 1);
      }
      // Remove the closing fence and anything after
      int lastFence = text.lastIndexOf("```");
      if (lastFence > 0) {
        text = text.mid(0, lastFence);
      }
    }
  }

  // 2. Strip reasoning/thinking prefix — jump straight to the first '{'
  {
    int bracePos = text.indexOf('{');
    if (bracePos > 0) {
      text = text.mid(bracePos);
    }
    // Also ensure the closing '}' is the last meaningful char
    int endBrace = text.lastIndexOf('}');
    if (endBrace > 0) {
      text = text.mid(0, endBrace + 1);
    }
  }

  text = text.trimmed();

  // Quick sanity before parsing.
  if (!text.startsWith("{") && !text.startsWith("```")) {
    int bracePos = text.indexOf('{');
    if (bracePos < 0) {
      qWarning() << "[SummaryHelpers] No JSON object found in output (length:"
                 << raw.length() << ")";
      qWarning() << "[SummaryHelpers] start:" << raw;
      return summary;
    }
    // If the JSON starts very late, the response was likely truncated.
    if (bracePos > raw.length() * 0.5) {
      qWarning() << "[SummaryHelpers] JSON brace at position" << bracePos
                 << "of" << raw.length() << "— response likely truncated";
    }
  }

  QJsonParseError err;
  QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8(), &err);
  if (err.error != QJsonParseError::NoError) {
    qWarning() << "[SummaryHelpers] JSON parse error:" << err.errorString();
    qWarning() << "[SummaryHelpers] offset:" << err.offset << "| context:"
               << text.mid(qMax(0, err.offset - 30), 60).replace('\n', " ");
    qWarning() << "[SummaryHelpers] raw length:" << raw.length()
               << "| cleaned length:" << text.length();
    if (text.length() < 200) {
      qWarning() << "[SummaryHelpers] full cleaned text:" << text;
    }
    return summary;
  }
  QJsonObject obj = doc.object();
  summary.title = obj.value("title").toString();
  summary.journal = obj.value("journal").toString();
  summary.year = obj.value("year").toInt();
  summary.researchObjective = obj.value("researchObjective").toString();
  summary.methodology = obj.value("methodology").toString();
  for (const auto &f : obj.value("coreFindingsAndSignificance").toArray())
    summary.coreFindingsAndSignificance.append(f.toString());
  for (const auto &l : obj.value("criticalLimitations").toArray())
    summary.criticalLimitations.append(l.toString());
  summary.citableSummary = obj.value("citableSummary").toString();
  for (const auto &a : obj.value("authors").toArray())
    summary.authors.append(a.toString());
  for (const auto &t : obj.value("topics").toArray())
    summary.topics.append(t.toString());
  return summary;
}

} // namespace SummaryHelpers

/* ── Markdown / LaTeX export ───────────────────────────────────── */

namespace SummaryHelpers {

QString toMarkdown(const PaperSummary &s) {
  QString authors = s.authors.join(", ");
  QString title = s.title;
  if (!title.isEmpty())
    title[0] = title[0].toUpper();
  QStringList parts;
  parts.append(
      QString("## %1, (%2): %3").arg(authors, QString::number(s.year), title));
  parts.append("");
  parts.append(QString("### Journal:  %1").arg(s.journal));
  parts.append("");
  parts.append("## Research objective");
  parts.append(s.researchObjective);
  parts.append("");
  parts.append("## Methodology");
  parts.append(s.methodology);
  parts.append("");
  parts.append("## Core findings and significance");
  parts.append(QString("- %1").arg(s.coreFindingsAndSignificance.join("\n- ")));
  parts.append("");
  parts.append("## Critical Limitations");
  parts.append(QString("- %1").arg(s.criticalLimitations.join("\n- ")));
  parts.append("");
  parts.append("## Citable summary");
  parts.append(s.citableSummary);
  return parts.join("\n");
}

QString toLatex(const PaperSummary &s) {
  QString authors = s.authors.join(", ");
  QString title = s.title;
  if (!title.isEmpty())
    title[0] = title[0].toUpper();
  QString findings, limitations;
  for (const QString &f : s.coreFindingsAndSignificance)
    findings += QString("\n    \\item %1").arg(f);
  for (const QString &l : s.criticalLimitations)
    limitations += QString("\n    \\item %1").arg(l);
  QStringList parts;
  parts.append(QString("\\section{%1, (%2): %3}")
                   .arg(authors, QString::number(s.year), title));
  parts.append("\\subsection{Research objective}");
  parts.append(s.researchObjective);
  parts.append("");
  parts.append("\\subsection{Methodology}");
  parts.append(s.methodology);
  parts.append("");
  parts.append("\\subsection{Core findings and significance}");
  parts.append(QString("\\begin{itemize}%1\\end{itemize}").arg(findings));
  parts.append("");
  parts.append("\\subsection{Critical Limitations}");
  parts.append(QString("\\begin{itemize}%1\\end{itemize}").arg(limitations));
  parts.append("");
  parts.append("\\subsection{Citable summary}");
  parts.append(s.citableSummary);
  return parts.join("\n");
}

} // namespace SummaryHelpers

/* ── File copy helper ──────────────────────────────────────────── */

/// Copy a single file into @p destDir. Emits modelCopyProgress on @p helper.
/// Runs in a thread-pool worker; signals are auto-queued to the main thread.
static bool copyFileToDir(const QString &sourcePath, const QString &destDir,
                          ModelHelper *helper) {
  QString srcPath = sourcePath;
  if (srcPath.startsWith("file://"))
    srcPath = QUrl(srcPath).toLocalFile();

  QFile src(srcPath);
  if (!src.exists())
    return false;

  QString fileName = QFileInfo(src).fileName();
  QString destPath = destDir + "/" + fileName;
  QDir().mkpath(destDir);

  if (QFile::exists(destPath) && !QFile::remove(destPath))
    return false;
  if (!src.open(QIODevice::ReadOnly))
    return false;

  qint64 total = src.size();
  if (total <= 0) {
    src.close();
    return false;
  }

  QFile dest(destPath);
  if (!dest.open(QIODevice::WriteOnly)) {
    src.close();
    return false;
  }

  const qint64 bufSize = 1024 * 1024;
  char buf[bufSize];
  qint64 copied = 0;
  while (!src.atEnd()) {
    qint64 n = src.read(buf, bufSize);
    if (n <= 0)
      break;
    copied += dest.write(buf, n);
    if (helper)
      Q_EMIT helper->modelCopyProgress(static_cast<int>(copied * 100 / total),
                                       fileName);
  }
  dest.close();
  src.close();

  if (copied != total) {
    QFile::remove(destPath);
    return false;
  }
  if (helper)
    Q_EMIT helper->modelCopyProgress(100, fileName);
  return true;
}

/* ── ModelHelper ───────────────────────────────────────────────── */

ModelHelper::ModelHelper(ConfigManager *config, QObject *parent)
    : QObject(parent), m_config(config) {
  m_networkManager = new QNetworkAccessManager(this);

  QObject::connect(m_config, &ConfigManager::activeBackendChanged, this,
                   &ModelHelper::loadActiveBackendModels);
  QObject::connect(m_config, &ConfigManager::modelCopied, this,
                   &ModelHelper::loadActiveBackendModels);

  if (m_config) {
    QObject::connect(this, &ModelHelper::modelCopyProgress, m_config,
                     &ConfigManager::modelCopyProgress);
    QObject::connect(this, &ModelHelper::modelCopyFinished, m_config,
                     &ConfigManager::modelCopyFinished);
    QObject::connect(this, &ModelHelper::modelCopied, m_config,
                     &ConfigManager::modelCopied);
    QObject::connect(this, &ModelHelper::modelCopied, this,
                     &ModelHelper::refreshLocalModelList, Qt::QueuedConnection);
  }

  refreshLocalModelList();
  loadActiveBackendModels();
}

ModelHelper::~ModelHelper() = default;

QString ModelHelper::summaryToMarkdown(const QString &json) const {
  return SummaryHelpers::toMarkdown(SummaryHelpers::parseSummaryJson(json));
}
QString ModelHelper::summaryToLatex(const QString &json) const {
  return SummaryHelpers::toLatex(SummaryHelpers::parseSummaryJson(json));
}

/* ── Local model listing ───────────────────────────────────────── */

void ModelHelper::refreshLocalModelList() {
  if (!m_config)
    return;
  QDir d(m_config->summarizersPath());
  if (!d.exists()) {
    m_localModelList.clear();
    emit localModelListChanged();
    return;
  }
  QStringList list = d.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
  if (list != m_localModelList) {
    m_localModelList = list;
    emit localModelListChanged();
  }
}

QStringList ModelHelper::localModelList() { return m_localModelList; }

void ModelHelper::deleteLocalModels(const QStringList &folderNames) {
  if (!m_config)
    return;
  for (const QString &name : folderNames) {
    if (name.trimmed().isEmpty())
      continue;
    QString folder = m_config->summarizersPath() + "/" + name.trimmed();
    QDir dir(folder);
    if (dir.exists()) {
      dir.setFilter(QDir::AllEntries | QDir::NoDotAndDotDot);
      for (const QFileInfo &entry : dir.entryInfoList()) {
        if (entry.isDir())
          QDir(entry.absoluteFilePath()).removeRecursively();
        else
          QFile::remove(entry.absoluteFilePath());
      }
      dir.rmdir(folder);
      qDebug() << "[ModelHelper] Local model removed:" << folder;
    }
  }
  refreshLocalModelList();
  loadActiveBackendModels();
}

QStringList ModelHelper::listLocalModels() {
  if (!m_config)
    return {};
  QString dir = m_config->summarizersPath();
  QDir d(dir);
  if (!d.exists())
    return {};
  QStringList models;
  for (const QString &subdir : d.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
    QStringList ggufs =
        QDir(dir + "/" + subdir)
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

QStringList ModelHelper::getImagesForHash(const QString &paperHash,
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

/* ── Remote backend ────────────────────────────────────────────── */

void ModelHelper::checkRemoteModels(const QString &name, const QString &apiUrl,
                                    const QString &apiKey) {
  if (apiUrl.isEmpty()) {
    emit modelsChecked(false, {}, "Invalid parameters");
    return;
  }
  QObject::connect(
      this, &ModelHelper::modelsChecked, this,
      [this, name, apiUrl, apiKey](bool ok, const QStringList &,
                                   const QString &) {
        if (ok && m_config) {
          m_config->setBackend(name, apiUrl, apiKey);
          if (m_config->activeBackend() != name)
            m_config->setActiveBackend(name);
        }
      },
      Qt::SingleShotConnection);
  fetchRemoteModels(apiUrl, apiKey);
}

void ModelHelper::fetchRemoteModels(const QString &apiUrl,
                                    const QString &apiKey) {
  if (apiUrl.isEmpty()) {
    emit modelsChecked(false, {}, "URL is empty");
    return;
  }
  if (m_currentReply) {
    m_currentReply->abort();
    m_currentReply = nullptr;
  }
  QUrl url(apiUrl);
  url.setPath(url.path() + "/models");
  QNetworkRequest req(url);
  if (!apiKey.isEmpty())
    req.setRawHeader("Authorization", ("Bearer " + apiKey).toUtf8());

  m_currentReply = m_networkManager->get(req);
  QObject::connect(m_currentReply, &QNetworkReply::finished, this,
                   [this, reply = m_currentReply]() {
                     if (m_currentReply == reply)
                       m_currentReply = nullptr;
                     handleModelsReply(reply);
                   });
}

void ModelHelper::handleModelsReply(QNetworkReply *reply) {
  if (!reply)
    return;
  if (reply->error() != QNetworkReply::NoError) {
    emit modelsChecked(false, {}, reply->errorString());
    return;
  }
  auto doc = QJsonDocument::fromJson(reply->readAll());
  if (!doc.isObject()) {
    emit modelsChecked(false, {}, "Invalid JSON");
    return;
  }
  QStringList models;
  for (const auto &e : doc.object().value("data").toArray()) {
    QString id = e.toObject().value("id").toString();
    if (!id.isEmpty())
      models.append(id);
  }
  qDebug() << "[ModelHelper] Received" << models.size()
           << "model(s) from server";
  emit modelsChecked(true, models, QString());
}

void ModelHelper::loadActiveBackendModels() {
  if (!m_config)
    return;
  QVariantMap cfg = m_config->backendConfig(m_config->activeBackend());
  QString url = cfg.value("url").toString();
  if (url.isEmpty()) {
    QStringList m = listLocalModels();
    emit modelsChecked(!m.isEmpty(), m, QString());
  } else {
    checkRemoteModels(cfg.value("name").toString(), url,
                      cfg.value("api_key").toString());
  }
}

/* ── Model copy (parallel, non-blocking) ───────────────────── */

void ModelHelper::addLocalSummarizer(const QString &sourcePath) {
  addLocalSummarizerWithMmproj(sourcePath, QString());
}

void ModelHelper::addLocalSummarizerWithMmproj(const QString &modelPath,
                                               const QString &mmprojPath) {
  if (!m_config)
    return;

  QString modelSrc = modelPath;
  if (modelSrc.startsWith("file://"))
    modelSrc = QUrl(modelSrc).toLocalFile();

  QString mmprojSrc;
  if (!mmprojPath.isEmpty()) {
    mmprojSrc = mmprojPath;
    if (mmprojSrc.startsWith("file://"))
      mmprojSrc = QUrl(mmprojSrc).toLocalFile();
  }

  if (!QFile::exists(modelSrc)) {
    emit modelCopyFinished(false, QString(),
                           "Model file does not exist: " + modelSrc);
    return;
  }
  if (!mmprojSrc.isEmpty() && !QFile::exists(mmprojSrc)) {
    emit modelCopyFinished(false, QString(),
                           "MMProj file does not exist: " + mmprojSrc);
    return;
  }

  QString modelName = QFileInfo(modelSrc).completeBaseName();
  QString modelFolder = m_config->summarizersPath() + "/" + modelName;
  QDir().mkpath(modelFolder);

  // Shared atomic: each copy task decrements; the one that reaches 0 emits
  // the final signal.
  auto *done = new QAtomicInt(!mmprojSrc.isEmpty() ? 2 : 1);
  QAtomicInt *modelOk = new QAtomicInt{0};
  QAtomicInt *mmprojOk = new QAtomicInt{1}; // 1 = ok by default

  ModelHelper *self = this;

  // Model copy
  QThreadPool::globalInstance()->start(
      [self, src = modelSrc, folder = modelFolder, done, modelOk]() {
        *modelOk = copyFileToDir(src, folder, self) ? 1 : 0;
        int remaining = done->fetchAndAddRelaxed(-1);
        if (remaining == 1) {
          // This task finished last.
        }
      });

  // MMProj copy (optional)
  if (!mmprojSrc.isEmpty()) {
    QThreadPool::globalInstance()->start(
        [self, src = mmprojSrc, folder = modelFolder, done, mmprojOk]() {
          *mmprojOk = copyFileToDir(src, folder, self) ? 1 : 0;
          int remaining = done->fetchAndAddRelaxed(-1);
          if (remaining == 1) {
            // This task finished last.
          }
        });
  }

  // Poll the atomic counter via a QTimer to avoid blocking the main thread.
  auto *timer = new QTimer(this);
  timer->setInterval(50);
  QObject::connect(
      timer, &QTimer::timeout, timer, [timer, done, modelOk, mmprojOk, self]() {
        if (done->loadRelaxed() == 0) {
          bool m = modelOk->loadRelaxed();
          bool mp = mmprojOk->loadRelaxed();
          delete done;
          delete modelOk;
          delete mmprojOk;
          Q_EMIT self->modelCopyFinished(m && mp, QString(), QString());
          if (m && mp)
            Q_EMIT self->modelCopied();
          timer->stop();
          timer->deleteLater();
        }
      });
  timer->start();
}
