#include "SignalTableModels.h"
#include "model/SignalCodec.h"
#include <QBrush>
#include <QComboBox>
namespace host {
using namespace signal;
SignalValueTableModel::SignalValueTableModel(SignalTransmitViewModel*vm,QObject*p):QAbstractTableModel(p),m_vm(vm){
    connect(vm,&SignalTransmitViewModel::structureChanged,this,[this]{beginResetModel();endResetModel();});
    connect(vm,&SignalTransmitViewModel::frameChanged,this,[this](const QString&key){if(key==m_key&&rowCount())emit dataChanged(index(0,0),index(rowCount()-1,6));});
    connect(vm,&SignalTransmitViewModel::changed,this,[this]{if(rowCount())emit dataChanged(index(0,0),index(rowCount()-1,6));});
}
void SignalValueTableModel::setFrame(const QString&key){if(m_key==key)return;beginResetModel();m_key=key;endResetModel();}
int SignalValueTableModel::rowCount(const QModelIndex&p)const{const auto*f=m_vm->frame(m_key);return !p.isValid()&&f?f->fields.size():0;}
QVariant SignalValueTableModel::headerData(int c,Qt::Orientation o,int role)const{if(role!=Qt::DisplayRole)return {};if(o==Qt::Vertical)return c+1;
    const QStringList normal={"信号",m_vm->database()->bus==Bus::Can?"dbc初始raw":"ldf初始raw","发送 raw","发送物理值","单位 / 枚举","注释","校验"};
    return normal.value(c);
}
QVariant SignalValueTableModel::data(const QModelIndex&i,int role)const{
    const auto*f=m_vm->frame(m_key);if(!i.isValid()||!f||i.row()>=f->fields.size())return {};const auto&s=f->fields[i.row()];const auto&d=m_vm->working().frames[m_key];
    if(role==Qt::ForegroundRole&&!(flags(i)&Qt::ItemIsEditable))return QBrush(QColor("#808080"));
    if(role==EnumOptionsRole){QVariantList options;for(auto it=s.labels.begin();it!=s.labels.end();++it)options.append(QVariantMap{{"text",it.value()+" ("+SignalCodec::rawText(s,{it.key(),{}})+")"},{"raw","0x"+QString::number(it.key(),16)}});return options;}
    if(role==EnumValueRole)return "0x"+QString::number(d.values.value(i.row()).bits,16);
    if(role==PhysicalEditableRole)return s.conversion&&!s.array;
    if(role==Qt::BackgroundRole){if(d.errors.contains(i.row()))return QBrush(QColor("#ffe0e0"));if(!d.warnings.value(i.row()).isEmpty())return QBrush(QColor("#fff0c4"));}
    if(role==Qt::ToolTipRole)return QString("%1\n%2 位，从 bit %3 起，%4\n发布：%5；接收：%6\n%7\n%8").arg(s.comment).arg(s.width).arg(s.start).arg(s.littleEndian?"Intel / little endian":"Motorola / big endian",s.publisher,s.receivers.join(", "),s.initialSource,d.inputs.value(i.row()));
    if(role!=Qt::DisplayRole&&role!=Qt::EditRole)return {};
    const auto raw=d.values.value(i.row());const auto input=d.inputs.value(i.row());
    switch(i.column()){
    case 0:return s.name+(SignalCodec::isActive(*f,i.row(),d.values)?QString():"（非活动分支）");
    case 1:return (s.initial.isEmpty()?"未定义 / 程序默认":s.initial);
    case 2:if(d.errors.contains(i.row())&&input.startsWith("raw "))return input.mid(4);return SignalCodec::rawText(s,raw);
    case 3:if(role==Qt::EditRole&&input.startsWith("物理 "))return input.mid(3);if(role==Qt::DisplayRole&&s.labels.contains(raw.bits))return s.labels.value(raw.bits);return SignalCodec::physicalText(s,raw);
    case 4:{QStringList labels;for(auto it=s.labels.begin();it!=s.labels.end();++it)labels.append(QString::number(it.key())+"="+it.value());return s.unit+(labels.isEmpty()?QString():" · "+labels.join(", "));}
    case 5:return s.comment;
    case 6:return d.errors.contains(i.row())?d.inputs.value(i.row())+"；"+d.errors.value(i.row()):d.warnings.value(i.row());
    }return {};
}
Qt::ItemFlags SignalValueTableModel::flags(const QModelIndex&i)const{auto flags=QAbstractTableModel::flags(i);const auto*f=m_vm->frame(m_key);if(!f||!i.isValid())return flags;
    if(m_vm->canData()&&f->issue.isEmpty()&&(i.column()==2||(i.column()==3&&(f->fields[i.row()].conversion||!f->fields[i.row()].labels.isEmpty())&&!f->fields[i.row()].array)))flags|=Qt::ItemIsEditable;return flags;
}
bool SignalValueTableModel::setData(const QModelIndex&i,const QVariant&value,int role){
    if((role!=Qt::EditRole&&role!=EnumValueRole)||!(flags(i)&Qt::ItemIsEditable))return false;
    if(role==EnumValueRole){bool ok=false;const auto bits=value.toString().mid(2).toULongLong(&ok,16);const auto*f=m_vm->frame(m_key);if(i.column()!=3||!ok||!f->fields[i.row()].labels.contains(bits))return false;}
    QString error;const bool ok=m_vm->editSignal(m_key,i.row(),value.toString(),i.column()==3&&role!=EnumValueRole,error);emit validation(error);return ok;
}
QWidget*SignalValueDelegate::createEditor(QWidget*parent,const QStyleOptionViewItem&option,const QModelIndex&i)const{
    const auto values=i.data(SignalValueTableModel::EnumOptionsRole).toList();
    if(i.column()!=3||values.isEmpty())return QStyledItemDelegate::createEditor(parent,option,i);
    auto*combo=new QComboBox(parent);combo->setObjectName("signalEnumEditor");combo->setEditable(i.data(SignalValueTableModel::PhysicalEditableRole).toBool());
    for(const auto&v:values){const auto entry=v.toMap();combo->addItem(entry["text"].toString(),entry["raw"]);}
    auto*self=const_cast<SignalValueDelegate*>(this);connect(combo,qOverload<int>(&QComboBox::activated),self,[self,combo]{emit self->commitData(combo);emit self->closeEditor(combo);});return combo;
}
void SignalValueDelegate::setEditorData(QWidget*editor,const QModelIndex&i)const{
    auto*combo=qobject_cast<QComboBox*>(editor);if(!combo){QStyledItemDelegate::setEditorData(editor,i);return;}
    const int selected=combo->findData(i.data(SignalValueTableModel::EnumValueRole));
    if(selected>=0)combo->setCurrentIndex(selected);else if(combo->isEditable())combo->setEditText(i.data(Qt::EditRole).toString());else{combo->addItem(i.data().toString());combo->setCurrentIndex(combo->count()-1);}
}
void SignalValueDelegate::setModelData(QWidget*editor,QAbstractItemModel*model,const QModelIndex&i)const{
    auto*combo=qobject_cast<QComboBox*>(editor);if(!combo){QStyledItemDelegate::setModelData(editor,model,i);return;}
    if(combo->currentData().isValid()&&combo->currentText()==combo->itemText(combo->currentIndex()))model->setData(i,combo->currentData(),SignalValueTableModel::EnumValueRole);
    else if(combo->isEditable())model->setData(i,combo->currentText(),Qt::EditRole);
}
CanTxTableModel::CanTxTableModel(SignalTransmitViewModel*vm,QObject*p):QAbstractTableModel(p),m_vm(vm),m_definitions(vm->queuedDefinitions()){
    connect(vm,&SignalTransmitViewModel::structureChanged,this,[this]{beginResetModel();m_definitions=m_vm->queuedDefinitions();endResetModel();});
    connect(vm,&SignalTransmitViewModel::changed,this,[this]{if(rowCount())emit dataChanged(index(0,0),index(rowCount()-1,8));});
}
QString CanTxTableModel::key(int row)const{return row>=0&&row<m_definitions.size()?m_definitions[row].key:QString();}
QVariant CanTxTableModel::headerData(int c,Qt::Orientation o,int role)const{if(role!=Qt::DisplayRole)return {};if(o==Qt::Vertical)return c+1;return QStringList{"使能","序号","名称","ID","发布节点","类型","字节","周期 ms","报文"}.value(c);}
QVariant CanTxTableModel::data(const QModelIndex&i,int role)const{
    if(!i.isValid()||i.row()>=m_definitions.size())return {};const auto&f=m_definitions[i.row()];const auto&d=m_vm->working().frames[f.key];const auto&status=m_vm->status();
    if(role==Qt::CheckStateRole&&i.column()==0)return d.sendEnabled?Qt::Checked:Qt::Unchecked;
    if(role==Qt::ToolTipRole)return f.comment+"\n"+f.issue+"\n"+d.frameError;
    if(role==Qt::BackgroundRole&&(!f.issue.isEmpty()||!d.frameError.isEmpty()||status.failures.contains(f.key)))return QBrush(QColor("#ffe0e0"));
    if(role!=Qt::DisplayRole&&role!=Qt::EditRole)return {};
    switch(i.column()){
    case 1:return i.row()+1;case 2:return f.name;case 3:return "0x"+QString::number(f.id,16).toUpper();case 4:return f.transmitters.isEmpty()?f.publisher:f.transmitters.join(", ");case 5:return QString(f.extended?"29 bit":"11 bit")+(f.custom?" · 自建":" · DBC");case 6:return f.length;case 7:return d.cycleMs;
    case 8:return role==Qt::EditRole&&!d.frameError.isEmpty()?d.frameInput:QString::fromLatin1(d.applied.bytes.toHex(' ')).toUpper();
    }return {};
}
Qt::ItemFlags CanTxTableModel::flags(const QModelIndex&i)const{
    auto flags=QAbstractTableModel::flags(i);if(!i.isValid()||i.row()>=m_definitions.size())return flags;
    if(i.column()==0&&m_vm->canData()&&m_definitions[i.row()].issue.isEmpty())flags|=Qt::ItemIsUserCheckable;
    if((i.column()==7&&m_vm->canStructure())||(i.column()==8&&m_vm->canData()&&m_definitions[i.row()].issue.isEmpty()))flags|=Qt::ItemIsEditable;return flags;
}
bool CanTxTableModel::setData(const QModelIndex&i,const QVariant&v,int role){
    if(i.isValid()&&i.column()==0&&role==Qt::CheckStateRole){QString error;const bool ok=m_vm->setFrameEnabled(key(i.row()),v.toInt()==Qt::Checked,error);emit validation(error);return ok;}
    if(role!=Qt::EditRole||!(flags(i)&Qt::ItemIsEditable))return false;QString error;bool ok=false;
    if(i.column()==8)ok=m_vm->editPayload(key(i.row()),v.toString(),error);
    else if(i.column()==7){bool parsed=false;int period=v.toString().toInt(&parsed);if(parsed&&period>=0)ok=m_vm->setCanOptions(key(i.row()),true,period,error);else error="周期须为非负整数 ms；0 表示仅单次";}
    emit validation(error);return ok;
}
SignalTreeModel::SignalTreeModel(SignalTransmitViewModel*vm,QObject*p):QStandardItemModel(p),m_vm(vm){connect(vm,&SignalTransmitViewModel::structureChanged,this,&SignalTreeModel::rebuild);rebuild();}
void SignalTreeModel::rebuild(){
    clear();setHorizontalHeaderLabels({"节点/发送属性/报文","ID"});QMap<QString,QStandardItem*> nodes;
    auto all=new QStandardItem("全部报文");all->setEditable(false);appendRow(all);
    QMap<QString,QStandardItem*> tx,rx;
    for(const auto&node:m_vm->database()->nodes){auto item=new QStandardItem(node);item->setEditable(false);nodes[node]=item;appendRow(item);if(m_vm->database()->bus==Bus::Can){rx[node]=new QStandardItem("Rx");tx[node]=new QStandardItem("Tx");rx[node]->setEditable(false);tx[node]->setEditable(false);item->appendRow(rx[node]);item->appendRow(tx[node]);}}
    auto append=[&](QStandardItem*parent,const FrameDefinition&f){auto label=new QStandardItem(f.name);label->setEditable(false);label->setData(f.key,Qt::UserRole);auto id=new QStandardItem(QString("0x%1").arg(f.id,0,16));id->setEditable(false);parent->appendRow({label,id});};
    for(const auto&f:m_vm->definitions()){append(all,f);QSet<QString> publishers;publishers.insert(f.publisher);for(const auto&n:f.transmitters)publishers.insert(n);for(const auto&n:publishers)if(nodes.contains(n))append(tx.contains(n)?tx[n]:nodes[n],f);QSet<QString> receivers;for(const auto&s:f.fields)for(const auto&r:s.receivers)receivers.insert(r);for(const auto&r:receivers)if(nodes.contains(r))append(rx.contains(r)?rx[r]:nodes[r],f);}
}
LinScheduleTableModel::LinScheduleTableModel(SignalTransmitViewModel*vm,QObject*p):QAbstractTableModel(p),m_vm(vm),m_schedule(vm->working().schedule){auto update=[this]{const auto next=m_vm->working().schedule;if(next!=m_schedule){beginResetModel();m_schedule=next;endResetModel();}else if(rowCount())emit dataChanged(index(0,0),index(rowCount()-1,8));};connect(vm,&SignalTransmitViewModel::changed,this,update);connect(vm,&SignalTransmitViewModel::structureChanged,this,[this]{beginResetModel();m_schedule=m_vm->working().schedule;endResetModel();});}
const Schedule*LinScheduleTableModel::schedule()const{for(const auto&s:m_vm->working().schedules)if(s.name==m_schedule)return &s;return nullptr;}
int LinScheduleTableModel::rowCount(const QModelIndex&p)const{const auto*s=schedule();return p.isValid()||!s?0:s->entries.size();}
QString LinScheduleTableModel::key(int row)const{const auto*s=schedule();return s&&row>=0&&row<s->entries.size()?s->entries[row].frame:QString();}
QVariant LinScheduleTableModel::headerData(int c,Qt::Orientation o,int role)const{if(role!=Qt::DisplayRole)return {};if(o==Qt::Vertical)return c+1;return QStringList{"使能","槽","帧","ID","发布节点","字节","delay ms","报文","帧头 · 数据发送类型"}.value(c);}
QVariant LinScheduleTableModel::data(const QModelIndex&i,int role)const{const auto*s=schedule();if(!i.isValid()||!s||i.row()>=s->entries.size()||(role!=Qt::DisplayRole&&role!=Qt::EditRole&&role!=Qt::ToolTipRole&&role!=Qt::BackgroundRole&&role!=Qt::CheckStateRole))return {};const auto&slot=s->entries[i.row()];const auto*f=m_vm->frame(slot.frame);
    const auto d=m_vm->working().frames.value(slot.frame);
    if(role==Qt::CheckStateRole)return i.column()==0?QVariant(d.sendEnabled?Qt::Checked:Qt::Unchecked):QVariant();
    if(role==Qt::ToolTipRole)return slot.issue+"\n"+(f?f->issue:QString())+"\n"+d.frameError;
    if(role==Qt::BackgroundRole)return d.frameError.isEmpty()?QVariant():QVariant(QBrush(QColor("#ffe0e0")));
    switch(i.column()){case 1:return i.row()+1;case 2:return f?f->name:slot.frame;case 3:return f?QString("0x%1").arg(f->id,2,16,QChar('0')).toUpper():QString();case 4:return f?f->publisher:QString();case 5:return f?QVariant(f->length):QVariant();case 6:return slot.delayMs;case 7:return role==Qt::EditRole&&!d.frameError.isEmpty()?d.frameInput:QString::fromLatin1(d.applied.bytes.toHex(' ')).toUpper();case 8:{const auto role=m_vm->working().role;const bool tx=role!=LinRole::Monitor&&f&&(f->publisher==m_vm->working().node||(f->id==61&&role==LinRole::Slave));return QString(role==LinRole::Master?"Tx":"Rx")+" · "+(tx?"Tx":"Rx");}}return {};
}
Qt::ItemFlags LinScheduleTableModel::flags(const QModelIndex&i)const{
    auto result=QAbstractTableModel::flags(i);const auto*f=m_vm->frame(key(i.row()));
    if(i.isValid()&&i.column()==0&&f&&f->issue.isEmpty()&&m_vm->canData())result|=Qt::ItemIsUserCheckable;
    if(i.isValid()&&i.column()==7&&f&&f->issue.isEmpty()&&m_vm->canData())result|=Qt::ItemIsEditable;return result;
}
bool LinScheduleTableModel::setData(const QModelIndex&i,const QVariant&v,int role){
    if(i.isValid()&&i.column()==0&&role==Qt::CheckStateRole){QString error;const bool ok=m_vm->setFrameEnabled(key(i.row()),v.toInt()==Qt::Checked,error);emit validation(error);return ok;}
    if(role!=Qt::EditRole||!(flags(i)&Qt::ItemIsEditable))return false;
    QString error;const bool ok=m_vm->editPayload(key(i.row()),v.toString(),error);emit validation(error);return ok;
}

}
