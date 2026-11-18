#include "apppaths.h"

#include <QCoreApplication>
#include <QFile>
#include <QStandardPaths>

namespace AppPaths {

QString dataDir() {
  return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}

QString configFile() { return dataDir() + "/config.json"; }

QString projectsRoot() { return dataDir() + "/Projects"; }

QString projectDbPath(const QString &projectName) {
  return projectsRoot() + "/" + projectName + "/" + projectName + ".db";
}

QString projectImgPath(const QString &projectName) {
  return projectsRoot() + "/" + projectName + "/IMG";
}

QString modelsRoot() { return dataDir() + "/Models"; }

QString embeddersPath() { return modelsRoot() + "/Embedders"; }

QString summarizersPath() { return modelsRoot() + "/Summarizers"; }

QString bundledModelsRoot() {
  return QCoreApplication::applicationDirPath() + "/Resources/Models";
}

QString embeddingModel() {
  for (const QString &dir :
       {embeddersPath(), bundledModelsRoot() + "/Embedders"}) {
    const QString path = dir + "/" + kEmbeddingModelFile;
    if (QFile::exists(path))
      return path;
  }
  return {};
}

} // namespace AppPaths
