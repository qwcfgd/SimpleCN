#include "SignalPlotModel.h"
#include "SignalTransmitViewModel.h"
#include "model/SignalCodec.h"
#include <QRandomGenerator>
#include <QUuid>
#include <QJsonArray>
#include <limits>
#include <algorithm>
#include <cmath>
namespace host {
using namespace signal;
SignalPlotModel::SignalPlotModel(SignalTransmitViewModel *vm,QObject *parent):QAbstractTableModel(parent),m_vm(vm),m_database(vm->database()){
    for(int n=0;n<m_database->frames.size();++n)m_frameLookup[m_database->frames[n].key]=n;
    connect(vm,&SignalTransmitViewModel::framesObserved,this,&SignalPlotModel::append);
    connect(vm,&SignalTransmitViewModel::traceCleared,this,&SignalPlotModel::clearSamples);
    connect(vm,&SignalTransmitViewModel::structureChanged,this,&SignalPlotModel::rebind);
    connect(vm,&SignalTransmitViewModel::configurationRestored,this,&SignalPlotModel::restoreConfiguration);restoreConfiguration();
}
QVariant SignalPlotModel::headerData(int c,Qt::Orientation o,int role) const {
    if(role!=Qt::DisplayRole)return {};if(o==Qt::Vertical)return c+1;
    return QStringList{"颜色","信号名称","报文 / ID","原始值","信号值","单位","y","dy"}.value(c);
}
QVariant SignalPlotModel::data(const QModelIndex &i,int role) const {
    if(!i.isValid()||i.row()>=m_series.size())return {};const auto &s=m_series[i.row()];
    if(role==Qt::DecorationRole&&i.column()==0)return s.color;
    if(role==Qt::ToolTipRole)return s.frameKey+" / "+s.name+"\n"+s.status;
    if(role!=Qt::DisplayRole)return {};
    switch(i.column()){
    case 0:return s.marked?"●":"";case 1:return s.name;case 2:return s.frameName+" / 0x"+QString::number(s.id,16).toUpper();
    case 3:return s.raw;case 4:return s.physical;case 5:return s.unit;
    case 6:return m_cursor||m_difference?valueAt(i.row(),m_t1).text:s.physical;
    case 7:return m_difference?differenceAt(i.row(),m_t1,m_t2):QString();
    }return {};
}
bool SignalPlotModel::addSignal(const QString &key,const QString &name){
    const auto id=QUuid::createUuid().toString(QUuid::WithoutBraces);
    const FrameDefinition *frame=nullptr;int field=-1;
    for(const auto &f:m_database->frames)if(f.key==key){frame=&f;for(int n=0;n<f.fields.size();++n)if(f.fields[n].name==name){field=n;break;}break;}
    if(!frame||field<0)return false;
    Series s;s.key=id;s.frameKey=key;s.name=name;s.frameName=frame->name;s.id=frame->id;s.extended=frame->extended;s.field=field;s.unit=frame->fields[field].unit;
    // Maximize distance from colors already in use; deterministic candidates avoid near duplicates.
    int bestHue=210;double bestDistance=-1;
    for(int hue=0;hue<360;hue+=7){const auto candidate=QColor::fromHsv(hue,200,185);double distance=1e12;
        for(const auto &existing:m_series){const double dr=candidate.red()-existing.color.red(),dg=candidate.green()-existing.color.green(),db=candidate.blue()-existing.color.blue();distance=qMin(distance,dr*dr+dg*dg+db*db);}
        if(distance>bestDistance){bestDistance=distance;bestHue=hue;}}
    s.color=QColor::fromHsv(bestHue,200,185);
    for(auto it=frame->fields[field].labels.begin();it!=frame->fields[field].labels.end();++it){bool ok=false;const double value=SignalCodec::physicalText(frame->fields[field],{it.key(),{}}).toDouble(&ok);if(ok)s.labels[value]=it.value();}s.raw=s.physical="—";s.status="等待采样";
    if(0.2126*s.color.red()+0.7152*s.color.green()+0.0722*s.color.blue()>140)s.color=s.color.darker(135);
    beginInsertRows({},m_series.size(),m_series.size());m_series.append(s);endInsertRows();
    // Backfill only the newly added series, without replaying existing samples.
    ingest(m_vm->traceHistory(),m_series.size()-1,true);
    setCapacity(m_capacity);++m_revision;refresh();saveConfiguration();emit structureChanged();return true;
}
bool SignalPlotModel::removeRows(int start,int count,const QModelIndex &p){
    if(p.isValid()||count<=0||start<0||start+count>m_series.size())return false;
    beginRemoveRows({},start,start+count-1);m_series.remove(start,count);endRemoveRows();m_active=-1;++m_revision;saveConfiguration();emit structureChanged();return true;
}
void SignalPlotModel::setColor(int row,QColor color){if(row<0||row>=m_series.size()||!color.isValid())return;m_series[row].color=color;++m_revision;emit dataChanged(index(row,0),index(row,0));saveConfiguration();}
void SignalPlotModel::mark(const QSet<int>&rows,int active){for(int n=0;n<m_series.size();++n)m_series[n].marked=rows.contains(n);m_active=active;++m_revision;refresh();saveConfiguration();emit selectionChanged();}
void SignalPlotModel::setCursors(bool enabled,bool diff,qint64 t1,qint64 t2){m_cursor=enabled;m_difference=diff;m_t1=t1;m_t2=t2;refresh();}
SignalPlotModel::Reading SignalPlotModel::valueAt(int row,qint64 us) const {
    Reading out;out.text="—";if(row<0||row>=m_series.size())return out;const auto &points=m_series[row].points;
    auto right=std::lower_bound(points.begin(),points.end(),us,[](const Point&p,qint64 t){return p.us<t;});
    if(right!=points.end()&&right->us==us){if(right->valid)return {true,right->value,right->exact,right->unit};return out;}
    if(right==points.begin()||right==points.end())return out;
    auto left=std::prev(right);
    if(!left->valid||!right->valid||right->us-left->us>5000000||right->unit!=left->unit)return out;
    const double value=left->value+(right->value-left->value)*double(us-left->us)/double(right->us-left->us);
    return {true,value,QString::number(value,'g',15),left->unit};
}
QString SignalPlotModel::differenceAt(int row,qint64 t1,qint64 t2) const {
    const auto a=valueAt(row,t1),b=valueAt(row,t2);if(!a.valid||!b.valid||a.unit!=b.unit)return "—";
    return SignalCodec::differenceText(b.text,a.text);
}
void SignalPlotModel::append(const FrameBatch &batch){ingest(batch);}
void SignalPlotModel::ingest(const FrameBatch &batch,int onlyRow,bool replay){
    QHash<QString,QVector<FrameRecord>> replayPending;
    auto &queues=replay?replayPending:m_pending;
    for(const auto &r:batch){
        if(r.bus!=m_database->bus||r.rtr)continue;
        const auto key=frameKey(r.bus,r.id,r.extended);
        bool wanted=false;for(int n=0;n<m_series.size();++n)if((onlyRow<0||onlyRow==n)&&m_series[n].frameKey==key){wanted=true;break;}if(!wanted)continue;
        auto &pending=queues[key];
        while(!pending.isEmpty()&&r.captureUs-pending.first().captureUs>100000)pending.removeFirst();
        if(r.echo){bool matched=false;for(int n=0;n<pending.size();++n)if(pending[n].payload==r.payload){pending.removeAt(n);matched=true;break;}if(matched)continue;}
        if(r.request){pending.append(r);if(pending.size()>256)pending.removeFirst();}
        const int frameIndex=m_frameLookup.value(key,-1);if(frameIndex<0)continue;const auto *frame=&m_database->frames[frameIndex];
        QVector<RawValue> values;QString error;
        const bool decoded=!r.error&&!r.noResponse&&SignalCodec::decode(*frame,r.payload,values,error);
        for(int seriesIndex=0;seriesIndex<m_series.size();++seriesIndex){auto &s=m_series[seriesIndex];if((onlyRow>=0&&onlyRow!=seriesIndex)||s.frameKey!=key||s.field<0)continue;
            const auto &field=frame->fields[s.field];Point p;p.us=r.timeUs;p.unit=field.unit;
            bool active=decoded&&SignalCodec::isActive(*frame,s.field,values);
            if(active){
                const auto raw=values.value(s.field);s.raw=SignalCodec::rawText(field,raw);s.physical=SignalCodec::physicalText(field,raw);
                for(const auto &range:field.ranges)if(raw.bits>=range.first&&raw.bits<=range.last){p.unit=range.unit;break;}
                p.exact=s.physical;p.value=s.physical.toDouble(&p.valid);p.valid=p.valid&&!field.array&&std::isfinite(p.value);
                s.status=p.valid?(r.simulated?"模拟采样":r.request?"驱动发送请求（非硬件确认）":r.echo?"硬件回读":"接收采样"):"非数值信号，无法绘制";
            }else {s.raw=s.physical="—";s.status=decoded?"非活动分支":error.isEmpty()?r.status:error;}
            s.unit=p.unit;
            if(!s.points.empty()&&p.us<s.points.back().us)continue;
            if(!s.points.empty()&&p.us==s.points.back().us)s.points.back()=p;else s.points.push_back(p);
            const int limit=qMin(m_capacity,qMax(1,1000000/qMax(1,int(m_series.size()))));
            while(int(s.points.size())>limit)s.points.pop_front();
        }
    }++m_revision;
}
void SignalPlotModel::refresh(){if(!m_series.isEmpty())emit dataChanged(index(0,0),index(m_series.size()-1,7));}
void SignalPlotModel::clearSamples(){for(auto &s:m_series){s.points.clear();s.raw=s.physical="—";s.status="等待采样";}m_pending.clear();++m_revision;refresh();}
void SignalPlotModel::setCapacity(int n){m_capacity=qBound(1000,n,1000000);const int limit=qMin(m_capacity,qMax(1,1000000/qMax(1,int(m_series.size()))));for(auto &s:m_series)while(int(s.points.size())>limit)s.points.pop_front();++m_revision;}
void SignalPlotModel::rebind(){
    if(m_database==m_vm->database())return;m_database=m_vm->database();clearSamples();
    m_frameLookup.clear();for(int n=0;n<m_database->frames.size();++n)m_frameLookup[m_database->frames[n].key]=n;
    for(auto &s:m_series){s.field=-1;s.status="数据库中已无此信号";
        for(const auto &f:m_database->frames)if(f.key==s.frameKey)for(int n=0;n<f.fields.size();++n)if(f.fields[n].name==s.name){s.field=n;s.unit=f.fields[n].unit;s.frameName=f.name;s.status="等待采样";}}
    refresh();
}
}

