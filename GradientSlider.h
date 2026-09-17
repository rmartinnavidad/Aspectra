#pragma once
#include <QtWidgets>
#include <cmath>

// Shared tactile slider. The value is painted in the track, so a separate
// numeric widget is not needed for an active setting.
class GradientSlider : public QSlider {
public:
    explicit GradientSlider(QWidget *parent=nullptr):GradientSlider(Qt::Horizontal,parent){}
    GradientSlider(Qt::Orientation orientation,QWidget *parent=nullptr):QSlider(orientation,parent){
        if(orientation==Qt::Horizontal)setMinimumHeight(36);else setMinimumWidth(36);
        setMouseTracking(true);
        setFocusPolicy(Qt::StrongFocus);
        setProperty("canvasScale",1.0);
        canvasAnimation=new QPropertyAnimation(this,"canvasScale",this);
        canvasAnimation->setDuration(140);
        canvasAnimation->setEasingCurve(QEasingCurve::OutCubic);
        edgeAnimation.setDuration(150);
        edgeAnimation.setStartValue(1.0);
        edgeAnimation.setEndValue(0.0);
        connect(&edgeAnimation,&QVariantAnimation::valueChanged,this,[this](const QVariant &value){edgeTension=value.toReal();update();});
        pulse.setInterval(16);
        connect(&pulse,&QTimer::timeout,this,[this]{phase=std::fmod(phase+.0035,1.);update();});
        connect(this,&QSlider::sliderPressed,this,[this]{dragging=true;update();});
        connect(this,&QSlider::sliderReleased,this,[this]{dragging=false;update();});
        pulse.start();
    }

