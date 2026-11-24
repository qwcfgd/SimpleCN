#include "localization/Language.h"
#include "SignalPlotCanvas.h"
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <limits>
namespace host {
namespace {
constexpr int axisWidth = 80;
constexpr int xLabelsHeight = 42;

QRect panelUpdateRect(const QRectF &panel)
{
    const int top = qRound(panel.top());
    return {0, top, int(panel.width()), qRound(panel.bottom()) - top};
}


QPair<double, double> zoomedRange(const QPair<double, double> &range,
                                  double anchor, double factor, double minSpan, double maxSpan)
{
    const double span = range.second - range.first;
    const double newSpan = qBound(minSpan, span * factor, maxSpan);
    if (newSpan == span)
        return range;

    const double value = range.first + anchor * span;
    const QPair<double, double> zoomed{value - anchor * newSpan, value + (1.0 - anchor) * newSpan};
    return zoomed.first < zoomed.second ? zoomed : range;
}

QPointF wheelPosition(QWheelEvent *e){
#if QT_VERSION >= QT_VERSION_CHECK(5,14,0)
    return e->position();
#else
    return e->posF();
#endif
}
QPointF mousePosition(QMouseEvent *e){
#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)
    return e->position();
#else
    return e->localPos();
#endif
}
}
SignalPlotCanvas::SignalPlotCanvas(SignalPlotModel *model,QWidget *parent):QWidget(parent),m_model(model){
    setObjectName("signalPlotCanvas");setMinimumSize(400,1);setMouseTracking(true);setFocusPolicy(Qt::StrongFocus);
    setToolTip("启用光标后拖动光标，关闭后拖动平移；滚轮不更改参数或图表范围");
}
QVector<SignalPlotCanvas::AxisTick> SignalPlotCanvas::enumTicks(const QMap<double,QString>&labels,double low,double high,double pixels){
    QVector<AxisTick> result;if(!std::isfinite(low)||!std::isfinite(high)||high<=low)return result;
    // Ordinary ticks stay on integers in the existing physical coordinate system.
    // Explicit enum values (including fractional physical values) keep their labels.
    const double raw=qMax(1.0,(high-low)/qMax(1.0,pixels/45.0));
    const double base=std::pow(10.0,std::floor(std::log10(raw))),normalized=raw/base;
    const double step=(normalized<=1?1:normalized<=2?2:normalized<=5?5:10)*base;
    if(!std::isfinite(step)||step<=0)return result;
    QMap<double,QString> ticks;const double first=std::ceil(low/step);
    for(int n=0;n<2000;++n){const double value=(first+n)*step;if(value>high)break;ticks[value]=QString::number(value==0?0:value,'f',0);}
    for(auto it=labels.lowerBound(low);it!=labels.end()&&it.key()<=high;++it){
        auto near=ticks.lowerBound(it.key()-step*1e-9);
        if(near!=ticks.end()&&std::abs(near.key()-it.key())<=step*1e-9)ticks.erase(near);
        ticks[it.key()]=it.value();
    }
    for(auto it=ticks.begin();it!=ticks.end();++it)result.append({it.key(),it.value()});return result;
}
QVector<int> SignalPlotCanvas::visibleRows() const {QVector<int> rows;for(int n=0;n<m_model->series().size();++n)if(m_display!=Marked||m_model->series()[n].marked)rows.append(n);return rows;}
QVector<int> SignalPlotCanvas::axisRows(const QVector<int> &rows) const {
    QVector<int> axes;
    if(m_axes==AllAxes||m_axes==Arrange){for(int n:rows)if(m_axes==Arrange||m_model->series()[n].marked){const int r=m_model->representative(n);if(!axes.contains(r))axes.append(r);}}
    if(axes.isEmpty()&&!rows.isEmpty())axes.append(m_model->representative(rows.contains(m_model->activeRow())?m_model->activeRow():rows.first()));
    return axes;
}
QRectF SignalPlotCanvas::panelRect(int panel, int panels) const {
    const double heightPerPanel = qMax(1.0, (height() - 20.0) / panels);
    return {0, panel * heightPerPanel, double(width()), heightPerPanel};
}
QRectF SignalPlotCanvas::area(int panel,int panels,int axisCount) const {
    const double left=axisWidth*qMax(1,axisCount);
    if(m_axes==Arrange){const double slot=panelRect(panel,panels).height();const double topPad=qMin(28.0,slot*0.25),bottomPad=qMin(52.0,slot*0.35);
        return {left,panel*slot+topPad,qMax(20.0,width()-left-24.0),qMax(1.0,slot-topPad-bottomPad)};}
    return {left,28.0,qMax(20.0,width()-left-24.0),qMax(1.0,height()-88.0)};
}
QString SignalPlotCanvas::rangeKey(int row) const {return m_axes==Fit?"shared":m_model->series()[m_model->representative(row)].key;}
QPair<double,double> SignalPlotCanvas::yRange(int row,const QVector<int>&rows) const {
    const auto key=rangeKey(row);if(m_yRanges.contains(key))return m_yRanges.value(key);
    if(m_autoRanges.contains(key))return m_autoRanges.value(key);
    double lo=0,hi=0;bool have=false;
    const auto scan=[&](int n){
        const auto include=[&](double value){if(!have){lo=hi=value;have=true;}else{lo=qMin(lo,value);hi=qMax(hi,value);}};
        const auto &series=m_model->series()[n];
        for(const auto &point:series.points)if(point.valid)include(point.value);
        for(auto it=series.labels.begin();it!=series.labels.end();++it)include(it.key());
    };
    if(m_axes==Fit){for(int n:rows)scan(n);}else scan(m_model->representative(row));
    if(!have)return {-1,1};double pad=qMax((hi-lo)*0.08,qMax(1.0,std::abs(lo))*0.01);
    const QPair<double,double> result{lo-pad,hi+pad};m_autoRanges.insert(key,result);return result;
}
void SignalPlotCanvas::setDisplay(int n){m_display=n;m_autoRanges.clear();refresh();}
void SignalPlotCanvas::setAxes(int n){m_axes=n;m_yRanges.clear();m_autoRanges.clear();refresh();}
void SignalPlotCanvas::setCursor(bool on,bool diff){m_cursor=on;m_difference=diff;update();}
void SignalPlotCanvas::setCursorTimes(double a,double b){m_t1=a;m_t2=b;update();}
void SignalPlotCanvas::fit(){m_yRanges.clear();double lo=0,hi=10;bool have=false;for(int n:visibleRows()){const auto &p=m_model->series()[n].points;if(p.empty())continue;
        if(!have){lo=p.front().us/1e6;hi=p.back().us/1e6;have=true;}else{lo=qMin(lo,p.front().us/1e6);hi=qMax(hi,p.back().us/1e6);}}
    m_x0=qMax(0.0,lo);m_x1=qMax(m_x0+0.001,hi);m_follow=false;emit followChanged(false);update();emit viewChanged();}
void SignalPlotCanvas::refresh(){
    if(m_autoRevision!=m_model->revision()){m_autoRanges.clear();m_autoRevision=m_model->revision();}
    auto rows=visibleRows();
    setMinimumWidth(m_axes==AllAxes?qMax(400,int(axisRows(rows).size())*axisWidth+260):400);
    if(m_follow){double end=0;for(int n:rows){const auto &p=m_model->series()[n].points;if(!p.empty())end=qMax(end,p.back().us/1e6);}
        double span=qMax(0.001,m_x1-m_x0);m_x1=qMax(span,end);m_x0=m_x1-span;}
    update();
}
void SignalPlotCanvas::paintEvent(QPaintEvent *event){
    QPainter p(this);p.setRenderHint(QPainter::Antialiasing);p.fillRect(rect(),QColor("#FAFCFE"));
    const auto rows=visibleRows();const auto axes=axisRows(rows);
    if(rows.isEmpty()){p.setPen(QColor("#64748B"));p.drawText(rect(),Qt::AlignCenter,m_model->rowCount()?Language::text("当前无选中信号"):Language::text("右键信号列表，添加数据库信号"));return;}
    const int panels=m_axes==Arrange?axes.size():1;
    for(int panel=0;panel<panels;++panel){
        const auto panelBounds = m_axes == Arrange ? panelRect(panel, panels) : QRectF(rect());
        const auto panelPixels = panelUpdateRect(panelBounds);
        if (!event->region().intersects(panelPixels))
            continue;
        p.save();
        p.setClipRect(panelPixels);
        const auto a=area(panel,panels,m_axes==AllAxes?axes.size():1);p.setPen(QColor("#CBD5E1"));p.drawRect(a);
        const double xSpan=m_x1-m_x0;const double xScale=xSpan<0.001?1e6:xSpan<1?1000:1;
        const QString xUnit=xSpan<0.001?"μs":xSpan<1?"ms":"s";
        auto majorStep=[](double span,double count){const double raw=span/qMax(1.0,count);const double exponent=std::pow(10.0,std::floor(std::log10(qMax(1e-300,raw))));const double normalized=raw/exponent;return (normalized<=1?1:normalized<=2?2:normalized<=5?5:10)*exponent;};
        const double majorX=majorStep(xSpan,a.width()/110.0),minorX=majorX/5;
        const qint64 firstX=qint64(std::ceil(m_x0/minorX)),lastX=qint64(std::floor(m_x1/minorX));
        for(qint64 k=firstX;k<=lastX&&k-firstX<2000;++k){const double t=k*minorX;const double x=a.left()+(t-m_x0)/xSpan*a.width();const bool major=k%5==0;
            p.setPen(QColor("#526174"));p.drawLine(QPointF(x,a.bottom()),QPointF(x,a.bottom()+(major?6:3)));
            if(major){p.drawText(QRectF(x-40,a.bottom()+6,80,18),Qt::AlignCenter,QString::number(t*xScale,'g',7));if(m_grid){p.setPen(QPen(QColor("#CFD8E4"),1,Qt::DashLine));p.drawLine(QPointF(x,a.top()),QPointF(x,a.bottom()));}}
        }
        p.setPen(QColor("#526174"));p.drawText(QRectF(a.right()-60,a.bottom()+24,60,18),Qt::AlignRight,xUnit);
        const QVector<int> shownAxes=m_axes==Arrange?QVector<int>{axes[panel]}:axes;
        for(int ax=0;ax<shownAxes.size();++ax){const int n=shownAxes[ax];const auto &series=m_model->series()[n];const auto range=yRange(n,rows);const double x=m_axes==AllAxes?axisWidth*(ax+1):a.left();
            const double span=range.second-range.first;const double magnitude=qMax(std::abs(range.first),std::abs(range.second));
            const double scale=magnitude>=1e6?1e-6:magnitude>=1e3?0.001:magnitude>0&&magnitude<0.001?1e6:magnitude>0&&magnitude<1?1000:1;
            const QString prefix=scale==1e-6?"M":scale==0.001?"k":scale==1e6?"μ":scale==1000?"m":"";
            p.setPen(series.color);p.drawLine(QPointF(x,a.top()),QPointF(x,a.bottom()));
            p.drawText(QRectF(x-78,a.top()-23,76,20),Qt::AlignRight,series.labels.isEmpty()?prefix+(series.unit.isEmpty()?Language::text("值"):series.unit):Language::text("枚举"));
            auto tick=[&](double value,const QString &label,bool major){const double y=a.bottom()-(value-range.first)/span*a.height();if(y<a.top()||y>a.bottom())return;
                p.setPen(series.color);p.drawLine(QPointF(x-(major?6:3),y),QPointF(x,y));if(major)p.drawText(QRectF(x-78,y-9,69,18),Qt::AlignRight|Qt::AlignVCenter,label);
                if(m_grid&&major&&ax==0){p.setPen(QPen(QColor("#CFD8E4"),1,Qt::DashLine));p.drawLine(QPointF(a.left(),y),QPointF(a.right(),y));}};
            if(!series.labels.isEmpty()){
                for(const auto &mark:enumTicks(series.labels,range.first,range.second,a.height()))tick(mark.value,mark.text,true);
            }
            else{const double minor=majorStep(span,a.height()/45.0)/5;const qint64 first=qint64(std::ceil(range.first/minor)),last=qint64(std::floor(range.second/minor));
                for(qint64 k=first;k<=last&&k-first<2000;++k)tick(k*minor,QString::number(k*minor*scale,'g',6),k%5==0);}
        }
        if(m_axes==Arrange&&a.height()>=40){p.setPen(m_model->series()[axes[panel]].color);p.drawText(QRectF(a.left()+8,a.top()-23,a.width(),20),(m_model->series()[axes[panel]].group.isEmpty()?m_model->series()[axes[panel]].name:m_model->series()[axes[panel]].groupName));}
        for(int n:rows){if(m_axes==Arrange&&m_model->representative(n)!=axes[panel])continue;const auto &s=m_model->series()[n];const auto range=yRange(n,rows);
            const auto map=[&](const SignalPlotModel::Point &pt){return QPointF(a.left()+(pt.us/1e6-m_x0)/(m_x1-m_x0)*a.width(),a.bottom()-(pt.value-range.first)/(range.second-range.first)*a.height());};
            p.save();p.setClipRect(a.adjusted(0,-1,1,1));const QColor color=m_display==GrayNoMarked&&!s.marked?QColor("#B8BEC7"):s.color;
            p.setPen(QPen(color,m_thick?3.0:1.4));QPainterPath path;
            for(const auto &sample:m_model->renderPoints(n,qint64(m_x0*1e6),qint64(m_x1*1e6))){
                const auto q=map(sample.point);if(sample.breakBefore)path.moveTo(q);else path.lineTo(q);if(m_points)p.drawEllipse(q,2.4,2.4);
            }p.drawPath(path);p.restore();
        }
        if(m_cursor||m_difference)for(int k=0;k<(m_difference?2:1);++k){
            const double t=k?m_t2:m_t1,x=a.left()+(t-m_x0)/(m_x1-m_x0)*a.width();if(x<a.left()||x>a.right())continue;
            p.setPen(QPen(k?QColor("#D97706"):QColor("#7C3AED"),1.5,Qt::DashLine));p.drawLine(QPointF(x,a.top()),QPointF(x,a.bottom()));
            p.drawText(QPointF(x+4,a.top()+15),k?"C2":"C1");
        }
        p.restore();
    }
    p.setPen(QColor("#526174"));p.drawText(QRectF(width()-130,height()-19,110,18),Qt::AlignRight,Language::text("时间"));
}
void SignalPlotCanvas::wheelEvent(QWheelEvent *event)
{
    const int delta = event->angleDelta().y();
    const auto rows = visibleRows();
    if (delta == 0 || rows.isEmpty()) {
        event->ignore();
        return;
    }

    const auto axes = axisRows(rows);
    const bool arranged = m_axes == Arrange;
    const int panels = arranged ? axes.size() : 1;
    const QPointF pos = wheelPosition(event);
    const int panel = arranged
        ? qBound(0, int(pos.y() / panelRect(0, panels).height()), panels - 1)
        : 0;
    const auto plot = area(panel, panels, m_axes == AllAxes ? axes.size() : 1);
    const auto bounds = arranged ? panelRect(panel, panels) : QRectF(rect());
    const bool withinX = pos.x() >= plot.left() && pos.x() <= plot.right();
    const bool withinY = pos.y() >= plot.top() && pos.y() < plot.bottom();
    const bool overX = withinX && pos.y() >= plot.bottom()
        && pos.y() < qMin(bounds.bottom(), plot.bottom() + xLabelsHeight);
    const bool overY = withinY && pos.x() >= 0 && pos.x() <= plot.left();
    const bool overPlot = withinY && withinX && !overY;
    if (!overX && !overY && !overPlot) {
        event->ignore();
        return;
    }

    const double factor = std::pow(1.2, delta / -120.0);
    bool xChanged = false;
    if (overX || overPlot) {
        const double anchor = (pos.x() - plot.left()) / plot.width();
        const auto range = zoomedRange(xRange(), anchor, factor, 0.000001, 1e12);
        xChanged = range != xRange();
        m_x0 = range.first;
        m_x1 = range.second;
        if (xChanged && m_follow) {
            m_follow = false;
            emit followChanged(false);
        }
    }

    bool yChanged = false;
    if (overY || overPlot) {
        QVector<int> targets;
        if (m_axes == Fit) {
            targets.append(rows.first());
        } else if (arranged) {
            targets.append(axes[panel]);
        } else if (overY) {
            const int axis = qBound(0, int(std::ceil(pos.x() / axisWidth)) - 1, int(axes.size()) - 1);
            targets.append(axes[axis]);
        } else {
            QSet<int> seen;
            for (int row : rows) {
                const int representative = m_model->representative(row);
                if (!seen.contains(representative)) {
                    seen.insert(representative);
                    targets.append(representative);
                }
            }
        }
        const double anchor = (plot.bottom() - pos.y()) / plot.height();
        for (int row : targets) {
            const auto current = yRange(row, rows);
            const auto range = zoomedRange(current, anchor, factor, 1e-12, 1e100);
            if (range != current) {
                m_yRanges[rangeKey(row)] = range;
                yChanged = true;
            }
        }
    }

    if (xChanged || yChanged) {
        if (arranged && !xChanged)
            update(panelUpdateRect(bounds));
        else
            update();
        emit viewChanged();
    }
    event->accept();
}
void SignalPlotCanvas::updateCursor(double x){
    const auto a=area(0,1,m_axes==AllAxes?axisRows(visibleRows()).size():1);double t=qMax(0.0,m_x0+qBound(0.0,(x-a.left())/a.width(),1.0)*(m_x1-m_x0));
    if(m_dragCursor==1)m_t2=t;else m_t1=t;emit cursorsMoved(m_t1,m_t2);update();
}
void SignalPlotCanvas::mousePressEvent(QMouseEvent *e){
    if(e->button()!=Qt::LeftButton)return;m_last=mousePosition(e);
    if(m_cursor||m_difference){const auto a=area(0,1,m_axes==AllAxes?axisRows(visibleRows()).size():1);const double t=m_x0+(m_last.x()-a.left())/a.width()*(m_x1-m_x0);
        m_dragCursor=m_difference&&std::abs(t-m_t2)<std::abs(t-m_t1)?1:0;updateCursor(m_last.x());}
    else {m_panning=true;m_follow=false;emit followChanged(false);}
}
void SignalPlotCanvas::mouseMoveEvent(QMouseEvent *e){
    const auto pos=mousePosition(e);if(m_dragCursor>=0){updateCursor(pos.x());return;}if(!m_panning)return;
    const auto rows=visibleRows();const auto a=area(0,1,m_axes==AllAxes?axisRows(rows).size():1);
    double dx=(pos.x()-m_last.x())/a.width()*(m_x1-m_x0);const double span=m_x1-m_x0;m_x0=qMax(qMin(0.0,m_x0),m_x0-dx);m_x1=m_x0+span;
    QSet<int> moved;for(int n:rows){const int r=m_model->representative(n);if((m_axes==Fit&&n!=rows.first())||(m_axes!=Fit&&moved.contains(r)))continue;moved.insert(r);auto range=yRange(n,rows);double dy=(pos.y()-m_last.y())/a.height()*(range.second-range.first);m_yRanges[rangeKey(n)]={range.first+dy,range.second+dy};}
    m_last=pos;update();
}
void SignalPlotCanvas::mouseReleaseEvent(QMouseEvent *){m_dragCursor=-1;m_panning=false;emit viewChanged();}
}

namespace host {
QJsonObject SignalPlotCanvas::viewSettings()const{
    QJsonObject ranges;for(auto i=m_yRanges.begin();i!=m_yRanges.end();++i)ranges[i.key()]=QJsonObject{{"min",i->first},{"max",i->second}};
    return {{"x0",m_x0},{"x1",m_x1},{"y",ranges}};
}
void SignalPlotCanvas::restoreViewSettings(const QJsonObject &settings){
    m_yRanges.clear();m_autoRanges.clear();
    const auto from=settings.value("x0").toDouble(0),to=settings.value("x1").toDouble(10);
    if(std::isfinite(from)&&std::isfinite(to)&&to>from){m_x0=from;m_x1=to;}
    const auto ranges=settings.value("y").toObject();for(auto i=ranges.begin();i!=ranges.end();++i){const auto range=i.value().toObject();const double lo=range["min"].toDouble(),hi=range["max"].toDouble();if(std::isfinite(lo)&&std::isfinite(hi)&&hi>lo)m_yRanges[i.key()]={lo,hi};}
    refresh();
}
}
