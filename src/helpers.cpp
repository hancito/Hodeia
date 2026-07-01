#include "headers/helpers.h"
#include "headers/configmanager.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

/* ── Summary parsing ─────────────────────────────────────────── */

namespace SummaryHelpers {

PaperSummary parseSummaryJson(const QString &json) {
  PaperSummary summary;

  QJsonParseError err;
  QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &err);
  if (err.error != QJsonParseError::NoError) {
    qWarning() << "[SummaryHelpers] JSON parse error:" << err.errorString();
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

  for (const auto &l : obj.value("references").toArray())
    summary.references.append(l.toString());

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

QString toMarkdown(const PaperSummary &summary) {
  QString authors = summary.authors.join(", ");
  QString title = summary.title;
  if (!title.isEmpty())
    title[0] = title[0].toUpper();

  QString findingsMd = summary.coreFindingsAndSignificance.join("\n- ");
  QString limitationsMd = summary.criticalLimitations.join("\n- ");

  QStringList parts;
  parts.append(QString("## %1, (%2): %3")
                   .arg(authors, QString::number(summary.year), title));
  parts.append(QString());
  parts.append(QString("### %1").arg(summary.journal));
  parts.append(QString());
  parts.append("## Research objective");
  parts.append(summary.researchObjective);
  parts.append(QString());
  parts.append("## Methodology");
  parts.append(summary.methodology);
  parts.append(QString());
  parts.append("## Core findings and significance");
  parts.append(QString("- %1").arg(findingsMd));
  parts.append(QString());
  parts.append("## Critical Limitations");
  parts.append(QString("- %1").arg(limitationsMd));
  parts.append(QString());
  parts.append("## Citable summary");
  parts.append(summary.citableSummary);

  return parts.join("\n");
}

QString toRef(const PaperSummary &summary) {

  QString refMd = summary.references.join("\n- ");

  QStringList parts;

  parts.append("## References");
  parts.append(QString("- %1").arg(refMd));
  parts.append(QString());

  return parts.join("\n");
}

QString toLatex(const PaperSummary &summary) {
  QString authors = summary.authors.join(", ");
  QString title = summary.title;
  if (!title.isEmpty())
    title[0] = title[0].toUpper();

  QString findingsItems;
  for (const QString &f : summary.coreFindingsAndSignificance)
    findingsItems += QString("\n    \\item %1").arg(f);

  QString limitationsItems;
  for (const QString &l : summary.criticalLimitations)
    limitationsItems += QString("\n    \\item %1").arg(l);

  QStringList parts;
  parts.append(QString("\\section{%1, (%2): %3}")
                   .arg(authors, QString::number(summary.year), title));
  parts.append("\\subsection{Research objective}");
  parts.append(summary.researchObjective);
  parts.append(QString());
  parts.append("\\subsection{Methodology}");
  parts.append(summary.methodology);
  parts.append(QString());
  parts.append("\\subsection{Core findings and significance}");
  parts.append(QString("\\begin{itemize}%1").arg(findingsItems));
  parts.append("\\end{itemize}");
  parts.append(QString());
  parts.append("\\subsection{Critical Limitations}");
  parts.append(QString("\\begin{itemize}%1").arg(limitationsItems));
  parts.append("\\end{itemize}");
  parts.append(QString());
  parts.append("\\subsection{Citable summary}");
  parts.append(summary.citableSummary);

  return parts.join("\n");
}

} // namespace SummaryHelpers

/* ── ModelHelper ───────────────────────────────────────────────── */

ModelHelper::ModelHelper(ConfigManager *config, QObject *parent)
    : QObject(parent), m_config(config) {
  m_networkManager = new QNetworkAccessManager(this);

  // Auto-reload when the active backend changes.
  QObject::connect(m_config, &ConfigManager::activeBackendChanged, this,
                   &ModelHelper::loadActiveBackendModels);

  // Reload local model list after a model file is copied.
  QObject::connect(m_config, &ConfigManager::modelCopied, this,
                   &ModelHelper::loadActiveBackendModels);

  // Initial load for the currently active backend.
  loadActiveBackendModels();
}

ModelHelper::~ModelHelper() = default;

QString ModelHelper::summaryToMarkdown(const QString &json) const {
  return SummaryHelpers::toMarkdown(SummaryHelpers::parseSummaryJson(json));
}

QString ModelHelper::summaryToRef(const QString &json) const {
  return SummaryHelpers::toRef(SummaryHelpers::parseSummaryJson(json));
}

QString ModelHelper::summaryToLatex(const QString &json) const {
  return SummaryHelpers::toLatex(SummaryHelpers::parseSummaryJson(json));
}

QStringList ModelHelper::listLocalModels() {
  if (!m_config)
    return QStringList();

  QString summarizersDir = m_config->summarizersPath();
  QDir dir(summarizersDir);
  if (!dir.exists())
    return QStringList();

  // Each model lives in its own subfolder (e.g. Summarizers/<model>/).
  // Scan subfolders and return the main .gguf path inside each.
  QStringList allDirs = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
  QStringList models;

  for (const QString &subdir : allDirs) {
    QDir subDir(summarizersDir + "/" + subdir);
    QStringList ggufFiles =
        subDir.entryList(QStringList{"*.gguf"}, QDir::Files | QDir::NoSymLinks);
    // Pick the first non-mmproj .gguf as the main model.
    for (const QString &f : ggufFiles) {
      if (!f.contains("mmproj", Qt::CaseInsensitive)) {
        models.append(subdir + "/" + f);
        break;
      }
    }
  }

  return models;
}

QStringList ModelHelper::getImagesForHash(const QString &paperHash,
                                          const QString &imgPath) const {
  QDir imgDir(imgPath);
  if (!imgDir.exists()) {
    qWarning() << "Image directory does not exist:" << imgPath;
    return QStringList();
  }

  imgDir.setNameFilters({paperHash + "_page_*.png"});
  const QFileInfoList matchingFiles = imgDir.entryInfoList();

  QStringList paths;
  paths.reserve(matchingFiles.size());
  for (const QFileInfo &fi : matchingFiles) {
    paths.append(fi.absoluteFilePath());
  }
  paths.sort();

  return paths;
}

void ModelHelper::checkRemoteModels(const QString &name, const QString &apiUrl,
                                    const QString &apiKey) {
  if (apiUrl.isEmpty()) {
    emit modelsChecked(false, QStringList(), "Invalid parameters");
    return;
  }

  // One-shot connection: when the fetch completes, save or report error.
  QObject::connect(
      this, &ModelHelper::modelsChecked, this,
      [this, name, apiUrl, apiKey](bool success, const QStringList & /*models*/,
                                   const QString & /*error*/) {
        if (success && m_config) {
          m_config->setBackend(name, apiUrl, apiKey);
          // Only set active if it's different — avoids triggering
          // activeBackendChanged and a re-entrant check loop.
          if (m_config->activeBackend() != name) {
            m_config->setActiveBackend(name);
          }
        }
      },
      Qt::SingleShotConnection);

  fetchRemoteModels(apiUrl, apiKey);
}

void ModelHelper::fetchRemoteModels(const QString &apiUrl,
                                    const QString &apiKey) {
  if (apiUrl.isEmpty()) {
    emit modelsChecked(false, QStringList(), "URL is empty");
    return;
  }

  // Cancel any in-flight request (reply will be cleaned up by its parent,
  // QNetworkAccessManager — we just null our watch pointer).
  if (m_currentReply) {
    m_currentReply->abort();
    m_currentReply = nullptr;
  }

  QUrl baseUrl(apiUrl);
  baseUrl.setPath(baseUrl.path() + "/models");

  QNetworkRequest request(baseUrl);
  if (!apiKey.isEmpty()) {
    request.setRawHeader("Authorization", ("Bearer " + apiKey).toUtf8());
  }

  m_currentReply = m_networkManager->get(request);
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
    QString err = reply->errorString();
    qDebug() << "[ModelHelper] Network error during models fetch:" << err;
    emit modelsChecked(false, QStringList(), err);
    return;
  }