namespace host {
int SignalPlotModel::representative(int row)const{if(row<0||row>=m_series.size())return row;const auto group=m_series[row].group;if(group.isEmpty())return row;for(int n=0;n<m_series.size();++n)if(m_series[n].group==group)return n;return row;}
void SignalPlotModel::saveConfiguration(){
    if(m_restoring)return;QJsonArray list;for(const auto &s:m_series)list.append(QJsonObject{{"id",s.key},{"frame",s.frameKey},{"signal",s.name},{"color",s.color.name()},{"group",s.group},{"groupName",s.groupName},{"marked",s.marked}});
    auto ui=m_vm->working().uiSettings;ui["plotSeries"]=list;m_vm->setUiSettings(ui);
}
void SignalPlotModel::restoreConfiguration(){
    const auto list=m_vm->working().uiSettings.value("plotSeries").toArray();m_restoring=true;
    beginResetModel();m_series.clear();m_pending.clear();m_active=-1;m_database=m_vm->database();m_frameLookup.clear();
    for(int n=0;n<m_database->frames.size();++n)m_frameLookup[m_database->frames[n].key]=n;endResetModel();
    for(const auto &v:list){const auto o=v.toObject();if(!addSignal(o["frame"].toString(),o["signal"].toString()))continue;auto &s=m_series.last();
        s.key=o["id"].toString(s.key);const QColor color(o["color"].toString());if(color.isValid())s.color=color;s.group=o["group"].toString();s.groupName=o["groupName"].toString();s.marked=o["marked"].toBool();}
    ++m_revision;emit structureChanged();m_restoring=false;
}
bool SignalPlotModel::setOrder(const QStringList &keys,const QMap<QString,QString> &groups){
    if(keys.size()!=m_series.size()||QSet<QString>(keys.begin(),keys.end()).size()!=keys.size())return false;
    QMap<QString,QString> names;for(const auto &s:m_series)if(!s.group.isEmpty())names[s.group]=s.groupName;
    QVector<Series> ordered;for(const auto &key:keys){auto it=std::find_if(m_series.begin(),m_series.end(),[&](const Series&s){return s.key==key;});if(it==m_series.end())return false;ordered.append(*it);ordered.last().group=groups.value(key);ordered.last().groupName=names.value(ordered.last().group);}
    beginResetModel();m_series=std::move(ordered);m_active=-1;endResetModel();++m_revision;saveConfiguration();emit structureChanged();return true;
}
void SignalPlotModel::createGroup(const QSet<int> &rows,const QString &name){
    if(rows.isEmpty()||name.trimmed().isEmpty())return;const auto group=QUuid::createUuid().toString(QUuid::WithoutBraces);int at=m_series.size();
    QVector<Series> selected,other;for(int n=0;n<m_series.size();++n){auto s=m_series[n];if(rows.contains(n)){at=qMin(at,int(other.size()));s.group=group;s.groupName=name.trimmed();selected.append(s);}else other.append(s);}
    for(int n=0;n<selected.size();++n)other.insert(at+n,selected[n]);beginResetModel();m_series=std::move(other);endResetModel();++m_revision;saveConfiguration();emit structureChanged();
}
void SignalPlotModel::dissolveGroup(const QString &group){for(auto &s:m_series)if(s.group==group){s.group.clear();s.groupName.clear();}++m_revision;saveConfiguration();emit structureChanged();}
void SignalPlotModel::renameGroup(const QString &group,const QString &name){if(name.trimmed().isEmpty())return;for(auto &s:m_series)if(s.group==group)s.groupName=name.trimmed();saveConfiguration();emit structureChanged();}
QVector<SignalPlotModel::RenderPoint> SignalPlotModel::renderPoints(int row,qint64 from,qint64 to,int limit)const{
    QVector<RenderPoint> result;if(row<0||row>=m_series.size()||limit<4)return result;const auto &points=m_series[row].points;
    auto begin=std::lower_bound(points.begin(),points.end(),from,[](const Point&p,qint64 x){return p.us<x;});if(begin!=points.begin())--begin;
    auto end=std::upper_bound(begin,points.end(),to,[](qint64 x,const Point&p){return x<p.us;});if(end!=points.end())++end;
    struct Sample{const Point*p=nullptr;int segment=0;};QVector<Sample> samples;int segment=0;const Point *previous=nullptr;
    for(auto i=begin;i!=end;++i){if(!i->valid){++segment;previous=nullptr;continue;}if(previous&&(i->us-previous->us>5000000||i->unit!=previous->unit))++segment;samples.append({&*i,segment});previous=&*i;}
    QVector<Sample> chosen;
    if(samples.size()<=limit)chosen=samples;
    else{const int buckets=limit/4;for(int b=0;b<buckets;++b){const int a=qint64(b)*samples.size()/buckets,z=qint64(b+1)*samples.size()/buckets;int lo=a,hi=a;for(int n=a+1;n<z;++n){if(samples[n].p->value<samples[lo].p->value)lo=n;if(samples[n].p->value>samples[hi].p->value)hi=n;}
            QVector<int> ids{a,lo,hi,z-1};std::sort(ids.begin(),ids.end());ids.erase(std::unique(ids.begin(),ids.end()),ids.end());for(int n:ids)chosen.append(samples[n]);}}
    int prior=-1;for(const auto &s:chosen){result.append({*s.p,prior!=s.segment});prior=s.segment;}return result;
}
}
