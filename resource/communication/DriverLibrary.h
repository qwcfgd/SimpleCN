#pragma once
#include <QCoreApplication>
#include <QDir>
#include <QString>
namespace communication {
inline QString driverDirectory(const QString &vendor)
{
    return QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(
        QStringLiteral("driver/") + vendor + (sizeof(void*) == 8 ? "/x64" : "/x86"));
}
inline QString driverLibraryPath(const QString &vendor, const QString &name)
{
    return QDir::toNativeSeparators(QDir(driverDirectory(vendor)).absoluteFilePath(name));
}
}
