#include "MainWindow.h"
#include "ChannelHardwareEditor.h"
#include "UiLanguageController.h"
#include "localization/Language.h"
#include <memory>
#include "ui_MainWindow.h"
#include <QFile>
#include <QLabel>
#include <QPushButton>
#include <QStatusBar>
#include <QScreen>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QBoxLayout>
#include <QLineEdit>
#include <QComboBox>
#include <QPainter>
#include <QScopedValueRollback>
#include <QTabBar>
#include <QFileDialog>
#include <QMenu>
#include <QPointer>
#include <QTimer>
#include <QCloseEvent>
#include <QMessageBox>
static void initHostResources(){
    static const bool initialized=[](){ Q_INIT_RESOURCE(resources); return true; }();
    Q_UNUSED(initialized);
}
namespace host {
static QIcon statusIcon(int state) {
    QPixmap pix(32,32);pix.setDevicePixelRatio(2);pix.fill(Qt::transparent);
    QPainter p(&pix);p.setRenderHint(QPainter::Antialiasing);
    const QColor color=state==1?QColor("#16A06B"):state==2?QColor("#DC4040"):QColor("#96A1AD");
    p.setPen(QPen(color,1.6));p.setBrush(state?QBrush(color):Qt::NoBrush);
    p.drawEllipse(QRectF(3,3,10,10));return QIcon(pix);
}
MainWindow::MainWindow(const QString &settingsPath,bool simulation,QWidget *parent):
    QMainWindow(parent),ui(new Ui::MainWindow),m_configuration(settingsPath),m_simulation(simulation) {
    connect(&m_replay,&ReplayViewModel::notice,this,[this](const QString &text){ui->statusbar->showMessage(text,10000);});
    initHostResources();ui->setupUi(this);ui->heroSubtitle->hide();setStyleSheet(styleSheetText());setWindowIcon(QIcon(":/Bootloader.ico"));
    UiLanguageController::instance();
    auto *language=new QComboBox(this);language->setObjectName("languageSelector");language->addItem("简中","zh_CN");language->addItem("Eng","en");
    language->setMinimumWidth(82);language->setMaximumWidth(110);language->setToolTip("语言");language->setAccessibleName("语言");language->setCurrentIndex(Language::instance().english()?1:0);
    ui->heroLayout->addWidget(language,0,Qt::AlignBottom);
    connect(language,qOverload<int>(&QComboBox::currentIndexChanged),this,[language]{Language::instance().setCode(language->currentData().toString());});
    connect(&Language::instance(),&Language::changed,language,[language]{QSignalBlocker blocker(language);language->setCurrentIndex(Language::instance().english()?1:0);});
    ui->mainLayout->setContentsMargins(0,10,16,8);ui->heroLayout->setContentsMargins(16,0,0,0);
    ui->versionBadge->setText(MainWindowInitialValues::versionLabel);
    ui->heroTitle->setText(MainWindowInitialValues::title);ui->eyebrow->setText(MainWindowInitialValues::description);
    ui->channelTabs->setUsesScrollButtons(true);ui->channelTabs->tabBar()->setExpanding(false);
    ui->heroLayout->removeWidget(ui->versionBadge);
    ui->versionBadge->setStyleSheet("color:#708397;background:transparent;padding:2px 8px;font-size:11px;");
    ui->versionBadge->hide();
    ui->channelTabs->tabBar()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->channelTabs->tabBar(),&QWidget::customContextMenuRequested,this,[this](const QPoint &position){
        channelContextMenu(ui->channelTabs->tabBar()->tabAt(position),ui->channelTabs->tabBar()->mapToGlobal(position));
    });
    connect(ui->channelTabs,&QWidget::customContextMenuRequested,this,[this](const QPoint&position){
        if(position.x()<ui->channelTabs->navigationWidth())channelContextMenu(-1,ui->channelTabs->mapToGlobal(position));
    });
    connect(ui->channelTabs->tabBar(),&QTabBar::tabBarDoubleClicked,this,[this](int index){if(index>=0&&index<m_channels.size()){ui->channelTabs->setCurrentIndex(index);createChannelDialog(m_channels[index]);}});
    auto can=ChannelSettings::defaults(communication::Bus::Can);can.simulation=simulation;
    auto lin=ChannelSettings::defaults(communication::Bus::Lin);lin.simulation=simulation;
    QString error;addChannel(can,error);addChannel(lin,error);ui->channelTabs->setCurrentIndex(0);
    m_configuration.markSaved(m_channels);
    ensurePolished();
    if(screen())resize(QSize(MainWindowInitialValues::width,MainWindowInitialValues::height).boundedTo(screen()->availableGeometry().size()-QSize(32,64)));
}
MainWindow::~MainWindow(){m_replay.stop();delete ui;}
bool MainWindow::removeChannel(ChannelViewModel *vm) {
    const int index=m_channels.indexOf(vm);if(index<0)return false;if(m_replay.contains(vm->signalTransmission()))m_replay.stop();
    const QString name=vm->settings().softwareId;
    m_updating=true;
    disconnect(vm,nullptr,this,nullptr);
    auto page=ui->channelTabs->widget(index);ui->channelTabs->removeTab(index);m_channels.removeAt(index);
    delete page;
    // The model stops timers/tasks, closes only its own port and joins its worker before the lease is offered again.
    delete vm;m_updating=false;updateChannels();
    ui->statusbar->showMessage("已删除 "+name+(m_channels.isEmpty()?"；右键通道标题列空白处新建":""),5000);
    return true;
}
ChannelViewModel *MainWindow::canChannel()const {
    for(auto vm:m_channels)if(vm->settings().bus==communication::Bus::Can)return vm;return nullptr;
}
ChannelViewModel *MainWindow::linChannel()const {
    for(auto vm:m_channels)if(vm->settings().bus==communication::Bus::Lin)return vm;return nullptr;
}
QString MainWindow::nextChannelName(communication::Bus bus)const {
    int number=1;for(auto vm:m_channels)if(vm->settings().bus==bus)++number;
    const QString prefix=bus==communication::Bus::Can?"CAN":"LIN";
    for(;;++number){
        const auto candidate=prefix+QString::number(number).rightJustified(2,'0');
        bool used=false;for(auto vm:m_channels)if(vm->settings().softwareId.compare(candidate,Qt::CaseInsensitive)==0)used=true;
        if(!used)return candidate;
    }
}
ChannelViewModel *MainWindow::addChannel(ChannelSettings settings,QString &error) {
    error.clear();settings.softwareId=settings.softwareId.trimmed();
    if(m_channels.size()>=64){error="最多支持 64 个软件通道。";return nullptr;}
    for(auto vm:m_channels)if(vm->settings().softwareId.compare(settings.softwareId,Qt::CaseInsensitive)==0){
        error="通道名称已存在，请使用其他名称。";return nullptr;
    }
    communication::SoftwareChannelConfiguration c;if(!settings.toConfiguration(c,error))return nullptr;
    auto vm=new ChannelViewModel(settings,this);m_channels.append(vm);
    auto *signalVm=vm->signalTransmission();signalVm->replayTargets=[this](signal::Bus bus){QStringList names;for(auto *target:m_channels)if((target->settings().bus==communication::Bus::Can)==(bus==signal::Bus::Can))names.append(target->settings().softwareId);return names;};
    connect(signalVm,&SignalTransmitViewModel::replayRequested,this,[this,signalVm](bool periodic){m_replay.start(signalVm,periodic,m_channels);});
    connect(signalVm,&SignalTransmitViewModel::replayStopRequested,this,[this,signalVm]{if(m_replay.contains(signalVm))m_replay.stop();});
    auto page=new ChannelPage(vm);ui->channelTabs->addTab(page,statusIcon(0),settings.softwareId);
    connect(vm,&ChannelViewModel::changed,this,&MainWindow::updateChannels);
    ui->channelTabs->setCurrentWidget(page);updateChannels();return vm;
}
void MainWindow::updateChannels() {
    if(m_updating)return;QScopedValueRollback<bool> guard(m_updating,true);
    for(int i=0;i<m_channels.size();++i){
        auto vm=m_channels[i];QSet<quint32> used;
        for(auto peer:m_channels)if(peer!=vm && peer->settings().bus==vm->settings().bus &&
            peer->settings().simulation==vm->settings().simulation && peer->reservesHardware() && peer->settings().handle)
                used.insert(peer->settings().handle);
        vm->setReservations(used);
        const int state=vm->communicationIndicator();
        ui->channelTabs->setTabText(i,vm->settings().softwareId);
        ui->channelTabs->setTabIcon(i,statusIcon(state));
        ui->channelTabs->setTabToolTip(i,state==1?"通讯正常":state==2?"通讯丢失":"未通讯");
    }
}
void MainWindow::channelContextMenu(int index,const QPoint&position) {
    QMenu menu(this);menu.setObjectName("channelContextMenu");
    if(index<0){
        auto*create=menu.addAction("新建通道");create->setObjectName("createChannelAction");create->setEnabled(m_channels.size()<64);
        if(menu.exec(position)==create)createChannelDialog();return;
    }
    if(index>=m_channels.size())return;
    ui->channelTabs->setCurrentIndex(index);QPointer<ChannelViewModel>target=m_channels[index];
    auto*edit=menu.addAction(target->connected()?"修改通道":"连接通道");edit->setObjectName("editChannelAction");
    connect(target,&ChannelViewModel::changed,&menu,[target,edit]{if(target)edit->setText(target->connected()?"修改通道":"连接通道");});
    auto*remove=menu.addAction("删除通道");remove->setObjectName("deleteChannelAction");
    const auto*chosen=menu.exec(position);if(!target)return;
    if(chosen==edit)createChannelDialog(target);else if(chosen==remove)removeChannel(target);
}
void MainWindow::createChannelDialog(ChannelViewModel*channel) {
    QPointer<ChannelViewModel>target=channel;
    QDialog dialog(this);dialog.setObjectName(target?"editChannelDialog":"createChannelDialog");dialog.setWindowTitle(target?"修改软件通道":"创建软件通道");dialog.setMinimumWidth(480);
    auto form=new QFormLayout(&dialog);form->setContentsMargins(24,20,24,20);form->setSpacing(14);
    auto type=new QComboBox;type->setObjectName("channelType");type->addItems({"CAN","LIN"});type->setCurrentIndex(target&&target->settings().bus==communication::Bus::Lin?1:0);type->setEnabled(!target);
    auto name=new QLineEdit(target?target->settings().softwareId:nextChannelName(communication::Bus::Can));name->setObjectName("channelName");name->setMaxLength(64);
    auto error=new QLabel;error->setObjectName("inlineError");error->setTextFormat(Qt::PlainText);error->setWordWrap(true);error->hide();
    auto buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);buttons->button(QDialogButtonBox::Ok)->setText("确认");buttons->button(QDialogButtonBox::Cancel)->setText("取消");
    form->addRow("通道类型",type);form->addRow("通道名称",name);
    auto*holder=new QWidget;auto*hardwareLayout=new QVBoxLayout(holder);hardwareLayout->setContentsMargins(0,0,0,0);form->addRow(holder);
    auto*files=new QHBoxLayout;auto*load=new QPushButton("载入配置");load->setObjectName("loadSettings");auto*save=new QPushButton("保存项目配置");save->setObjectName("saveSettings");files->addWidget(load);files->addWidget(save);form->addRow(files);form->addRow(error);form->addRow(buttons);
    QString pendingLoad;std::unique_ptr<ChannelViewModel> draft;ChannelHardwareEditor*editor=nullptr;QPushButton*connection=nullptr;
    auto showError=[&](const QString&message){error->setText(message);error->setVisible(!message.isEmpty());};
    auto reservations=[&]{if(!draft)return;QSet<quint32>used;for(auto*peer:m_channels)if(peer!=target && peer->reservesHardware() && peer->settings().bus==draft->settings().bus && peer->settings().simulation==draft->settings().simulation)used.insert(peer->settings().handle);draft->setReservations(used);};
    auto refresh=[&]{
        if(!draft||!editor)return;const bool locked=target&&target->hardwareLocked();editor->setExternalLocked(locked);name->setEnabled(!locked);
        buttons->button(QDialogButtonBox::Ok)->setEnabled(!draft->busy()&&(!target||!target->pending()));
        if(connection){connection->setText(target&&target->pending()?"处理中…":target&&target->connected()?"断开连接":"连接设备");connection->setEnabled(target&&!target->pending()&&(target->connected()||(!locked&&draft->canConnect())));}
        bool idle=true,unconnected=true;for(auto*peer:m_channels){idle&=!peer->busy();unconnected&=!peer->hardwareLocked();}save->setEnabled(idle&&!draft->busy());load->setEnabled(unconnected);
    };
    auto apply=[&]()->bool{
        if(!draft||draft->busy())return false;
        if(target&&target->hardwareLocked())return !target->pending();
        auto settings=target?target->settings():draft->settings();const auto&hardware=draft->settings();settings.softwareId=name->text();settings.simulation=hardware.simulation;settings.hardwareKey=hardware.hardwareKey;settings.handle=hardware.handle;settings.bitrate=hardware.bitrate;settings.autoReconnect=hardware.autoReconnect;
        QString message;const bool ok=target?m_configuration.update(target,settings,m_channels,message):addChannel(settings,message)!=nullptr;showError(message);return ok;
    };
    auto reset=[&]{
        delete editor;editor=nullptr;connection=nullptr;draft.reset();
        auto settings=ChannelSettings::defaults(type->currentIndex()?communication::Bus::Lin:communication::Bus::Can);settings.simulation=m_simulation;
        if(target){const auto&original=target->settings();settings.softwareId=original.softwareId;settings.simulation=original.simulation;settings.hardwareKey=original.hardwareKey;settings.handle=original.handle;settings.bitrate=original.bitrate;settings.autoReconnect=original.autoReconnect;}
        draft.reset(new ChannelViewModel(settings));reservations();editor=new ChannelHardwareEditor(draft.get());hardwareLayout->addWidget(editor);
        if(target){connection=new QPushButton("连接设备");connection->setObjectName("connectButton");connection->setProperty("primary",true);editor->addConnectionControl(connection);
            connect(connection,&QPushButton::clicked,&dialog,[&]{if(!target||target->pending())return;if(target->connected())target->toggleConnection();else if(apply()){
                if(target->canConnect())target->toggleConnection();
                else {
                    // Backend changes and hardware enumeration finish asynchronously.
                    auto*waiting=new QTimer(target);waiting->setSingleShot(true);
                    const auto requested=target->settings();QPointer<ChannelViewModel>live=target;
                    connect(waiting,&QTimer::timeout,waiting,&QObject::deleteLater);
                    connect(target,&ChannelViewModel::changed,waiting,[live,waiting,requested]{
                        if(!live)return;
                        const auto current=live->settings();
                        if(live->connected()||current.simulation!=requested.simulation||(!requested.hardwareKey.isEmpty()&&current.hardwareKey!=requested.hardwareKey)){waiting->deleteLater();return;}
                        if(!live->canConnect())return;
                        QObject::disconnect(live,nullptr,waiting,nullptr);waiting->stop();waiting->deleteLater();live->toggleConnection();
                    });waiting->start(5000);
                }
            }refresh();});}
        connect(draft.get(),&ChannelViewModel::changed,&dialog,[&]{reservations();refresh();});refresh();
    };reset();
    for(auto*peer:m_channels)connect(peer,&ChannelViewModel::changed,&dialog,[&]{reservations();refresh();});
    if(target)connect(target,&QObject::destroyed,&dialog,&QDialog::reject);
    connect(type,qOverload<int>(&QComboBox::currentIndexChanged),&dialog,[&]{if(!name->isModified())name->setText(nextChannelName(type->currentIndex()?communication::Bus::Lin:communication::Bus::Can));reset();});
    connect(save,&QPushButton::clicked,&dialog,[&]{if(target&&!apply())return;QString message;const bool ok=m_configuration.save(m_channels,message);if(ok)m_configuration.markSaved(m_channels);showError(ok?QString("已保存 %1 个软件通道").arg(m_channels.size()):message);});
    connect(load,&QPushButton::clicked,&dialog,[&]{
        const auto path=QFileDialog::getOpenFileName(&dialog,"载入已保存通道",QCoreApplication::applicationDirPath()+"/config","JSON (*.json)");if(path.isEmpty())return;
        QVector<ChannelSettings> settings;QString message;if(!m_configuration.load(path,m_channels,settings,message)){showError(message);return;}pendingLoad=path;dialog.reject();
    });
    connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);connect(buttons,&QDialogButtonBox::accepted,&dialog,[&]{if(apply())dialog.accept();});dialog.exec();
    for(auto*peer:m_channels)disconnect(peer,nullptr,&dialog,nullptr);disconnect(draft.get(),nullptr,&dialog,nullptr);delete editor;draft.reset();
    // Loading replaces real channels only after all draft callbacks and controls have been destroyed.
    if(!pendingLoad.isEmpty()){QString message;if(!restoreChannels(pendingLoad,message))ui->statusbar->showMessage(message,8000);}
}
bool MainWindow::restoreChannels(const QString &path,QString &error) {
    error.clear();
    QVector<ChannelSettings> settings;
    if(!m_configuration.load(path,m_channels,settings,error))return false;
    m_replay.stop();
    // Validate the whole file before disposing any page or worker.
    m_updating=true;
    while(ui->channelTabs->count()){auto page=ui->channelTabs->widget(0);ui->channelTabs->removeTab(0);delete page;}
    qDeleteAll(m_channels);m_channels.clear();m_updating=false;
    for(auto s:settings){if(m_simulation)s.simulation=true;addChannel(s,error);}
    auto *settle=new QTimer(this);settle->setInterval(25);connect(settle,&QTimer::timeout,this,[this,settle]{for(auto *vm:m_channels)if(vm->signalTransmission()->importing())return;m_configuration.markSaved(m_channels);settle->stop();settle->deleteLater();});settle->start();
    ui->channelTabs->setCurrentIndex(0);ui->statusbar->showMessage(QString("已载入 %1 个软件通道").arg(settings.size()),5000);return true;
}
QString MainWindow::styleSheetText(){
    QFile f(":/theme.qss");return f.open(QIODevice::ReadOnly)?QString::fromUtf8(f.readAll()):QString();
}
}

namespace host {
void MainWindow::closeEvent(QCloseEvent *event){
    if(!m_configuration.isDirty(m_channels)){event->accept();return;}
    QMessageBox box(QMessageBox::Question,"保存项目配置","项目配置已变更，是否保存后退出？",QMessageBox::Save|QMessageBox::Discard|QMessageBox::Cancel,this);
    box.setObjectName("saveProjectOnClose");box.button(QMessageBox::Save)->setText("保存并退出");box.button(QMessageBox::Discard)->setText("不保存");box.button(QMessageBox::Cancel)->setText("取消");
    const auto choice=box.exec();if(choice==QMessageBox::Cancel){event->ignore();return;}
    if(choice==QMessageBox::Save){QString error;if(!m_configuration.save(m_channels,error)){QMessageBox::warning(this,"保存失败",error);event->ignore();return;}m_configuration.markSaved(m_channels);}
    event->accept();
}
}
