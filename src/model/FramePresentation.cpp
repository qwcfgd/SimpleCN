#include "FramePresentation.h"
namespace host {
void normalizeFramePresentation(FrameRecord&r){
    const QStringList states={"待确认","正常","无响应","错误","硬件状态"};
    if(r.detail.isEmpty()&&!states.contains(r.status))r.detail=r.status;
    const auto old=r.direction.toUpper();
    if(r.echo||old.contains("ECHO"))r.direction="Tx";
    else if(r.noResponse)r.direction="Rx";
    else if(old=="TXPENDING"||(r.request&&!r.simulated))r.direction="TxPending";
    else if(old.contains("TX"))r.direction=(r.simulated||old.startsWith("SIM")||r.direction=="Tx")?"Tx":"TxPending";
    else r.direction="Rx";
    if(r.hardwareStatus||r.status=="硬件状态")r.status="硬件状态";
    else if(r.error)r.status="错误";
    else if(r.noResponse)r.status="无响应";
    else r.status=r.direction=="TxPending"?"待确认":"正常";
}
QString frameColumnKey(const FrameRecord&r,int column){
    switch(column){
    case 0:return r.timestamp;case 1:return r.relativeTime;case 2:return r.intervalSeconds;
    case 3:return r.channel;case 4:return r.direction;case 5:return QString::number(r.id);
    case 6:return QString::number(r.length);case 7:return r.data;case 8:return r.status;
    }return {};
}
}
