#include "Version.h"

#include "app/version.h"
#include "core/utils.h"

const QVersionNumber Version::versionNumber = QVersionNumber(APP_VERSION_MAJOR, APP_VERSION_MINOR, APP_VERSION_PATCH);

const QString Version::gitRevision = APP_VERSION_GIT_REV;
const QString Version::gitBranch   = APP_VERSION_GIT_BRANCH;

const QString   Version::shortVersion = versionNumber.toString();
const QDateTime Version::buildTime    = BuildTime::compileTime();
const QString   Version::longVersion  = QString("%1_git%2-%3_%4")
                                         .arg(shortVersion)
                                         .arg(gitRevision)
                                         .arg(gitBranch)
                                         .arg(buildTime.toString(Qt::ISODate));
