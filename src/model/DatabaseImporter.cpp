#include "DatabaseImporter.h"
#include <QFile>
#include <QFileInfo>
#include <QCryptographicHash>
#ifdef Q_OS_WIN
#include <windows.h>
#endif
namespace host::signal {
ImportResult DatabaseImporter::load(const QString&path,Bus bus){
    QFile file(path);if(!file.open(QIODevice::ReadOnly))return {{},path+": "+file.errorString()};
    if(file.size()>32*1024*1024)return {{},"数据库超过 32 MiB 读取上限"};
    const auto bytes=file.readAll();return bus==Bus::Can?dbc(bytes,QFileInfo(path).absoluteFilePath()):ldf(bytes,QFileInfo(path).absoluteFilePath());
}
bool DatabaseImporter::prepare(const QByteArray&bytes,const QString&path,DatabaseDefinition&db,QString&text,QString&error){
    if(bytes.isEmpty()){error="数据库文件为空";return false;}
    QByteArray data=bytes;if(data.startsWith(QByteArray::fromHex("efbbbf")))data.remove(0,3);
    text=QString::fromUtf8(data);
    if(text.toUtf8()!=data){
#ifdef Q_OS_WIN
        // Automotive databases commonly use the Chinese ANSI encoding. Decode
        // strictly, independently of the workstation's active ANSI code page.
        const int count=MultiByteToWideChar(54936,MB_ERR_INVALID_CHARS,data.constData(),data.size(),nullptr,0);
        if(count>0){
            std::wstring decoded(size_t(count),L'\0');
            if(MultiByteToWideChar(54936,MB_ERR_INVALID_CHARS,data.constData(),data.size(),decoded.data(),count)==count){
                text=QString::fromStdWString(decoded);
                db.diagnostics.append("源文件使用 GB18030/GBK 编码，已转换为 Unicode；原件未修改");
            }else text.clear();
        }else text.clear();
#else
        text.clear();
#endif
        if(text.isEmpty()){error="文件不是有效 UTF-8 或 GB18030/GBK 文本（原件未修改）";return false;}
    }
    if(text.contains(QChar(0))){error="数据库文本包含 NUL 字符";return false;}
    db.path=path;db.sha256=QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());return true;
}
}
