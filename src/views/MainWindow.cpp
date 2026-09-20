#include "MainWindow.h"
#include "ChannelHardwareEditor.h"
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
    initHostResources();ui->setupUi(this);ui->heroSubtitle->hide();setStyleSheet(styleSheetText());setWindowIcon(QIcon(":/Bootloader.ico"));
    ui->mainLayout->setContentsMargins(0,10,16,8);ui->heroLayout->setContentsMargins(16,0,0,0);
    ui->versionBadge->setText(MainWindowInitialValues::versionLabel);
    ui->heroTitle->setText(MainWindowInitialValues::title);ui->eyebrow->setText(MainWindowInitialValues::description);
    ui->channelTabs->setUsesScrollButtons(true);ui->channelTabs->tabBar()->setExpanding(false);
    ui->heroLayout->removeWidget(ui->versionBadge);
    ui->versionBadge->setStyleSheet("color:#708397;background:transparent;padding:2px 8px;font-size:11px;");
    ui->statusbar->addPermanentWidget(ui->versionBadge);
    ui->channelTabs->tabBar()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->channelTabs->tabBar(),&QWidget::customContextMenuRequested,this,[this](const QPoint &position){
        channelContextMenu(ui->channelTabs->tabBar()->tabAt(position),ui->channelTabs->tabBar()->mapToGlobal(position));
    });
    connect(ui->channelTabs,&QWidget::customContextMenuRequested,this,[this](const QPoint&position){
        if(position.x()<ui->channelTabs->navigationWidth())channelContextMenu(-1,ui->channelTabs->mapToGlobal(position));
    });
    auto can=ChannelSettings::defaults(communication::Bus::Can);can.simulation=simulation;
    auto lin=ChannelSettings::defaults(communication::Bus::Lin);lin.simulation=simulation;
    QString error;addChannel(can,error);addChannel(lin,error);ui->channelTabs->setCurrentIndex(0);
    connect(ui->loadSettings,&QPushButton::clicked,this,&MainWindow::loadChannels);
    connect(ui->saveSettings,&QPushButton::clicked,this,[this](){
        QString error;const bool ok=m_configuration.save(m_channels,error);
        ui->statusbar->showMessage(ok?QString("已保存 %1 个软件通道").arg(m_channels.size()):error,8000);
    });
    ensurePolished();
    if(screen())resize(QSize(MainWindowInitialValues::width,MainWindowInitialValues::height).boundedTo(screen()->availableGeometry().size()-QSize(32,64)));
}
MainWindow::~MainWindow(){delete ui;}
bool MainWindow::removeChannel(ChannelViewModel *vm) {
    const int index=m_channels.indexOf(vm);if(index<0)return false;
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
    auto page=new ChannelPage(vm);ui->channelTabs->addTab(page,statusIcon(0),settings.softwareId);
    connect(vm,&ChannelViewModel::changed,this,&MainWindow::updateChannels);
    ui->channelTabs->setCurrentWidget(page);updateChannels();return vm;
}
void MainWindow::updateChannels() {
    if(m_updating)return;QScopedValueRollback<bool> guard(m_updating,true);
    bool idle=true,unconnected=true;
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
        idle=idle && !vm->busy();unconnected=unconnected && !vm->hardwareLocked();
    }
    ui->saveSettings->setEnabled(idle);ui->loadSettings->setEnabled(unconnected);
}
void MainWindow::channelContextMenu(int index,const QPoint&position) {
    QMenu menu(this);menu.setObjectName("channelContextMenu");
    if(index<0){
        auto*create=menu.addAction("新建通道");create->setObjectName("createChannelAction");create->setEnabled(m_channels.size()<64);
        if(menu.exec(position)==create)createChannelDialog();return;
    }
    if(index>=m_channels.size())return;
    ui->channelTabs->setCurrentIndex(index);QPointer<ChannelViewModel>target=m_channels[index];
    auto*connection=menu.addAction("");connection->setObjectName("connectChannelAction");
    auto refreshConnection=[target,connection]{if(!target)return;connection->setText(target->pending()?"处理中…":target->connected()?"断开连接":"连接设备");connection->setEnabled(!target->pending()&&(target->connected()||target->canConnect()));};
    refreshConnection();connect(target,&ChannelViewModel::changed,&menu,refreshConnection);
    auto*edit=menu.addAction("修改通道");edit->setObjectName("editChannelAction");edit->setEnabled(!target->hardwareLocked());
    edit->setToolTip("连接或运行期间请先断开通道");
    auto*remove=menu.addAction("删除通道");remove->setObjectName("deleteChannelAction");
    const auto*chosen=menu.exec(position);if(!target)return;
    if(chosen==connection)target->toggleConnection();else if(chosen==edit)createChannelDialog(target);else if(chosen==remove)removeChannel(target);
}
void MainWindow::createChannelDialog(ChannelViewModel*target) {
    if(target && target->hardwareLocked())return;
    QDialog dialog(this);dialog.setObjectName(target?"editChannelDialog":"createChannelDialog");dialog.setWindowTitle(target?"修改软件通道":"创建软件通道");dialog.setMinimumWidth(480);
    auto form=new QFormLayout(&dialog);form->setContentsMargins(24,20,24,20);form->setSpacing(14);
    auto type=new QComboBox;type->setObjectName("channelType");type->addItems({"CAN","LIN"});
    type->setCurrentIndex(target && target->settings().bus==communication::Bus::Lin?1:0);type->setEnabled(!target);type->setToolTip("已创建通道的总线类型固定；需要其他类型时请新建通道。");
    auto name=new QLineEdit(target?target->settings().softwareId:nextChannelName(communication::Bus::Can));name->setObjectName("channelName");name->setMaxLength(64);
    auto error=new QLabel;error->setObjectName("inlineError");error->setTextFormat(Qt::PlainText);error->setWordWrap(true);error->hide();
    auto buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText("确认");buttons->button(QDialogButtonBox::Cancel)->setText("取消");
    form->addRow("通道类型",type);form->addRow("通道名称",name);
    auto*holder=new QWidget;auto*hardwareLayout=new QVBoxLayout(holder);hardwareLayout->setContentsMargins(0,0,0,0);form->addRow(holder);form->addRow(error);form->addRow(buttons);
    std::unique_ptr<ChannelViewModel> draft;ChannelHardwareEditor*editor=nullptr;
    auto reservations=[&]{if(!draft)return;QSet<quint32>used;for(auto*peer:m_channels)if(peer!=target && peer->reservesHardware() && peer->settings().bus==draft->settings().bus && peer->settings().simulation==draft->settings().simulation)used.insert(peer->settings().handle);draft->setReservations(used);};
    auto reset=[&]{
        delete editor;editor=nullptr;draft.reset();
        auto settings=ChannelSettings::defaults(type->currentIndex()?communication::Bus::Lin:communication::Bus::Can);settings.simulation=m_simulation;
        if(target){const auto&original=target->settings();settings.softwareId=original.softwareId;settings.simulation=original.simulation;settings.hardwareKey=original.hardwareKey;settings.handle=original.handle;settings.bitrate=original.bitrate;settings.autoReconnect=original.autoReconnect;}
        draft.reset(new ChannelViewModel(settings));reservations();editor=new ChannelHardwareEditor(draft.get());hardwareLayout->addWidget(editor);
        connect(draft.get(),&ChannelViewModel::changed,&dialog,[&]{reservations();buttons->button(QDialogButtonBox::Ok)->setEnabled(!draft->busy());});
    };reset();
    for(auto*peer:m_channels)connect(peer,&ChannelViewModel::changed,&dialog,reservations);
    connect(type,qOverload<int>(&QComboBox::currentIndexChanged),&dialog,[&]{
        if(!name->isModified())name->setText(nextChannelName(type->currentIndex()?communication::Bus::Lin:communication::Bus::Can));reset();
    });
    connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    connect(buttons,&QDialogButtonBox::accepted,&dialog,[&]{
        if(!draft || draft->busy())return;
        auto settings=target?target->settings():draft->settings();const auto&hardware=draft->settings();settings.softwareId=name->text();
        settings.simulation=hardware.simulation;settings.hardwareKey=hardware.hardwareKey;settings.handle=hardware.handle;settings.bitrate=hardware.bitrate;settings.autoReconnect=hardware.autoReconnect;
        QString message;const bool ok=target?m_configuration.update(target,settings,m_channels,message):addChannel(settings,message)!=nullptr;
        if(ok)dialog.accept();else{error->setText(message);error->show();}
    });dialog.exec();
    // Disconnect closures capturing local draft state before its destruction.
    for(auto*peer:m_channels)disconnect(peer,nullptr,&dialog,nullptr);
    disconnect(draft.get(),nullptr,&dialog,nullptr);delete editor;draft.reset();
}
void MainWindow::loadChannels() {
    for(auto vm:m_channels)if(vm->hardwareLocked())return;
    const QString path=QFileDialog::getOpenFileName(this,"载入已保存通道",QCoreApplication::applicationDirPath()+"/config","JSON (*.json)");
    if(path.isEmpty())return;
    QString error;
    if(!restoreChannels(path,error))ui->statusbar->showMessage(error,8000);
}
bool MainWindow::restoreChannels(const QString &path,QString &error) {
    error.clear();
    QVector<ChannelSettings> settings;
    if(!m_configuration.load(path,m_channels,settings,error))return false;
    // Validate the whole file before disposing any page or worker.
    m_updating=true;
    while(ui->channelTabs->count()){auto page=ui->channelTabs->widget(0);ui->channelTabs->removeTab(0);delete page;}
    qDeleteAll(m_channels);m_channels.clear();m_updating=false;
    for(auto s:settings){if(m_simulation)s.simulation=true;addChannel(s,error);}
    ui->channelTabs->setCurrentIndex(0);ui->statusbar->showMessage(QString("已载入 %1 个软件通道").arg(settings.size()),5000);return true;
}
QString MainWindow::styleSheetText(){
    QFile f(":/theme.qss");return f.open(QIODevice::ReadOnly)?QString::fromUtf8(f.readAll()):QString();
}
}
