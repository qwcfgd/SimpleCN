#include "ChannelPage.h"
#include "localization/Language.h"
#include "SignalTransmitPage.h"
#include "UdsDiagnosticPage.h"
#include <QTabWidget>
#include <QTabBar>
#include <QBoxLayout>
#include <QGridLayout>
#include <QFrame>
#include <QComboBox>
#include <QLineEdit>
#include <QSpinBox>
#include <QIntValidator>
#include <QRegularExpressionValidator>
#include <QGroupBox>
#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QToolButton>
#include <QProgressBar>
#include <QTableView>
#include <QTreeView>
#include <memory>
#include "FrameDataDelegate.h"
#include <QPlainTextEdit>
#include <QSplitter>
#include <QScrollArea>
#include <QScrollBar>
#include <QHeaderView>
#include <QSortFilterProxyModel>
#include <QFileDialog>
#include <QInputDialog>
#include <QSignalBlocker>
#include <QTimer>
#include <QStyle>
#include <QDialog>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <QFormLayout>
#include <QScopedValueRollback>
#include <QScreen>
#include <QResizeEvent>
#include <QtMath>
#include "protocol/FlashJob.h"
namespace host {
class FrameFilterProxy : public QSortFilterProxyModel {
public:
    using QSortFilterProxyModel::QSortFilterProxyModel;
protected:
    bool filterAcceptsRow(int row,const QModelIndex &parent) const override {
        if(parent.isValid()){
            if(QSortFilterProxyModel::filterAcceptsRow(parent.row(),parent.parent()))return true;
            if(row==0)for(int n=1;n<sourceModel()->rowCount(parent);++n)
                if(QSortFilterProxyModel::filterAcceptsRow(n,parent))return true;
        }
        return QSortFilterProxyModel::filterAcceptsRow(row,parent);
    }
};
static QLabel *label(const QString &text,QWidget *parent=nullptr){auto w=new QLabel(text,parent);w->setTextFormat(Qt::PlainText);return w;}
static QFrame *card(const QString &title,QVBoxLayout *&layout) {
    auto frame=new QFrame;frame->setProperty("card",true);
    layout=new QVBoxLayout(frame);layout->setContentsMargins(12,8,12,8);layout->setSpacing(6);
    auto heading=label(title);heading->setObjectName("sectionTitle");layout->addWidget(heading);return frame;
}
static QLineEdit *edit(const char *name,int maxLength=80){auto w=new QLineEdit;w->setObjectName(name);w->setMaxLength(maxLength);return w;}
static QSpinBox *spin(const char *name,int low,int high) {
    auto s=new QSpinBox;s->setObjectName(name);s->setRange(low,high);s->setButtonSymbols(QAbstractSpinBox::PlusMinus);return s;
}
ChannelPage::ChannelPage(ChannelViewModel *vm,QWidget *parent):QWidget(parent),m_vm(vm) {
    setObjectName(vm->settings().bus==communication::Bus::Lin?"linPage":"canPage");
    build();loadSettings();render();
    connect(vm,&ChannelViewModel::changed,this,&ChannelPage::render);

    connect(vm,&ChannelViewModel::packageConfirmationRequested,this,[this](quint64 id,const QString &details){
        QMessageBox dialog(QMessageBox::Question,QString::fromUtf8("确认下载镜像"),details,QMessageBox::Yes|QMessageBox::No,this);
        dialog.setObjectName("packageConfirmation");dialog.setTextFormat(Qt::PlainText);dialog.setDefaultButton(QMessageBox::No);
        m_vm->confirmPackage(id,dialog.exec()==QMessageBox::Yes);
    });

    connect(vm,&ChannelViewModel::settingsChanged,this,[this](){if(!m_applying)loadSettings();});
    connect(vm,&ChannelViewModel::logAdded,m_log,[this](const QString &line){m_log->appendPlainText(Language::text(line));});
    connect(&Language::instance(),&Language::changed,this,[this]{const int scroll=m_log->verticalScrollBar()->value();const bool follow=scroll==m_log->verticalScrollBar()->maximum();QStringList lines;for(const auto &line:m_vm->logs())lines.append(Language::text(line));m_log->setPlainText(lines.join('\n'));m_log->verticalScrollBar()->setValue(follow?m_log->verticalScrollBar()->maximum():scroll);});
    connect(vm,&ChannelViewModel::logsCleared,m_log,&QPlainTextEdit::clear);
    auto timer=new QTimer(this);timer->setInterval(ChannelPageInitialValues::elapsedRefreshMs);
    connect(timer,&QTimer::timeout,this,[this](){if(m_vm->taskState()==TaskState::Running && m_elapsed.isValid())
        m_elapsedText->setText(QString("%1 s").arg(m_elapsed.elapsed()/1000.0,0,'f',1));});timer->start();
}
void ChannelPage::build() {
    const bool lin=m_vm->settings().bus==communication::Bus::Lin;
    auto root=new QVBoxLayout(this);root->setContentsMargins(6,0,0,0);root->setSpacing(0);
    m_regions=new QSplitter(Qt::Vertical);m_regions->setObjectName("channelRegions");m_regions->setChildrenCollapsible(false);
    auto hardwareCard=new QFrame;hardwareCard->setProperty("card",true);
    auto hardwareLayout=new QVBoxLayout(hardwareCard);hardwareLayout->setContentsMargins(12,6,12,6);hardwareLayout->setSpacing(6);
    auto status=new QHBoxLayout;
    m_hardwareSummary=label("");m_hardwareSummary->setObjectName("hardwareSummary");m_hardwareSummary->setWordWrap(true);status->addWidget(m_hardwareSummary,1);
    m_connectionStatus=label("");m_connectionStatus->setObjectName("connectionState");m_connectionStatus->setAlignment(Qt::AlignRight|Qt::AlignVCenter);status->addWidget(m_connectionStatus);
    m_connectButton=new QPushButton("连接");m_connectButton->setObjectName("channelConnectButton");
    status->addWidget(m_connectButton);
    hardwareLayout->addLayout(status);
    connect(m_connectButton,&QPushButton::clicked,m_vm,&ChannelViewModel::toggleConnection);
    root->addWidget(hardwareCard);root->addWidget(m_regions,1);

    auto download=new QFrame;download->setObjectName("downloadPanel");download->setProperty("card",true);download->setSizePolicy(QSizePolicy::Preferred,QSizePolicy::Minimum);
    auto downloadLayout=new QVBoxLayout(download);downloadLayout->setContentsMargins(12,8,12,8);downloadLayout->setSpacing(6);
    m_images=new QWidget;m_images->setObjectName("imageRegion");
    auto imageGrid=new QGridLayout(m_images);imageGrid->setContentsMargins(0,0,0,0);imageGrid->setHorizontalSpacing(6);imageGrid->setVerticalSpacing(3);
    auto heading=label("下载镜像");heading->setObjectName("sectionTitle");
    m_downloadSettings=new QPushButton("下载设置…");m_downloadSettings->setObjectName("downloadSettings");
    auto imageHeader=new QHBoxLayout;imageHeader->addWidget(heading);imageHeader->addStretch();
    imageHeader->addWidget(m_downloadSettings);imageGrid->addLayout(imageHeader,0,0,1,3);
    connect(m_downloadSettings,&QPushButton::clicked,this,&ChannelPage::editDownload);
    m_flashPath=edit("flashPath",32767);m_flashPath->setPlaceholderText("Flash Driver · BIN / HEX（可选）");
    m_appPath=edit("applicationPath",32767);m_appPath->setPlaceholderText("Application · BIN / HEX");
    m_flashInfo=label("");m_appInfo=label("");m_flashInfo->setObjectName("muted");m_appInfo->setObjectName("muted");
    for(auto info:{m_flashInfo,m_appInfo})info->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);
    for(int i=0;i<2;++i){
        imageGrid->addWidget(label(i==0?"Driver":"Application"),1+i*2,0);
        imageGrid->addWidget(i==0?m_flashPath:m_appPath,1+i*2,1);
        auto file=new QPushButton("文件…");file->setObjectName(i==0?"browseFlash":"browseApplication");
        if(i==0)m_browseFlash=file;
        imageGrid->addWidget(file,1+i*2,2);
        imageGrid->addWidget(i==0?m_flashInfo:m_appInfo,2+i*2,1,1,2);
        connect(file,&QPushButton::clicked,this,[this,i](){browseImage(i==0);});
    }
    m_flashAddress=edit("flashAddress",10);m_appAddress=edit("applicationAddress",10);
    // Image addresses live in the settings dialog; image selection and task controls stay visible.
    for(auto address:{m_flashAddress,m_appAddress}){address->setParent(this);address->hide();}
    imageGrid->setColumnStretch(1,1);
    connect(m_vm->signalTransmission(),&SignalTransmitViewModel::changed,this,&ChannelPage::render);
    auto downloadSplit=new QSplitter(Qt::Vertical);downloadSplit->setObjectName("downloadSplit");downloadSplit->setChildrenCollapsible(false);
    downloadLayout->addWidget(downloadSplit);downloadSplit->addWidget(m_images);
    auto taskRegion=new QWidget;taskRegion->setObjectName("taskRegion");
    auto runLayout=new QVBoxLayout(taskRegion);runLayout->setContentsMargins(8,0,0,0);runLayout->setSpacing(8);
    auto taskHeading=new QHBoxLayout;auto title=label("下载任务");title->setObjectName("sectionTitle");taskHeading->addWidget(title);
    // Equal flexible bands keep the progress bar vertically centered while
    // keeping the title at the top and the status/actions directly below it.
    auto upperBand=new QVBoxLayout;upperBand->setSpacing(0);upperBand->addLayout(taskHeading);upperBand->addStretch();runLayout->addLayout(upperBand,1);
    m_progress=new QProgressBar;m_progress->setObjectName("downloadProgress");m_progress->setRange(0,100);m_progress->setValue(0);m_progress->setMinimumHeight(22);
    auto progressRow=new QHBoxLayout;progressRow->addWidget(m_progress,1);runLayout->addLayout(progressRow);
    auto footer=new QHBoxLayout;footer->setSpacing(8);auto statusLayout=new QVBoxLayout;statusLayout->setSpacing(3);auto statusRow=new QHBoxLayout;statusRow->setSpacing(6);
    auto elapsedLabel=label("总耗时");elapsedLabel->setObjectName("muted");statusRow->addWidget(elapsedLabel);m_elapsedText=label("0.0 s");m_elapsedText->setObjectName("muted");statusRow->addWidget(m_elapsedText);
    m_task=label("等待开始");m_task->setWordWrap(true);m_task->setObjectName("taskText");statusRow->addWidget(m_task,1);statusLayout->addLayout(statusRow);
    m_hint=label("");m_hint->setObjectName("downloadStartHint");m_hint->setWordWrap(true);statusLayout->addWidget(m_hint);
    m_error=label("");m_error->setObjectName("inlineError");m_error->setWordWrap(true);m_error->hide();statusLayout->addWidget(m_error);
    footer->addLayout(statusLayout,1);
    m_start=new QPushButton("开始下载");m_start->setObjectName("startButton");m_start->setProperty("primary",true);
    m_cancel=new QPushButton("取消");m_cancel->setObjectName("cancelButton");progressRow->addWidget(m_start);progressRow->addWidget(m_cancel);
    auto lowerBand=new QVBoxLayout;lowerBand->setSpacing(0);lowerBand->addLayout(footer);lowerBand->addStretch();runLayout->addLayout(lowerBand,1);
    downloadSplit->addWidget(taskRegion);downloadSplit->setSizes({150,110});
    auto tasks=new QTabWidget;m_tasks=tasks;tasks->setObjectName("taskPages");tasks->setDocumentMode(true);
    tasks->tabBar()->setObjectName("taskTabBar");
    tasks->addTab(download,"下载");
    auto diagnosticScroll=new QScrollArea;diagnosticScroll->setObjectName("udsScrollArea");diagnosticScroll->setWidgetResizable(true);
    diagnosticScroll->setFrameShape(QFrame::NoFrame);diagnosticScroll->setWidget(new UdsDiagnosticPage(m_vm));
    tasks->addTab(diagnosticScroll,"UDS 诊断");
    tasks->addTab(new SignalTransmitPage(m_vm->signalTransmission()),"信号发送");m_regions->addWidget(tasks);

