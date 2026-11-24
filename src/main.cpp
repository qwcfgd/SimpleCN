#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFont>
#include <QFileInfo>
#include <QMessageBox>
#include <QSettings>
#include "localization/Language.h"
#include "views/MainWindow.h"
int main(int argc,char **argv){
    QApplication app(argc,argv);QApplication::setStyle("Fusion");
    app.setApplicationName(host::MainWindowInitialValues::applicationName);app.setApplicationVersion(host::MainWindowInitialValues::version);
    app.setOrganizationName(host::MainWindowInitialValues::organizationName);app.setFont(QFont(host::MainWindowInitialValues::fontFamily,host::MainWindowInitialValues::fontSize));
    QCommandLineParser parser;parser.addHelpOption();parser.addVersionOption();
    parser.addOption({"demo","Start with simulated adapters; no physical bus operations."});
    parser.addOption({"config","Save configuration to this file; use Load Configuration to restore channels.","file",QCoreApplication::applicationDirPath()+host::MainWindowInitialValues::configPath});
    parser.process(app);
    QSettings preferences("Qt-GeneralController","Appearance");
    host::Language::instance().setCode(preferences.value("language","zh_CN").toString());
    QObject::connect(&host::Language::instance(),&host::Language::changed,&app,[&preferences]{preferences.setValue("language",host::Language::instance().code());});
    host::MainWindow window(parser.value("config"),parser.isSet("demo")||host::MainWindowInitialValues::simulation);if(QFileInfo::exists(parser.value("config"))){QString error;if(!window.restoreChannels(parser.value("config"),error))QMessageBox::warning(&window,"项目配置载入失败",error);}window.show();return app.exec();
}
