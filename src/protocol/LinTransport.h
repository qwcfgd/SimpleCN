#pragma once
#include "DiagnosticTransport.h"
#include <QTimer>
#include <QQueue>
#include <QElapsedTimer>
#include <functional>
namespace boot {
class LinCodec {
public:
    enum Result { Ignored, Partial, Complete, Invalid };
    static QList<QByteArray> encode(quint8 nad,const QByteArray &pdu);
    Result accept(quint8 nad,const QByteArray &frame,QByteArray &pdu,QString &error);
    void reset(){m_bytes.clear();m_length=0;m_sequence=1;}
private:
    QByteArray m_bytes;int m_length=0,m_sequence=1;
};
class LinTransport final : public DiagnosticTransport {
    Q_OBJECT
public:
    using Write=std::function<bool(quint8,const QByteArray &,QString &)>;
    LinTransport(quint8 nad,int slotMs,int nCrMs,Write write,QObject *parent=nullptr);
    bool send(const QByteArray &,QString &) override;
    void cancel() override;
    int maximumPdu() const override{return 4095;}
    void requireTransmitConfirmation(bool enabled,int timeoutMs=1000){m_requireConfirm=enabled;m_as.setInterval(qMax(1,timeoutMs));}
    static constexpr int diagnosticFrameBits=124;
    static int automaticDownloadRequestSlotMs(int bitrate, int configuredSlotMs);
    void setDownloadRequestSlot(int intervalMs);
    bool awaitingDownloadTransmit() const{return m_downloadSlotMs>0&&m_awaitTx;}
    void confirmTransmitted(const QByteArray &,bool success=true);
    void receiveFrame(quint8 id,const QByteArray &,bool noResponse=false,bool bad=false);
signals:
    void trace(bool transmit,quint8 id,QByteArray bytes);
private:
    void tick();
    void scheduleDownloadSlot();
    void fail(const QString &);
    quint8 m_nad;Write m_write;QTimer m_slot,m_cr,m_as;QQueue<QByteArray> m_tx;LinCodec m_decoder;
    QByteArray m_lastTx;
    QElapsedTimer m_lastSlot;
    int m_downloadSlotMs=0,m_responseSlotMs=0;
    bool m_active=false,m_confirm=false,m_requireConfirm=false,m_awaitTx=false;
};
}
