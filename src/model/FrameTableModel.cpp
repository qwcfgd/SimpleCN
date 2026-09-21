#include "localization/Language.h"
#include <cmath>
#include <limits>
#include "FrameTableModel.h"
#include "SignalCodec.h"
#include "infrastructure/TraceExporter.h"
#include <QColor>
#include <QDateTime>
#include <QRegularExpression>
namespace host {
namespace {
QString seconds(double value){
    if(!std::isfinite(value))return {};
    auto text=QString::number(value,'f',9);
    while(text.endsWith('0'))text.chop(1);
    if(text.endsWith('.'))text.chop(1);
    return text=="-0"?QString("0"):text;
}
}

QModelIndex FrameTableModel::index(int row,int col,const QModelIndex &p) const {
    if(row<0||col<0||col>=9||row>=rowCount(p))return {};
    return createIndex(row,col,p.isValid()?m_details[p.row()].token:quintptr(0));
}
QModelIndex FrameTableModel::parent(const QModelIndex &i) const {
    if(!i.isValid()||!i.internalId())return {};
    const int row=m_parentRows.value(i.internalId(),-1);
    return row<0?QModelIndex():createIndex(row,0,quintptr(0));
}
int FrameTableModel::rowCount(const QModelIndex &p) const {
    if(!p.isValid())return m_rows.size();
    if(p.column()!=0||p.internalId()||p.row()>=m_details.size())return 0;
    return merged()&&!m_details[p.row()].children.isEmpty()?m_details[p.row()].children.size()+1:0;
}
QVariant FrameTableModel::data(const QModelIndex &i,int role) const {
    if(!i.isValid()||i.column()<0||i.column()>=9)return {};
    if(role==Qt::TextAlignmentRole)return int(Qt::AlignLeft|Qt::AlignVCenter);
    if(i.internalId()){
        const auto p=parent(i);if(!p.isValid())return {};
        if(role==Qt::ForegroundRole)return QColor("#526779");
        if(role==Qt::DisplayRole||role==Qt::ToolTipRole){
            if(i.column()<5)return {};
            if(i.row()==0)return Language::text(QStringList{"信号名称","原始值","信号值","单位"}.value(i.column()-5));
            return m_details[p.row()].children.value(i.row()-1).value(i.column()-5);
        }return {};
    }
    if(i.row()<0||i.row()>=m_rows.size())return {};
    const auto &r=m_rows[i.row()];
    if(role==Qt::UserRole+2)return key(r);
    if(role==RelativeTimeSecondsRole)return r.relativeSeconds;
    if(role==NibbleAgeRole&&merged()&&i.column()==7)return m_details[i.row()].ages;
    if(role==Qt::ForegroundRole)return QColor(r.error?"#C93838":(r.warning?"#9A6700":"#29425D"));

    if(role==Qt::ToolTipRole||role==Qt::DisplayRole){
        switch(i.column()){case 0:return r.timestamp;case 1:return r.relativeTime;case 2:return r.intervalSeconds;
        case 3:return r.channel;case 4:return r.direction;case 5:return r.identifier;case 6:return r.length;case 7:return r.data;case 8:return Language::text(r.status);}
    }return {};
}
QVariant FrameTableModel::headerData(int c,Qt::Orientation o,int role) const {
    if(o==Qt::Horizontal&&c==1&&role==Qt::ToolTipRole)
        return Language::text("时刻单位为 s：从首条记录起算，省略小数末尾无效的 0。");
    if(o!=Qt::Horizontal||role!=Qt::DisplayRole)return {};
    return Language::text(QStringList{"时间","时刻/s","绝对时间/s","软件通道","方向","ID","长度","数据","状态"}.value(c));
}
QString FrameTableModel::key(const FrameRecord &r) const {
    return r.channel+":"+signal::frameKey(r.bus,r.id,r.extended);
}
FrameTableModel::Detail FrameTableModel::detail(const FrameRecord &r,quintptr token) const {
    Detail d;d.token=token;if(!merged())return d;for(int n=0;n<r.payload.size()*2;++n)d.ages.append(0);
    if(!m_database||m_database->bus!=r.bus)return d;
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
FrameTableModel::Detail FrameTableModel::updatedDetail(const FrameRecord &before,const FrameRecord &after,const Detail &previous) const {
    // Called only for the same frame key. Database changes rebuild all details.
    const bool same=before.payload==after.payload&&before.error==after.error&&before.noResponse==after.noResponse&&before.rtr==after.rtr;
    auto d=same?previous:detail(after,previous.token);
    const bool comparable=!after.error&&!after.noResponse&&!before.error&&!before.noResponse;
    for(int n=0;n<d.ages.size();++n){
        const int byte=n/2,mask=(n%2)?0x0f:0xf0;
        const bool unchanged=comparable&&byte<before.payload.size()&&((quint8(before.payload[byte])&mask)==(quint8(after.payload[byte])&mask));
        d.ages[n]=unchanged?qMin(10,previous.ages.value(n).toInt()+1):0;
    }
    return d;
}
void FrameTableModel::setDatabase(signal::Database db){if(m_database==db)return;m_database=std::move(db);m_frameLookup.clear();
    if(m_database)for(int n=0;n<m_database->frames.size();++n){const auto &f=m_database->frames[n];m_frameLookup[quint64(f.id)|(f.extended?(quint64(1)<<32):0)]=n;}if(!m_paused&&m_rolling)rebuild();}
void FrameTableModel::setRolling(bool on){if(on==m_rolling)return;m_rolling=on;if(!m_paused)rebuild();}
void FrameTableModel::setPaused(bool on){
    if(m_paused==on)return;m_paused=on;
    if(!on){m_windowStart=qMax(qint64(0),historyCount()-Capacity);m_windowSize=int(qMin(qint64(Capacity),historyCount()));}
    if(!on||m_rolling)rebuild();emit historyChanged();
}
void FrameTableModel::setHistoryStart(qint64 first){
    if(!m_paused)return;
    first=qBound(qint64(0),first,qMax(qint64(0),historyCount()-Capacity));
    const int size=int(qMin(qint64(Capacity),historyCount()-first));
    if(first==m_windowStart&&size==m_windowSize)return;
    m_windowStart=first;m_windowSize=size;
    rebuild();emit historyChanged();
}
void FrameTableModel::rebuild(){
    beginResetModel();m_rows.clear();m_details.clear();m_keys.clear();m_parentRows.clear();
    if(!merged()){
        m_rows.reserve(m_windowSize);m_details.reserve(m_windowSize);
        for(qint64 at=m_windowStart;at<m_windowStart+m_windowSize;++at){m_rows.append(m_history.at(at));Detail d;d.token=m_nextToken++;m_details.append(d);}
        endResetModel();return;
    }
    for(qint64 at=m_windowStart;at<m_windowStart+m_windowSize;++at){const auto &r=m_history.at(at);const auto k=key(r);int n=m_keys.value(k,-1);
        if(n<0){n=m_rows.size();m_keys[k]=n;m_rows.append(r);m_details.append(detail(r,m_nextToken++));m_parentRows.insert(m_details.last().token,n);}
        else {auto d=updatedDetail(m_rows[n],r,m_details[n]);
            m_rows[n]=r;m_details[n]=d;}
    }endResetModel();
}
void FrameTableModel::append(const FrameBatch &input){
    if(input.isEmpty())return;FrameBatch batch=input;
    static const QRegularExpression timestampPattern(QStringLiteral("^\\d{2}:\\d{2}:\\d{2}\\.\\d{3}$"));
    for(auto &r:batch){
        bool ok=r.captureUs>=0;double rawUs=double(r.captureUs);
        if(!ok)rawUs=r.relativeTime.toDouble(&ok);if(!ok)rawUs=r.timestamp.toDouble(&ok);
        ok=ok&&std::isfinite(rawUs)&&rawUs>=0&&rawUs<double(std::numeric_limits<qint64>::max());
        if(ok){if(!m_hasRelativeOrigin){m_relativeOriginUs=rawUs;m_hasRelativeOrigin=true;}
            r.captureUs=qRound64(rawUs);r.timeUs=qRound64(rawUs-m_relativeOriginUs);
            r.intervalSeconds=seconds(m_hasPrevious?(rawUs-m_previousUs)/1000000.0:0.0);
            m_previousUs=rawUs;m_hasPrevious=true;
            r.relativeSeconds=(rawUs-m_relativeOriginUs)/1000000.0;
            r.relativeTime=seconds(r.relativeSeconds);
            if(!timestampPattern.match(r.timestamp).hasMatch())r.timestamp=QDateTime::currentDateTime().toString("HH:mm:ss.zzz");
        }else{r.relativeSeconds=std::numeric_limits<double>::quiet_NaN();r.relativeTime.clear();r.intervalSeconds.clear();m_hasPrevious=false;}
        if(!r.typed){r.id=r.identifier.toUInt(nullptr,16);r.payload=QByteArray::fromHex(r.data.toLatin1());r.typed=true;}
        if(!r.epochMs)r.epochMs=QDateTime::currentMSecsSinceEpoch();if(!m_startedEpochMs)m_startedEpochMs=r.epochMs;
    }
    // History is session-owned and is released only by clear()/destruction.
    // Capacity limits the view, never the recorded/exportable collection.
    m_history+=batch;
    emit recorded(batch);
    if(m_paused){emit historyChanged();return;}
    m_windowStart=qMax(qint64(0),historyCount()-Capacity);m_windowSize=int(qMin(qint64(Capacity),historyCount()));
    emit historyChanged();
    if(!m_rolling){
        if(batch.size()>Capacity)batch=batch.mid(batch.size()-Capacity);
        int remove=qMax(0,int(m_rows.size()+batch.size())-Capacity);
        if(remove){beginRemoveRows({},0,remove-1);m_rows.remove(0,remove);m_details.remove(0,remove);endRemoveRows();}
        beginInsertRows({},m_rows.size(),m_rows.size()+batch.size()-1);
        for(const auto &r:batch){m_rows.append(r);m_details.append(detail(r,m_nextToken++));}endInsertRows();return;
    }
    for(const auto &r:batch){const auto k=key(r);int n=m_keys.value(k,-1);
        if(n<0){if(m_rows.size()>=Capacity){rebuild();return;}n=m_rows.size();beginInsertRows({},n,n);m_keys[k]=n;m_rows.append(r);m_details.append(detail(r,m_nextToken++));m_parentRows.insert(m_details.last().token,n);endInsertRows();}
        else {auto d=updatedDetail(m_rows[n],r,m_details[n]);
            m_rows[n]=r;m_details[n]=d;emit dataChanged(index(n,0),index(n,8));
            const auto p=index(n,0);if(rowCount(p))emit dataChanged(index(0,0,p),index(rowCount(p)-1,8,p));}
    }
}
void FrameTableModel::clear(){beginResetModel();m_rows.clear();m_history=FrameBatch();m_details.clear();m_keys.clear();m_parentRows.clear();m_windowStart=0;m_windowSize=0;m_relativeOriginUs=0;m_startedEpochMs=0;m_hasRelativeOrigin=false;m_previousUs=0;m_hasPrevious=false;endResetModel();emit cleared();emit historyChanged();}
bool FrameTableModel::exportCsv(const QString &p,QString &e) const{return TraceExporter::write(p,m_history,e,"csv");}
bool FrameTableModel::exportTrace(const QString &p,QString &e) const{return TraceExporter::write(p,m_history,e);}
}