    auto outputs=new QSplitter(Qt::Horizontal);m_outputs=outputs;outputs->setObjectName("outputSplit");outputs->setChildrenCollapsible(false);outputs->setMinimumHeight(150);
    QVBoxLayout *frameLayout,*logLayout;auto frameCard=card("报文监视",frameLayout);auto logCard=card("运行日志",logLayout);
    frameCard->setObjectName("frameMonitorPanel");logCard->setObjectName("eventLogPanel");
    auto frameActions=new QHBoxLayout;auto filter=edit("frameFilter",120);filter->setPlaceholderText("筛选 ID、方向、数据或状态");
    m_count=label("0 条");m_count->setObjectName("muted");
    auto clear=new QPushButton("清空");clear->setObjectName("clearFrames");
    auto exportButton=new QPushButton("导出");m_follow=new QCheckBox("跟随");m_follow->setChecked(ChannelPageInitialValues::followFrames);
    // Assign a parent before setVisible(true), which otherwise shows a temporary top-level window.
    m_rxdEnabled=new QCheckBox("RxD使能",frameCard);m_rxdEnabled->setObjectName("rxdEnabled");m_rxdEnabled->setVisible(lin);
    m_rxdEnabled->setToolTip("勾选后显示发送帧在 LIN 硬件接收队列中的 0x3C 回读；不勾选仅隐藏该回读，不影响发送确认。");
    m_scan=new QPushButton("扫描帧头",frameCard);m_scan->setObjectName("scanHeaders");m_scan->setVisible(lin);
    frameActions->addWidget(filter,1);frameActions->addWidget(m_count);frameActions->addWidget(m_rxdEnabled);frameActions->addWidget(m_follow);
    frameActions->addWidget(m_scan);frameActions->addWidget(clear);frameActions->addWidget(exportButton);frameLayout->addLayout(frameActions);
    m_table=new QTreeView;m_table->setObjectName("frameTable");m_table->setAlternatingRowColors(true);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setUniformRowHeights(true);m_table->setItemDelegate(new FrameDataDelegate(m_table));m_table->setIndentation(16);
    auto proxy=new FrameFilterProxy(this);proxy->setRecursiveFilteringEnabled(true);proxy->setSourceModel(m_vm->frames());proxy->setFilterKeyColumn(-1);proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
    m_table->setModel(proxy);m_table->header()->setStretchLastSection(true);
    const QList<int> widths={110,95,95,65,45,150,80,205,120};for(int i=0;i<widths.size();++i)m_table->setColumnWidth(i,widths[i]);
    auto historyScroll=new QScrollBar(Qt::Vertical);historyScroll->setObjectName("frameHistoryScroll");
    historyScroll->setAccessibleName("历史报文浏览");historyScroll->setVisible(false);
    // The thumb represents the loaded history window, not the tree viewport.
    historyScroll->setStyleSheet("QScrollBar:vertical { width: 12px; margin: 0px; background: #F2F5F9; } QScrollBar::handle:vertical { background: #12889C; min-height: 1px; border-radius: 0px; } QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }");
    auto displayActions=new QHBoxLayout;
    auto t=new QCheckBox("t"),rt=new QCheckBox("rt"),dt=new QCheckBox("dt"),rolling=new QCheckBox("滚动显示"),pause=new QCheckBox("暂停更新");
    pause->setObjectName("framePause");pause->setChecked(false);pause->setToolTip("暂停表格更新，后台继续记录；拖动左侧滚动条浏览历史报文");
    t->setObjectName("frameTimeT");rt->setObjectName("frameTimeRt");dt->setObjectName("frameTimeDt");rolling->setObjectName("frameRolling");
    t->setToolTip("当前系统时间");rt->setToolTip("从记录开始为 0 s 的时刻");dt->setToolTip("相邻记录时间差 / s");
    rolling->setToolTip("同一软件通道、总线类型和 ID 原位更新；半字节连续不变 10 帧后变灰");rt->setChecked(true);
    for(auto box:{t,rt,dt,rolling,pause})displayActions->addWidget(box);displayActions->addStretch();
    exportButton->setToolTip("导出启动或上次清空后的全部缓存报文，不受暂停、筛选或显示窗口影响");
    auto titleItem=frameLayout->takeAt(0);displayActions->insertWidget(0,titleItem->widget());delete titleItem;frameLayout->insertLayout(0,displayActions);
    auto timeColumns=[this,t,rt,dt]{m_table->setColumnHidden(0,!t->isChecked());m_table->setColumnHidden(1,!rt->isChecked());m_table->setColumnHidden(2,!dt->isChecked());m_table->setTreePosition(t->isChecked()?0:rt->isChecked()?1:dt->isChecked()?2:3);};
    for(auto box:{t,rt,dt})connect(box,&QCheckBox::toggled,this,timeColumns);timeColumns();
    connect(rolling,&QCheckBox::toggled,this,[this,pause](bool on){m_vm->frames()->setRolling(on);m_follow->setEnabled(!on&&!pause->isChecked());});
    connect(pause,&QCheckBox::toggled,this,[this,rolling](bool on){
        m_vm->frames()->setPaused(on);rolling->setEnabled(!on);m_follow->setEnabled(!on&&!rolling->isChecked());
        if(!on&&m_follow->isChecked()&&!rolling->isChecked())m_table->scrollToBottom();
    });
    auto historyRange=[this,historyScroll]{
        auto *model=m_vm->frames();const qint64 maximum=qMax(qint64(0),model->historyCount()-FrameTableModel::Capacity);
        historyScroll->setVisible(model->paused()&&maximum>0);
        if(!model->paused())return;
        if(!historyScroll->isSliderDown()){
            QSignalBlocker blocker(historyScroll);const qint64 span=qMax(qint64(0),model->historyCount()-model->windowSize());
            const int range=int(qMin(span,qint64(1000000000)));
            historyScroll->setRange(0,range);
            historyScroll->setPageStep(span?int(qRound64(double(model->windowSize())*range/span)):model->windowSize());
            historyScroll->setValue(maximum?int(qRound64(double(model->windowStart())*range/maximum)):0);
            historyScroll->setProperty("historyMaximum",maximum);
        }
        const qint64 first=model->windowSize()?model->windowStart()+1:0,last=model->windowStart()+model->windowSize();
        historyScroll->setToolTip(Language::text("缓存第 %1–%2 条 / 共 %3 条").arg(first).arg(last).arg(model->historyCount()));
    };
    connect(historyScroll,&QScrollBar::valueChanged,this,[this,historyScroll](int value){
        const auto maximum=historyScroll->property("historyMaximum").toLongLong();
        m_vm->frames()->setHistoryStart(historyScroll->maximum()?qRound64(double(value)*maximum/historyScroll->maximum()):0);
        m_table->scrollToTop();
    });
    connect(historyScroll,&QScrollBar::sliderReleased,this,historyRange);
    connect(m_vm->frames(),&FrameTableModel::historyChanged,this,historyRange);
    connect(&Language::instance(),&Language::changed,this,historyRange);historyRange();
    auto savingDisplay=std::make_shared<bool>(false);
    auto saveDisplay=[this,t,rt,dt,rolling,filter,savingDisplay]{if(*savingDisplay)return;auto ui=m_vm->signalTransmission()->working().uiSettings;ui["monitor"]=QJsonObject{{"t",t->isChecked()},{"rt",rt->isChecked()},{"dt",dt->isChecked()},{"rolling",rolling->isChecked()},{"follow",m_follow->isChecked()},{"filter",filter->text()}};m_vm->signalTransmission()->setUiSettings(ui);};
    for(auto *box:{t,rt,dt,rolling,m_follow})connect(box,&QCheckBox::toggled,this,saveDisplay);connect(filter,&QLineEdit::textChanged,this,saveDisplay);
    auto restoreDisplay=[this,t,rt,dt,rolling,filter,timeColumns,savingDisplay]{const auto config=m_vm->signalTransmission()->working().uiSettings.value("monitor").toObject();if(config.isEmpty())return;*savingDisplay=true;
        t->setChecked(config["t"].toBool());rt->setChecked(config.value("rt").toBool(true));dt->setChecked(config["dt"].toBool());rolling->setChecked(config["rolling"].toBool());m_follow->setChecked(config.value("follow").toBool(true));filter->setText(config["filter"].toString());timeColumns();*savingDisplay=false;};
    connect(m_vm->signalTransmission(),&SignalTransmitViewModel::structureChanged,this,restoreDisplay);restoreDisplay();
    auto expanded=std::make_shared<QSet<QString>>();
    connect(m_table,&QTreeView::expanded,this,[expanded](const QModelIndex &i){expanded->insert(i.data(Qt::UserRole+2).toString());});
    connect(m_table,&QTreeView::collapsed,this,[expanded](const QModelIndex &i){expanded->remove(i.data(Qt::UserRole+2).toString());});
    connect(m_vm->frames(),&QAbstractItemModel::modelReset,this,[this,proxy,expanded]{
        if(expanded->isEmpty()||!m_vm->frames()->rolling())return;
        for(int row=0;row<m_vm->frames()->rowCount();++row){auto source=m_vm->frames()->index(row,0);if(expanded->contains(source.data(Qt::UserRole+2).toString()))m_table->setExpanded(proxy->mapFromSource(source),true);}
    });
    auto historyLayout=new QHBoxLayout;historyLayout->setSpacing(4);historyLayout->addWidget(historyScroll);historyLayout->addWidget(m_table,1);frameLayout->addLayout(historyLayout,1);
    auto logActions=new QHBoxLayout;auto clearLog=new QPushButton("清空");auto exportLog=new QPushButton("导出");
    logActions->addWidget(label(QString("最近 %1 条事件").arg(ChannelPageInitialValues::logCapacity)),1);logActions->addWidget(clearLog);logActions->addWidget(exportLog);logLayout->addLayout(logActions);
    m_log=new QPlainTextEdit;m_log->setObjectName("eventLog");m_log->setReadOnly(true);m_log->setMaximumBlockCount(ChannelPageInitialValues::logCapacity);
    m_log->setPlaceholderText("连接变化、扫描和任务结果将在这里显示。");logLayout->addWidget(m_log);
    frameCard->setMinimumWidth(510);logCard->setMinimumWidth(210);outputs->addWidget(frameCard);outputs->addWidget(logCard);outputs->setSizes({850,330});
    m_regions->addWidget(outputs);m_regions->setStretchFactor(0,0);m_regions->setStretchFactor(1,1);
    connect(tasks,&QTabWidget::currentChanged,this,[this]{updateRegionSizes();});
    connect(m_regions,&QSplitter::splitterMoved,this,[this]{const auto sizes=m_regions->sizes();const int total=sizes.value(0)+sizes.value(1);if(total>0)(m_tasks->currentIndex()==2?m_signalRatio:m_tasks->currentIndex()==1?m_udsRatio:m_downloadRatio)=double(sizes[0])/total;});
    QTimer::singleShot(0,this,[this]{updateRegionSizes();});
    connect(m_start,&QPushButton::clicked,m_vm,&ChannelViewModel::start);
    connect(m_cancel,&QPushButton::clicked,m_vm,&ChannelViewModel::cancel);
    connect(m_scan,&QPushButton::clicked,m_vm,&ChannelViewModel::scanHeaders);
    connect(filter,&QLineEdit::textChanged,proxy,&QSortFilterProxyModel::setFilterFixedString);
    connect(clear,&QPushButton::clicked,m_vm->frames(),&FrameTableModel::clear);
    connect(exportButton,&QPushButton::clicked,this,&ChannelPage::exportFrames);
    connect(clearLog,&QPushButton::clicked,m_vm,&ChannelViewModel::clearLogs);
    connect(exportLog,&QPushButton::clicked,this,&ChannelPage::exportLogs);
    const auto count=[this,proxy](){m_count->setText(Language::text("显示 %1 / 缓存 %2 条").arg(proxy->rowCount()).arg(m_vm->frames()->historyCount()));};
    connect(proxy,&QAbstractItemModel::rowsInserted,this,[this,count](){count();if(m_follow->isChecked()&&!m_vm->frames()->rolling()&&!m_vm->frames()->paused())m_table->scrollToBottom();});
    connect(proxy,&QAbstractItemModel::rowsRemoved,this,count);connect(proxy,&QAbstractItemModel::modelReset,this,count);
    connect(m_vm->frames(),&FrameTableModel::historyChanged,this,count);connect(&Language::instance(),&Language::changed,this,count);count();
    for(auto e:{m_flashPath,m_appPath,m_flashAddress,m_appAddress})
        connect(e,&QLineEdit::textChanged,this,&ChannelPage::applyForm);
    connect(m_rxdEnabled,&QCheckBox::toggled,this,&ChannelPage::applyForm);
}
void ChannelPage::updateRegionSizes(){
    if(!m_outputs||!m_tasks)return;
    const bool uds=m_tasks->currentIndex()==1;
    m_outputs->setMinimumHeight(uds?qMax(150,qCeil(window()->height()*0.30)):150);
    const int total=qMax(1,m_regions->height()-m_regions->handleWidth());
    const int top=qRound(total*(m_tasks->currentIndex()==2?m_signalRatio:uds?m_udsRatio:m_downloadRatio));
    m_regions->setSizes({top,total-top});
}
void ChannelPage::resizeEvent(QResizeEvent *event){
    QWidget::resizeEvent(event);
    updateRegionSizes();
}
void ChannelPage::loadSettings() {
    m_loading=true;const auto &s=m_vm->settings();
    m_rxdEnabled->setChecked(s.rxdEnabled);
    m_flashPath->setText(s.flashPath);m_appPath->setText(s.applicationPath);m_flashAddress->setText(s.flashAddress);m_appAddress->setText(s.applicationAddress);
    m_loading=false;
}
void ChannelPage::applyForm() {
    if(m_loading || m_applying || m_vm->busy())return;
    auto s=m_vm->settings();s.rxdEnabled=m_rxdEnabled->isChecked();
    s.flashPath=m_flashPath->text();s.applicationPath=m_appPath->text();s.flashAddress=m_flashAddress->text();s.applicationAddress=m_appAddress->text();
    QScopedValueRollback<bool> guard(m_applying,true);m_vm->setSettings(s);
}

