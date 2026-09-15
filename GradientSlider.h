#pragma once
#include <QtWidgets>
// Paint the gradient only up to the handle: its blue endpoint moves with the handle.
class GradientSlider : public QSlider {
public:
    explicit GradientSlider(QWidget *parent=nullptr):GradientSlider(Qt::Horizontal,parent){}
    GradientSlider(Qt::Orientation orientation,QWidget *parent=nullptr):QSlider(orientation,parent){if(orientation==Qt::Horizontal)setMinimumHeight(48);else setMinimumWidth(48);setMouseTracking(true);pulse.setInterval(16);connect(&pulse,&QTimer::timeout,this,[this]{phase=std::fmod(phase+.0035,1.);update();});connect(this,&QSlider::sliderPressed,this,[this]{dragging=true;update();});connect(this,&QSlider::sliderReleased,this,[this]{dragging=false;update();});pulse.start();}
protected:
    void enterEvent(QEnterEvent *event) override {hovered=true;update();QSlider::enterEvent(event);}
    void leaveEvent(QEvent *event) override {hovered=false;update();QSlider::leaveEvent(event);}
    void paintEvent(QPaintEvent *) override {
        QStyleOptionSlider option;initStyleOption(&option);QPainter p(this);p.setRenderHint(QPainter::Antialiasing);
        const double first=8,last=(orientation()==Qt::Horizontal?width():height())-8,center=orientation()==Qt::Horizontal?height()/2.:width()/2.;double fraction=maximum()==minimum()?0:double(value()-minimum())/(maximum()-minimum());if(invertedAppearance())fraction=1-fraction;const double position=orientation()==Qt::Horizontal?first+(last-first)*fraction:last-(last-first)*fraction;const double life=(std::sin(phase*6.28318530718)+1.)*.5;
        p.setPen(Qt::NoPen);p.setBrush(QColor("#171a1d"));if(orientation()==Qt::Horizontal)p.drawRoundedRect(QRectF(first,center-2.5,last-first,5),2.5,2.5);else p.drawRoundedRect(QRectF(center-2.5,first,5,last-first),2.5,2.5);
        QLinearGradient gradient=orientation()==Qt::Horizontal?QLinearGradient(first,0,qMax(first+1.,position),0):QLinearGradient(0,last,0,qMin(last-1.,position));gradient.setColorAt(0,QColor("#57dd7b"));gradient.setColorAt(qBound(.12,.46+life*.26,.86),QColor("#3ddcff"));gradient.setColorAt(1,QColor("#3d91fb"));p.setBrush(gradient);if(orientation()==Qt::Horizontal)p.drawRoundedRect(QRectF(first,center-2.5,position-first,5),2.5,2.5);else p.drawRoundedRect(QRectF(center-2.5,position,5,last-position),2.5,2.5);
        const QPointF handle=orientation()==Qt::Horizontal?QPointF(position,center):QPointF(center,position);const double glowPosition=orientation()==Qt::Horizontal?first+(position-first)*phase:last-(last-position)*phase;const QPointF flowing=orientation()==Qt::Horizontal?QPointF(glowPosition,center):QPointF(center,glowPosition);auto fluid=QRadialGradient(flowing,32);fluid.setColorAt(0,QColor("#e5fff4"));fluid.setColorAt(.20,QColor(61,220,255,140));fluid.setColorAt(.62,QColor(61,220,255,40));fluid.setColorAt(1,QColor(61,220,255,0));p.save();if(orientation()==Qt::Horizontal)p.setClipRect(QRectF(first,center-2.5,qMax(0.,position-first),5));else p.setClipRect(QRectF(center-2.5,position,5,qMax(0.,last-position)));p.setBrush(fluid);p.drawEllipse(flowing,32,11);p.restore();
        const bool active=hovered||dragging||hasFocus();if(active){QRadialGradient aura(handle,16+life*4);aura.setColorAt(0,QColor(255,255,255,dragging?130:70));aura.setColorAt(1,QColor(255,255,255,0));p.setBrush(aura);p.drawEllipse(handle,19+life*4,19+life*4);p.setPen(QPen(QColor(255,255,255,130),3+life*2));p.setBrush(Qt::NoBrush);p.drawEllipse(handle,9+life*2,9+life*2);}
        const double radius=dragging?8.5:7.;p.setPen(QPen(active?Qt::white:QColor("#77b4ff"),active?2:1));p.setBrush(QColor("#3d91fb"));p.drawEllipse(handle,radius,radius);
    }
private:
    bool hovered=false,dragging=false;QTimer pulse;double phase=0;
};
