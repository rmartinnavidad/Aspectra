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

    void setTitle(const QString &title){m_title=title;setAccessibleName(title);update();}
    void setIcon(const QIcon &icon){m_icon=icon;update();}

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
        const bool horizontal = orientation() == Qt::Horizontal;
        
        QStyleOptionSlider option;
        initStyleOption(&option);

        // 1. ISOLATED VERTICAL DRAWING LOGIC (Fixes the giant floating bar glitch)
        if (!horizontal) {
            const qreal center = width() / 2.0;
            const qreal first = 8.0, last = height() - 8.0;
            const qreal fraction = maximum() == minimum() ? 0.0 : qreal(value() - minimum()) / qreal(maximum() - minimum());
            const qreal position = first + (last - first) * (option.upsideDown ? 1.0 - fraction : fraction);

            // Draw a thin, elegant track for vertical sliders
            p.setPen(Qt::NoPen);
            p.setBrush(QColor("#171a1d"));
            p.drawRoundedRect(QRectF(center - 2.0, first, 4.0, last - first), 2.0, 2.0);

            // Draw thin fill
            QLinearGradient vGrad(0, option.upsideDown ? last : first, 0, position);
            vGrad.setColorAt(0, QColor("#57dd7b"));
            vGrad.setColorAt(1, QColor("#3d91fb"));
            p.setBrush(vGrad);
            p.drawRoundedRect(QRectF(center - 2.0, option.upsideDown ? position : first, 4.0, (last - first) * fraction), 2.0, 2.0);

            // Draw small handle
            const bool active = hovered || dragging || hasFocus();
            p.setPen(QPen(active ? Qt::white : QColor("#77b4ff"), 1));
            p.setBrush(QColor("#3d91fb"));
            p.drawEllipse(QPointF(center, position), 6.0, 6.0);
            return; // EXIT EARLY so it never draws the fat pill
        }

        // 2. HORIZONTAL "FAT PILL" CARD LOGIC
        const qreal first = 1.0, last = width() - 1.0;
        const qreal center = height() / 2.0;
        const qreal fraction = maximum() == minimum() ? 0.0 : qreal(value() - minimum()) / qreal(maximum() - minimum());
        const qreal position = first + (last - first) * (option.upsideDown ? 1.0 - fraction : fraction);
        const qreal life = (std::sin(phase * 6.28318530718) + 1.0) * 0.5;
        const QRectF track = QRectF(rect()).adjusted(1, 1, -1, -1);
        if (track.isEmpty()) return;
        
        const qreal radius = qMin(track.width(), track.height()) / 2.0;
        QPainterPath pill;
        pill.addRoundedRect(track, radius, radius);
        
        const qreal length = (last - first) * fraction;
        const QRectF fill = QRectF(option.upsideDown ? position : first, track.top(), length, track.height());
        QPainterPath fillRectangle;
        fillRectangle.addRect(fill);
        const QPainterPath fillPath = pill.intersected(fillRectangle);
        
        // Draw Pill Background
        p.setPen(Qt::NoPen);
        p.fillPath(pill, QColor("#171a1d"));
        
        // Draw Colored Fill
        QLinearGradient gradient(track.topLeft(), track.topRight());
        gradient.setColorAt(0, QColor("#57dd7b"));
        gradient.setColorAt(qBound(0.12, 0.46 + life * 0.26, 0.86), QColor("#3ddcff"));
        gradient.setColorAt(1, QColor("#3d91fb"));
        p.fillPath(fillPath, gradient);
        
        // Draw Fluid Glow
        const QPointF handle(position, center);
        const qreal glowPosition = first + (position - first) * phase;
        const QPointF flowing(glowPosition, center);
        QRadialGradient fluid(flowing, 32);
        fluid.setColorAt(0, QColor("#e5fff4"));
        fluid.setColorAt(.20, QColor(61, 220, 255, 140));
        fluid.setColorAt(.62, QColor(61, 220, 255, 40));
        fluid.setColorAt(1, QColor(61, 220, 255, 0));
        p.save(); p.setClipPath(fillPath); p.setBrush(fluid); p.drawEllipse(flowing, 32, 11); p.restore();
        
        // Draw Edge Resistance Physics
        p.save(); p.setClipPath(pill);
        if (edgeTension > 0) {
            QRadialGradient warning(handle, 28);
            warning.setColorAt(0, QColor(255, 50, 50, qRound(150 * edgeTension)));
            warning.setColorAt(1, QColor(255, 50, 50, 0));
            p.setBrush(warning); p.drawEllipse(handle, 28, 28);
        }
        
        // Draw Active Aura
        const bool active = hovered || dragging || hasFocus();
        if (active) {
            QRadialGradient aura(handle, 16 + life * 4);
            aura.setColorAt(0, QColor(255, 255, 255, dragging ? 130 : 70));
            aura.setColorAt(1, QColor(255, 255, 255, 0));
            p.setBrush(aura); p.drawEllipse(handle, 19 + life * 4, 19 + life * 4);
            p.setPen(QPen(QColor(255, 255, 255, 130), 3 + life * 2));
            p.setBrush(Qt::NoBrush); p.drawEllipse(handle, 9 + life * 2, 9 + life * 2);
        }
        p.restore();
        
        // Draw Text and Icon Natively
        QFont valueFont = font(); valueFont.setBold(true); valueFont.setPixelSize(12); p.setFont(valueFont);
        const QString text = QString::number(value());
        const qreal textWidth = p.fontMetrics().horizontalAdvance(text) + 8;
        const QRectF valueRect(qMax(16.0, width() - 24.0 - textWidth), track.top(), textWidth, track.height());
        const qreal titleLeft = m_icon.isNull() ? 16.0 : 46.0;
        const QRectF titleRect(titleLeft, track.top(), qMax(0.0, valueRect.left() - titleLeft - 12), track.height());
        const QString title = p.fontMetrics().elidedText(m_title, Qt::ElideRight, qRound(titleRect.width()));
        
        auto drawContent = [&](const QPainterPath &clip, const QColor &color) {
            p.save(); p.setClipPath(clip);
            const qreal emphasis = qMax(1.0, property("canvasScale").toReal());
            p.translate(track.center()); p.scale(emphasis, emphasis); p.translate(-track.center());
            p.setPen(color);
            if (!m_icon.isNull()) {
                QPixmap icon = m_icon.pixmap(QSize(20, 20), devicePixelRatioF(), isEnabled() ? QIcon::Normal : QIcon::Disabled);
                QPainter tint(&icon); tint.setCompositionMode(QPainter::CompositionMode_SourceIn); tint.fillRect(icon.rect(), color); tint.end();
                p.drawPixmap(QRectF(16, center - 10, 20, 20), icon, QRectF(icon.rect()));
            }
            p.drawText(titleRect, Qt::AlignLeft | Qt::AlignVCenter, title);
            p.drawText(valueRect, Qt::AlignRight | Qt::AlignVCenter, text);
            p.restore();
        };
        
        // Text Color Inversion
        drawContent(pill.subtracted(fillPath), Qt::white);
        drawContent(fillPath, QColor("#101418"));
    }

private:
    int valueAt(const QPointF &point) const {
        const bool horizontal=orientation()==Qt::Horizontal;
        const qreal inset=horizontal?1.0:8.0;
        const qreal extent=qMax<qreal>(1,(horizontal?width():height())-2.0*inset);
        qreal fraction=((horizontal?point.x():point.y())-inset)/extent;
        QStyleOptionSlider option;initStyleOption(&option);
        if(option.upsideDown)fraction=1.0-fraction;
        return minimum()+qRound(fraction*(maximum()-minimum()));
    }
    void triggerEdgeResistance(){
        if(edgeAnimation.state()==QAbstractAnimation::Running)return;
        edgeAnimation.start();
    }
    QString m_title;
    QIcon m_icon;
    bool hovered=false,dragging=false;
    QTimer pulse;
    QVariantAnimation edgeAnimation;
    QPropertyAnimation *canvasAnimation=nullptr;
    qreal phase=0.0,edgeTension=0.0;
};
