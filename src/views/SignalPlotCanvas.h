#pragma once
#include <QWidget>
#include <QJsonObject>
#include <QHash>
#include "viewmodels/SignalPlotModel.h"
namespace host {
class SignalPlotCanvas : public QWidget {
    Q_OBJECT
public:
    enum Display { All,Marked,GrayNoMarked };
    enum Axes { Fit,Arrange,AllAxes };
    explicit SignalPlotCanvas(SignalPlotModel *,QWidget *parent=nullptr);
    void setDisplay(int);
    void setAxes(int);
    void setGrid(bool on){m_grid=on;update();}
    void setPoints(bool on){m_points=on;update();}
    void setThick(bool on){m_thick=on;update();}
    void setCursor(bool on,bool diff);
    void setCursorTimes(double,double);
    void setFollow(bool on){m_follow=on;refresh();}
    QJsonObject viewSettings()const;void restoreViewSettings(const QJsonObject&);
    void fit();
    void refresh();
    QVector<int> visibleRows() const;
    QPair<double,double> xRange()const{return {m_x0,m_x1};}
signals:
    void viewChanged();
    void cursorsMoved(double,double);
    void followChanged(bool);
protected:
    void paintEvent(QPaintEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
private:
    QRectF area(int panel,int panels,int axisCount) const;
    QVector<int> axisRows() const;
    QPair<double,double> yRange(int,const QVector<int>&) const;
    QString rangeKey(int row) const;
    void updateCursor(double x);
    SignalPlotModel *m_model;
    int m_display=All,m_axes=Fit,m_dragCursor=-1;
    bool m_grid=true,m_points=false,m_thick=false,m_cursor=false,m_difference=false,m_follow=true,m_panning=false;
    double m_x0=0,m_x1=10,m_t1=0,m_t2=1;
    QPointF m_last;
    QHash<QString,QPair<double,double>> m_yRanges;
    mutable QHash<QString,QPair<double,double>> m_autoRanges;
    quint64 m_autoRevision=quint64(-1);
};
}
