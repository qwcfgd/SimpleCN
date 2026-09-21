#include "PathFileDialog.h"
#include "localization/Language.h"
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QShortcut>
#include <QKeyEvent>
namespace host {
PathFileDialog::PathFileDialog(QWidget *parent,const QString &title,const QString &directory,const QString &filter)
    :QFileDialog(parent,title,directory,filter){
    setObjectName("pathFileDialog");setOption(QFileDialog::DontUseNativeDialog);setFileMode(QFileDialog::ExistingFile);
    auto *bar=new QWidget(this);auto *layout=new QVBoxLayout(bar);layout->setContentsMargins(0,0,0,6);
    auto *row=new QHBoxLayout;auto *label=new QLabel("路径",bar);m_path=new QLineEdit(bar);m_path->setObjectName("fileAddress");
    m_path->setMaxLength(32767);m_path->setPlaceholderText("粘贴目录或完整文件路径，按回车定位（Ctrl+L）");label->setBuddy(m_path);
    auto *go=new QPushButton("转到",bar);go->setObjectName("fileAddressGo");go->setAutoDefault(false);
    row->addWidget(label);row->addWidget(m_path,1);row->addWidget(go);layout->addLayout(row);
    m_error=new QLabel(bar);m_error->setObjectName("fileAddressError");m_error->setWordWrap(true);m_error->hide();layout->addWidget(m_error);
    this->layout()->setMenuBar(bar);m_path->setText(QDir::toNativeSeparators(this->directory().absolutePath()));
    connect(m_path,&QLineEdit::textEdited,this,[this]{m_pathEdited=true;m_error->hide();});
    m_path->installEventFilter(this);
    connect(go,&QPushButton::clicked,this,[this]{navigateToPath();});
    connect(this,&QFileDialog::directoryEntered,this,[this](const QString &path){m_path->setText(QDir::toNativeSeparators(path));m_pathEdited=false;m_error->hide();});
    auto *focusPath=new QShortcut(QKeySequence(Qt::CTRL|Qt::Key_L),this);
    connect(focusPath,&QShortcut::activated,this,[this]{m_path->setFocus();m_path->selectAll();});
}
bool PathFileDialog::eventFilter(QObject *object,QEvent *event){
    if(object==m_path&&event->type()==QEvent::KeyPress){const auto *key=static_cast<QKeyEvent*>(event);
        if(key->key()==Qt::Key_Return||key->key()==Qt::Key_Enter){navigateToPath();return true;}}
    return QFileDialog::eventFilter(object,event);
}
bool PathFileDialog::navigateToPath(){
    QString path=m_path->text().trimmed();if(path.size()>1&&path.startsWith('"')&&path.endsWith('"'))path=path.mid(1,path.size()-2);
    path=QDir::fromNativeSeparators(path);if(QDir::isRelativePath(path))path=directory().absoluteFilePath(path);
    const QFileInfo info(path);
    if(!info.exists()||(!info.isDir()&&!info.isFile())){m_error->setText(Language::text("路径不存在或无法访问"));m_error->show();return false;}
    m_pathEdited=false;m_error->hide();
    if(info.isDir()){setDirectory(info.absoluteFilePath());m_path->setText(QDir::toNativeSeparators(info.absoluteFilePath()));return false;}
    setDirectory(info.absolutePath());selectFile(info.absoluteFilePath());m_path->setText(QDir::toNativeSeparators(info.absoluteFilePath()));return true;
}
void PathFileDialog::accept(){
    if(m_pathEdited&&!navigateToPath())return;
    QFileDialog::accept();
}
QString PathFileDialog::getOpenFileName(QWidget *parent,const QString &title,const QString &directory,const QString &filter){
    PathFileDialog dialog(parent,title,directory,filter);
    return dialog.exec()==QDialog::Accepted?dialog.selectedFiles().value(0):QString();
}
}
