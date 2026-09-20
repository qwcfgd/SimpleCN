#include "ChannelModel.h"
#include "infrastructure/ChannelWorker.h"
namespace host {
ChannelModel::ChannelModel(ChannelSettings settings,QObject *parent):QObject(parent),m_worker(new ChannelWorker(settings)) {
    qRegisterMetaType<ChannelSettings>();qRegisterMetaType<FrameBatch>();qRegisterMetaType<TaskState>();
    qRegisterMetaType<diag::Request>();
    qRegisterMetaType<signal::TxPlan>();qRegisterMetaType<signal::PayloadUpdate>();qRegisterMetaType<signal::RunStatus>();qRegisterMetaType<signal::BusFrameEvents>();
    qRegisterMetaType<communication::HardwareChannels>();qRegisterMetaType<communication::ConnectionState>();qRegisterMetaType<communication::Health>();
    m_thread.setObjectName(settings.softwareId+"-worker");m_worker->moveToThread(&m_thread);
    connect(&m_thread,&QThread::started,m_worker,&ChannelWorker::initialize);
    connect(&m_thread,&QThread::finished,m_worker,&QObject::deleteLater);
    connect(this,&ChannelModel::settingsRequested,m_worker,&ChannelWorker::updateSettings);
    connect(this,&ChannelModel::connectionRequested,m_worker,&ChannelWorker::connectChannel);
    connect(this,&ChannelModel::disconnectionRequested,m_worker,&ChannelWorker::disconnectChannel);
    connect(this,&ChannelModel::refreshRequested,m_worker,&ChannelWorker::refresh);
    connect(this,&ChannelModel::previewRequested,m_worker,&ChannelWorker::startPreview);
    connect(this,&ChannelModel::cancelRequested,m_worker,&ChannelWorker::cancelTask);
    connect(this,&ChannelModel::scanRequested,m_worker,&ChannelWorker::startHeaderScan);
    connect(this,&ChannelModel::diagnosticRequested,m_worker,&ChannelWorker::sendDiagnostic);
    connect(this,&ChannelModel::resetDiagnosticRequested,m_worker,&ChannelWorker::resetDiagnostic);
    connect(this,&ChannelModel::signalsRequested,m_worker,&ChannelWorker::startSignals);
    connect(this,&ChannelModel::signalStopRequested,m_worker,&ChannelWorker::stopSignals);
    connect(this,&ChannelModel::signalPayloadRequested,m_worker,&ChannelWorker::updateSignalPayload);
    connect(this,&ChannelModel::signalScheduleRequested,m_worker,&ChannelWorker::switchSignalSchedule);
    connect(m_worker,&ChannelWorker::signalStatus,this,&ChannelModel::signalStatus);
    connect(m_worker,&ChannelWorker::busEvents,this,&ChannelModel::busEvents);
    connect(m_worker,&ChannelWorker::connectionGenerationChanged,this,&ChannelModel::connectionGenerationChanged);
    connect(m_worker,&ChannelWorker::diagnosticFinished,this,&ChannelModel::diagnosticFinished);
    connect(m_worker,&ChannelWorker::diagnosticActivity,this,&ChannelModel::diagnosticActivity);
    connect(m_worker,&ChannelWorker::bindingChanged,this,&ChannelModel::bindingChanged);
    connect(m_worker,&ChannelWorker::ready,this,&ChannelModel::ready);
    connect(m_worker,&ChannelWorker::hardwareChanged,this,&ChannelModel::hardwareChanged);
    connect(m_worker,&ChannelWorker::stateChanged,this,&ChannelModel::stateChanged);
    connect(m_worker,&ChannelWorker::healthChanged,this,&ChannelModel::healthChanged);
    connect(m_worker,&ChannelWorker::framesReceived,this,&ChannelModel::framesReceived);
    connect(m_worker,&ChannelWorker::observedFrames,this,&ChannelModel::observedFrames);
    connect(m_worker,&ChannelWorker::logMessage,this,&ChannelModel::logMessage);
    connect(m_worker,&ChannelWorker::taskChanged,this,&ChannelModel::taskChanged);
    connect(m_worker,&ChannelWorker::scanChanged,this,&ChannelModel::scanChanged);
    connect(m_worker,&ChannelWorker::commandFinished,this,&ChannelModel::commandFinished);
    m_thread.start();
}
ChannelModel::~ChannelModel() {
    if(m_thread.isRunning()) {
        QMetaObject::invokeMethod(m_worker,&ChannelWorker::shutdown,Qt::BlockingQueuedConnection);
        m_thread.quit();m_thread.wait();
    }
}
}
