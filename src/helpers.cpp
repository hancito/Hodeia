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
#include <QPdfWriter>
#include <QStandardPaths>
#include <QTextDocument>
#include <QThreadPool>
#include <QTimer>
#include <QUrl>

/* ── Constructor / Destructor ────────────────────────────────── */

Helpers::Helpers(ConfigManager *config, QObject *parent)
    : QObject(parent), m_config(config) {
  m_networkManager = new QNetworkAccessManager(this);

  QObject::connect(m_config, &ConfigManager::activeBackendChanged, this,
                   &Helpers::loadActiveBackendModels);
  QObject::connect(m_config, &ConfigManager::modelCopied, this,
                   &Helpers::loadActiveBackendModels);

  QObject::connect(this, &Helpers::modelCopyProgress, m_config,
                   &ConfigManager::modelCopyProgress);
  QObject::connect(this, &Helpers::modelCopyFinished, m_config,
                   &ConfigManager::modelCopyFinished);
  QObject::connect(this, &Helpers::modelCopied, m_config,
                   &ConfigManager::modelCopied);
  QObject::connect(this, &Helpers::modelCopied, this,
                   &Helpers::refreshLocalModelList, Qt::QueuedConnection);

  refreshLocalModelList();
  loadActiveBackendModels();
}

Helpers::~Helpers() = default;

/* ── Summary parsing ─────────────────────────────────────────── */

