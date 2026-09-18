#pragma once
#include "model/ChannelModel.h"
#include "model/FrameTableModel.h"
#include "model/UdsServiceTableModel.h"
#include <QSet>
#include <QTimer>
#include "SignalTransmitViewModel.h"
namespace host {
class ChannelViewModel : public QObject {
    Q_OBJECT
public:
    explicit ChannelViewModel(ChannelSettings,QObject *parent=nullptr);
    const ChannelSettings &settings()const{return m_settings;}
    ChannelSettings snapshotSettings()const;
    communication::HardwareChannels hardware()const;
    void setReservations(const QSet<quint32> &);
    // 0: idle, 1: healthy, 2: communication lost.
    int communicationIndicator()const;
    bool reservesHardware()const{return connected() || m_connecting;}
    communication::ConnectionState state()const{return m_state;}
    communication::Health health()const{return m_health;}
    QString healthDetail()const{return m_healthDetail;}
    QString error()const{return m_error;}
    QString taskText()const{return m_taskText;}
    QString scanText()const{return m_scanText;}
    int progress()const{return m_progress;}
    TaskState taskState()const{return m_task;}
    bool pending()const{return m_pending;}
    bool scanning()const{return m_scanning;}
    bool connected()const{return m_state==communication::ConnectionState::Connected;}
    bool otherTaskBusy()const{return m_protocolBusy || m_repeatActive || m_diagnosticBusy || m_pending || m_scanning || m_task==TaskState::Running || m_state==communication::ConnectionState::Connecting || m_state==communication::ConnectionState::Disconnecting;}
    bool busy()const{return otherTaskBusy() || (m_signals && (m_signals->running()||m_signals->importing()));}
    SignalTransmitViewModel *signalTransmission()const{return m_signals;}
    bool hardwareLocked()const{return connected() || busy();}
    bool canConnect()const;
    bool canStart()const;
    bool canScan()const;
    QString startHint()const;
    FrameTableModel *frames(){return &m_frames;}
    QStringList logs()const{return m_logs;}
    bool setSettings(const ChannelSettings &);
    bool chooseImage(bool flash,const QString &path);
    static QStringList imageCandidates(const QString &path);
    bool exportLogs(const QString &,QString &error)const;
    const diag::Database &diagnosticDatabase()const{return m_database;}
    UdsServiceTableModel *diagnosticServices(){return &m_services;}
    bool loadCdd(const QString &,QString &error,bool applyCommunication=true);
    bool applyDiagnosticConfiguration(const diag::Database &,const ChannelSettings &,QString &error);
    bool selectDiagnosticTarget(const QString &ecu,const QString &variant,QString &error);
    bool sendDiagnostic(int serviceRow,const QByteArray &,QString &error);
    bool repeatDiagnostic(int serviceRow,const QByteArray &,QString &error);
    bool diagnosticBusy()const{return m_protocolBusy||m_diagnosticBusy||m_repeatActive;}
    bool diagnosticRepeating()const{return m_repeatActive;}
    QString diagnosticRepeatStatus()const;
    QString diagnosticResult()const{return m_diagnosticResult;}
    QString diagnosticHint()const;
public slots:
    void toggleConnection();
    void refresh();
    void start();
    void cancel();
    void scanHeaders();
    void clearLogs();
    void log(const QString &);
signals:
    void changed();
    void hardwareListChanged();
    void settingsChanged();
    void logAdded(QString);
    void logsCleared();
    void diagnosticDatabaseChanged();
    void diagnosticFinished(bool,QByteArray,QString);
private:
    void selectAvailableHardware();
    ChannelSettings m_settings;
    ChannelModel *m_model;
    SignalTransmitViewModel *m_signals=nullptr;
    quint64 m_connectionGeneration=0;
    FrameTableModel m_frames;
    communication::HardwareChannels m_hardware;
    communication::ConnectionState m_state=communication::ConnectionState::Missing;
    communication::Health m_health=communication::Health::Removed;
    QString m_healthDetail="等待设备",m_error,m_taskText="等待开始",m_scanText;
    TaskState m_task=TaskState::Idle;int m_progress=0;
    QSet<quint32> m_reserved;
    bool m_lost=false,m_wasConnected=false,m_manualDisconnect=false,m_connecting=false;
    bool m_pending=false,m_scanning=false,m_ready=false,m_backendSimulation=false;
    QStringList m_logs;
    diag::Database m_database;
    UdsServiceTableModel m_services;
    diag::Request m_lastDiagnostic;
    ChannelSettings m_diagnosticSettings;
    QString m_diagnosticResult;
    bool m_diagnosticBusy=false;
    bool m_protocolBusy=false;
    QTimer m_diagnosticRepeatTimer;
    bool m_repeatActive=false;
    int m_repeatRemaining=0,m_repeatCompleted=0,m_repeatTotal=0;
};
}