  QByteArray data = reply->readAll();

  QJsonParseError parseErr;
  QJsonDocument doc = QJsonDocument::fromJson(data, &parseErr);
  if (parseErr.error != QJsonParseError::NoError) {
    QString err = QString("JSON parse error: %1").arg(parseErr.errorString());
    qDebug() << "[ModelHelper]" << err;
    emit modelsChecked(false, QStringList(), err);
    return;
  }

  QJsonObject root = doc.object();
  QJsonArray dataArray = root.value("data").toArray();

  QStringList models;
  for (const auto &elem : dataArray) {
    QJsonObject m = elem.toObject();
    QString id = m.value("id").toString();
    if (!id.isEmpty()) {
      models.append(id);
    }
  }

  qDebug() << "[ModelHelper] Received" << models.size()
           << "model(s) from server";
  emit modelsChecked(true, models, QString());
}

void ModelHelper::loadActiveBackendModels() {
  if (!m_config)
    return;

  QVariantMap cfg = m_config->backendConfig(m_config->activeBackend());
  QString name = cfg.value("name").toString();
  QString url = cfg.value("url").toString();
  QString key = cfg.value("api_key").toString();

  if (url.isEmpty()) {
    // Local backend — list .gguf files in Summarizers folder
    QStringList models = listLocalModels();
    emit modelsChecked(!models.isEmpty(), models, QString());
  } else {
    // Remote backend — fetch via HTTP
    checkRemoteModels(name, url, key);
  }
}