PaperSummary Helpers::parseSummaryJson(const QString &raw) {
  PaperSummary summary;
  QString text = raw;

  // Strip markdown code fences (```json ... ``` or ``` ... ```)
  {
    int firstFence = text.indexOf("```");
    if (firstFence >= 0) {
      text = text.mid(firstFence + 3);
      int nl = text.indexOf('\n');
      if (nl >= 0 && nl < 10)
        text = text.mid(nl + 1);
      int lastFence = text.lastIndexOf("```");
      if (lastFence > 0)
        text = text.mid(0, lastFence);
    }
  }

  // Strip reasoning prefix — jump to first '{'
  {
    int bracePos = text.indexOf('{');
    if (bracePos > 0)
      text = text.mid(bracePos);
    int endBrace = text.lastIndexOf('}');
    if (endBrace > 0)
      text = text.mid(0, endBrace + 1);
  }

  text = text.trimmed();

  if (!text.startsWith("{") && !text.startsWith("```")) {
    int bracePos = text.indexOf('{');
    if (bracePos < 0) {
      qWarning() << "[Helpers] No JSON object found (length:" << raw.length()
                 << ")";
      return summary;
    }
    if (bracePos > raw.length() * 0.5)
      qWarning() << "[Helpers] JSON brace at" << bracePos << "of"
                 << raw.length() << "— likely truncated";
  }

  QJsonParseError err;
  QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8(), &err);
  if (err.error != QJsonParseError::NoError) {
    qWarning() << "[Helpers] JSON parse error:" << err.errorString();
    qWarning() << "[Helpers] offset:" << err.offset << "| context:"
               << text.mid(qMax(0, err.offset - 30), 60).replace('\n', " ");
    if (text.length() < 200)
      qWarning() << "[Helpers] full cleaned text:" << text;
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

/* ── HTML / LaTeX formatting ─────────────────────────────────── */

// Escape a text field so LLM free-text (which may contain <, >, &) is treated
// as literal content, not markup, when building the summary HTML.
static QString escapeHtml(const QString &text) {
  QString out = text;
  out.replace(QStringLiteral("&"), QStringLiteral("&amp;"));
  out.replace(QStringLiteral("<"), QStringLiteral("&lt;"));
  out.replace(QStringLiteral(">"), QStringLiteral("&gt;"));
  out.replace(QStringLiteral("\""), QStringLiteral("&quot;"));
  return out;
}

// Build a <ul> of escaped items; empty list yields an empty string.
static QString toHtmlList(const QStringList &items) {
  if (items.isEmpty())
    return QString();
  QStringList li;
  for (const QString &item : items)
    li.append(QStringLiteral("<li>%1</li>").arg(escapeHtml(item)));
  return QStringLiteral("<ul>%1</ul>").arg(li.join(QString()));
}

QString Helpers::toHtml(const PaperSummary &s) {
  QString authors = s.authors.join(QStringLiteral(", "));
  QString title = s.title;
  if (!title.isEmpty())
    title[0] = title[0].toUpper();
  QString topics = s.topics.join(QStringLiteral(", "));

  // Font family is intentionally NOT set here: the same HTML is rendered both
  // in QML (Google Sans via Fonts.ui) and in the PDF (EB Garamond via
  // kDefaultPdfStyle / default font). Each renderer supplies its own font.
  QString html;
  html += QStringLiteral("<h2>%1</h2>").arg(escapeHtml(title));
  html += QStringLiteral("<h4>%1 (%2)</h4>")
              .arg(escapeHtml(authors), QString::number(s.year));
  html += QStringLiteral("<h4><em>Journal: %1</em></h4>")
              .arg(escapeHtml(s.journal));
  if (!topics.isEmpty())
    html +=
        QStringLiteral("<h4><em>Topics: %1</em></h4>").arg(escapeHtml(topics));
  html += QStringLiteral("<h3>Research objective</h3>");
  html += QStringLiteral("<p>%1</p>").arg(escapeHtml(s.researchObjective));
  html += QStringLiteral("<h3>Methodology</h3>");
  html += QStringLiteral("<p>%1</p>").arg(escapeHtml(s.methodology));
  html += QStringLiteral("<h3>Core findings and significance</h3>");
  html += toHtmlList(s.coreFindingsAndSignificance);
  html += QStringLiteral("<h3>Critical Limitations</h3>");
  html += toHtmlList(s.criticalLimitations);
  html += QStringLiteral("<h3>Citable summary</h3>");
  html += QStringLiteral("<p>%1</p>").arg(escapeHtml(s.citableSummary));
  return html;
}

QString Helpers::toLatex(const PaperSummary &s) {
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

/* ── File copy helper (static, used by thread-pool tasks) ────── */

static bool copyFileToDir(const QString &sourcePath, const QString &destDir,
                          Helpers *helper) {
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

/* ── Summary QML wrappers ────────────────────────────────────── */

QString Helpers::summaryToHtml(const QString &json) const {
  return toHtml(parseSummaryJson(json));
}

QString Helpers::summaryToLatex(const QString &json) const {
  return toLatex(parseSummaryJson(json));
}

/* ── PDF export ──────────────────────────────────────────────── */

static const QString kDefaultPdfStyle =
    "body { font-family: 'EB Garamond'; font-size: 11pt; }"
    "h1 { font-family: 'EB Garamond'; font-size: 22pt; font-weight: bold; }"
    "h2 { font-family: 'EB Garamond'; font-size: 16pt; font-weight: bold; }"
    "h3 { font-family: 'EB Garamond'; font-size: 13pt; font-weight: bold; }"
    "code { font-family: 'EB Garamond'; font-size: 10pt; }";

// Footer appended as the last block of the document: a thin rule above the
// centered, muted attribution line.
static const QString kPdfFooterHtml =
    "<hr><p style=\"text-align:center; font-family:'EB Garamond'; "
    "font-size:8pt; color:#888;\">Generated with Hodeia</p>";

// Single PDF backend: all output is rendered as HTML. Markdown is only a
// convenience source that gets converted to HTML before reaching this.
static QString writeHtmlToPdf(const QString &html, const QString &destPath,
                              const QString &extraStyle = QString()) {
  QString path = destPath;
  if (path.startsWith("file://"))
    path = QUrl(path).toLocalFile();

  QPdfWriter writer(path);
  writer.setCreator("Hodeia 1.0");
  writer.setPageSize(QPageSize(QPageSize::A4));
  writer.setResolution(300);
  writer.setPageMargins(QMarginsF(10, 10, 10, 10), QPageLayout::Millimeter);
  // left, top, right, bottom

  QString style = kDefaultPdfStyle;
  if (!extraStyle.isEmpty())
    style += extraStyle;

  QTextDocument doc;
  doc.setDefaultFont(QFont("EB Garamond", 11));
  doc.setDefaultStyleSheet(style);
  // Append the footer as the last block of the document.
  doc.setHtml(html + kPdfFooterHtml);

  doc.print(&writer);

  return QFile::exists(path) ? path : QString();
}

QString Helpers::exportSummaryToPdf(const QString &json,
                                    const QString &destPath) const {
  if (json.isEmpty())
    return QString();
  return writeHtmlToPdf(toHtml(parseSummaryJson(json)), destPath);
}

QString Helpers::generateReport(const QStringList &jsonList,
                                const QString &destPath) const {
  if (jsonList.isEmpty())
    return QString();

  QStringList sections;
  for (const QString &json : jsonList) {
    if (json.isEmpty())
      continue;
    sections.append(toHtml(parseSummaryJson(json)));
  }
  if (sections.isEmpty())
    return QString();

  QString html =
      "<h1 style=\"text-align:center\">Research Summary Report</h1>"
      "<h5 style=\"text-align:center; font-size:8pt; font-style:italic; "
      "color:#888;\">Made with Hodeia</h5>"
      "<hr>" +
      sections.join("<hr>");
  return writeHtmlToPdf(html, destPath);
}

/* ── Local model listing ─────────────────────────────────────── */

void Helpers::refreshLocalModelList() {
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

QStringList Helpers::localModelList() { return m_localModelList; }

void Helpers::deleteLocalModels(const QStringList &folderNames) {
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
      qDebug() << "[Helpers] Local model removed:" << folder;
    }
  }
  refreshLocalModelList();
  loadActiveBackendModels();
}

QStringList Helpers::listLocalModels() {
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

/* ── Remote backend ──────────────────────────────────────────── */

void Helpers::checkRemoteModels(const QString &name, const QString &apiUrl,
                                const QString &apiKey) {
  if (apiUrl.isEmpty()) {
    emit modelsChecked(false, {}, "Invalid parameters");
    return;
  }
  QObject::connect(
      this, &Helpers::modelsChecked, this,
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

void Helpers::fetchRemoteModels(const QString &apiUrl, const QString &apiKey) {
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

void Helpers::handleModelsReply(QNetworkReply *reply) {
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
  qDebug() << "[Helpers] Received" << models.size() << "model(s) from server";
  emit modelsChecked(true, models, QString());
}

void Helpers::loadActiveBackendModels() {
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

/* ── Model copy (parallel, non-blocking) ─────────────────────── */

void Helpers::addLocalSummarizer(const QString &sourcePath) {
  addLocalSummarizerWithMmproj(sourcePath, QString());
}

void Helpers::addLocalSummarizerWithMmproj(const QString &modelPath,
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

  auto *done = new QAtomicInt(!mmprojSrc.isEmpty() ? 2 : 1);
  QAtomicInt *modelOk = new QAtomicInt{0};
  QAtomicInt *mmprojOk = new QAtomicInt{1};

  Helpers *self = this;

  QThreadPool::globalInstance()->start(
      [self, src = modelSrc, folder = modelFolder, done, modelOk]() {
        *modelOk = copyFileToDir(src, folder, self) ? 1 : 0;
        done->fetchAndAddRelaxed(-1);
      });

  if (!mmprojSrc.isEmpty()) {
    QThreadPool::globalInstance()->start(
        [self, src = mmprojSrc, folder = modelFolder, done, mmprojOk]() {
          *mmprojOk = copyFileToDir(src, folder, self) ? 1 : 0;
          done->fetchAndAddRelaxed(-1);
        });
  }

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
