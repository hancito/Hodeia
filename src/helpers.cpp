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
  summary.coreFindingsAndSignificance =
      obj.value("coreFindingsAndSignificance").toString();
  summary.criticalLimitations = obj.value("criticalLimitations").toString();
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

// Split a field that may be a bulleted list or a plain paragraph into lines.
static QStringList splitLines(const QString &text) {
  QStringList lines;
  for (const QString &line : text.split('\n', Qt::SkipEmptyParts)) {
    QString trimmed = line.trimmed();
    if (trimmed.startsWith("- ") || trimmed.startsWith("* "))
      trimmed = trimmed.mid(2);
    else if (trimmed.startsWith("• ") || trimmed.startsWith("– "))
      trimmed = trimmed.mid(2);
    if (!trimmed.isEmpty())
      lines.append(trimmed);
  }
  return lines;
}

QString toMarkdown(const PaperSummary &summary) {
  QString authors = summary.authors.join(", ");
  QString title = summary.title;
  if (!title.isEmpty())
    title[0] = title[0].toUpper();

  QStringList findings = splitLines(summary.coreFindingsAndSignificance);
  QStringList limitations = splitLines(summary.criticalLimitations);

  QString findingsMd = findings.join("\n- ");
  QString limitationsMd = limitations.join("\n- ");

  return QString("## %1, (%2): %3\n"
                 "\n"
                 "### %4\n"
                 "\n"
                 "## Research objective\n"
                 "%5\n"
                 "\n"
                 "## Methodology\n"
                 "%6\n"
                 "\n"
                 "## Core findings and significance\n"
                 "- %7\n"
                 "\n"
                 "## Critical Limitations\n"
                 "- %8\n"
                 "\n"
                 "## Citable summary\n"
                 "%9")
      .arg(authors, QString::number(summary.year), title, summary.journal,
           summary.researchObjective, summary.methodology, findingsMd,
           limitationsMd, summary.citableSummary);
}

QString toLatex(const PaperSummary &summary) {
  QString authors = summary.authors.join(", ");
  QString title = summary.title;
  if (!title.isEmpty())
    title[0] = title[0].toUpper();

  QStringList findings = splitLines(summary.coreFindingsAndSignificance);
  QStringList limitations = splitLines(summary.criticalLimitations);

  QString findingsItems;
  for (const QString &f : findings)
    findingsItems += QString("\n    \\item %1").arg(f);

  QString limitationsItems;
  for (const QString &l : limitations)
    limitationsItems += QString("\n    \\item %1").arg(l);

  return QString(R"(\section{%1, (%2): %3}
\subsection{Research objective}
%4

\subsection{Methodology}
%5

\subsection{Core findings and significance}
\begin{itemize}%6
\end{itemize}

\subsection{Critical Limitations}
\begin{itemize}%7
\end{itemize}

\subsection{Citable summary}
%8)")
      .arg(authors, QString::number(summary.year), title,
           summary.researchObjective, summary.methodology, findingsItems,
           limitationsItems, summary.citableSummary);
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
