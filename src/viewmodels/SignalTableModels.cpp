#include "SignalTableModels.h"
#include "model/SignalCodec.h"
#include <QBrush>
namespace host {
using namespace signal;
SignalValueTableModel::SignalValueTableModel(SignalTransmitViewModel*vm,bool defaults,QObject*p):QAbstractTableModel(p),m_vm(vm),m_defaults(defaults){
    connect(vm,&SignalTransmitViewModel::structureChanged,this,[this]{beginResetModel();endResetModel();});
    connect(vm,&SignalTransmitViewModel::frameChanged,this,[this](const QString&key){if(key==m_key&&rowCount())emit dataChanged(index(0,0),index(rowCount()-1,6));});
    connect(vm,&SignalTransmitViewModel::changed,this,[this]{if(rowCount())emit dataChanged(index(0,0),index(rowCount()-1,6));});
}
void SignalValueTableModel::setFrame(const QString&key){if(m_key==key)return;beginResetModel();m_key=key;endResetModel();}
int SignalValueTableModel::rowCount(const QModelIndex&p)const{const auto*f=m_vm->frame(m_key);return !p.isValid()&&f?f->fields.size():0;}
QVariant SignalValueTableModel::headerData(int c,Qt::Orientation o,int role)const{if(role!=Qt::DisplayRole)return {};if(o==Qt::Vertical)return c+1;
    const QStringList normal={"信号","文件初始 raw","发送 raw","实际物理值 / 输入","单位 / 枚举","最近 RX","校验 / 草稿"};
    const QStringList defaults={"信号","默认值（只读）","使用值（可编辑）","实际物理值 / 输入","单位 / 枚举","文件来源","校验 / 草稿"};return (m_defaults?defaults:normal).value(c);
}
QVariant SignalValueTableModel::data(const QModelIndex&i,int role)const{
    const auto*f=m_vm->frame(m_key);if(!i.isValid()||!f||i.row()>=f->fields.size())return {};const auto&s=f->fields[i.row()];const auto&d=m_vm->working().frames[m_key];
    if(role==Qt::BackgroundRole){if(d.errors.contains(i.row()))return QBrush(QColor("#ffe0e0"));if(!d.warnings.value(i.row()).isEmpty())return QBrush(QColor("#fff0c4"));}
    if(role==Qt::ToolTipRole)return QString("%1\n%2 位，从 bit %3 起，%4\n发布：%5；接收：%6\n%7\n%8").arg(s.comment).arg(s.width).arg(s.start).arg(s.littleEndian?"Intel / little endian":"Motorola / big endian",s.publisher,s.receivers.join(", "),s.initialSource,d.inputs.value(i.row()));
    if(role!=Qt::DisplayRole&&role!=Qt::EditRole)return {};
    const auto raw=d.values.value(i.row());const auto input=d.inputs.value(i.row());
    switch(i.column()){
    case 0:return s.name+(SignalCodec::isActive(*f,i.row(),d.values)?QString():"（非活动分支）");
    case 1:return m_defaults?SignalCodec::rawText(s,SignalCodec::defaults(s)):(s.initial.isEmpty()?"未定义 / 程序默认":s.initial);
    case 2:if(d.errors.contains(i.row())&&input.startsWith("raw "))return input.mid(4);return SignalCodec::rawText(s,raw);
    case 3:if(role==Qt::EditRole&&input.startsWith("物理 "))return input.mid(3);return SignalCodec::physicalText(s,raw);
    case 4:{QStringList labels;for(auto it=s.labels.begin();it!=s.labels.end();++it)labels.append(QString::number(it.key())+"="+it.value());return s.unit+(labels.isEmpty()?QString():" · "+labels.join(", "));}
    case 5:{if(m_defaults)return s.initial.isEmpty()?"程序默认（文件未定义）":s.initialSource+" = "+s.initial;if(!d.rx.received)return "未接收";if(!d.rx.valid)return d.rx.detail;
        auto values=d.values;QString error;if(!SignalCodec::decode(*f,d.rx.bytes,values,error))return error;if(!SignalCodec::isActive(*f,i.row(),values))return "非活动分支";
        return SignalCodec::rawText(s,values[i.row()])+" / "+SignalCodec::physicalText(s,values[i.row()]);}
    case 6:return d.errors.contains(i.row())?d.inputs.value(i.row())+"；"+d.errors.value(i.row()):d.warnings.value(i.row());
    }return {};
}
Qt::ItemFlags SignalValueTableModel::flags(const QModelIndex&i)const{auto flags=QAbstractTableModel::flags(i);const auto*f=m_vm->frame(m_key);if(!f||!i.isValid())return flags;
    if((m_defaults?m_vm->canStructure():m_vm->canData())&&f->issue.isEmpty()&&(i.column()==2||(i.column()==3&&f->fields[i.row()].conversion&&!f->fields[i.row()].array)))flags|=Qt::ItemIsEditable;return flags;
}
bool SignalValueTableModel::setData(const QModelIndex&i,const QVariant&value,int role){if(role!=Qt::EditRole||!(flags(i)&Qt::ItemIsEditable))return false;QString error;
    const bool ok=m_vm->editSignal(m_key,i.row(),value.toString(),i.column()==3,error);emit validation(error);return ok;
}
CanTxTableModel::CanTxTableModel(SignalTransmitViewModel*vm,QObject*p):QAbstractTableModel(p),m_vm(vm),m_definitions(vm->queuedDefinitions()){
    connect(vm,&SignalTransmitViewModel::structureChanged,this,[this]{beginResetModel();m_definitions=m_vm->queuedDefinitions();endResetModel();});
    connect(vm,&SignalTransmitViewModel::changed,this,[this]{if(rowCount())emit dataChanged(index(0,0),index(rowCount()-1,6));});
}
QString CanTxTableModel::key(int row)const{return row>=0&&row<m_definitions.size()?m_definitions[row].key:QString();}
QVariant CanTxTableModel::headerData(int c,Qt::Orientation o,int role)const{if(role!=Qt::DisplayRole)return {};if(o==Qt::Vertical)return c+1;return QStringList{"名称","ID","类型","字节","周期 ms","报文","状态"}.value(c);}
QVariant CanTxTableModel::data(const QModelIndex&i,int role)const{
    if(!i.isValid()||i.row()>=m_definitions.size())return {};const auto&f=m_definitions[i.row()];const auto&d=m_vm->working().frames[f.key];const auto&status=m_vm->status();
    if(role==Qt::ToolTipRole)return f.comment+"\n"+f.issue+"\n"+d.frameError;
    if(role==Qt::BackgroundRole&&(!f.issue.isEmpty()||!d.frameError.isEmpty()||status.failures.contains(f.key)))return QBrush(QColor("#ffe0e0"));
    if(role!=Qt::DisplayRole&&role!=Qt::EditRole)return {};
    switch(i.column()){
    case 0:return f.name;case 1:return "0x"+QString::number(f.id,16).toUpper();case 2:return QString(f.extended?"29 bit":"11 bit")+(f.custom?" · 自建":" · DBC");case 3:return f.length;case 4:return d.cycleMs;
    case 5:return role==Qt::EditRole&&!d.frameError.isEmpty()?d.frameInput:QString::fromLatin1(d.applied.bytes.toHex(' ')).toUpper();
    case 6:return !f.issue.isEmpty()?f.issue:status.failures.contains(f.key)?status.failures.value(f.key):!d.errors.isEmpty()||!d.frameError.isEmpty()?"错误草稿 / 继续有效值":"待发送";
    }return {};
}
Qt::ItemFlags CanTxTableModel::flags(const QModelIndex&i)const{
    auto flags=QAbstractTableModel::flags(i);if(!i.isValid()||i.row()>=m_definitions.size())return flags;
    if((i.column()==4&&m_vm->canStructure())||(i.column()==5&&m_vm->canData()&&m_definitions[i.row()].issue.isEmpty()))flags|=Qt::ItemIsEditable;return flags;
}
bool CanTxTableModel::setData(const QModelIndex&i,const QVariant&v,int role){
    if(role!=Qt::EditRole||!(flags(i)&Qt::ItemIsEditable))return false;QString error;bool ok=false;
    if(i.column()==5)ok=m_vm->editPayload(key(i.row()),v.toString(),error);
    else if(i.column()==4){bool parsed=false;int period=v.toString().toInt(&parsed);if(parsed&&period>=0)ok=m_vm->setCanOptions(key(i.row()),true,period,error);else error="周期须为非负整数 ms；0 表示仅单次";}
    emit validation(error);return ok;
}
SignalTreeModel::SignalTreeModel(SignalTransmitViewModel*vm,QObject*p):QStandardItemModel(p),m_vm(vm){connect(vm,&SignalTransmitViewModel::structureChanged,this,&SignalTreeModel::rebuild);rebuild();}
void SignalTreeModel::rebuild(){clear();setHorizontalHeaderLabels({"节点 / 报文","ID / 关系"});QMap<QString,QStandardItem*> nodes;
    auto all=new QStandardItem("全部报文");all->setEditable(false);appendRow(all);
    for(const auto&node:m_vm->database()->nodes){auto item=new QStandardItem(node);item->setEditable(false);nodes[node]=item;appendRow(item);}
    auto append=[&](QStandardItem*parent,const FrameDefinition&f,const QString&relationship){auto label=new QStandardItem(f.name);label->setEditable(false);label->setData(f.key,Qt::UserRole);auto id=new QStandardItem(QString("0x%1 · %2").arg(f.id,0,16).arg(relationship));id->setEditable(false);parent->appendRow({label,id});};
    for(const auto&f:m_vm->definitions()){append(all,f,f.custom?"自建":"数据库");QSet<QString> publishers;publishers.insert(f.publisher);for(const auto&n:f.transmitters)publishers.insert(n);for(const auto&n:publishers)if(nodes.contains(n))append(nodes[n],f,"发布");QSet<QString> receivers;for(const auto&s:f.fields)for(const auto&r:s.receivers)receivers.insert(r);for(const auto&r:receivers)if(nodes.contains(r)&&!publishers.contains(r))append(nodes[r],f,"订阅");}
}
LinScheduleTableModel::LinScheduleTableModel(SignalTransmitViewModel*vm,QObject*p):QAbstractTableModel(p),m_vm(vm),m_schedule(vm->working().schedule){auto update=[this]{const auto next=m_vm->working().schedule;if(next!=m_schedule){beginResetModel();m_schedule=next;endResetModel();}else if(rowCount())emit dataChanged(index(0,0),index(rowCount()-1,5));};connect(vm,&SignalTransmitViewModel::changed,this,update);connect(vm,&SignalTransmitViewModel::structureChanged,this,[this]{beginResetModel();m_schedule=m_vm->working().schedule;endResetModel();});}
const Schedule*LinScheduleTableModel::schedule()const{for(const auto&s:m_vm->working().schedules)if(s.name==m_schedule)return &s;return nullptr;}
int LinScheduleTableModel::rowCount(const QModelIndex&p)const{const auto*s=schedule();return p.isValid()||!s?0:s->entries.size();}
QString LinScheduleTableModel::key(int row)const{const auto*s=schedule();return s&&row>=0&&row<s->entries.size()?s->entries[row].frame:QString();}
QVariant LinScheduleTableModel::headerData(int c,Qt::Orientation o,int role)const{if(role!=Qt::DisplayRole)return {};if(o==Qt::Vertical)return c+1;return QStringList{"槽","帧","发布者","delay ms","报文","执行能力"}.value(c);}
QVariant LinScheduleTableModel::data(const QModelIndex&i,int role)const{const auto*s=schedule();if(!i.isValid()||!s||i.row()>=s->entries.size()||(role!=Qt::DisplayRole&&role!=Qt::EditRole&&role!=Qt::ToolTipRole&&role!=Qt::BackgroundRole))return {};const auto&slot=s->entries[i.row()];const auto*f=m_vm->frame(slot.frame);
    const auto d=m_vm->working().frames.value(slot.frame);
    if(role==Qt::ToolTipRole)return d.frameError;
    if(role==Qt::BackgroundRole)return d.frameError.isEmpty()?QVariant():QVariant(QBrush(QColor("#ffe0e0")));
    switch(i.column()){case 0:return i.row()+1;case 1:return f?f->name:slot.frame;case 2:return f?f->publisher:QString();case 3:return slot.delayMs;case 4:return role==Qt::EditRole&&!d.frameError.isEmpty()?d.frameInput:QString::fromLatin1(d.applied.bytes.toHex(' ')).toUpper();case 5:return !slot.issue.isEmpty()?slot.issue:f&&!f->issue.isEmpty()?f->issue:m_vm->working().role==LinRole::Monitor?"仅监听，不发布或响应":f&&f->publisher==m_vm->working().node?"当前节点发布":"外部节点响应";}return {};
}
Qt::ItemFlags LinScheduleTableModel::flags(const QModelIndex&i)const{
    auto result=QAbstractTableModel::flags(i);const auto*f=m_vm->frame(key(i.row()));
    if(i.isValid()&&i.column()==4&&f&&f->issue.isEmpty()&&m_vm->canData())result|=Qt::ItemIsEditable;return result;
}
bool LinScheduleTableModel::setData(const QModelIndex&i,const QVariant&v,int role){
    if(role!=Qt::EditRole||!(flags(i)&Qt::ItemIsEditable))return false;
    QString error;const bool ok=m_vm->editPayload(key(i.row()),v.toString(),error);emit validation(error);return ok;
}

}