void ChannelPage::editDownload() {
    if(m_vm->busy())return;
    const auto original=m_vm->settings();boot::FlashProfile profile;QString message;
    if(!m_vm->flashProfile(profile,message))return;
    profile.target="generic";
    QDialog dialog(this);dialog.setObjectName("downloadSettingsDialog");dialog.setWindowTitle(original.softwareId+" · 下载设置");
    dialog.resize(990,520);if(screen())dialog.resize(dialog.size().boundedTo(screen()->availableGeometry().size()-QSize(40,80)));
    auto outer=new QVBoxLayout(&dialog);auto scroll=new QScrollArea;scroll->setWidgetResizable(true);scroll->setFrameShape(QFrame::NoFrame);
    auto content=new QWidget;auto contentLayout=new QVBoxLayout(content);
    auto repeatGroup=new QGroupBox("重复下载");repeatGroup->setObjectName("repeatDownloadGroup");auto repeatRow=new QHBoxLayout(repeatGroup);contentLayout->addWidget(repeatGroup);
    auto parameterGroup=new QGroupBox("下载参数");parameterGroup->setObjectName("downloadParametersGroup");auto form=new QFormLayout(parameterGroup);form->setContentsMargins(12,10,12,10);form->setSpacing(10);contentLayout->addWidget(parameterGroup);
    scroll->setWidget(content);outer->addWidget(scroll,1);
    auto keyLibrary=edit("keyLibrary",32767);keyLibrary->setText(profile.keyLibrary);
    keyLibrary->setPlaceholderText("未选择 DLL 时，将收到的 seed 原样作为 key 发送");
    keyLibrary->setToolTip("未选择 DLL 时，将收到的 seed 原样作为 key 发送");
    auto keyRow=new QWidget;keyRow->setObjectName("keyLibraryRow");
    auto keyLayout=new QHBoxLayout(keyRow);keyLayout->setContentsMargins(0,0,0,0);
    auto keyDefault=new QLineEdit;keyDefault->setReadOnly(true);keyDefault->setObjectName("download_default_keyLibrary");keyDefault->setPlaceholderText("默认：未选择");keyLayout->addWidget(keyDefault,1);
    keyLayout->addWidget(label("使用值"));keyLayout->addWidget(keyLibrary,1);
    auto browseKey=new QPushButton("文件…");browseKey->setObjectName("browseKeyLibrary");keyLayout->addWidget(browseKey);
    connect(browseKey,&QPushButton::clicked,&dialog,[&dialog,keyLibrary]{
        const QDir app(QCoreApplication::applicationDirPath());
        const auto current=keyLibrary->text().trimmed();
        const auto start=current.isEmpty()?app.absolutePath():QFileInfo(app.absoluteFilePath(current)).absolutePath();
        const auto path=QFileDialog::getOpenFileName(&dialog,Language::text("选择 27 DLL"),start,"DLL (*.dll *.DLL)");
        if(!path.isEmpty())keyLibrary->setText(path);
    });
    form->addRow("27 DLL",keyRow);
    auto flow=new QComboBox;flow->setObjectName("downloadFlow");flow->addItem("App下载流程","app");flow->addItem("Boot下载流程","boot");
    flow->setCurrentIndex(profile.flow=="boot"?1:0);
    auto flashRequired=new QCheckBox("Flash Driver使能");flashRequired->setObjectName("flashRequired");
    flashRequired->setChecked(original.flashRequired);flashRequired->setToolTip("勾选后下载需要 Flash Driver 镜像。");
    auto downloadRow=new QWidget;auto downloadRowLayout=new QHBoxLayout(downloadRow);downloadRowLayout->setContentsMargins(0,0,0,0);
    auto flowDefault=new QLineEdit("App下载流程");flowDefault->setReadOnly(true);flowDefault->setProperty("translateStaticText",true);flowDefault->setObjectName("download_default_flow");
    downloadRowLayout->addWidget(label("下载流程 默认值"));downloadRowLayout->addWidget(flowDefault,1);downloadRowLayout->addWidget(label("使用值"));downloadRowLayout->addWidget(flow,1);
    auto driverDefault=new QCheckBox("默认");driverDefault->setObjectName("download_default_flashRequired");driverDefault->setChecked(ChannelSettings::defaults(original.bus).flashRequired);driverDefault->setEnabled(false);
    downloadRowLayout->addSpacing(24);downloadRowLayout->addWidget(label("下载镜像"));downloadRowLayout->addWidget(driverDefault);downloadRowLayout->addWidget(flashRequired);
    form->addRow(downloadRow);
    auto checksWidget=new QWidget;auto grid=new QGridLayout(checksWidget);grid->setContentsMargins(0,0,0,0);
    QVector<QCheckBox*> stepEnables,negativeChecks,timeoutChecks;
    checksWidget->setStyleSheet("QCheckBox::indicator:disabled { background-color:#D9DDE3;border:1px solid #AAB2BE;border-radius:2px; } QCheckBox:disabled { color:#8993A2; }");
    for(int group=0;group<2;++group){
        const int base=group*8;
        for(int col=0;col<7;++col){auto header=label(QStringList({"步骤","使能\n默认","使能\n使用","负响应\n默认","负响应\n使用","超时\n默认","超时\n使用"})[col]);header->setObjectName("feedbackHeader");grid->addWidget(header,0,base+col);}
        if(group==0)grid->setColumnMinimumWidth(7,16);
    }
    const int rowsPerColumn=(boot::downloadSteps().size()+1)/2;
    for(int i=0;i<boot::downloadSteps().size();++i){
        const auto &step=boot::downloadSteps()[i];
        const auto id=QString::fromLatin1(step.id);
        const int group=i/rowsPerColumn,base=group*8,row=i%rowsPerColumn+1;
        auto stepLabel=label(QString("%1 · %2").arg(i+1,2,10,QChar('0')).arg(QString::fromUtf8(step.label)));
        stepLabel->setObjectName("step_"+id);
        auto stepEnable=new QCheckBox;stepEnable->setObjectName("enable_"+id);
        stepEnable->setChecked(profile.enabled(id));
        stepEnable->setToolTip("勾选：下载时执行该步骤。未勾选：跳过该步骤，负响应和超时判定不可用。");
        auto negative=new QCheckBox;negative->setObjectName("negative_"+id);
        negative->setChecked(profile.negativeResponseChecks.value(step.id).toBool(ChannelPageInitialValues::feedbackChecked));
        negative->setToolTip("勾选：收到除 78 以外的负响应时停止。未勾选：忽略该负响应并继续流程。");
        auto timeout=new QCheckBox;timeout->setObjectName("timeout_"+id);
        timeout->setChecked(!boot::suppressesPositiveResponse(id)&&profile.timeoutChecks.value(step.id).toBool(ChannelPageInitialValues::feedbackChecked));
        timeout->setToolTip(boot::suppressesPositiveResponse(id)?"本步骤开启正响应抑制，超时判定不可用。":"勾选：达到 P2/P2* 或总时限时停止。未勾选：达到时限后忽略该超时并继续下一步。");
        auto defaultBox=[&](const QString &name,bool checked){auto box=label(checked?"是":"否");box->setObjectName("download_default_"+name+"_"+id);box->setToolTip(checked?"默认启用":"默认关闭");return box;};
        grid->addWidget(stepLabel,row,base);
        grid->addWidget(defaultBox("enable",ChannelPageInitialValues::initialStepEnabled("app").value(step.id).toBool()),row,base+1,Qt::AlignHCenter);grid->addWidget(stepEnable,row,base+2,Qt::AlignHCenter);
        grid->addWidget(defaultBox("negative",ChannelPageInitialValues::feedbackChecked),row,base+3,Qt::AlignHCenter);grid->addWidget(negative,row,base+4,Qt::AlignHCenter);
        grid->addWidget(defaultBox("timeout",ChannelPageInitialValues::feedbackChecked&&!boot::suppressesPositiveResponse(id)),row,base+5,Qt::AlignHCenter);grid->addWidget(timeout,row,base+6,Qt::AlignHCenter);
        stepEnables.append(stepEnable);negativeChecks.append(negative);timeoutChecks.append(timeout);
    }
    grid->setColumnStretch(0,1);grid->setColumnStretch(8,1);
    const auto driverStep=[](int i){
        const auto id=QString::fromLatin1(boot::downloadSteps()[i].id);
        return id=="driver34"||id=="driver36"||id=="driver37"||id=="driverVerify";
    };
    const auto selectedPolicy=[flow]{
        boot::FlashProfile selected;selected.flow=flow->currentData().toString();return selected;
    };
    auto enableChecks=[flashRequired,stepEnables,negativeChecks,timeoutChecks,driverStep]{
        for(int i=0;i<negativeChecks.size();++i){
            const bool driverAllowed=flashRequired->isChecked()||!driverStep(i);
            if(!driverAllowed){QSignalBlocker blocker(stepEnables[i]);stepEnables[i]->setChecked(false);}
            stepEnables[i]->setEnabled(driverAllowed);
            const bool active=driverAllowed&&stepEnables[i]->isChecked();
            negativeChecks[i]->setEnabled(active);
            const bool suppressed=boot::suppressesPositiveResponse(QString::fromLatin1(boot::downloadSteps()[i].id));
            if(suppressed){QSignalBlocker blocker(timeoutChecks[i]);timeoutChecks[i]->setChecked(false);}
            timeoutChecks[i]->setEnabled(active&&!suppressed);
        }
    };
    for(auto stepEnable:stepEnables)connect(stepEnable,&QCheckBox::toggled,&dialog,[enableChecks](bool){enableChecks();});
    connect(flow,qOverload<int>(&QComboBox::currentIndexChanged),&dialog,[selectedPolicy,stepEnables,enableChecks](int){
        for(int i=0;i<stepEnables.size();++i)
            stepEnables[i]->setChecked(selectedPolicy().enabled(QString::fromLatin1(boot::downloadSteps()[i].id)));
        enableChecks();
    });
    connect(flashRequired,&QCheckBox::toggled,&dialog,[stepEnables,driverStep,enableChecks](bool checked){
        if(checked)for(int i=0;i<stepEnables.size();++i)if(driverStep(i)){
            stepEnables[i]->setEnabled(true);stepEnables[i]->setChecked(true);
        }
        enableChecks();
    });enableChecks();
    auto feedbackTitle=label("反馈判定");feedbackTitle->setObjectName("feedbackDecisionTitle");

    auto policy=label("使能：勾选后执行该步骤；未勾选时跳过，且负响应/超时判定不可操作。负响应或超时判定勾选时遇到对应异常停止，未勾选时忽略并继续。App 默认全部使能；Boot 默认不使能 10 02 之前的步骤。下载开始后本页不可修改。");policy->setWordWrap(true);policy->setObjectName("muted");
    const auto defaults=ChannelSettings::defaults(original.bus);
    boot::FlashProfile defaultProfile;QString defaultError;boot::FlashProfile::fromJson(defaults.downloadProfile,defaultProfile,defaultError);
    auto enabled=new QCheckBox("重复下载使能（默认关闭）");enabled->setObjectName("repeatDownloadEnabled");enabled->setChecked(original.repeatDownloadEnabled);
    auto decimal=[&](const char *name,int low,int high,int value){auto e=edit(name,10);e->setValidator(new QIntValidator(low,high,e));e->setText(QString::number(value));return e;};
    auto count=decimal("repeatDownloadCount",1,10000,original.repeatDownloadCount),interval=decimal("repeatDownloadIntervalMs",0,86400000,original.repeatDownloadIntervalMs);
    count->setMaximumWidth(90);interval->setMaximumWidth(110);repeatRow->addWidget(enabled);repeatRow->addWidget(label("次数（默认 2）"));repeatRow->addWidget(count);repeatRow->addWidget(label("间隔 / ms（默认 1000）"));repeatRow->addWidget(interval);repeatRow->addStretch();
    count->setEnabled(enabled->isChecked());interval->setEnabled(enabled->isChecked());connect(enabled,&QCheckBox::toggled,count,&QWidget::setEnabled);connect(enabled,&QCheckBox::toggled,interval,&QWidget::setEnabled);
    auto columns=new QWidget;auto columnsLayout=new QHBoxLayout(columns);columnsLayout->setContentsMargins(0,0,0,0);auto lw=new QWidget;auto rw=new QWidget;auto left=new QFormLayout(lw);auto right=new QFormLayout(rw);columnsLayout->addWidget(lw,1);columnsLayout->addWidget(rw,1);form->addRow(columns);
    auto pair=[&](QFormLayout *layout,const QString &title,QLineEdit *field,const QString &baseline){auto row=new QWidget;auto box=new QHBoxLayout(row);box->setContentsMargins(0,0,0,0);auto initial=new QLineEdit(baseline);initial->setReadOnly(true);initial->setCursorPosition(0);initial->setToolTip(baseline);initial->setObjectName("download_default_"+field->objectName());initial->setStyleSheet("QLineEdit { background:#EEF1F4;color:#687582; }");box->addWidget(initial);box->addWidget(field);layout->addRow(title,row);};
    auto header=[&](QFormLayout *layout){auto row=new QWidget;auto box=new QHBoxLayout(row);box->setContentsMargins(0,0,0,0);box->addWidget(label("默认值"),1);box->addWidget(label("使用值"),1);layout->addRow("参数",row);};header(left);header(right);
    auto hexField=[&](const char *name,int digits,quint32 value){auto e=edit(name,digits);e->setValidator(new QRegularExpressionValidator(QRegularExpression(QString("[0-9A-Fa-f]{1,%1}").arg(digits)),e));e->setText(QString::number(value,16).toUpper());return e;};
    auto h=[](quint32 v){return QString::number(v,16).toUpper();};
    auto appLevel=hexField("appSecurityLevel",2,profile.appSecurityLevel),bootLevel=hexField("bootSecurityLevel",2,profile.securityLevel);
    appLevel->setText(appLevel->text().rightJustified(2,'0'));bootLevel->setText(bootLevel->text().rightJustified(2,'0'));
    pair(left,"App安全等级 / hex",appLevel,h(defaultProfile.appSecurityLevel).rightJustified(2,'0'));pair(right,"Boot安全等级 / hex",bootLevel,h(defaultProfile.securityLevel).rightJustified(2,'0'));
    auto flashAddress=edit("settingsFlashAddress",10),appAddress=edit("settingsApplicationAddress",10),appLength=edit("settingsApplicationLength",10);
    flashAddress->setText(original.flashAddress);appAddress->setText(original.applicationAddress);appLength->setText(original.applicationLength);
    pair(left,"Driver 地址 / hex",flashAddress,defaults.flashAddress);pair(left,"App 地址 / hex",appAddress,defaults.applicationAddress);pair(left,"App 擦除长度 / hex",appLength,defaults.applicationLength);appLength->setToolTip("从 App 基址开始的擦除范围长度，含旧代码尾部；不表示镜像大小。");
    QLineEdit *txId=nullptr,*rxId=nullptr;
    if(original.bus==communication::Bus::Can){txId=edit("downloadTxId",10);rxId=edit("downloadRxId",10);txId->setText(original.downloadTxId.isEmpty()?original.requestId:original.downloadTxId);rxId->setText(original.downloadRxId.isEmpty()?original.responseId:original.downloadRxId);pair(left,"Tx ID / hex",txId,original.requestId);pair(left,"Rx ID / hex",rxId,original.responseId);}
    auto appNote=edit("applicationNote",256);appNote->setText(original.applicationNote);pair(left,"App 备注",appNote,defaults.applicationNote);
    auto fingerprint=edit("fingerprintHex",38);fingerprint->setText(original.fingerprintHex);
    QString fpError;const auto defaultFingerprint=boot::t008::fingerprint(QDate::currentDate(),QByteArray("TEST-PLACEHOLDER"),fpError);
    fingerprint->setPlaceholderText("留空使用默认值");pair(left,"F184 / 19 bytes hex",fingerprint,QString::fromLatin1(defaultFingerprint.toHex()).toUpper());
    fingerprint->setToolTip("默认设备号 TEST-PLACEHOLDER 为测试占位；留空时下载使用当天日期。");
    auto erase=hexField("eraseRoutine",4,profile.eraseRoutine),verifyRid=hexField("verifyRoutine",4,profile.verifyRoutine),dependencyRid=hexField("dependencyRoutine",4,profile.dependencyRoutine),did=hexField("identityDid",4,profile.identityDid);
    pair(right,"擦除 RID / hex",erase,h(defaultProfile.eraseRoutine));pair(right,"校验 RID / hex",verifyRid,h(defaultProfile.verifyRoutine));pair(right,"依赖 RID / hex",dependencyRid,h(defaultProfile.dependencyRoutine));pair(right,"身份 DID / hex",did,h(defaultProfile.identityDid));
    auto block=decimal("consecutiveFrameByteLimit",3,4095,profile.consecutiveFrameByteLimit),resetWait=decimal("resetWaitMs",0,60000,profile.resetWaitMs);
    pair(right,"连续帧字节限制",block,QString::number(defaultProfile.consecutiveFrameByteLimit));pair(right,"复位等待 / ms",resetWait,QString::number(defaultProfile.resetWaitMs));
    form->addRow(feedbackTitle);form->addRow(checksWidget);form->addRow(policy);
    auto error=label("");error->setObjectName("inlineError");error->setWordWrap(true);error->hide();outer->addWidget(error);
    auto buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);outer->addWidget(buttons);
    buttons->button(QDialogButtonBox::Ok)->setText("保存");buttons->button(QDialogButtonBox::Cancel)->setText("取消");
    connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    connect(buttons,&QDialogButtonBox::accepted,&dialog,[&](){
        for(auto field:{count,interval,erase,verifyRid,dependencyRid,did,block,resetWait})if(!field->hasAcceptableInput()){error->setText("请输入有效范围内的参数");error->show();field->setFocus();return;}
        auto s=m_vm->settings();s.flashRequired=flashRequired->isChecked();
        bool appOk=false,bootOk=false;
        const auto appValue=appLevel->text().toUInt(&appOk,16),bootValue=bootLevel->text().toUInt(&bootOk,16);
        if(!appOk||!bootOk||!appLevel->hasAcceptableInput()||!bootLevel->hasAcceptableInput()||
           appValue<1||appValue>0x7d||!(appValue&1)||bootValue<1||bootValue>0x7d||!(bootValue&1)){
            error->setText("解锁等级须为单字节十六进制奇数（01–7D），密钥子功能为该值 + 1。");error->show();return;
        }
        profile.appSecurityLevel=quint8(appValue);profile.securityLevel=quint8(bootValue);
        s.repeatDownloadEnabled=enabled->isChecked();s.repeatDownloadCount=count->text().toInt();s.repeatDownloadIntervalMs=interval->text().toInt();
        s.flashAddress=flashAddress->text();s.applicationAddress=appAddress->text();s.applicationLength=appLength->text();s.applicationNote=appNote->text().trimmed();s.fingerprintHex=fingerprint->text().trimmed();
        if(txId){s.downloadTxId=txId->text();s.downloadRxId=rxId->text();}
        profile.flow=flow->currentData().toString();profile.stepEnabled={};profile.negativeResponseChecks={};profile.timeoutChecks={};
        for(int i=0;i<stepEnables.size();++i){
            profile.stepEnabled[boot::downloadSteps()[i].id]=stepEnables[i]->isChecked();
            profile.negativeResponseChecks[boot::downloadSteps()[i].id]=negativeChecks[i]->isChecked();
            profile.timeoutChecks[boot::downloadSteps()[i].id]=timeoutChecks[i]->isChecked();
        }
        profile.keyLibrary=keyLibrary->text();profile.simulationOnly=false;
        profile.keyProvider=profile.keyLibrary.trimmed().isEmpty()?"seed-as-key":"external-generatekeyex";
        profile.eraseRoutine=quint16(erase->text().toUInt(nullptr,16));profile.verifyRoutine=quint16(verifyRid->text().toUInt(nullptr,16));profile.dependencyRoutine=quint16(dependencyRid->text().toUInt(nullptr,16));profile.identityDid=quint16(did->text().toUInt(nullptr,16));
        profile.consecutiveFrameByteLimit=block->text().toInt();profile.resetWaitMs=resetWait->text().toInt();
        QString message;
        if(!m_vm->updateDownloadSettings(s,profile,message)){error->setText(message);error->show();return;}
        dialog.accept();
    });dialog.exec();
}
void ChannelPage::render() {
    const auto &s=m_vm->settings();
    QString hardware="未选择硬件",port="未选择通道";
    for(const auto&h:m_vm->hardware())if(h.key==s.hardwareKey){hardware=h.label.section(" / SDK channel",0,0).section(" / channel",0,0);port=QString("通道%1").arg(h.controller);break;}
    if(hardware=="未选择硬件"&&!s.hardwareKey.isEmpty())hardware="已选设备未连接";
    if(s.simulation)hardware.replace("模拟 CAN 双通道适配器","模拟CAN双通道适配器").replace("模拟 LIN 双通道适配器","模拟LIN双通道适配器");
    m_hardwareSummary->setText(QString("%1 · %2 · %3 · %4 bit/s").arg(s.simulation?"模拟模式":"在线硬件",hardware,port).arg(s.bitrate));
    m_connectionStatus->setText(connectionText(m_vm->state()));m_connectionStatus->setToolTip(m_vm->healthDetail());
    m_images->setEnabled(!m_vm->busy());m_flashPath->setEnabled(s.flashRequired);m_browseFlash->setEnabled(s.flashRequired);
    m_flashInfo->setEnabled(s.flashRequired);
    m_connectButton->setText(m_vm->connected()?"断开":"连接");
    m_connectButton->setEnabled(!m_vm->pending()&&(m_vm->connected()?!m_vm->otherTaskBusy():m_vm->canConnect()));
    if(m_tasks&&m_tasks->count()>2){
        m_tasks->setTabText(2,Language::text("信号发送"));
    }
    m_flashAddress->setEnabled(!m_vm->busy()&&s.flashRequired);
    m_appAddress->setEnabled(!m_vm->busy());
    m_start->setText(s.simulation?"开始模拟下载":"开始下载");m_start->setEnabled(m_vm->canStart());
    const auto hint=m_vm->startHint();
    m_start->setToolTip(hint);m_cancel->setEnabled(m_vm->taskState()==TaskState::Running || m_vm->scanning());
    m_scan->setEnabled(m_vm->canScan());m_scan->setText(m_vm->scanning()?"正在扫描…":"扫描帧头");
    m_scan->setToolTip(m_vm->scanText().isEmpty()?"仅发送 00–3B、3D 帧头，检测从节点响应":m_vm->scanText());
    m_progress->setValue(m_vm->progress());m_task->setText(m_vm->taskText());
    const bool blocked=m_vm->downloadStartBlocked();
    if(m_task->property("downloadStartBlocked").toBool()!=blocked){
        m_task->setProperty("downloadStartBlocked",blocked);
        m_task->style()->unpolish(m_task);m_task->style()->polish(m_task);m_task->update();
    }
    m_flashInfo->setText(imageDescription(s.flashPath));m_appInfo->setText(imageDescription(s.applicationPath));
    m_flashInfo->setToolTip(m_flashInfo->text());m_appInfo->setToolTip(m_appInfo->text());
    m_hint->setText(hint);m_hint->setVisible(!hint.isEmpty());
    m_error->setText(m_vm->error());m_error->setVisible(!m_vm->error().isEmpty());
    if(m_vm->taskState()==TaskState::Running && m_lastTask!=TaskState::Running){
        m_elapsed.restart();m_elapsedText->setText("0.0 s");
    }else if(m_lastTask==TaskState::Running&&m_vm->taskState()!=TaskState::Running&&m_elapsed.isValid()){
        m_elapsedText->setText(QString("%1 s").arg(m_elapsed.elapsed()/1000.0,0,'f',1));
    }
    m_lastTask=m_vm->taskState();

}
void ChannelPage::browseImage(bool flash) {
    const QString current=flash?m_vm->settings().flashPath:m_vm->settings().applicationPath;
    const QString start=current.isEmpty()?QDir::homePath():QFileInfo(current).absolutePath();
    const QString path=QFileDialog::getOpenFileName(this,Language::text("选择镜像"),start,"Firmware (*.bin *.hex *.BIN *.HEX)");
    if(!path.isEmpty())m_vm->chooseImage(flash,path);
}
void ChannelPage::exportFrames(){
    QString filter="BLF (*.blf)";
    auto user=qEnvironmentVariable("USERNAME");if(user.isEmpty())user=qEnvironmentVariable("USER");if(user.isEmpty())user="user";
    const auto epoch=m_vm->frames()->startedEpochMs();const auto stamp=QDateTime::fromMSecsSinceEpoch(epoch?epoch:QDateTime::currentMSecsSinceEpoch()).toString("yyyyMMdd_HHmmss_zzz");
    QString name="Qt-GeneralController_"+m_vm->settings().softwareId+"_"+user+"_"+stamp;name.replace(QRegularExpression("[<>:\"/\\\\|?*]"),"_");
    auto file=QFileDialog::getSaveFileName(this,Language::text("导出当前缓存报文"),name,"BLF (*.blf);;ASC (*.asc);;CSV (*.csv)",&filter);
    if(file.isEmpty())return;if(QFileInfo(file).suffix().isEmpty())file+=filter.startsWith("BLF")?".blf":filter.startsWith("CSV")?".csv":".asc";QString error;
    m_vm->log(m_vm->frames()->exportTrace(file,error)?"已导出报文："+file:error);
}
void ChannelPage::exportLogs(){
    const auto file=QFileDialog::getSaveFileName(this,Language::text("导出运行日志"),m_vm->settings().softwareId+"-log.txt","Text (*.txt)");
    if(file.isEmpty())return;QString error;m_vm->log(m_vm->exportLogs(file,error)?"已导出日志："+file:error);
}
}
