#pragma once
#include <QElapsedTimer>
namespace host {
inline qint64 captureTimeUs() {
    static const QElapsedTimer origin=[] { QElapsedTimer timer;timer.start();return timer; }();
    return origin.nsecsElapsed()/1000;
}
}
