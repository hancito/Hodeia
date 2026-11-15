#include "helpers.h"
#include "configmanager.h"
#include "pdfexport.h"
#include "textformat.h"

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
#include <QStack>
#include <QStandardPaths>
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
    // Small LLMs produce a predictable set of malformed-JSON patterns.
    // Apply a progressive set of repairs (each retried against the parser)
    // so we recover as much of the summary as possible instead of returning
    // an empty object.  Strategy order is cheapest/most-likely first.
    const QJsonParseError::ParseError origError = err.error;

    // ── Repair 1: drop trailing / duplicated commas before a closer ──
    // (e.g. `...positions.", }` or `["a","b",]`)
    auto dropTrailingCommas = [](const QString &in) {
      QString out;
      out.reserve(in.length());
      bool inString = false;
      for (int i = 0; i < in.length(); ++i) {
        const QChar c = in[i];
        if (inString) {
          out += c;
          if (c == '\\' && i + 1 < in.length()) {
            ++i;
            out += in[i];
          } else if (c == '"') {
            inString = false;
          }
          continue;
        }
        if (c == '"') {
          inString = true;
          out += c;
          continue;
        }
        if (c == ',') {
          int j = i + 1;
          while (j < in.length() && (in[j] == ',' || in[j].isSpace()))
            ++j;
          if (j < in.length() && (in[j] == '}' || in[j] == ']'))
            continue; // drop the comma run
        }
        out += c;
      }
      return out;
    };

    // ── Repair 2: close any unterminated string at the very end ──
    // The model bailing mid-value leaves a dangling `"...` with no closing
    // quote.  If the trailing run of non-quote chars after the last '"' is
    // the tail of an unterminated string, close it.
    auto closeUnterminatedString = [](const QString &in) {
      // Count whether the number of unescaped quotes is odd; if so, the
      // final string is unterminated.
      int quotes = 0;
      for (int i = 0; i < in.length(); ++i) {
        if (in[i] == '\\') {
          ++i;
          continue;
        }
        if (in[i] == '"')
          ++quotes;
      }
      if (quotes % 2 == 1)
        return in + "\"";
      return in;
    };

    // ── Repair 3: append missing closers so the object/array balance ──
    // The model bailing mid-object leaves `{...` with an unclosed `{`/`[`.
    // Walk the structure (string-aware) and append the needed `]`/`}` in
    // the correct order, dropping a trailing comma first.
    auto appendMissingClosers = [](const QString &in) {
      QString s = in;
      // Drop a trailing comma + whitespace that would precede the closers.
      while (!s.isEmpty() && s.at(s.length() - 1).isSpace())
        s.chop(1);
      if (!s.isEmpty() && s.at(s.length() - 1) == ',')
        s.chop(1);

      QStack<QString> openers;
      bool inString = false;
      for (int i = 0; i < s.length(); ++i) {
        const QChar c = s[i];
        if (inString) {
          if (c == '\\' && i + 1 < s.length())
            ++i;
          else if (c == '"')
            inString = false;
          continue;
        }
        if (c == '"') {
          inString = true;
        } else if (c == '{') {
          openers.push(QStringLiteral("}"));
        } else if (c == '[') {
          openers.push(QStringLiteral("]"));
        } else if (c == '}' || c == ']') {
          if (!openers.isEmpty())
            openers.pop();
        }
      }
      if (inString)
        s += QLatin1Char('"'); // close a dangling string first
      while (!openers.isEmpty())
        s += openers.pop();
      return s;
    };

    QString candidate = text;
    const char *repairName = "none";
    doc = QJsonDocument::fromJson(candidate.toUtf8(), &err);

    if (err.error != QJsonParseError::NoError) {
      candidate = dropTrailingCommas(text);
      repairName = "trailing-comma";
      doc = QJsonDocument::fromJson(candidate.toUtf8(), &err);
    }
    if (err.error != QJsonParseError::NoError) {
      candidate = closeUnterminatedString(dropTrailingCommas(text));
      repairName = "unterminated-string";
      doc = QJsonDocument::fromJson(candidate.toUtf8(), &err);
    }
    if (err.error != QJsonParseError::NoError) {
      candidate = appendMissingClosers(
          closeUnterminatedString(dropTrailingCommas(text)));
      repairName = "missing-closers";
      doc = QJsonDocument::fromJson(candidate.toUtf8(), &err);
    }

    if (err.error != QJsonParseError::NoError) {
      qWarning() << "[Helpers] JSON parse error (unrecovered):"
                 << err.errorString()
                 << "| original error:" << static_cast<int>(origError)
                 << "| offset:" << err.offset << "| context:"
                 << text.mid(qMax(0, err.offset - 30), 60).replace('\n', " ");
      if (text.length() < 200)
        qWarning() << "[Helpers] full cleaned text:" << text;
      return summary;
    }
    if (repairName != QLatin1String("none"))
      qInfo() << "[Helpers] Repaired malformed summary JSON via" << repairName;
  }

  // Models often emit LaTeX despite the prompt; convert it to Unicode here so
  // every consumer (UI, HTML/PDF export) and already-stored summaries benefit.
  auto clean = [](const QJsonValue &v) {
    return TextFormat::latexToUnicode(v.toString(),
                                      TextFormat::Target::PlainText);
  };

  QJsonObject obj = doc.object();
  summary.title = clean(obj.value("title"));
  summary.journal = obj.value("journal").toString();
  summary.year = obj.value("year").toInt();
  summary.researchObjective = clean(obj.value("researchObjective"));
  summary.methodology = clean(obj.value("methodology"));
  for (const auto &f : obj.value("coreFindingsAndSignificance").toArray())
    summary.coreFindingsAndSignificance.append(clean(f));
  for (const auto &l : obj.value("criticalLimitations").toArray())
    summary.criticalLimitations.append(clean(l));
  summary.citableSummary = clean(obj.value("citableSummary"));
  for (const auto &a : obj.value("authors").toArray())
    summary.authors.append(a.toString());
  for (const auto &t : obj.value("topics").toArray())
    summary.topics.append(t.toString());
  return summary;
}

