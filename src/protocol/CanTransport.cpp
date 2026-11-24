#include "CanTransport.h"
namespace boot {
int CanTransport::separationUs(int value){
    if(value>=0&&value<=0x7f)return value*1000;
    if(value>=0xf1&&value<=0xf9)return (value-0xf0)*100;
    return -1;
}
bool CanOptions::valid(QString &error) const {
    if(strictClassicCc&&(extended||padding!=0xcc)){error="Strict CAN requires 11-bit Classic CAN and CC padding";return false;}
    const quint32 max=extended?0x1fffffff:0x7ff;
    if(txId>max||rxId>max||functionalTxId>max||txId==rxId||blockSize<0||blockSize>255||CanTransport::separationUs(stMin)<0||
       maxWaitFrames<0||maxWaitFrames>255||nAsMs<1||nAsMs>60000||nBsMs<1||nBsMs>60000||
       nCrMs<1||nCrMs>60000||nArMs<0||nArMs>60000||receiveCapacity<8||receiveCapacity>4095||padding<0||padding>255||bitrate<1){
        error="Invalid Classic CAN ISO-TP configuration";return false;
    }
    error.clear();return true;
}
QJsonObject CanOptions::toJson() const{
    return {{"txId",int(txId)},{"rxId",int(rxId)},{"functionalTxId",int(functionalTxId)},{"blockSize",blockSize},{"stMin",stMin},{"maxWaitFrames",maxWaitFrames},{"nAsMs",nAsMs},
      {"nBsMs",nBsMs},{"nCrMs",nCrMs},{"nArMs",nArMs},{"receiveCapacity",receiveCapacity},{"padding",padding},{"strictClassicCc",strictClassicCc}};
}
bool CanOptions::fromJson(const QJsonObject &json,CanOptions &out,QString &error){
    CanOptions options=out;
    const auto defaults=options.toJson();
    for(auto it=json.begin();it!=json.end();++it){
        if(it.key()=="strictClassicCc"&&it.value().isBool())continue;
        if(!defaults.contains(it.key())||!it.value().isDouble()||it.value().toDouble()!=it.value().toInt(-1)){
            error="Unknown or non-integer CAN network setting";return false;
        }
    }
    options.blockSize=json.value("blockSize").toInt(options.blockSize);
    options.stMin=json.value("stMin").toInt(options.stMin);
    options.maxWaitFrames=json.value("maxWaitFrames").toInt(options.maxWaitFrames);
    options.nAsMs=json.value("nAsMs").toInt(options.nAsMs);
    options.nArMs=json.value("nArMs").toInt(options.nArMs);
    options.nBsMs=json.value("nBsMs").toInt(options.nBsMs);
    options.nCrMs=json.value("nCrMs").toInt(options.nCrMs);
    options.receiveCapacity=json.value("receiveCapacity").toInt(options.receiveCapacity);
    options.padding=json.value("padding").toInt(options.padding);
    if(json.contains("strictClassicCc")){
        if(!json["strictClassicCc"].isBool()){error="strictClassicCc must be boolean";return false;}
        options.strictClassicCc=json["strictClassicCc"].toBool();
    }
    if(json.contains("functionalTxId"))options.functionalTxId=quint32(json.value("functionalTxId").toInt(int(options.functionalTxId)));
    if(!options.valid(error))return false;out=options;return true;
}
CanTransport::CanTransport(CanOptions options,Write write,QObject *parent)
 :DiagnosticTransport(parent),m_options(options),m_write(std::move(write)){
    for(auto timer:{&m_as,&m_bs,&m_cr,&m_cf}){timer->setSingleShot(true);timer->setTimerType(Qt::PreciseTimer);}
    connect(&m_as,&QTimer::timeout,this,[this]{
        if(!m_fastPending.isEmpty()&&m_fastClock.nsecsElapsed()/1000-m_fastPending.head().queuedUs<qint64(m_options.nAsMs)*1000){armFastDeadline();return;}
        fail("CAN N_As/N_Ar transmit confirmation timeout");
    });
    connect(&m_bs,&QTimer::timeout,this,[this]{fail("CAN N_Bs flow control timeout");});
    connect(&m_cr,&QTimer::timeout,this,[this]{fail("CAN N_Cr consecutive frame timeout");});
    connect(&m_cf,&QTimer::timeout,this,&CanTransport::sendCf);
}
bool CanTransport::send(const QByteArray &pdu,QString &error){
    return send(pdu,error,false);
}
bool CanTransport::send(const QByteArray &pdu,QString &error,bool functional){
    if(!m_options.valid(error))return false;
    if(busy()){error="CAN ISO-TP busy";return false;}
    if(pdu.isEmpty()||pdu.size()>maximumPdu()){error="Classic CAN PDU length must be 1..4095";return false;}
    m_listening=true;m_tx=pdu;m_txState=FramePending;m_txOffset=0;m_txSequence=1;m_waitFrames=0;
    m_peerStUs=0;m_zeroPacingUs=0;m_lastCf.invalidate();m_earlyFc.clear();m_txFunctional=functional;
    m_fastPending.clear();m_fastClock.invalidate();m_fastNextUs=0;m_fastDispatching=false;m_earlyResponses.clear();
    QByteArray frame;
    if(pdu.size()<=7){frame=QByteArray(1,char(pdu.size()))+pdu;submit(frame,Single);}
    else {frame=QByteArray(1,char(0x10|(pdu.size()>>8)))+char(pdu.size())+pdu.left(6);m_txOffset=6;submit(frame,First);}
    return true;
}
void CanTransport::cancel(){
    ++m_epoch;m_listening=false;m_as.stop();m_bs.stop();m_cr.stop();m_cf.stop();
    m_tx.clear();m_rx.clear();m_earlyFc.clear();m_rxLength=0;m_pending=None;m_txState=Idle;
    m_token=0;m_lastCf.invalidate();m_txFunctional=false;
    m_fastPending.clear();m_fastClock.invalidate();m_fastDispatching=false;m_earlyResponses.clear();emit cancelled();
}
void CanTransport::fail(const QString &error){
    if(error=="CAN N_As/N_Ar transmit confirmation timeout")emit deadlineObserved((m_pending==FlowControl||m_pending==Overflow)?"CAN_N_AR":"CAN_N_AS",m_transmitDeadlineMs,!m_fastPending.isEmpty()?(m_fastClock.nsecsElapsed()/1000-m_fastPending.head().queuedUs)/1000:m_transmitElapsed.elapsed());
    else if(error=="CAN N_Bs flow control timeout")emit deadlineObserved("CAN_N_BS",m_options.nBsMs,m_bsElapsed.elapsed());
    else if(error=="CAN N_Cr consecutive frame timeout")emit deadlineObserved("CAN_N_CR",m_options.nCrMs,m_crElapsed.elapsed());
    cancel();emit failed(error);
}
void CanTransport::submit(QByteArray bytes,Kind kind){
    if(m_pending!=None){fail("CAN adapter already has an outstanding frame");return;}
    while(bytes.size()<8)bytes.append(char(m_options.padding));
    const CanFrame frame{m_txFunctional?m_options.functionalTxId:m_options.txId,m_options.extended,false,false,false,bytes};
    m_pending=kind;m_token=++m_serial;const auto token=m_token,epoch=m_epoch;
    m_transmitDeadlineMs=(kind==FlowControl||kind==Overflow)&&m_options.nArMs>0?m_options.nArMs:m_options.nAsMs;
    m_transmitElapsed.start();m_as.start(m_transmitDeadlineMs);
    // Always return from send() before publishing frames or completing a PDU.
    QTimer::singleShot(0,this,[this,frame,token,epoch]{
        if(epoch!=m_epoch||token!=m_token)return;
        if(m_transmitElapsed.elapsed()>=m_transmitDeadlineMs){fail("CAN N_As/N_Ar transmit confirmation timeout");return;}
        emit trace(true,frame);
        if(epoch!=m_epoch||token!=m_token)return;
        if(m_transmitElapsed.elapsed()>=m_transmitDeadlineMs){fail("CAN N_As/N_Ar transmit confirmation timeout");return;}
        QString error;const bool written=m_write(frame,token,error);
        if(epoch!=m_epoch||token!=m_token)return;
        if(!written)fail(error.isEmpty()?"CAN write failed":error);
        else if(m_transmitElapsed.elapsed()>=m_transmitDeadlineMs)fail("CAN N_As/N_Ar transmit confirmation timeout");
    });
}
void CanTransport::confirmTransmitted(quint64 token,bool success){
    if(!m_fastPending.isEmpty()&&token==m_fastPending.head().token){
        const auto item=m_fastPending.dequeue();
        if(m_fastClock.nsecsElapsed()/1000-item.queuedUs>=qint64(m_options.nAsMs)*1000){fail("CAN N_As/N_Ar transmit confirmation timeout");return;}
        if(!success){fail("CAN transmit confirmation failed");return;}
        // A later FC can raise STmin. Measure it from the last confirmed CF,
        // including when the previous block used the zero-STmin pipeline.
        m_lastCf.start();
        armFastDeadline();
        if(!m_fastDispatching){finishFastBlock();if(m_txState==SendingCf)scheduleCf();}
        return;
    }
    for(const auto &item:m_fastPending)if(item.token==token){fail("CAN transmit confirmations arrived out of order");return;}
    if(!token||token!=m_token||m_pending==None)return;
    if(m_transmitElapsed.elapsed()>=m_transmitDeadlineMs){fail("CAN N_As/N_Ar transmit confirmation timeout");return;}
    if(!success){fail("CAN transmit confirmation failed");return;}
    const auto kind=m_pending;
    if(kind==Consecutive&&m_peerStUs==0){
        const int bits=m_options.extended?160:135;
        const qint64 period=(qint64(bits)*100000000+qint64(m_options.bitrate)*85-1)/(qint64(m_options.bitrate)*85);
        m_zeroPacingUs=int(qMax<qint64>(0,period-m_transmitElapsed.nsecsElapsed()/1000));
    }
    m_as.stop();m_pending=None;m_token=0;
    if(kind==Overflow){fail("CAN receive capacity exceeded");return;}
    if(kind==FlowControl){if(m_rxLength){m_crElapsed.start();m_cr.start(m_options.nCrMs);}return;}
    if(kind==Single){txComplete();return;}
    if(kind==First){
        m_txState=WaitingFc;m_bsElapsed.start();m_bs.start(m_options.nBsMs);m_lastCf.start();
    }else{
        m_txOffset+=m_pendingCount;m_txSequence=(m_txSequence+1)&15;m_lastCf.start();
        if(m_txOffset>=m_tx.size()){txComplete();return;}
        if(m_peerBlock&&--m_remainingBlock==0){m_txState=WaitingFc;m_bsElapsed.start();m_bs.start(m_options.nBsMs);}
        else {m_txState=SendingCf;scheduleCf();}
    }
    if(m_txState==WaitingFc)applyEarlyFlowControls();
}
void CanTransport::txComplete(){m_tx.clear();m_txState=Idle;emit sent();}
void CanTransport::applyEarlyFlowControls(){
    while(m_txState==WaitingFc&&!m_earlyFc.isEmpty())flowControl(m_earlyFc.dequeue());
    if(m_txState!=Idle&&m_txState!=WaitingFc&&!m_earlyFc.isEmpty())fail("Unexpected CAN flow control");
}
void CanTransport::flowControl(const QByteArray &bytes){
    if(bytes.size()<3){fail("Short CAN flow control frame");return;}
    const int status=quint8(bytes[0])&15;
    if(status==2){fail("CAN peer overflow");return;}
    if(status==1){
        if(++m_waitFrames>m_options.maxWaitFrames){fail("CAN WAIT frame limit exceeded");return;}
        m_bsElapsed.start();m_bs.start(m_options.nBsMs);return;
    }
    if(status!=0){fail("Invalid CAN flow status");return;}
    const int separation=separationUs(quint8(bytes[2]));
    if(separation<0){fail("Reserved CAN STmin value");return;}
    m_bs.stop();m_waitFrames=0;m_peerBlock=quint8(bytes[1]);m_remainingBlock=m_peerBlock;m_peerStUs=separation;
    m_txState=SendingCf;scheduleCf();
}
void CanTransport::scheduleCf(){
    if(m_txState!=SendingCf)return;
    if(m_options.boundedFastSend&&m_peerStUs==0){
        if(m_txOffset>=m_tx.size()||(m_peerBlock&&m_remainingBlock==0)||m_fastPending.size()>=4)return;
        if(!m_fastClock.isValid()){m_fastClock.start();m_fastNextUs=0;}
        const qint64 left=qMax<qint64>(0,m_fastNextUs-m_fastClock.nsecsElapsed()/1000);
        m_cf.start(int((left+999)/1000));return;
    }
    const qint64 elapsed=m_lastCf.isValid()?m_lastCf.nsecsElapsed()/1000:0;
    const auto remaining=qMax<qint64>(0,(m_peerStUs?m_peerStUs:m_zeroPacingUs)-elapsed);
    m_cf.start(int((remaining+999)/1000));
}
void CanTransport::sendCf(){
    if(m_txState!=SendingCf)return;
    if(m_options.boundedFastSend&&m_peerStUs==0){sendFastCf();return;}
    if(m_lastCf.isValid()&&m_lastCf.nsecsElapsed()/1000<(m_peerStUs?m_peerStUs:m_zeroPacingUs)){scheduleCf();return;}
    m_pendingCount=qMin(7,int(m_tx.size())-m_txOffset);
    const auto frame=QByteArray(1,char(0x20|m_txSequence))+m_tx.mid(m_txOffset,m_pendingCount);
    m_txState=FramePending;submit(frame,Consecutive);
}
void CanTransport::armFastDeadline(){
    m_as.stop();
    if(m_fastPending.isEmpty())return;
    m_transmitDeadlineMs=m_options.nAsMs;
    const qint64 remaining=qint64(m_options.nAsMs)*1000-(m_fastClock.nsecsElapsed()/1000-m_fastPending.head().queuedUs);
    m_as.start(int(qMax<qint64>(1,(remaining+999)/1000)));
}
void CanTransport::finishFastBlock(){
    if(!m_fastPending.isEmpty()||m_fastDispatching||m_txState!=SendingCf)return;
    if(m_txOffset>=m_tx.size()){
        auto responses=std::move(m_earlyResponses);m_earlyResponses.clear();
        txComplete();
        while(!responses.isEmpty()&&m_listening){m_replayingResponse=true;receiveFrame(responses.dequeue());m_replayingResponse=false;}
        return;
    }
    if(m_peerBlock&&m_remainingBlock==0){
        m_txState=WaitingFc;m_bsElapsed.start();m_bs.start(m_options.nBsMs);
        applyEarlyFlowControls();
    }
}
void CanTransport::sendFastCf(){
    if(m_txState!=SendingCf||!m_fastClock.isValid())return;
    // Conservative classic-CAN 8-byte frame budget, including stuffing and intermission.
    const int bits=m_options.extended?160:135;
    const qint64 period=qMax<qint64>(1,(qint64(bits)*100000000+qint64(m_options.bitrate)*85-1)/(qint64(m_options.bitrate)*85));
    const qint64 now=m_fastClock.nsecsElapsed()/1000;
    m_fastNextUs=qMax(m_fastNextUs,now-2*period); // no more than three overdue frames at once
    const auto epoch=m_epoch;m_fastDispatching=true;
    for(int burst=0;burst<3&&m_txState==SendingCf&&m_txOffset<m_tx.size()&&(!m_peerBlock||m_remainingBlock>0)&&m_fastPending.size()<4;++burst){
        const qint64 current=m_fastClock.nsecsElapsed()/1000;
        if(!m_fastPending.isEmpty()&&current-m_fastPending.head().queuedUs>=qint64(m_options.nAsMs)*1000){fail("CAN N_As/N_Ar transmit confirmation timeout");return;}
        if(current<m_fastNextUs)break;
        const int count=qMin(7,int(m_tx.size())-m_txOffset);
        QByteArray bytes=QByteArray(1,char(0x20|m_txSequence))+m_tx.mid(m_txOffset,count);
        while(bytes.size()<8)bytes.append(char(m_options.padding));
        const CanFrame frame{m_txFunctional?m_options.functionalTxId:m_options.txId,m_options.extended,false,false,false,bytes};
        const quint64 token=++m_serial;m_fastPending.enqueue({token,current});
        m_txOffset+=count;m_txSequence=(m_txSequence+1)&15;if(m_peerBlock)--m_remainingBlock;
        m_fastNextUs+=period;armFastDeadline();emit trace(true,frame);
        if(epoch!=m_epoch)return;
        if(m_fastClock.nsecsElapsed()/1000-current>=qint64(m_options.nAsMs)*1000){fail("CAN N_As/N_Ar transmit confirmation timeout");return;}
        QString error;const bool written=m_write(frame,token,error);
        if(epoch!=m_epoch)return;
        if(!written){fail(error.isEmpty()?"CAN write failed":error);return;}
        if(!m_fastPending.isEmpty()&&m_fastClock.nsecsElapsed()/1000-m_fastPending.head().queuedUs>=qint64(m_options.nAsMs)*1000){fail("CAN N_As/N_Ar transmit confirmation timeout");return;}
    }
    m_fastDispatching=false;finishFastBlock();if(m_txState==SendingCf)scheduleCf();
}
void CanTransport::sendFc(bool overflow){
    m_cr.stop();
    submit(QByteArray(1,char(overflow?0x32:0x30))+char(m_options.blockSize)+char(m_options.stMin),overflow?Overflow:FlowControl);
}
void CanTransport::receiveFrame(const CanFrame &frame){
    const bool idMatch=(frame.id==m_options.rxId)||(m_options.functionalRx&&frame.id==m_options.functionalTxId);
    if(!m_listening||!idMatch)return;
    if(frame.extended!=m_options.extended||frame.rtr||frame.fd){if(m_options.strictClassicCc)fail("Strict CAN received non-classic 11-bit data frame");return;}
    if(frame.error){fail("CAN receive error");return;}
    const auto &bytes=frame.data;
    if(bytes.isEmpty()||bytes.size()>8||(m_options.strictClassicCc&&bytes.size()!=8)){fail("Invalid Classic CAN DLC");return;}
    const auto padding=[&](int used){
        if(m_options.strictClassicCc&&bytes.mid(used)!=QByteArray(bytes.size()-used,char(0xcc))){fail("Strict CAN unused bytes must be CC");return false;}return true;
    };
    if(!m_replayingResponse)emit trace(false,frame);
    const int pci=quint8(bytes[0]),kind=pci>>4;
    if(m_pending!=None&&m_as.isActive()&&m_transmitElapsed.elapsed()>=m_transmitDeadlineMs){fail("CAN N_As/N_Ar transmit confirmation timeout");return;}
    if(kind==3&&m_bs.isActive()&&m_bsElapsed.elapsed()>=m_options.nBsMs){fail("CAN N_Bs flow control timeout");return;}
    if(kind==2&&m_cr.isActive()&&m_crElapsed.elapsed()>=m_options.nCrMs){fail("CAN N_Cr consecutive frame timeout");return;}
    if(kind==3){
        if(bytes.size()<3||!padding(3)){if(bytes.size()<3)fail("Short CAN flow control frame");return;}
        if(m_txState==WaitingFc){flowControl(bytes);return;}
        // A real adapter can report peer FC before its own TX echo.
        if(m_pending==First||(m_pending==Consecutive&&m_peerBlock&&m_remainingBlock==1)||
           (m_txState==SendingCf&&m_peerBlock&&m_remainingBlock==0&&!m_fastPending.isEmpty())){
            if(m_earlyFc.size()>=m_options.maxWaitFrames+1){fail("Too many early CAN flow control frames");return;}
            m_earlyFc.enqueue(bytes);return;
        }
        if(m_txState!=Idle)fail("Unexpected CAN flow control");
        return;
    }
    if(m_txState!=Idle){
        if((kind==0||kind==1)&&m_txState==SendingCf&&m_txOffset>=m_tx.size()&&!m_fastPending.isEmpty()){
            if(m_earlyResponses.size()>=8){fail("Too many early CAN responses");return;}
            m_earlyResponses.enqueue(frame);return;
        }
        const bool lastTxFrame=(m_pending==Single)||(m_pending==Consecutive&&m_txOffset+m_pendingCount>=m_tx.size());
        if(lastTxFrame&&(kind==0||kind==1)){
            confirmTransmitted(m_token,true);
            if(!m_listening)return;
        }else{
            fail("CAN response arrived before request transmission completed");return;
        }
    }
    if(kind==0){
        const int size=pci&15;
        if(m_rxLength||size<1||size>7||bytes.size()<size+1){fail("Invalid CAN single frame");return;}
        if(!padding(size+1))return;
        emit received(bytes.mid(1,size));return;
    }
    if(kind==1){
        if(m_rxLength||bytes.size()!=8){fail("Invalid CAN first frame");return;}
        const int length=((pci&15)<<8)|quint8(bytes[1]);
        if(length<8){fail("Unsupported CAN FF length (8..4095 required)");return;}
        if(length>m_options.receiveCapacity){sendFc(true);return;}
        m_rxLength=length;m_rx=bytes.mid(2,6);m_rxSequence=1;m_rxBlock=0;
        emit responseStarted(m_rx);
        if(!m_listening)return;
        sendFc(false);return;
    }
    if(kind==2){
        if(!m_rxLength||(pci&15)!=m_rxSequence){fail("CAN consecutive frame sequence mismatch");return;}
        const int count=qMin(7,m_rxLength-int(m_rx.size()));
        if(bytes.size()<count+1){fail("Short CAN consecutive frame");return;}
        if(!padding(count+1))return;
        m_rx+=bytes.mid(1,count);m_rxSequence=(m_rxSequence+1)&15;
        if(m_rx.size()==m_rxLength){
            const auto pdu=m_rx;m_rx.clear();m_rxLength=0;m_cr.stop();emit received(pdu);return;
        }
        if(m_options.blockSize&&++m_rxBlock==m_options.blockSize){m_rxBlock=0;sendFc(false);}
        else {m_crElapsed.start();m_cr.start(m_options.nCrMs);}
        return;
    }
    fail("Unsupported CAN PCI");
}
}
