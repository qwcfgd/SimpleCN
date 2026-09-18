#pragma once
#include <QTabWidget>
#include <QTabBar>
#include <QToolButton>
#include <QWheelEvent>
#include <QResizeEvent>
#include <QStylePainter>
#include <QStyleOptionTab>
#include <algorithm>
namespace host {
// Horizontal labels in a vertically scrolling navigation rail.
class ChannelTabBar final : public QTabBar {
public:
    explicit ChannelTabBar(QWidget *parent=nullptr):QTabBar(parent){setObjectName("channelTabBar");setExpanding(false);setUsesScrollButtons(true);}
    int rowHeight()const{return qMax(38,fontMetrics().height()+18);}
    QSize minimumSizeHint()const override{return QSize(104,rowHeight());}
protected:
    QSize tabSizeHint(int index)const override {Q_UNUSED(index);return QSize(104,rowHeight());}
    void wheelEvent(QWheelEvent*event)override{
        // Scroll the rail without changing the active channel or any connection.
        m_wheel+=event->angleDelta().y();int steps=m_wheel/120;m_wheel%=120;
        if(!steps && !event->pixelDelta().isNull())steps=event->pixelDelta().y()>0?1:-1;
        auto buttons=findChildren<QToolButton*>(QString(),Qt::FindDirectChildrenOnly);
        std::sort(buttons.begin(),buttons.end(),[](QToolButton*a,QToolButton*b){const auto pa=a->geometry().center(),pb=b->geometry().center();return pa.y()==pb.y()?pa.x()<pb.x():pa.y()<pb.y();});
        if(buttons.size()>=2){auto*button=steps>0?buttons.first():buttons.last();for(int i=0;i<qAbs(steps)&&button->isVisible()&&button->isEnabled();++i)button->click();}
        event->accept();
    }
    void paintEvent(QPaintEvent *)override {
        QStylePainter painter(this);
        for(int i=0;i<count();++i){QStyleOptionTab option;initStyleOption(&option,i);option.shape=QTabBar::RoundedNorth;painter.drawControl(QStyle::CE_TabBarTab,option);}
    }
private:int m_wheel=0;
};
class ChannelTabs final : public QTabWidget {
public:
    explicit ChannelTabs(QWidget *parent=nullptr):QTabWidget(parent){
        setTabBar(new ChannelTabBar(this));setTabPosition(West);setContextMenuPolicy(Qt::CustomContextMenu);
        m_blank=new QWidget(this);m_blank->setObjectName("channelCreationSpace");m_blank->setContextMenuPolicy(Qt::CustomContextMenu);
        m_blank->setToolTip("右键空白处新建软件通道；滚轮浏览通道列表");
        connect(m_blank,&QWidget::customContextMenuRequested,this,[this](const QPoint&p){emit customContextMenuRequested(m_blank->mapTo(this,p));});
    }
    int navigationWidth()const{return 104;}
protected:
    void resizeEvent(QResizeEvent*event)override{
        const int row=static_cast<ChannelTabBar*>(tabBar())->rowHeight();
        tabBar()->setMaximumHeight(qMax(row,height()-row));QTabWidget::resizeEvent(event);
        m_blank->setGeometry(0,qMax(0,height()-row),navigationWidth(),row);m_blank->raise();
    }
private:QWidget*m_blank;
};
}
