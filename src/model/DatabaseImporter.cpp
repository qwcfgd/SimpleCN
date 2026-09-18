#include "DatabaseImporter.h"
#include <QFile>
#include <QFileInfo>
#include <QCryptographicHash>
namespace host::signal {
ImportResult DatabaseImporter::load(const QString&path,Bus bus){
    QFile file(path);if(!file.open(QIODevice::ReadOnly))return {{},path+": "+file.errorString()};
    if(file.size()>32*1024*1024)return {{},"数据库超过 32 MiB 读取上限"};
    const auto bytes=file.readAll();return bus==Bus::Can?dbc(bytes,QFileInfo(path).absoluteFilePath()):ldf(bytes,QFileInfo(path).absoluteFilePath());
}
bool DatabaseImporter::prepare(const QByteArray&bytes,const QString&path,DatabaseDefinition&db,QString&text,QString&error){
    if(bytes.isEmpty()){error="数据库文件为空";return false;}
    QByteArray data=bytes;if(data.startsWith(QByteArray::fromHex("efbbbf")))data.remove(0,3);
    text=QString::fromUtf8(data);if(text.toUtf8()!=data||text.contains(QChar(0))){error="文件不是有效 UTF-8；请提供 UTF-8 副本（原件未修改）";return false;}
    db.path=path;db.sha256=QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());return true;
}
}
