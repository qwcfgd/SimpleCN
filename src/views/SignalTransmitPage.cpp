#include "SignalTransmitPage.h"
#include "SignalPlotDialog.h"
#include "viewmodels/SignalTableModels.h"
#include <QBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QComboBox>
#include <QTreeView>
#include <QTableView>
#include <QTableWidget>
#include <QHeaderView>
#include <QSplitter>
#include <QSortFilterProxyModel>
#include <QFileDialog>
#include <QDialog>
#include <QDialogButtonBox>
#include <QSpinBox>
#include <QSignalBlocker>
#include <QScopedValueRollback>
#include <QInputDialog>
#include <QTimer>
#include <QMenu>
#include <limits>
#include <QJsonArray>
namespace host {
using namespace signal;
static QLabel*plain(const QString&s){auto*l=new QLabel(s);l->setTextFormat(Qt::PlainText);l->setWordWrap(true);return l;}
static QPushButton*button(const QString&text,const char*name){auto*b=new QPushButton(text);b->setObjectName(name);return b;}
static void tableStyle(QTableView*t){t->setAlternatingRowColors(true);t->setSelectionBehavior(QAbstractItemView::SelectRows);t->setSelectionMode(QAbstractItemView::SingleSelection);t->verticalHeader()->hide();t->horizontalHeader()->setStretchLastSection(true);t->verticalHeader()->setDefaultSectionSize(27);}
SignalTransmitPage::SignalTransmitPage(SignalTransmitViewModel*vm,QWidget*parent):QWidget(parent),m_vm(vm){
    setObjectName("signalTransmitPage");auto*root=new QVBoxLayout(this);root->setContentsMargins(10,6,10,6);root->setSpacing(5);
    m_configurationDialog=new QDialog(this);m_configurationDialog->setObjectName("signalSettingsDialog");m_configurationDialog->setWindowTitle("通信配置");m_configurationDialog->resize(800,480);
    auto*configuration=new QVBoxLayout(m_configurationDialog);
    auto*heading=new QHBoxLayout;auto*title=plain("报文工作台");title->setObjectName("sectionTitle");title->setWordWrap(false);heading->addWidget(title);heading->addStretch();
    auto*settings=button("通信配置…","signalSettings");heading->addWidget(settings);
    auto*plot=button("图像…","signalPlot");heading->addWidget(plot);root->addLayout(heading);
    connect(plot,&QPushButton::clicked,this,[this]{if(!m_plotDialog)m_plotDialog=new SignalPlotDialog(m_vm,this);m_plotDialog->show();m_plotDialog->raise();m_plotDialog->activateWindow();});
    connect(settings,&QPushButton::clicked,this,[this]{m_configurationDialog->exec();});
    auto*files=new QHBoxLayout;m_path=new QLineEdit;m_path->setObjectName("signalDatabasePath");m_path->setReadOnly(true);m_path->setPlaceholderText(vm->database()->bus==Bus::Can?"导入只读 DBC，或新增自建 CAN 报文":"导入只读 LDF");
    m_import=button("导入…","signalImport");m_reload=button("重新加载","signalReload");files->addWidget(m_path,1);files->addWidget(m_import);files->addWidget(m_reload);configuration->addLayout(files);
    auto *replay=new QHBoxLayout;m_replayPath=new QLineEdit;m_replayPath->setObjectName("signalReplayPath");m_replayPath->setReadOnly(true);m_replayPath->setPlaceholderText("信号回放源文件（ASC / BLF）");
    m_replayImport=button("导入…","signalReplayImport");m_replayReset=button("重置","signalReplayReset");m_replayMap=button("通道映射…","signalReplayMapping");replay->addWidget(m_replayPath,1);replay->addWidget(m_replayImport);replay->addWidget(m_replayReset);replay->addWidget(m_replayMap);configuration->addLayout(replay);
    connect(m_replayImport,&QPushButton::clicked,this,[this]{const auto path=QFileDialog::getOpenFileName(m_configurationDialog,"导入信号回放源",m_replayPath->text(),"日志 (*.blf *.asc *.BLF *.ASC)");if(!path.isEmpty())m_vm->importReplay(path);});
    connect(m_replayReset,&QPushButton::clicked,m_vm,&SignalTransmitViewModel::resetReplay);connect(m_replayMap,&QPushButton::clicked,this,&SignalTransmitPage::editReplayMapping);
    m_summary=plain("");m_summary->setObjectName("signalDatabaseSummary");configuration->addWidget(m_summary);
    m_rolePanel=new QWidget;auto*roles=new QHBoxLayout(m_rolePanel);roles->setContentsMargins(0,0,0,0);
    m_role=new QComboBox;m_role->setObjectName("signalLinRole");m_role->addItems({"主节点","从节点","观测节点"});m_node=new QComboBox;m_node->setObjectName("signalLinNode");m_schedule=new QComboBox;m_schedule->setObjectName("signalLinSchedule");
    roles->addWidget(plain("实际角色"));roles->addWidget(m_role);roles->addWidget(plain("实际节点"));roles->addWidget(m_node);roles->addWidget(plain("调度表"));roles->addWidget(m_schedule,1);
    m_schedules=button("编辑调度结构…","signalEditSchedules");roles->addWidget(m_schedules);configuration->addWidget(m_rolePanel);
    m_canPanel=new QWidget;auto*canOptions=new QHBoxLayout(m_canPanel);canOptions->setContentsMargins(0,0,0,0);
    m_canNode=new QComboBox;m_canNode->setObjectName("signalCanNode");m_canDirection=new QComboBox;m_canDirection->setObjectName("signalCanDirection");m_canDirection->addItems({"Tx","Rx","Tx/Rx"});
    canOptions->addWidget(plain("DBC 节点"));canOptions->addWidget(m_canNode,1);canOptions->addWidget(plain("方向"));canOptions->addWidget(m_canDirection);configuration->addWidget(m_canPanel);
    m_canDirection->setToolTip("相对于所选节点；切换时替换数据库报文，保留自建报文");
    auto*split=new QSplitter(Qt::Horizontal);split->setObjectName("signalMainSplit");auto*browser=new QWidget;auto*bl=new QVBoxLayout(browser);bl->setContentsMargins(0,0,0,0);
    m_search=new QLineEdit;m_search->setObjectName("signalSearch");m_search->setPlaceholderText("浏览节点 / 搜索报文，不改变发送选择");bl->addWidget(m_search);
    m_tree=new QTreeView;m_tree->setObjectName("signalTree");auto*treeModel=new SignalTreeModel(vm,this);auto*proxy=new QSortFilterProxyModel(this);proxy->setSourceModel(treeModel);proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);proxy->setFilterKeyColumn(-1);proxy->setRecursiveFilteringEnabled(true);m_tree->setModel(proxy);m_tree->header()->setStretchLastSection(false);m_tree->header()->setSectionResizeMode(0,QHeaderView::Stretch);m_tree->header()->setSectionResizeMode(1,QHeaderView::Fixed);m_tree->setColumnWidth(1,85);bl->addWidget(m_tree);split->addWidget(browser);
    m_plan=new QTableView;m_plan->setObjectName("signalPlanTable");tableStyle(m_plan);
    if(vm->database()->bus==Bus::Can){m_canModel=new CanTxTableModel(vm,this);m_plan->setModel(m_canModel);connect(m_canModel,&CanTxTableModel::validation,this,&SignalTransmitPage::feedback);m_plan->setColumnWidth(0,45);m_plan->setColumnWidth(1,45);m_plan->setColumnWidth(2,150);m_plan->setColumnWidth(3,90);m_plan->setColumnWidth(6,45);m_plan->setColumnWidth(7,80);}
    else{m_linModel=new LinScheduleTableModel(vm,this);m_plan->setModel(m_linModel);connect(m_linModel,&LinScheduleTableModel::validation,this,&SignalTransmitPage::feedback);}
    m_activeSchedule=new QComboBox;m_activeSchedule->setObjectName("signalActiveSchedule");auto*planPanel=new QWidget;auto*planLayout=new QVBoxLayout(planPanel);planLayout->setContentsMargins(0,0,0,0);if(m_linModel){auto*scheduleRow=new QHBoxLayout;auto*label=plain("调度表");label->setBuddy(m_activeSchedule);scheduleRow->addWidget(label);scheduleRow->addWidget(m_activeSchedule,1);planLayout->addLayout(scheduleRow);}else m_activeSchedule->setParent(this);m_activeSchedule->setVisible(m_linModel!=nullptr);planLayout->addWidget(m_plan);
    split->addWidget(planPanel);split->setSizes({220,780});root->addWidget(split,1);
    m_editor=new QWidget;auto*editor=new QVBoxLayout(m_editor);editor->setContentsMargins(0,0,0,0);editor->setSpacing(4);
    m_values=new QTableView;m_values->setObjectName("signalValues");m_valueModel=new SignalValueTableModel(vm,this);m_values->setModel(m_valueModel);
    connect(m_values,&QTableView::clicked,this,[this](const QModelIndex &index){
        if((index.column()==2||index.column()==3)&&(index.flags()&Qt::ItemIsEditable))m_values->edit(index);
    });m_values->setItemDelegateForColumn(3,new SignalValueDelegate(m_values));tableStyle(m_values);m_values->setColumnWidth(0,150);m_values->setColumnWidth(1,130);m_values->setColumnWidth(2,130);m_values->setColumnWidth(3,145);editor->addWidget(m_values,1);root->addWidget(m_editor,1);
    auto*actions=new QHBoxLayout;m_once=button("多次发送","signalSendOnce");m_start=button("周期发送","signalStart");m_start->setProperty("primary",true);m_stop=button("停止","signalStop");m_back=button("回退：上一步","signalBack");m_restore=button("撤销：恢复配置基准","signalRestore");
    auto*history=new QHBoxLayout;history->addWidget(m_back);history->addWidget(m_restore);history->addStretch();configuration->addLayout(history);
    m_configurationFeedback=plain("");m_configurationFeedback->setObjectName("signalConfigurationFeedback");configuration->addWidget(m_configurationFeedback);configuration->addStretch();
    auto*close=new QDialogButtonBox(QDialogButtonBox::Ok);close->button(QDialogButtonBox::Ok)->setText("确认");configuration->addWidget(close);connect(close,&QDialogButtonBox::accepted,m_configurationDialog,&QDialog::accept);
    m_count=new QSpinBox;m_count->setObjectName("signalRepeatCount");m_count->setRange(1,1000000);m_count->setValue(m_vm->working().repeatCount);m_count->setToolTip("多次发送 / 调度 / 回放次数");connect(m_count,qOverload<int>(&QSpinBox::valueChanged),m_vm,&SignalTransmitViewModel::setRepeatCount);
    actions->addStretch();actions->addWidget(m_count);for(auto*b:{m_once,m_start,m_stop})actions->addWidget(b);root->addLayout(actions);
    m_validation=plain("");m_validation->setObjectName("signalValidation");root->addWidget(m_validation);
    connect(m_search,&QLineEdit::textChanged,proxy,&QSortFilterProxyModel::setFilterFixedString);
    connect(m_tree->selectionModel(),&QItemSelectionModel::currentChanged,this,[this,proxy](const QModelIndex&i){const auto source=proxy->mapToSource(i.sibling(i.row(),0));const auto key=source.data(Qt::UserRole).toString();if(!key.isEmpty()){for(int row=0;row<m_plan->model()->rowCount();++row)if((m_canModel?m_canModel->key(row):m_linModel->key(row))==key){m_plan->selectRow(row);break;}}});
    connect(m_plan->selectionModel(),&QItemSelectionModel::currentRowChanged,this,[this](const QModelIndex&i){selectFrame(m_canModel?m_canModel->key(i.row()):m_linModel->key(i.row()));});
    connect(m_import,&QPushButton::clicked,this,[this]{const auto path=QFileDialog::getOpenFileName(m_configurationDialog,"导入只读信号数据库",m_path->text(),m_canModel?"DBC (*.dbc *.DBC)":"LDF (*.ldf *.LDF)");if(!path.isEmpty())m_vm->importAsync(path);});
    connect(m_reload,&QPushButton::clicked,this,[this]{m_vm->importAsync(m_vm->database()->path);});
    connect(m_valueModel,&SignalValueTableModel::validation,this,&SignalTransmitPage::feedback);
    m_plan->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_plan,&QWidget::customContextMenuRequested,this,[this](const QPoint&position){
        const auto index=m_plan->indexAt(position);QMenu menu(this);menu.setObjectName("signalPlanMenu");
        auto*create=menu.addAction(m_canModel?"新建报文…":"新建帧…");create->setObjectName("signalCreateFrame");create->setEnabled(m_vm->canStructure()&&(m_canModel||!m_vm->working().schedule.isEmpty()));connect(create,&QAction::triggered,this,&SignalTransmitPage::editCustom);
        if(index.isValid()&&m_canModel){
            const auto key=m_canModel->key(index.row());m_plan->selectRow(index.row());auto*remove=menu.addAction("删除报文");remove->setObjectName("signalRemoveFrame");remove->setEnabled(m_vm->canStructure());
            connect(remove,&QAction::triggered,this,[this,key]{QString error;if(!m_vm->removeQueuedFrame(key,error))feedback(error);});
        }
        menu.exec(m_plan->viewport()->mapToGlobal(position));
    });
    m_activeSchedule->setContextMenuPolicy(Qt::CustomContextMenu);connect(m_activeSchedule,&QWidget::customContextMenuRequested,this,[this](const QPoint &p){QMenu menu(this);auto *add=menu.addAction("新建调度表");add->setEnabled(m_vm->canStructure());if(menu.exec(m_activeSchedule->mapToGlobal(p))==add){QString error;if(!m_vm->createSchedule(error))feedback(error);}});
    auto selectCan=[this]{if(m_rendering)return;QString error;if(!m_vm->selectCanNode(m_canNode->currentData().toString(),m_canDirection->currentText(),error)){feedback(error);rebuild();}};
    connect(m_canNode,qOverload<int>(&QComboBox::currentIndexChanged),this,selectCan);
    connect(m_canDirection,qOverload<int>(&QComboBox::currentIndexChanged),this,selectCan);
    connect(m_schedules,&QPushButton::clicked,this,&SignalTransmitPage::editSchedules);
    connect(m_once,&QPushButton::clicked,this,[this]{QString error;if(!m_vm->start(false,error))feedback(error);});
    connect(m_start,&QPushButton::clicked,this,[this]{QString error;if(!m_vm->start(true,error))feedback(error);});connect(m_stop,&QPushButton::clicked,m_vm,&SignalTransmitViewModel::stop);
    connect(m_back,&QPushButton::clicked,m_vm,&SignalTransmitViewModel::back);connect(m_restore,&QPushButton::clicked,m_vm,&SignalTransmitViewModel::restore);
    connect(m_role,qOverload<int>(&QComboBox::currentIndexChanged),this,[this](int role){if(m_rendering)return;QString error;
        if(!m_vm->setRole(LinRole(role),{},error)){feedback(error);rebuild();}});
    connect(m_node,qOverload<int>(&QComboBox::currentIndexChanged),this,[this]{if(m_rendering)return;QString error;if(!m_vm->setRole(LinRole(m_role->currentIndex()),m_node->currentText(),error))feedback(error);});
    connect(m_activeSchedule,qOverload<int>(&QComboBox::currentIndexChanged),this,[this]{if(m_rendering)return;QString error;if(!m_vm->selectSchedule(m_activeSchedule->currentText(),error))feedback(error);render();});
    connect(m_schedule,qOverload<int>(&QComboBox::currentIndexChanged),this,[this]{if(m_rendering)return;QString error;if(!m_vm->selectSchedule(m_schedule->currentText(),error))feedback(error);render();});
    connect(m_plan->model(),&QAbstractItemModel::modelReset,this,&SignalTransmitPage::rebuild);
    connect(vm,&SignalTransmitViewModel::structureChanged,this,&SignalTransmitPage::rebuild);connect(vm,&SignalTransmitViewModel::changed,this,&SignalTransmitPage::render);
    connect(vm,&SignalTransmitViewModel::frameChanged,this,[this](const QString&key){if(key==m_key)renderFrame();});
    for(auto *table:{m_plan,m_values}){auto *header=table->horizontalHeader();header->setStretchLastSection(false);header->setMinimumSectionSize(38);header->setMaximumSectionSize(4096);header->setSectionResizeMode(QHeaderView::Interactive);header->setSectionsMovable(true);}
    m_plan->setColumnWidth(m_canModel?8:7,260);m_values->setColumnWidth(5,260);restoreTableLayouts();
    for(auto *table:{m_plan,m_values}){auto *header=table->horizontalHeader();connect(header,&QHeaderView::sectionMoved,this,&SignalTransmitPage::saveTableLayouts);connect(header,&QHeaderView::sectionResized,this,&SignalTransmitPage::saveTableLayouts);}
    connect(vm,&SignalTransmitViewModel::configurationRestored,this,&SignalTransmitPage::restoreTableLayouts);rebuild();
}
void SignalTransmitPage::saveTableLayouts(){
    if(m_restoringColumns)return;auto ui=m_vm->working().uiSettings;auto tables=ui.value("tableLayouts").toObject();
    for(auto *table:{m_plan,m_values}){const auto *header=table->horizontalHeader();QJsonArray order,widths;for(int i=0;i<header->count();++i){order.append(header->logicalIndex(i));widths.append(header->sectionSize(i));}
        tables[table==m_plan?(m_canModel?"canTransmit":"linTransmit"):"signalValues"]=QJsonObject{{"order",order},{"widths",widths}};}
    ui["tableLayouts"]=tables;m_vm->setUiSettings(ui);
}
void SignalTransmitPage::restoreTableLayouts(){
    QScopedValueRollback<bool> guard(m_restoringColumns,true);const auto tables=m_vm->working().uiSettings.value("tableLayouts").toObject();
    for(auto *table:{m_plan,m_values}){auto *header=table->horizontalHeader();QSignalBlocker blocker(header);
        const auto saved=tables.value(table==m_plan?(m_canModel?"canTransmit":"linTransmit"):"signalValues").toObject();const auto order=saved.value("order").toArray(),widths=saved.value("widths").toArray();
        if(order.size()!=header->count()||widths.size()!=header->count())continue;QSet<int> seen;bool valid=true;
        for(int i=0;i<order.size();++i){const int logical=order[i].toInt(-1),width=widths[i].toInt(-1);if(logical<0||logical>=header->count()||seen.contains(logical)||width<38||width>4096||order[i].toDouble()!=logical||widths[i].toDouble()!=width){valid=false;break;}seen.insert(logical);}
        if(!valid)continue;for(int visual=0;visual<header->count();++visual){header->moveSection(header->visualIndex(order[visual].toInt()),visual);header->resizeSection(visual,widths[visual].toInt());}
    }
}
void SignalTransmitPage::feedback(const QString&text){m_validation->setText(text);m_validation->setVisible(!text.isEmpty());m_configurationFeedback->setText(text);}
void SignalTransmitPage::selectFrame(const QString&key){if(key.isEmpty()||!m_vm->frame(key))return;m_key=key;m_valueModel->setFrame(key);renderFrame();}
void SignalTransmitPage::rebuild(){QScopedValueRollback<bool>guard(m_rendering,true);m_path->setText(m_vm->database()->path);m_role->setCurrentIndex(int(m_vm->working().role));m_node->clear();
    for(const auto&n:m_vm->database()->nodes)if(m_vm->working().role!=LinRole::Slave||n!=m_vm->database()->master)m_node->addItem(n);m_node->setCurrentText(m_vm->working().node);
    m_schedule->clear();m_activeSchedule->clear();for(const auto&s:m_vm->working().schedules){m_schedule->addItem(s.name);m_activeSchedule->addItem(s.name);}m_schedule->setCurrentText(m_vm->working().schedule);m_activeSchedule->setCurrentText(m_vm->working().schedule);
    m_canNode->clear();m_canNode->addItem("请选择节点",QString());for(const auto&node:m_vm->database()->nodes)m_canNode->addItem(node,node);
    m_canNode->setCurrentIndex(m_canNode->findData(m_vm->working().canNode));m_canDirection->setCurrentText(m_vm->working().canDirection);
    m_tree->header()->setStretchLastSection(false);m_tree->header()->setSectionResizeMode(0,QHeaderView::Stretch);m_tree->header()->setSectionResizeMode(1,QHeaderView::Fixed);m_tree->setColumnWidth(1,85);m_tree->expandToDepth(0);int selected=-1;
    for(int row=0;row<m_plan->model()->rowCount();++row)if((m_canModel?m_canModel->key(row):m_linModel->key(row))==m_key){selected=row;break;}
    if(selected<0 && m_plan->model()->rowCount()>0)selected=0;
    if(selected>=0){m_plan->selectRow(selected);selectFrame(m_canModel?m_canModel->key(selected):m_linModel->key(selected));}
    else{m_key.clear();m_valueModel->setFrame({});}render();
}
void SignalTransmitPage::render(){QScopedValueRollback<bool>guard(m_rendering,true);const bool can=m_canModel!=nullptr;const bool structure=m_vm->canStructure();
    m_replayPath->setText(m_vm->working().replaySettings.value("path").toString());m_replayImport->setEnabled(structure);m_replayReset->setEnabled(structure&&m_vm->replayConfigured());m_replayMap->setEnabled(structure&&!m_vm->replayLog().channels.isEmpty());
    m_rolePanel->setVisible(!can);m_import->setEnabled(structure);m_reload->setEnabled(structure&&!m_vm->database()->path.isEmpty());m_canPanel->setVisible(can);m_canNode->setEnabled(structure);m_canDirection->setEnabled(structure);m_once->setVisible(true);m_once->setText(m_vm->replayConfigured()?"多次回放":can?"多次发送":"多次调度");
    m_schedules->setEnabled(structure);m_role->setEnabled(structure);m_node->setEnabled(structure&&m_vm->working().role==LinRole::Slave);
    const auto state=m_vm->status().state;m_schedule->setEnabled(!m_vm->status().cleanupPending&&!m_vm->replayRunning()&&(structure||m_vm->running())&&state!=RunState::SwitchPending&&state!=RunState::Switching&&state!=RunState::Stopping);
    m_schedule->setCurrentText(m_vm->status().pending.isEmpty()?m_vm->working().schedule:m_vm->status().pending);m_activeSchedule->setCurrentText(m_schedule->currentText());m_activeSchedule->setEnabled(m_schedule->isEnabled());
    const bool enabled=m_vm->replayConfigured()?m_vm->canStructure()&&!m_vm->replayLog().frames.isEmpty():m_vm->canStart()&&m_vm->hasEnabled();m_once->setEnabled(enabled&&(m_vm->replayConfigured()||can||m_vm->working().role==LinRole::Master));m_count->setEnabled(m_once->isEnabled());{QSignalBlocker block(m_count);m_count->setValue(m_vm->working().repeatCount);}m_start->setEnabled(enabled);m_stop->setEnabled(m_vm->running()&&state!=RunState::Stopping);m_back->setEnabled(m_vm->canBack());m_restore->setEnabled(m_vm->canRestore());
    m_start->setText(m_vm->replayConfigured()?"周期回放":can?"周期发送":m_vm->working().role==LinRole::Master?"周期调度":m_vm->working().role==LinRole::Slave?"启动从节点响应":"开始监听");
    const auto db=m_vm->database();m_summary->setText(m_vm->importing()?m_vm->message():QString("%1 · %2 个节点 / %3 个帧 · %4%5").arg(can?"DBC":"LDF").arg(db->nodes.size()).arg(m_vm->definitions().size()).arg(db->version,db->diagnostics.isEmpty()?QString():QString(" · %1 条能力诊断（悬停查看）").arg(db->diagnostics.size())));m_summary->setToolTip(db->diagnostics.join('\n'));if(!db->diagnostics.isEmpty())m_summary->setText(m_summary->text()+"\n"+db->diagnostics.join('\n'));
    if(!m_vm->message().isEmpty())m_configurationFeedback->setText(m_vm->message());renderFrame();
}
void SignalTransmitPage::renderFrame(){m_editor->setEnabled(m_vm->canData());}
void SignalTransmitPage::editCustom(){
    if(!m_vm->canStructure())return;
    QDialog dialog(this);dialog.setObjectName("signalCustomDialog");dialog.setWindowTitle(m_canModel?"新建 CAN 报文":"新建 LIN 帧");auto*layout=new QFormLayout(&dialog);auto*name=new QLineEdit;auto*id=new QLineEdit;name->setObjectName("signalCustomName");id->setObjectName("signalCustomId");auto*type=plain(m_canModel?"ID ≤ 0x7FF：标准；其余：扩展（自动）":"LIN ID 0–0x3D；0x3C/0x3D 使用经典校验和 8 字节");
    auto*length=new QSpinBox;length->setRange(m_canModel?0:1,8);length->setValue(8);auto*period=new QSpinBox;period->setRange(0,std::numeric_limits<int>::max());period->setValue(100);auto*error=plain("");
    layout->addRow("名称",name);layout->addRow("ID（HEX）",id);layout->addRow(type);layout->addRow("字节数",length);layout->addRow("周期 ms",period);layout->addRow(error);auto*buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);layout->addRow(buttons);
    connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);connect(buttons,&QDialogButtonBox::accepted,&dialog,[&]{bool parsed=false;const auto value=id->text().toULongLong(&parsed,16);QString why;if(!parsed||value>(m_canModel?0x1fffffff:61)){error->setText("ID 必须在 0–0x1FFFFFFF");return;}if(!m_vm->putCustom({},name->text(),quint32(value),length->value(),period->value(),why)){error->setText(why);return;}m_key=frameKey(m_canModel?Bus::Can:Bus::Lin,quint32(value),value>0x7ff);rebuild();dialog.accept();});dialog.exec();
}
void SignalTransmitPage::editSchedules(){
    if(!m_vm->canStructure())return;QDialog dialog(m_configurationDialog);dialog.setWindowTitle("LIN 运行调度副本（不修改 LDF）");dialog.resize(680,460);auto*layout=new QVBoxLayout(&dialog);auto schedules=m_vm->working().schedules;
    auto*toolbar=new QHBoxLayout;auto*selector=new QComboBox;auto*add=button("新增表","signalNewSchedule");auto*remove=button("删除表","signalRemoveSchedule");toolbar->addWidget(selector,1);toolbar->addWidget(add);toolbar->addWidget(remove);layout->addLayout(toolbar);
    auto*table=new QTableWidget;table->setColumnCount(2);table->setHorizontalHeaderLabels({"帧-名称","delay ms（相邻槽起点）"});table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);tableStyle(table);layout->addWidget(table,1);
    auto*slotActions=new QHBoxLayout;auto*insert=button("增加槽","signalAddSlot");auto*erase=button("删除槽","signalRemoveSlot");auto*up=button("上移","signalSlotUp");auto*down=button("下移","signalSlotDown");for(auto*b:{insert,erase,up,down})slotActions->addWidget(b);layout->addLayout(slotActions);auto*error=plain("");layout->addWidget(error);
    int current=-1;const auto definitions=m_vm->definitions();
    auto capture=[&]{if(current<0||current>=schedules.size())return;auto&entries=schedules[current].entries;entries.clear();schedules[current].issue.clear();for(int row=0;row<table->rowCount();++row){auto*combo=qobject_cast<QComboBox*>(table->cellWidget(row,0));entries.append({combo->currentData().toString(),table->item(row,1)->text(),{},0});}};
    auto display=[&]{table->setRowCount(0);if(current<0||current>=schedules.size())return;const auto entries=schedules[current].entries;table->setRowCount(entries.size());for(int row=0;row<entries.size();++row){auto*combo=new QComboBox;for(const auto&f:definitions)combo->addItem(f.name+" · 0x"+QString::number(f.id,16),f.key);int index=combo->findData(entries[row].frame);if(index<0){combo->addItem(entries[row].frame+"（不支持）",entries[row].frame);index=combo->count()-1;}combo->setCurrentIndex(index);table->setCellWidget(row,0,combo);table->setItem(row,1,new QTableWidgetItem(entries[row].delayMs));}};
    auto refresh=[&]{QSignalBlocker block(selector);selector->clear();for(const auto&s:schedules)selector->addItem(s.name);selector->setCurrentIndex(current);display();};
    for(int i=0;i<schedules.size();++i)if(schedules[i].name==m_vm->working().schedule)current=i;if(current<0&&!schedules.isEmpty())current=0;refresh();
    connect(selector,qOverload<int>(&QComboBox::currentIndexChanged),&dialog,[&](int index){capture();current=index;display();});
    connect(add,&QPushButton::clicked,&dialog,[&]{bool ok=false;QSet<QString> names;for(const auto &s:schedules)names.insert(s.name);int n=1;QString defaultName;do{defaultName=QString("Schedule_Default%1").arg(n++,3,10,QChar('0'));}while(names.contains(defaultName));const auto name=QInputDialog::getText(&dialog,"新增调度表","名称",QLineEdit::Normal,defaultName,&ok);if(!ok)return;capture();schedules.append({name,{},{}});current=schedules.size()-1;refresh();});
    connect(remove,&QPushButton::clicked,&dialog,[&]{if(current<0)return;schedules.removeAt(current);current=schedules.isEmpty()?-1:qMin(current,int(schedules.size())-1);refresh();});
    connect(insert,&QPushButton::clicked,&dialog,[&]{if(current<0||definitions.isEmpty())return;capture();schedules[current].entries.append({definitions.first().key,"100",{},0});display();});
    connect(erase,&QPushButton::clicked,&dialog,[&]{const int row=table->currentRow();if(current<0||row<0)return;capture();schedules[current].entries.removeAt(row);display();});
    auto move=[&](int delta){const int row=table->currentRow();if(current<0||row<0||row+delta<0||row+delta>=table->rowCount())return;capture();schedules[current].entries.swapItemsAt(row,row+delta);display();table->selectRow(row+delta);};connect(up,&QPushButton::clicked,&dialog,[&]{move(-1);});connect(down,&QPushButton::clicked,&dialog,[&]{move(1);});
    auto*buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);layout->addWidget(buttons);connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);connect(buttons,&QDialogButtonBox::accepted,&dialog,[&]{capture();QString why;if(!m_vm->replaceSchedules(schedules,current>=0?schedules[current].name:QString(),why)){error->setText(why);return;}dialog.accept();});dialog.exec();
}
}