    void setCanvasEmphasis(bool active){
        canvasAnimation->stop();
        canvasAnimation->setStartValue(property("canvasScale").toReal());
        canvasAnimation->setEndValue(active?1.05:1.0);
        canvasAnimation->start();
    }
    void setValueWithResistance(int requested){
        if(requested<minimum()||requested>maximum())triggerEdgeResistance();
        setValue(qBound(minimum(),requested,maximum()));
    }

protected:
    bool event(QEvent *event) override {
        if(event->type()==QEvent::DynamicPropertyChange &&
           static_cast<QDynamicPropertyChangeEvent*>(event)->propertyName()=="canvasScale")update();
        return QSlider::event(event);
    }
    void enterEvent(QEnterEvent *event) override {hovered=true;update();QSlider::enterEvent(event);}
    void leaveEvent(QEvent *event) override {hovered=false;update();QSlider::leaveEvent(event);}
    void mousePressEvent(QMouseEvent *event) override {
        if(event->button()!=Qt::LeftButton){QSlider::mousePressEvent(event);return;}
        setFocus(Qt::MouseFocusReason);
        setSliderDown(true);
        setValueWithResistance(valueAt(event->position()));
        event->accept();
    }
    void mouseMoveEvent(QMouseEvent *event) override {
        if(event->buttons()&Qt::LeftButton){setValueWithResistance(valueAt(event->position()));event->accept();return;}
        QSlider::mouseMoveEvent(event);
    }
    void mouseReleaseEvent(QMouseEvent *event) override {
        if(event->button()==Qt::LeftButton&&isSliderDown()){setValueWithResistance(valueAt(event->position()));setSliderDown(false);event->accept();return;}
        QSlider::mouseReleaseEvent(event);
    }
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const qreal emphasis=qMax(1.0,property("canvasScale").toReal());
        p.translate(rect().center());p.scale(emphasis,emphasis);p.translate(-rect().center());
        const bool horizontal=orientation()==Qt::Horizontal;
        const qreal first=8.0,last=(horizontal?width():height())-8.0;
        const qreal center=horizontal?height()/2.0:width()/2.0;
        qreal fraction=maximum()==minimum()?0.0:qreal(value()-minimum())/qreal(maximum()-minimum());
        if(invertedAppearance())fraction=1.0-fraction;
        const qreal position=horizontal?first+(last-first)*fraction:last-(last-first)*fraction;
        const qreal life=(std::sin(phase*6.28318530718)+1.0)*0.5;
        const QRectF track=horizontal?QRectF(first,center-14,last-first,28):QRectF(center-14,first,28,last-first);
        const QRectF fill=horizontal?QRectF(first,center-14,qMax<qreal>(0,position-first),28):QRectF(center-14,position,28,qMax<qreal>(0,last-position));
        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#171a1d"));
        p.drawRoundedRect(track,14,14);
        QLinearGradient gradient=horizontal?QLinearGradient(first,0,qMax(first+1.0,position),0):QLinearGradient(0,last,0,qMin(last-1.0,position));
        gradient.setColorAt(0,QColor("#57dd7b"));
        gradient.setColorAt(qBound(0.12,0.46+life*0.26,0.86),QColor("#3ddcff"));
        gradient.setColorAt(1,QColor("#3d91fb"));
        p.save();
        p.setClipRect(track);
        p.setBrush(gradient);
        p.drawRoundedRect(fill,14,14);
        p.restore();
        const QPointF handle=horizontal?QPointF(position,center):QPointF(center,position);
        const qreal glowPosition=horizontal?first+(position-first)*phase:last-(last-position)*phase;
        const QPointF flowing=horizontal?QPointF(glowPosition,center):QPointF(center,glowPosition);
        QRadialGradient fluid(flowing,32);
        fluid.setColorAt(0,QColor("#e5fff4"));
        fluid.setColorAt(.20,QColor(61,220,255,140));
        fluid.setColorAt(.62,QColor(61,220,255,40));
        fluid.setColorAt(1,QColor(61,220,255,0));
        p.save();p.setClipRect(fill);p.setBrush(fluid);p.drawEllipse(flowing,32,11);p.restore();
        if(edgeTension>0){
            QRadialGradient warning(handle,28);
            warning.setColorAt(0,QColor(255,50,50,qRound(150*edgeTension)));
            warning.setColorAt(1,QColor(255,50,50,0));
            p.setBrush(warning);p.drawEllipse(handle,28,28);
        }
        const bool active=hovered||dragging||hasFocus();
        if(active){
            QRadialGradient aura(handle,16+life*4);
            aura.setColorAt(0,QColor(255,255,255,dragging?130:70));
            aura.setColorAt(1,QColor(255,255,255,0));
            p.setBrush(aura);p.drawEllipse(handle,19+life*4,19+life*4);
            p.setPen(QPen(QColor(255,255,255,130),3+life*2));
            p.setBrush(Qt::NoBrush);p.drawEllipse(handle,9+life*2,9+life*2);
        }
        p.setPen(QPen(active?Qt::white:QColor("#77b4ff"),active?2:1));
        p.setBrush(QColor("#3d91fb"));p.drawEllipse(handle,dragging?8.5:7.0,dragging?8.5:7.0);
        QFont valueFont=font();valueFont.setBold(true);valueFont.setPixelSize(12);p.setFont(valueFont);
        const QString text=QString::number(value());
        p.save();p.setClipRect(fill);p.setPen(QColor("#101418"));p.drawText(track,Qt::AlignCenter,text);p.restore();
        p.save();p.setClipRegion(QRegion(track.toRect()).subtracted(QRegion(fill.toRect())));p.setPen(Qt::white);p.drawText(track,Qt::AlignCenter,text);p.restore();
    }

private:
    int valueAt(const QPointF &point) const {
        const bool horizontal=orientation()==Qt::Horizontal;
        const qreal extent=qMax<qreal>(1,(horizontal?width():height())-16.0);
        qreal fraction=horizontal?(point.x()-8.0)/extent:1.0-(point.y()-8.0)/extent;
        if(invertedAppearance())fraction=1.0-fraction;
        return minimum()+qRound(fraction*(maximum()-minimum()));
    }
    void triggerEdgeResistance(){
        if(edgeAnimation.state()==QAbstractAnimation::Running)return;
        edgeAnimation.start();
    }
    bool hovered=false,dragging=false;
    QTimer pulse;
    QVariantAnimation edgeAnimation;
    QPropertyAnimation *canvasAnimation=nullptr;
    qreal phase=0.0,edgeTension=0.0;
};
