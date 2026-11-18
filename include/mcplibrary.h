#pragma once

#include "sqlitedb.h"
#include "zoterosource.h"

#include <QHash>
#include <QJsonObject>
#include <QString>
#include <expected>
#include <memory>

class McpServer;
class SqliteVecManager;

/// The read-only MCP tools over the user's library: search, list, summary,
/// page text, citations — for the project active in HodeIA.
///
/// Runs in the `Hodeia --mcp` process, next to a possibly open GUI app:
///  - the settings are only read (config.json, at every call, so a project
///    switch in HodeIA is followed without restarting the agent);
///  - the project database is opened as is, never created nor migrated
///    (HodeIA does that when it opens the project);
///  - the E5 embedding model is loaded on the first search only.
///
/// Papers are named by a short id (the start of their hash) shown as
/// "[@95f8ed154398]", pages as "[@95f8ed154398:p12]" (CitationFormat).
class McpLibrary {
public:
  struct Paths {
    QString configFile;     ///< config.json (lastProject)
    QString projectsRoot;   ///< Projects/<name>/<name>.db
    QString embeddingModel; ///< E5 .gguf (empty: search unavailable)
  };
  /// The app's real locations (AppPaths).
  static Paths defaultPaths();

  explicit McpLibrary(Paths paths);
  ~McpLibrary();

  /// Expose the tools on `server`.
  void registerTools(McpServer &server);

  /* Tools — the text for the model, or an error message for it. */
  std::expected<QString, QString> searchLibrary(const QJsonObject &args);
  std::expected<QString, QString> listPapers(const QJsonObject &args);
  std::expected<QString, QString> getSummary(const QJsonObject &args);
  std::expected<QString, QString> getPage(const QJsonObject &args);
  std::expected<QString, QString> getCitations(const QJsonObject &args);

private:
  /// Open the database of the project active in HodeIA (reopened when it
  /// changed since the last call).
  std::expected<void, QString> openActiveProject();
  /// Full hash of the paper an id names (a hash prefix, possibly written as
  /// a mark "[@id]" or "[@id:p3]").
  std::expected<QString, QString> resolvePaper(const QString &id);
  /// "Authors (year) — Title" of a paper.
  QString label(const QString &paperHash);
  /// Zotero's metadata by paper hash when they take precedence
  /// (zoteroPriority), else empty.
  QHash<QString, ZoteroMeta> zoteroMeta();

  Paths m_paths;
  SqliteDb m_db;
  QString m_project; ///< project whose database is open
  bool m_zoteroPriority = false; ///< config.json, read at each call
  std::unique_ptr<SqliteVecManager> m_embeddings;
  bool m_modelLoaded = false;
};
