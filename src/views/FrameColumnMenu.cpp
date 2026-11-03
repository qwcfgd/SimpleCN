#include "FrameColumnMenu.h"
#include "localization/Language.h"
#include <QAbstractListModel>
#include <QBoxLayout>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFutureWatcher>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSortFilterProxyModel>
#include <QTableWidget>
#include <QToolButton>
#include <QWidgetAction>
namespace host {
namespace {
QVector<FrameFilterOp> operations(bool id){return id?QVector<FrameFilterOp>{FrameFilterOp::Equal,FrameFilterOp::NotEqual,FrameFilterOp::Greater,FrameFilterOp::GreaterEqual,FrameFilterOp::Less,FrameFilterOp::LessEqual,FrameFilterOp::Between,FrameFilterOp::NotBetween}:QVector<FrameFilterOp>{FrameFilterOp::Equal,FrameFilterOp::NotEqual,FrameFilterOp::Starts,FrameFilterOp::Ends,FrameFilterOp::Contains,FrameFilterOp::NotContains};}
class ValueListModel : public QAbstractListModel {
public:
    ValueListModel(const FrameColumnFilter&filter,QObject*parent):QAbstractListModel(parent),all(!filter.valuesEnabled),selected(filter.values){}
    int rowCount(const QModelIndex&p={})const override{return p.isValid()?0:items.size();}
    QVariant data(const QModelIndex&i,int role)const override{
        if(!i.isValid()||i.row()>=items.size())return {};const auto&v=items[i.row()];
        if(role==Qt::DisplayRole)return v.second.isEmpty()?Language::text("（空白）"):v.second;
        if(role==Qt::UserRole)return v.first;if(role==Qt::CheckStateRole)return all||selected.contains(v.first)?Qt::Checked:Qt::Unchecked;return {};
    }
    Qt::ItemFlags flags(const QModelIndex&i)const override{return QAbstractListModel::flags(i)|Qt::ItemIsUserCheckable;}
    bool setData(const QModelIndex&i,const QVariant&value,int role)override{
        if(!i.isValid()||role!=Qt::CheckStateRole)return false;
        touched=true;
        if(all){all=false;for(const auto&v:items)selected.insert(v.first);}
        if(value.toInt()==Qt::Checked)selected.insert(items[i.row()].first);else selected.remove(items[i.row()].first);
        emit dataChanged(i,i,{Qt::CheckStateRole});return true;
    }
    void load(FrameCandidates next){beginResetModel();items=std::move(next);endResetModel();}
    void selectAll(bool checked){touched=true;all=checked;selected.clear();if(rowCount())emit dataChanged(index(0),index(rowCount()-1),{Qt::CheckStateRole});}
    FrameCandidates items;bool all,touched=false;QSet<QString>selected;
};
}
FrameColumnMenu::FrameColumnMenu(FrameFilterProxy*proxy,int column,QWidget*parent):QMenu(parent),m_result(proxy->columnFilter(column)){
    setObjectName("frameColumnMenu");setMinimumWidth(310);
    auto*remove=addAction(Language::text("删除本列筛选"));remove->setObjectName("clearColumnFilter");remove->setEnabled(m_result.active());remove->setProperty("clearColumnFilter",true);
    auto*rules=addMenu(Language::text(column==5?"ID 筛选":"文本筛选"));rules->setObjectName("frameColumnRuleMenu");
    for(auto op:operations(column==5)){auto*action=rules->addAction(frameFilterOperatorName(op)+"…");action->setProperty("frameRuleOperation",int(op));}
    rules->addSeparator();auto*custom=rules->addAction(Language::text("自定义筛选…"));custom->setProperty("frameRuleOperation",-1);
    addSeparator();auto*panel=new QWidget(this);auto*layout=new QVBoxLayout(panel);layout->setContentsMargins(8,4,8,8);
    auto*search=new QLineEdit;search->setObjectName("frameColumnSearch");search->setPlaceholderText(Language::text("搜索"));layout->addWidget(search);
    auto*all=new QCheckBox(Language::text("（全选）"));all->setObjectName("frameColumnSelectAll");all->setTristate(true);layout->addWidget(all);
    auto*values=new ValueListModel(m_result,this);auto*viewProxy=new QSortFilterProxyModel(this);viewProxy->setSourceModel(values);viewProxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
    auto*view=new QListView;view->setObjectName("frameColumnValues");view->setModel(viewProxy);view->setUniformItemSizes(true);view->setMinimumHeight(210);layout->addWidget(view);
    auto*hint=new QLabel(Language::text("正在载入候选值…"));hint->setObjectName("frameColumnLoading");layout->addWidget(hint);
    auto*buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);buttons->setObjectName("frameColumnButtons");buttons->button(QDialogButtonBox::Ok)->setText(Language::text("确定"));buttons->button(QDialogButtonBox::Cancel)->setText(Language::text("取消"));buttons->button(QDialogButtonBox::Ok)->setEnabled(false);layout->addWidget(buttons);
    auto*widget=new QWidgetAction(this);widget->setDefaultWidget(panel);addAction(widget);
    auto updateAll=[all,viewProxy]{int checked=0;for(int i=0;i<viewProxy->rowCount();++i)checked+=viewProxy->index(i,0).data(Qt::CheckStateRole).toInt()==Qt::Checked;QSignalBlocker blocker(all);all->setCheckState(checked==0?Qt::Unchecked:checked==viewProxy->rowCount()?Qt::Checked:Qt::PartiallyChecked);};
    connect(search,&QLineEdit::textChanged,this,[viewProxy,updateAll](const QString&text){viewProxy->setFilterFixedString(text);updateAll();});
    connect(values,&QAbstractItemModel::dataChanged,this,[updateAll]{updateAll();});
    connect(all,&QCheckBox::clicked,this,[values,viewProxy,search,all,updateAll]{
        const bool checked=all->checkState()!=Qt::Unchecked;
        if(search->text().isEmpty())values->selectAll(checked);
        else{
            values->touched=true;
            if(values->all){values->all=false;for(const auto&v:values->items)values->selected.insert(v.first);}
            for(int row=0;row<viewProxy->rowCount();++row){const auto key=viewProxy->index(row,0).data(Qt::UserRole).toString();if(checked)values->selected.insert(key);else values->selected.remove(key);}
            if(values->rowCount())emit values->dataChanged(values->index(0),values->index(values->rowCount()-1),{Qt::CheckStateRole});
        }updateAll();
    });
    connect(buttons,&QDialogButtonBox::rejected,this,&QMenu::close);
    connect(buttons,&QDialogButtonBox::accepted,this,[this,values,viewProxy,search]{
        if(!search->text().isEmpty()){
            m_result.valuesEnabled=true;m_result.values.clear();for(int row=0;row<viewProxy->rowCount();++row){const auto i=viewProxy->index(row,0);if(i.data(Qt::CheckStateRole).toInt()==Qt::Checked)m_result.values.insert(i.data(Qt::UserRole).toString());}
        }else{
            bool every=values->rowCount()>0;for(int row=0;row<values->rowCount();++row)every&=values->data(values->index(row),Qt::CheckStateRole).toInt()==Qt::Checked;
            m_result.valuesEnabled=!(values->all||(values->touched&&every));m_result.values=m_result.valuesEnabled?values->selected:QSet<QString>();
        }
        m_applied=true;close();
    });
    auto*watcher=new QFutureWatcher<FrameCandidates>(this);connect(watcher,&QFutureWatcher<FrameCandidates>::finished,this,[watcher,values,hint,buttons,updateAll]{values->load(watcher->result());hint->hide();buttons->button(QDialogButtonBox::Ok)->setEnabled(true);updateAll();});watcher->setFuture(proxy->candidateValues(column));
}
FrameRuleDialog::FrameRuleDialog(int column,const QString&name,const FrameColumnFilter&filter,FrameFilterOp initial,quint32 maximum,QWidget*parent):QDialog(parent),m_column(column),m_maximum(maximum),m_initial(filter){
    setObjectName("frameRuleDialog");setWindowTitle(Language::text("自定义筛选"));resize(740,350);auto*layout=new QVBoxLayout(this);layout->addWidget(new QLabel(Language::text("显示行：")+name));
    m_join=new QComboBox;m_join->setObjectName("frameRuleJoin");m_join->addItem(Language::text("与：满足全部规则"),true);m_join->addItem(Language::text("或：满足任意规则"),false);m_join->setCurrentIndex(filter.all?0:1);layout->addWidget(m_join);
    m_table=new QTableWidget(0,column==5?5:3);m_table->setObjectName("frameColumnRules");m_table->setHorizontalHeaderLabels(column==5?QStringList{Language::text("规则"),Language::text("值 / 下限"),Language::text("上限"),Language::text("格式"),Language::text("删除")}:QStringList{Language::text("规则"),Language::text("值"),Language::text("删除")});
    m_table->horizontalHeader()->setSectionResizeMode(1,QHeaderView::Stretch);m_table->verticalHeader()->hide();layout->addWidget(m_table,1);
    if(!filter.conditions.isEmpty()){for(const auto&c:filter.conditions)addRow(c);if(int(initial)>=0)static_cast<QComboBox*>(m_table->cellWidget(0,0))->setCurrentIndex(static_cast<QComboBox*>(m_table->cellWidget(0,0))->findData(int(initial)));}
    else{FrameCondition c;c.op=int(initial)>=0?initial:(column==5?FrameFilterOp::Equal:FrameFilterOp::Contains);addRow(c);}
    auto*add=new QPushButton(Language::text("添加规则"));add->setObjectName("frameAddRule");connect(add,&QPushButton::clicked,this,[this]{FrameCondition c;c.op=m_column==5?FrameFilterOp::Equal:FrameFilterOp::Contains;addRow(c);});layout->addWidget(add);
    if(column!=5)layout->addWidget(new QLabel(Language::text("? 代表单个字符，* 代表任意多个字符，~ 用于转义")));
    m_error=new QLabel;m_error->setObjectName("frameRuleError");m_error->setWordWrap(true);layout->addWidget(m_error);
    auto*buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);buttons->button(QDialogButtonBox::Ok)->setText(Language::text("确定"));buttons->button(QDialogButtonBox::Cancel)->setText(Language::text("取消"));layout->addWidget(buttons);connect(buttons,&QDialogButtonBox::accepted,this,&FrameRuleDialog::validateAndAccept);connect(buttons,&QDialogButtonBox::rejected,this,&QDialog::reject);
}
void FrameRuleDialog::addRow(const FrameCondition&condition){
    const int row=m_table->rowCount();m_table->insertRow(row);auto*op=new QComboBox;
    for(auto item:operations(m_column==5))op->addItem(frameFilterOperatorName(item),int(item));op->setCurrentIndex(op->findData(int(condition.op)));m_table->setCellWidget(row,0,op);
    m_table->setItem(row,1,new QTableWidgetItem(condition.value));
    if(m_column==5){m_table->setItem(row,2,new QTableWidgetItem(condition.upper));auto*base=new QComboBox;for(const auto&item:QVector<QPair<QString,int>>{{"BIN",2},{"OCT",8},{"DEC",10},{"HEX",16}})base->addItem(item.first,item.second);base->setCurrentIndex(base->findData(condition.base));m_table->setCellWidget(row,3,base);}
    auto*remove=new QToolButton;remove->setText("×");remove->setObjectName("frameRemoveRule");m_table->setCellWidget(row,m_table->columnCount()-1,remove);
    connect(remove,&QToolButton::clicked,this,[this,remove]{for(int i=0;i<m_table->rowCount();++i)if(m_table->cellWidget(i,m_table->columnCount()-1)==remove){m_table->removeRow(i);break;}});
}
FrameColumnFilter FrameRuleDialog::result()const{
    auto filter=m_initial;filter.all=m_join->currentData().toBool();filter.conditions.clear();
    for(int row=0;row<m_table->rowCount();++row){FrameCondition c;c.op=FrameFilterOp(static_cast<QComboBox*>(m_table->cellWidget(row,0))->currentData().toInt());c.value=m_table->item(row,1)->text();if(m_column==5){c.upper=m_table->item(row,2)->text();c.base=static_cast<QComboBox*>(m_table->cellWidget(row,3))->currentData().toInt();}filter.conditions.append(c);}return filter;
}
void FrameRuleDialog::validateAndAccept(){auto filter=result();QString error;if(!prepareFrameFilter(m_column,filter,m_maximum,error)){m_error->setText(Language::text(error));return;}accept();}
}
