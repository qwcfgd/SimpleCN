#include "SignalPlotDialog.h"
#include "SignalPlotCanvas.h"
#include "viewmodels/SignalTransmitViewModel.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QSplitter>
#include <QScrollArea>
#include <QCheckBox>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QDoubleSpinBox>
#include <QTimer>
#include <QMenu>
#include <QAction>
#include <QColorDialog>
#include <QTreeWidget>
#include <QLineEdit>
#include <QDialogButtonBox>
#include <QSignalBlocker>
#include <QInputDialog>
#include <QDropEvent>
#include <QJsonArray>
#include <algorithm>
namespace host {
class PlotSignalTree:public QTreeWidget {
public:using QTreeWidget::QTreeWidget;std::function<void()> reordered;
protected:void dropEvent(QDropEvent *event)override{
#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)
    const auto point=event->position().toPoint();
#else
    const auto point=event->pos();
#endif
    auto *target=itemAt(point);bool groupDragged=false;for(auto *item:selectedItems())if(item->data(0,Qt::UserRole).toString().isEmpty())groupDragged=true;
    if(groupDragged&&target&&(target->parent()||dropIndicatorPosition()==OnItem)){event->ignore();return;}
    QTreeWidget::dropEvent(event);if(event->isAccepted()&&reordered)QTimer::singleShot(0,this,reordered);
}};
SignalPlotDialog::SignalPlotDialog(SignalTransmitViewModel*vm,QWidget*parent):QDialog(parent),m_vm(vm){
    setObjectName("signalPlotDialog");setWindowTitle("图像观测");setWindowFlag(Qt::Window,true);setModal(false);resize(1280,760);setMinimumSize(880,520);
    m_model=new SignalPlotModel(vm,this);auto *root=new QVBoxLayout(this);auto *tools=new QHBoxLayout;
    m_cursor=new QCheckBox("光标窗格");m_difference=new QCheckBox("差分光标");m_thick=new QCheckBox("信号线加粗");m_grid=new QCheckBox("参考格");m_points=new QCheckBox("采样点");
    m_cursor->setObjectName("plotCursor");m_difference->setObjectName("plotDifference");m_thick->setObjectName("plotThick");m_grid->setObjectName("plotGrid");m_points->setObjectName("plotPoints");
    m_display=new QComboBox;m_display->setObjectName("plotDisplayMode");m_display->addItems({"All","Marked","GrayNoMarked"});
    m_axes=new QComboBox;m_axes->setObjectName("plotAxisMode");m_axes->addItems({"Fit","arrange","All"});
    tools->addWidget(m_cursor);tools->addWidget(m_difference);tools->addWidget(m_thick);tools->addWidget(new QLabel("信号"));tools->addWidget(m_display);tools->addWidget(new QLabel("坐标"));tools->addWidget(m_axes);tools->addWidget(m_grid);tools->addWidget(m_points);tools->addStretch();root->addLayout(tools);
    auto *cursorPane=new QWidget;cursorPane->setObjectName("plotCursorPane");auto *cursorLayout=new QHBoxLayout(cursorPane);cursorLayout->setContentsMargins(0,0,0,0);
    m_t1=new QDoubleSpinBox;m_t2=new QDoubleSpinBox;m_t1->setObjectName("plotC1");m_t2->setObjectName("plotC2");for(auto *spin:{m_t1,m_t2}){spin->setRange(0,1e9);spin->setDecimals(6);spin->setSuffix(" s");spin->setSingleStep(0.01);}m_t2->setValue(1);m_delta=new QLabel;m_delta->setObjectName("plotDeltaTime");
    cursorLayout->addWidget(new QLabel("C1"));cursorLayout->addWidget(m_t1);cursorLayout->addWidget(new QLabel("C2"));cursorLayout->addWidget(m_t2);cursorLayout->addWidget(m_delta);cursorLayout->addStretch();cursorLayout->addWidget(new QLabel("y = y(C1)    dy = y(C2) − y(C1)"));root->addWidget(cursorPane);
    auto *split=new QSplitter;split->setObjectName("plotSplitter");auto *tree=new PlotSignalTree;m_list=tree;m_list->setObjectName("plotSignals");m_list->setHeaderLabels({"信号 / 组","原始值","y","dy"});m_list->setAlternatingRowColors(true);m_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_list->setDragDropMode(QAbstractItemView::InternalMove);m_list->setDefaultDropAction(Qt::MoveAction);m_list->setContextMenuPolicy(Qt::CustomContextMenu);m_list->setEditTriggers(QAbstractItemView::SelectedClicked|QAbstractItemView::EditKeyPressed);m_list->setColumnWidth(0,150);for(int c=1;c<4;++c)m_list->setColumnWidth(c,72);m_list->header()->setStretchLastSection(false);split->addWidget(m_list);
    m_canvas=new SignalPlotCanvas(m_model);auto *scroll=new QScrollArea;scroll->setWidgetResizable(true);scroll->setWidget(m_canvas);scroll->setFrameShape(QFrame::NoFrame);split->addWidget(scroll);split->setSizes({375,875});split->setStretchFactor(0,3);split->setStretchFactor(1,7);root->addWidget(split,1);
    auto *bottom=new QHBoxLayout;auto *add=new QPushButton("添加信号…"),*remove=new QPushButton("删除"),*fit=new QPushButton("适应数据"),*clear=new QPushButton("清空曲线");add->setObjectName("plotAdd");remove->setObjectName("plotDelete");fit->setObjectName("plotFit");clear->setObjectName("plotClear");m_follow=new QCheckBox("跟随最新");m_follow->setObjectName("plotFollow");
    for(auto *button:{add,remove,fit,clear})bottom->addWidget(button);bottom->addWidget(m_follow);bottom->addStretch();root->addLayout(bottom);
    auto *hint=new QLabel("Shift 多选 · Delete 删除 · 拖动排序 · 右键分组或改色 · 视窗内每信号最多绘制 1,000 点，缩放后重新抽样");hint->setWordWrap(true);hint->setStyleSheet("color:#64748b");root->addWidget(hint);
    connect(add,&QPushButton::clicked,this,&SignalPlotDialog::addSignals);connect(remove,&QPushButton::clicked,this,&SignalPlotDialog::removeSignals);auto *del=new QAction(this);del->setShortcut(QKeySequence::Delete);del->setShortcutContext(Qt::WidgetWithChildrenShortcut);m_list->addAction(del);connect(del,&QAction::triggered,this,&SignalPlotDialog::removeSignals);
    connect(m_list,&QTreeWidget::itemSelectionChanged,this,&SignalPlotDialog::selectSignals);
    connect(m_list,&QTreeWidget::itemClicked,this,[this](QTreeWidgetItem*item,int col){if(col==0&&item->data(0,Qt::UserRole).toString().isEmpty())m_list->editItem(item,0);});
    connect(m_list,&QTreeWidget::itemChanged,this,[this](QTreeWidgetItem*item,int col){if(!m_rebuilding&&col==0&&item->data(0,Qt::UserRole).toString().isEmpty()){const auto group=item->data(0,Qt::UserRole+1).toString(),name=item->text(0);QTimer::singleShot(0,this,[this,group,name]{m_model->renameGroup(group,name);});}});
    connect(m_list,&QWidget::customContextMenuRequested,this,[this](const QPoint &point){QMenu menu(this);menu.addAction("添加信号…",this,&SignalPlotDialog::addSignals);auto *color=menu.addAction("更改颜色…",this,&SignalPlotDialog::chooseColor);auto *remove=menu.addAction("删除信号",this,&SignalPlotDialog::removeSignals);auto *group=menu.addAction("创建组");const auto selected=selectedRows();color->setEnabled(!selected.isEmpty());remove->setEnabled(!selected.isEmpty());group->setEnabled(!selected.isEmpty());
        auto *item=m_list->itemAt(point);const auto groupId=item?(item->parent()?item->parent():item)->data(0,Qt::UserRole+1).toString():QString();auto *ungroup=menu.addAction("解散组");ungroup->setEnabled(!groupId.isEmpty());auto *chosen=menu.exec(m_list->viewport()->mapToGlobal(point));
        if(chosen==group){bool ok=false;const auto name=QInputDialog::getText(this,"创建组","组名称",QLineEdit::Normal,"Group",&ok);if(ok)m_model->createGroup(selected,name);}
        else if(chosen==ungroup)m_model->dissolveGroup(groupId);});
    tree->reordered=[this]{QStringList order;QMap<QString,QString> groups;for(int n=0;n<m_list->topLevelItemCount();++n){auto *item=m_list->topLevelItem(n);const auto key=item->data(0,Qt::UserRole).toString();if(!key.isEmpty()){order.append(key);continue;}for(int c=0;c<item->childCount();++c){const auto key=item->child(c)->data(0,Qt::UserRole).toString();order.append(key);groups[key]=item->data(0,Qt::UserRole+1).toString();}}m_model->setOrder(order,groups);};
    connect(m_model,&SignalPlotModel::structureChanged,this,&SignalPlotDialog::rebuildList);
    // activated also fires when the user chooses the already-selected option.
    connect(m_display,qOverload<int>(&QComboBox::activated),this,[this](int n){m_canvas->setDisplay(n);persistControls();});connect(m_axes,qOverload<int>(&QComboBox::activated),this,[this](int n){m_canvas->setAxes(n);persistControls();});
    connect(m_display,qOverload<int>(&QComboBox::currentIndexChanged),m_canvas,&SignalPlotCanvas::setDisplay);connect(m_axes,qOverload<int>(&QComboBox::currentIndexChanged),m_canvas,&SignalPlotCanvas::setAxes);
    connect(m_thick,&QCheckBox::clicked,m_canvas,&SignalPlotCanvas::setThick);connect(m_grid,&QCheckBox::clicked,m_canvas,&SignalPlotCanvas::setGrid);connect(m_points,&QCheckBox::clicked,m_canvas,&SignalPlotCanvas::setPoints);
    auto cursors=[this,cursorPane]{cursorPane->setVisible(m_cursor->isChecked()||m_difference->isChecked());m_t2->setEnabled(m_difference->isChecked());updateCursors();};
    connect(m_cursor,&QCheckBox::toggled,this,cursors);connect(m_difference,&QCheckBox::toggled,this,cursors);connect(m_t1,qOverload<double>(&QDoubleSpinBox::valueChanged),this,&SignalPlotDialog::updateCursors);connect(m_t2,qOverload<double>(&QDoubleSpinBox::valueChanged),this,&SignalPlotDialog::updateCursors);
    connect(m_canvas,&SignalPlotCanvas::cursorsMoved,this,[this](double a,double b){QSignalBlocker x(m_t1),y(m_t2);m_t1->setValue(a);m_t2->setValue(b);updateCursors();});connect(fit,&QPushButton::clicked,m_canvas,&SignalPlotCanvas::fit);connect(clear,&QPushButton::clicked,m_model,&SignalPlotModel::clearSamples);connect(m_follow,&QCheckBox::toggled,m_canvas,&SignalPlotCanvas::setFollow);connect(m_canvas,&SignalPlotCanvas::followChanged,m_follow,&QCheckBox::setChecked);
    for(auto *check:{m_cursor,m_difference,m_thick,m_grid,m_points,m_follow})connect(check,&QCheckBox::clicked,this,&SignalPlotDialog::persistControls);
    connect(vm,&SignalTransmitViewModel::configurationRestored,this,&SignalPlotDialog::restoreControls);
    connect(m_canvas,&SignalPlotCanvas::viewChanged,this,&SignalPlotDialog::persistControls);
    restoreControls();
    auto *timer=new QTimer(this);timer->setInterval(33);connect(timer,&QTimer::timeout,this,[this,revision=quint64(-1)]()mutable{if(!isVisible()||revision==m_model->revision())return;revision=m_model->revision();refreshList();m_canvas->refresh();});timer->start();
}
void SignalPlotDialog::restoreControls(){
    m_restoring=true;
    const auto settings=m_vm->working().uiSettings.value("plotControls").toObject();m_display->setCurrentIndex(settings["display"].toInt());m_axes->setCurrentIndex(settings["axes"].toInt());m_cursor->setChecked(settings["cursor"].toBool());m_difference->setChecked(settings["difference"].toBool());m_thick->setChecked(settings["thick"].toBool());m_grid->setChecked(settings.value("grid").toBool(true));m_points->setChecked(settings["points"].toBool());m_follow->setChecked(settings.value("follow").toBool(true));m_t1->setValue(settings["c1"].toDouble());m_t2->setValue(settings.value("c2").toDouble(1));
    m_canvas->setDisplay(m_display->currentIndex());m_canvas->setAxes(m_axes->currentIndex());
    m_canvas->setThick(m_thick->isChecked());m_canvas->setGrid(m_grid->isChecked());m_canvas->setPoints(m_points->isChecked());m_canvas->setFollow(m_follow->isChecked());
    m_canvas->restoreViewSettings(settings.value("viewport").toObject());
    rebuildList();
    findChild<QWidget*>("plotCursorPane")->setVisible(m_cursor->isChecked()||m_difference->isChecked());m_t2->setEnabled(m_difference->isChecked());updateCursors();m_restoring=false;
}
QSet<int> SignalPlotDialog::selectedRows()const{QSet<QString> keys;for(auto *item:m_list->selectedItems()){const auto key=item->data(0,Qt::UserRole).toString();if(!key.isEmpty())keys.insert(key);else for(int n=0;n<item->childCount();++n)keys.insert(item->child(n)->data(0,Qt::UserRole).toString());}QSet<int> rows;for(int n=0;n<m_model->series().size();++n)if(keys.contains(m_model->series()[n].key))rows.insert(n);return rows;}
void SignalPlotDialog::selectSignals(){if(m_rebuilding)return;const auto rows=selectedRows();int current=-1;if(m_list->currentItem()){const auto key=m_list->currentItem()->data(0,Qt::UserRole).toString();for(int n:rows)if(m_model->series()[n].key==key)current=n;}if(current<0&&!rows.isEmpty())current=*rows.begin();m_model->mark(rows,current);m_canvas->refresh();}
void SignalPlotDialog::rebuildList(){
    m_rebuilding=true;QSet<QString> selected;for(int n=0;n<m_model->series().size();++n)if(m_model->series()[n].marked)selected.insert(m_model->series()[n].key);m_list->clear();QMap<QString,QTreeWidgetItem*> groups;
    for(const auto &s:m_model->series()){QTreeWidgetItem *parent=nullptr;if(!s.group.isEmpty()){parent=groups.value(s.group);if(!parent){parent=new QTreeWidgetItem(m_list,{s.groupName});parent->setData(0,Qt::UserRole+1,s.group);parent->setFlags(parent->flags()|Qt::ItemIsEditable|Qt::ItemIsDropEnabled);groups[s.group]=parent;}}
        auto *item=parent?new QTreeWidgetItem(parent):new QTreeWidgetItem(m_list);item->setText(0,s.name);item->setData(0,Qt::UserRole,s.key);item->setFlags((item->flags()|Qt::ItemIsDragEnabled)&~Qt::ItemIsDropEnabled&~Qt::ItemIsEditable);item->setSelected(selected.contains(s.key));}
    m_list->expandAll();m_rebuilding=false;refreshList();selectSignals();m_canvas->refresh();
}
void SignalPlotDialog::refreshList(){QSignalBlocker block(m_list);QMap<QString,int> rows;for(int n=0;n<m_model->series().size();++n)rows[m_model->series()[n].key]=n;QTreeWidgetItemIterator it(m_list);for(;*it;++it){auto *item=*it;const auto key=item->data(0,Qt::UserRole).toString();if(!rows.contains(key))continue;const int n=rows[key];const auto&s=m_model->series()[n];QPixmap pix(12,12);pix.fill(s.color);item->setIcon(0,QIcon(pix));item->setToolTip(0,s.frameName+" / "+s.name+"\n"+s.status);item->setText(1,s.raw);item->setText(2,m_model->data(m_model->index(n,6)).toString());item->setText(3,m_model->data(m_model->index(n,7)).toString());}}
void SignalPlotDialog::updateCursors(){const bool on=m_cursor->isChecked(),diff=m_difference->isChecked();m_list->setColumnHidden(3,!diff);m_model->setCursors(on,diff,qRound64(m_t1->value()*1e6),qRound64(m_t2->value()*1e6));m_canvas->setCursor(on,diff);m_canvas->setCursorTimes(m_t1->value(),m_t2->value());m_delta->setText(diff?QString("Δt = %1 s").arg(m_t2->value()-m_t1->value(),0,'f',6):QString());refreshList();persistControls();}
void SignalPlotDialog::persistControls(){if(m_restoring)return;auto ui=m_vm->working().uiSettings;ui["plotControls"]=QJsonObject{{"viewport",m_canvas->viewSettings()},{"display",m_display->currentIndex()},{"axes",m_axes->currentIndex()},{"cursor",m_cursor->isChecked()},{"difference",m_difference->isChecked()},{"thick",m_thick->isChecked()},{"grid",m_grid->isChecked()},{"points",m_points->isChecked()},{"follow",m_follow->isChecked()},{"c1",m_t1->value()},{"c2",m_t2->value()}};m_vm->setUiSettings(ui);}
void SignalPlotDialog::removeSignals(){auto rows=selectedRows().values();std::sort(rows.begin(),rows.end(),std::greater<int>());for(int n:rows)m_model->removeRows(n,1);}
void SignalPlotDialog::chooseColor(){const auto rows=selectedRows();if(rows.isEmpty())return;auto color=QColorDialog::getColor(m_model->series()[*rows.begin()].color,this,"信号颜色");if(color.isValid())for(int n:rows)m_model->setColor(n,color);refreshList();m_canvas->refresh();}
void SignalPlotDialog::addSignals(){
    QDialog dialog(this);dialog.setObjectName("plotSignalChooser");dialog.setWindowTitle("当前软件通道 · 添加数据库信号");dialog.resize(600,600);
    auto *layout=new QVBoxLayout(&dialog);auto *search=new QLineEdit;search->setPlaceholderText("搜索报文、ID 或信号");search->setObjectName("plotSignalSearch");layout->addWidget(search);
    auto *tree=new QTreeWidget;tree->setObjectName("plotSignalTree");tree->setHeaderLabels({"报文 / 信号","ID / 单位"});tree->setSelectionMode(QAbstractItemView::ExtendedSelection);tree->setColumnWidth(0,370);layout->addWidget(tree);
    const auto database=m_vm->database();
    for(const auto &frame:database->frames){auto *root=new QTreeWidgetItem(tree,{frame.name,"0x"+QString::number(frame.id,16).toUpper()});root->setFlags(root->flags()&~Qt::ItemIsSelectable);
        for(const auto &field:frame.fields){auto *leaf=new QTreeWidgetItem(root,{field.name,field.unit});leaf->setData(0,Qt::UserRole,frame.key);leaf->setData(0,Qt::UserRole+1,field.name);leaf->setToolTip(0,field.array?"数组信号可查看值，无法直接绘制数值曲线":field.comment);}}
    tree->expandAll();
    connect(search,&QLineEdit::textChanged,&dialog,[tree](const QString &text){for(int n=0;n<tree->topLevelItemCount();++n){auto *root=tree->topLevelItem(n);bool any=false;const bool match=(root->text(0)+" "+root->text(1)).contains(text,Qt::CaseInsensitive);for(int c=0;c<root->childCount();++c){auto *leaf=root->child(c);bool show=match||leaf->text(0).contains(text,Qt::CaseInsensitive);leaf->setHidden(!show);any|=show;}root->setHidden(!any);}});
    auto *buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);buttons->button(QDialogButtonBox::Ok)->setText("添加选中信号");layout->addWidget(buttons);
    connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    if(dialog.exec()!=QDialog::Accepted)return;
    for(auto *item:tree->selectedItems())if(item->parent())m_model->addSignal(item->data(0,Qt::UserRole).toString(),item->data(0,Qt::UserRole+1).toString());
    m_canvas->refresh();
}
}