namespace host {
void SignalTransmitPage::editReplayMapping(){
    QDialog dialog(m_configurationDialog);dialog.setObjectName("replayChannelMapping");dialog.setWindowTitle("日志通道 → 软件通道");dialog.resize(600,420);auto *layout=new QVBoxLayout(&dialog);
    layout->addWidget(plain("无：排除此日志通道。多个日志通道可映射到同一个软件通道；一次操作统一控制全部映射目标。"));
    auto *table=new QTableWidget;table->setColumnCount(2);table->setHorizontalHeaderLabels({"日志通道","目标软件通道"});table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);layout->addWidget(table);
    const auto channels=m_vm->replayLog().channels;const auto mapping=m_vm->working().replaySettings.value("mapping").toObject();table->setRowCount(channels.size());
    for(int n=0;n<channels.size();++n){auto *item=new QTableWidgetItem(channels[n]);item->setFlags(item->flags()&~Qt::ItemIsEditable);table->setItem(n,0,item);auto *target=new QComboBox;target->setProperty("translateFirstOption",true);target->addItem("无",QString());if(m_vm->replayTargets)for(const auto &name:m_vm->replayTargets(channels[n].startsWith("CAN:")?signal::Bus::Can:signal::Bus::Lin))target->addItem(name,name);target->setCurrentIndex(qMax(0,target->findData(mapping.value(channels[n]).toString())));table->setCellWidget(n,1,target);}
    auto *buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);layout->addWidget(buttons);connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    if(dialog.exec()!=QDialog::Accepted)return;QJsonObject result;for(int n=0;n<channels.size();++n)result[channels[n]]=qobject_cast<QComboBox*>(table->cellWidget(n,1))->currentData().toString();m_vm->setReplayMapping(result);
}
}
