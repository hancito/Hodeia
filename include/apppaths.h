#pragma once

#include <QString>

/// Where HodeIA keeps its files.  Pure path computations: nothing is created
/// or written, so they are safe from any process (the MCP server runs next to
/// the GUI app and must not touch its settings).
namespace AppPaths {

/// Per-user data folder (%APPDATA%/Hodeia, ~/.local/share/Hodeia...).
QString dataDir();
/// The settings file (config.json).
QString configFile();
/// Folder of the projects (Projects/<name>/<name>.db + IMG/).
QString projectsRoot();
QString projectDbPath(const QString &projectName);
QString projectImgPath(const QString &projectName);
/// The user's model tree (Models/Embedders, Models/Summarizers).
QString modelsRoot();
QString embeddersPath();
QString summarizersPath();
/// The model tree shipped with the app (Resources/Models next to the
/// executable), copied into modelsRoot() by the GUI app at startup.
QString bundledModelsRoot();

/// File name of the E5 embedding model.
inline constexpr const char *kEmbeddingModelFile =
    "multilingual-e5-small-q8_0.gguf";

/// The E5 embedding model: the user's copy, else the bundled one; empty if
/// neither exists.
QString embeddingModel();

} // namespace AppPaths
