#pragma once
#include <QEventLoop>
#include <QTimer>
#include <functional>
// Run the real Qt event loop while simulated transports progress. QTest polling
// sleeps between event dispatches and can starve 1 ms LIN/CAN fixture timers.
inline bool waitForTest(const std::function<bool()> &condition,int timeout=3000){
    if(condition())return true;
    QEventLoop loop;QTimer poll,deadline;poll.setTimerType(Qt::PreciseTimer);poll.setInterval(1);deadline.setSingleShot(true);
    QObject::connect(&poll,&QTimer::timeout,&loop,[&]{if(condition())loop.quit();});
    QObject::connect(&deadline,&QTimer::timeout,&loop,&QEventLoop::quit);
    poll.start();deadline.start(timeout);loop.exec();return condition();
}
