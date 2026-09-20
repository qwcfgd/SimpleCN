#pragma once
#include <QObject>
#include <QPointer>
#include <QVector>
class QTimer;
namespace host {
class ChannelViewModel;
class SignalTransmitViewModel;
// Coordinates one replay session across channel ViewModels; no widget dependency.
class ReplayViewModel : public QObject {
    Q_OBJECT
public:
    explicit ReplayViewModel(QObject *parent=nullptr):QObject(parent){}
    ~ReplayViewModel()override;
    void start(SignalTransmitViewModel*,bool periodic,const QVector<ChannelViewModel*>&);
    void stop();
    bool contains(SignalTransmitViewModel*)const;
signals:
    void notice(const QString&);
private:
    QPointer<SignalTransmitViewModel> m_replayOwner;
    QVector<QPointer<SignalTransmitViewModel>> m_replayParticipants;
    QTimer *m_replayTimer=nullptr;
    bool m_stoppingReplay=false;
};
}
