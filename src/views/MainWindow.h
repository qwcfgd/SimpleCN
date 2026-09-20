#pragma once
#include <QMainWindow>
#include <QPointer>
#include "ChannelPage.h"
#include "MainWindowDefaults.h"
#include "viewmodels/ChannelConfigurationViewModel.h"
#include "viewmodels/ReplayViewModel.h"
namespace Ui {class MainWindow;}
namespace host {
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(const QString &settingsPath,bool simulation=MainWindowInitialValues::simulation,QWidget *parent=nullptr);
    ~MainWindow() override;
    ChannelViewModel *canChannel()const;
    ChannelViewModel *linChannel()const;
    const QVector<ChannelViewModel*> &channels()const{return m_channels;}
    ChannelViewModel *addChannel(ChannelSettings,QString &error);
    bool removeChannel(ChannelViewModel *);
    bool restoreChannels(const QString &path,QString &error);
    QString nextChannelName(communication::Bus)const;
    static QString styleSheetText();
protected:
    void closeEvent(QCloseEvent*)override;
private:
    ReplayViewModel m_replay;
    void createChannelDialog(ChannelViewModel*target=nullptr);
    void channelContextMenu(int index,const QPoint&globalPosition);
    void updateChannels();
    Ui::MainWindow *ui;
    ChannelConfigurationViewModel m_configuration;
    QVector<ChannelViewModel*> m_channels;
    bool m_simulation=false,m_updating=false;
};
}