/* ── HTML formatting ─────────────────────────────────── */

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

  // Heap-allocated buffer.  This runs on a QThreadPool worker thread whose
  // default stack is only 1 MB on Windows; a 1 MB *stack* array here overflowed
  // the stack and crashed the process on the first copy.
  const qint64 bufSize = 1024 * 1024;
  const QByteArray buf =
      QByteArray(static_cast<int>(bufSize), Qt::Uninitialized);
  qint64 copied = 0;
  while (!src.atEnd()) {
    const qint64 n = src.read(const_cast<char *>(buf.constData()), bufSize);
    if (n <= 0)
      break;
    copied += dest.write(buf.constData(), n);
    if (helper)
      QMetaObject::invokeMethod(
          helper,
          [helper, fileName, pct = static_cast<int>(copied * 100 / total)] {
            Q_EMIT helper->modelCopyProgress(pct, fileName);
          },
          Qt::QueuedConnection);
  }
  dest.close();
  src.close();

  if (copied != total) {
    QFile::remove(destPath);
    return false;
  }
  if (helper)
    QMetaObject::invokeMethod(
        helper,
        [helper, fileName] { Q_EMIT helper->modelCopyProgress(100, fileName); },
        Qt::QueuedConnection);
  return true;
}

/* ── Summary QML wrappers ────────────────────────────────────── */

QString Helpers::summaryToHtml(const QString &json) const {
  return toHtml(parseSummaryJson(json));
}

/* ── PDF export ──────────────────────────────────────────────── */

// A single summary is a one-paper report: same layout, without the report
// title.
static QString reportToHtml(const QList<PaperSummary> &papers) {
  QString html;
  if (papers.size() > 1)
    html += QStringLiteral(
        "<h1 class=\"center\">Research Summary Report</h1><hr>");
  // Summaries flow on one after another, separated by a rule.
  QStringList sections;
  for (const PaperSummary &paper : papers)
    sections.append(Helpers::toHtml(paper));
  return html + sections.join(QStringLiteral("<hr>"));
}

QString Helpers::exportPdf(const QStringList &jsonList,
                           const QString &destPath) const {
  QList<PaperSummary> papers;
  for (const QString &json : jsonList)
    if (!json.isEmpty())
      papers.append(parseSummaryJson(json));
  if (papers.isEmpty())
    return QString();
  return PdfExport::writeHtml(reportToHtml(papers), destPath);
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

QString Helpers::localFileUrl(const QString &path) const {
  return QUrl::fromLocalFile(path).toString();
}

/* ── Remote backend ──────────────────────────────────────────── */

void Helpers::checkRemoteModels(const QString &name, const QString &apiUrl,
                                const QString &apiKey) {
  if (apiUrl.isEmpty()) {
    emit modelsChecked(false, {}, tr("Invalid parameters"));
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
    emit modelsChecked(false, {}, tr("URL is empty"));
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
    emit modelsChecked(false, {}, tr("Invalid JSON"));
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

// Normalize a path that may arrive as a file: URL (from QFileDialog's
// selectedFile) or as a raw native path (after a QML "file://" strip).
// QUrl::fromUserInput round-trips both forms to the correct local path on
// every platform, so the downstream copy logic always gets a clean path.
static QString normalizeLocalPath(const QString &in) {
  if (in.isEmpty())
    return {};
  if (in.startsWith(QLatin1String("file://"), Qt::CaseInsensitive))
    return QUrl(in).toLocalFile();
  return QUrl::fromUserInput(in).toLocalFile();
}

void Helpers::addLocalSummarizerWithMmproj(const QString &modelPath,
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
