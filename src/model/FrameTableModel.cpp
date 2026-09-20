#include "FrameTableModel.h"
#include "SignalCodec.h"
#include "infrastructure/TraceExporter.h"
#include <QColor>
#include <QDateTime>
#include <QRegularExpression>
namespace host {
QModelIndex FrameTableModel::index(int row,int col,const QModelIndex &p) const {
    if(row<0||col<0||col>=9||row>=rowCount(p))return {};
    return createIndex(row,col,p.isValid()?m_details[p.row()].token:quintptr(0));
}
QModelIndex FrameTableModel::parent(const QModelIndex &i) const {
    if(!i.isValid()||!i.internalId())return {};
    for(int n=0;n<m_details.size();++n)if(m_details[n].token==i.internalId())return createIndex(n,0,quintptr(0));
    return {};
}
int FrameTableModel::rowCount(const QModelIndex &p) const {
    if(!p.isValid())return m_rows.size();
    if(p.column()!=0||p.internalId()||p.row()>=m_details.size())return 0;
    return m_rolling&&!m_details[p.row()].children.isEmpty()?m_details[p.row()].children.size()+1:0;
}
QVariant FrameTableModel::data(const QModelIndex &i,int role) const {
    if(!i.isValid()||i.column()<0||i.column()>=9)return {};
    if(role==Qt::TextAlignmentRole)return int(Qt::AlignLeft|Qt::AlignVCenter);
    if(i.internalId()){
        const auto p=parent(i);if(!p.isValid())return {};
        if(role==Qt::ForegroundRole)return QColor("#526779");
        if(role==Qt::DisplayRole||role==Qt::ToolTipRole){
            if(i.column()<5)return {};
            if(i.row()==0)return QStringList{"信号名称","原始值","信号值","单位"}.value(i.column()-5);
            return m_details[p.row()].children.value(i.row()-1).value(i.column()-5);
        }return {};
    }
    if(i.row()<0||i.row()>=m_rows.size())return {};
    const auto &r=m_rows[i.row()];
    if(role==Qt::UserRole+2)return key(r);
    if(role==NibbleAgeRole&&m_rolling&&i.column()==7)return m_details[i.row()].ages;
    if(role==Qt::ForegroundRole)return QColor(r.error?"#C93838":(r.warning?"#9A6700":"#29425D"));

    if(role==Qt::ToolTipRole||role==Qt::DisplayRole){
        switch(i.column()){case 0:return r.timestamp;case 1:return r.relativeTime;case 2:return r.intervalMs;
        case 3:return r.channel;case 4:return r.direction;case 5:return r.identifier;case 6:return r.length;case 7:return r.data;case 8:return r.status;}
    }return {};
}
QVariant FrameTableModel::headerData(int c,Qt::Orientation o,int role) const {
    if(o!=Qt::Horizontal||role!=Qt::DisplayRole)return {};
    return QStringList{"时间","时刻/ms","绝对时间/ms","软件通道","方向","ID","长度","数据","状态"}.value(c);
}
QString FrameTableModel::key(const FrameRecord &r) const {
    return r.channel+":"+signal::frameKey(r.bus,r.id,r.extended);
}
FrameTableModel::Detail FrameTableModel::detail(const FrameRecord &r,quintptr token) const {
    Detail d;d.token=token;if(!m_rolling)return d;for(int n=0;n<r.payload.size()*2;++n)d.ages.append(0);
    if(!m_rolling||!m_database||m_database->bus!=r.bus)return d;
    const int frameIndex=m_frameLookup.value(quint64(r.id)|(r.extended?(quint64(1)<<32):0),-1);
    if(frameIndex>=0){const auto &f=m_database->frames[frameIndex];
        QVector<signal::RawValue> values;QString error;
        const bool ok=!r.error&&!r.noResponse&&!r.rtr&&signal::SignalCodec::decode(f,r.payload,values,error);
        for(int n=0;n<f.fields.size();++n){const auto &s=f.fields[n];
            const bool active=ok&&signal::SignalCodec::isActive(f,n,values);
            QString unit=s.unit;
            if(active)for(const auto &range:s.ranges)if(values.value(n).bits>=range.first&&values.value(n).bits<=range.last){unit=range.unit;break;}
            d.children.append({s.name,active?signal::SignalCodec::rawText(s,values.value(n)):QString("—"),
                active?signal::SignalCodec::physicalText(s,values.value(n)):(ok?QString("非活动分支"):QString("无有效数据")),unit});
        }
    }return d;
}
void FrameTableModel::setDatabase(signal::Database db){if(m_database==db)return;m_database=std::move(db);m_frameLookup.clear();
    if(m_database)for(int n=0;n<m_database->frames.size();++n){const auto &f=m_database->frames[n];m_frameLookup[quint64(f.id)|(f.extended?(quint64(1)<<32):0)]=n;}rebuild();}
void FrameTableModel::setRolling(bool on){if(on==m_rolling)return;m_rolling=on;rebuild();}
void FrameTableModel::rebuild(){
    beginResetModel();m_rows.clear();m_details.clear();m_keys.clear();
    for(const auto &r:m_history){const auto k=key(r);int n=m_rolling?m_keys.value(k,-1):-1;
        if(n<0){n=m_rows.size();m_keys[k]=n;m_rows.append(r);m_details.append(detail(r,m_nextToken++));}
        else {auto d=detail(r,m_details[n].token);const auto before=m_rows[n].payload.toHex(),after=r.payload.toHex();
            for(int a=0;a<after.size();++a)if(!r.error&&!r.noResponse&&!m_rows[n].error&&!m_rows[n].noResponse&&a<before.size()&&before[a]==after[a])d.ages[a]=qMin(10,m_details[n].ages.value(a).toInt()+1);
            m_rows[n]=r;m_details[n]=d;}
    }endResetModel();
}
void FrameTableModel::append(const FrameBatch &input){
    if(input.isEmpty())return;FrameBatch batch=input;
    for(auto &r:batch){
        bool ok=r.captureUs>=0;qint64 rawUs=r.captureUs;
        if(!ok)rawUs=r.relativeTime.toLongLong(&ok);if(!ok)rawUs=r.timestamp.toLongLong(&ok);
        if(ok){if(!m_hasRelativeOrigin){m_relativeOriginUs=rawUs;m_hasRelativeOrigin=true;}
            r.captureUs=rawUs;r.timeUs=rawUs-m_relativeOriginUs;
            r.intervalMs=QString::number(m_hasPrevious?(rawUs-m_previousUs)/1000.0:0.0,'f',3);
            m_previousUs=rawUs;m_hasPrevious=true;r.relativeTime=QString::number(r.timeUs/1000.0,'f',6);
            if(!QRegularExpression("^\\d{2}:\\d{2}:\\d{2}\\.\\d{3}$").match(r.timestamp).hasMatch())r.timestamp=QDateTime::currentDateTime().toString("HH:mm:ss.zzz");
        }else{r.intervalMs.clear();m_hasPrevious=false;}
        if(!r.typed){r.id=r.identifier.toUInt(nullptr,16);r.payload=QByteArray::fromHex(r.data.toLatin1());r.typed=true;}
        if(!r.epochMs)r.epochMs=QDateTime::currentMSecsSinceEpoch();if(!m_startedEpochMs)m_startedEpochMs=r.epochMs;
    }
    m_history+=batch;if(m_history.size()>Capacity)m_history.remove(0,m_history.size()-Capacity);
    emit recorded(batch);
    if(!m_rolling){
        if(batch.size()>Capacity)batch=batch.mid(batch.size()-Capacity);
        int remove=qMax(0,int(m_rows.size()+batch.size())-Capacity);
        if(remove){beginRemoveRows({},0,remove-1);m_rows.remove(0,remove);m_details.remove(0,remove);endRemoveRows();}
        beginInsertRows({},m_rows.size(),m_rows.size()+batch.size()-1);
        for(const auto &r:batch){m_rows.append(r);m_details.append(detail(r,m_nextToken++));}endInsertRows();return;
    }
    for(const auto &r:batch){const auto k=key(r);int n=m_keys.value(k,-1);
        if(n<0){if(m_rows.size()>=Capacity){rebuild();return;}n=m_rows.size();beginInsertRows({},n,n);m_keys[k]=n;m_rows.append(r);m_details.append(detail(r,m_nextToken++));endInsertRows();}
        else {auto d=detail(r,m_details[n].token);const auto before=m_rows[n].payload.toHex(),after=r.payload.toHex();
            for(int a=0;a<after.size();++a)if(!r.error&&!r.noResponse&&!m_rows[n].error&&!m_rows[n].noResponse&&a<before.size()&&before[a]==after[a])d.ages[a]=qMin(10,m_details[n].ages.value(a).toInt()+1);
            m_rows[n]=r;m_details[n]=d;emit dataChanged(index(n,0),index(n,8));
            const auto p=index(n,0);if(rowCount(p))emit dataChanged(index(0,0,p),index(rowCount(p)-1,8,p));}
    }
}
void FrameTableModel::clear(){beginResetModel();m_rows.clear();m_history.clear();m_details.clear();m_keys.clear();m_relativeOriginUs=0;m_startedEpochMs=0;m_hasRelativeOrigin=false;m_previousUs=0;m_hasPrevious=false;endResetModel();emit cleared();}
bool FrameTableModel::exportCsv(const QString &p,QString &e) const{return TraceExporter::write(p,m_history,e,"csv");}
bool FrameTableModel::exportTrace(const QString &p,QString &e) const{return TraceExporter::write(p,m_history,e);}
}
