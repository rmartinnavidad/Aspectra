#include "GalleryTile.h"

#include <algorithm>
#include <cmath>
#include <numbers>

GalleryTile::GalleryTile(QString projectPath,QWidget *parent)
    : QWidget(parent),m_projectPath(std::move(projectPath)) {
    setAttribute(Qt::WA_Hover,true);
    setMouseTracking(true);
    setCursor(Qt::PointingHandCursor);
    setSizePolicy(QSizePolicy::Fixed,QSizePolicy::Fixed);
    loadThumbnail();

    m_geometryAnimation=new QVariantAnimation(this);
    m_geometryAnimation->setDuration(240);
    connect(m_geometryAnimation,&QVariantAnimation::valueChanged,this,[this](const QVariant &value){
        setGeometry(value.toRect());
        update();
    });

    m_hoverAnimation=new QVariantAnimation(this);
    m_hoverAnimation->setDuration(240);
    m_hoverAnimation->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_hoverAnimation,&QVariantAnimation::valueChanged,this,[this](const QVariant &value){
        m_hoverIntensity=value.toReal();
        update();
    });
    m_motionTimer.setInterval(33);
    connect(&m_motionTimer,&QTimer::timeout,this,[this]{
        // A non-harmonic loop never has a visible start/end seam.
        m_timeScale=std::fmod(m_timeScale+(m_hovered?.036:.022),2.0*std::numbers::pi_v<qreal>);
        update();
    });
    m_motionTimer.start();
}

void GalleryTile::loadThumbnail(){
    if(m_projectPath.startsWith("aspectra://test_item_"))return;
    const QFileInfo info(m_projectPath);
    m_thumbnail=QImage(info.absoluteFilePath());
    if(!m_thumbnail.isNull())m_thumbnail=m_thumbnail.convertToFormat(QImage::Format_RGBA8888);
}

QColor GalleryTile::accentColor() const {
    if(m_projectPath.startsWith("aspectra://test_item_"))return QColor::fromHsv(qHash(m_projectPath)%360,170,228);
    const QString suffix=QFileInfo(m_projectPath).suffix().toLower();
    if(suffix=="psd"||suffix=="psb")return QColor("#3c86d8");
    if(suffix=="ai")return QColor("#efae37");
    if(suffix=="xd")return QColor("#d55895");
    if(suffix=="aspectra")return QColor("#45d7c1");
    return QColor("#65a9ed");
}

QString GalleryTile::displayName() const {
    if(m_projectPath.startsWith("aspectra://"))return m_projectPath.section('/',-1).replace('_',' ');
    return QFileInfo(m_projectPath).completeBaseName();
}

QString GalleryTile::displayType() const {
    if(m_projectPath.startsWith("aspectra://test_item_"))return "TEST ART";
    const QString suffix=QFileInfo(m_projectPath).suffix().toUpper();
    return suffix.isEmpty()?"PROJECT":suffix;
}

void GalleryTile::setBaseGeometry(const QRect &geometry,bool animate){
    m_baseGeometry=geometry;
    if(!animate||this->geometry().isNull()){
        m_geometryAnimation->stop();
        setGeometry(geometry);
        return;
    }
    animateTo(geometry,QEasingCurve(QEasingCurve::OutQuad));
}

void GalleryTile::animateTo(const QRect &geometry,const QEasingCurve &curve){
    if(this->geometry()==geometry)return;
    m_geometryAnimation->stop();
    m_geometryAnimation->setStartValue(this->geometry());
    m_geometryAnimation->setEndValue(geometry);
    m_geometryAnimation->setEasingCurve(curve);
    m_geometryAnimation->start();
}

void GalleryTile::paintThumbnail(QPainter &painter,const QRectF &bounds) const {
    QPainterPath clip;clip.addRoundedRect(bounds,10,10);
    painter.save();
    painter.setClipPath(clip);
    painter.setTransform(perspectiveTransform(bounds),true);
    if(!m_thumbnail.isNull()){
        const qreal panX=std::sin(m_timeScale*1.3)*.5+.5;
        const qreal panY=std::sin(m_timeScale*.91+1.17)*.5+.5;
        const qreal zoom=1.05+(std::sin(m_timeScale*1.07+.4)*.5+.5)*(.035+.115*m_hoverIntensity);
        const QPointF focal(.30+panX*.40,.30+panY*.40);
        const QSizeF sourceSize(m_thumbnail.width()/zoom,m_thumbnail.height()/zoom);
        QPointF topLeft(focal.x()*m_thumbnail.width()-sourceSize.width()/2.0,
                        focal.y()*m_thumbnail.height()-sourceSize.height()/2.0);
        topLeft.setX(qBound<qreal>(0.0,topLeft.x(),m_thumbnail.width()-sourceSize.width()));
        topLeft.setY(qBound<qreal>(0.0,topLeft.y(),m_thumbnail.height()-sourceSize.height()));
        painter.drawImage(bounds,m_thumbnail,QRectF(topLeft,sourceSize));
    }else{
        QLinearGradient fallback(bounds.topLeft(),bounds.bottomRight());
        const QColor accent=accentColor();
        fallback.setColorAt(0,accent.darker(190));
        fallback.setColorAt(.55,QColor("#121825"));
        fallback.setColorAt(1,accent.darker(145));
        painter.fillRect(bounds,fallback);
        painter.setPen(QColor(255,255,255,125));
        QFont extensionFont("Segoe UI",10,QFont::DemiBold);
        extensionFont.setLetterSpacing(QFont::AbsoluteSpacing,2);
        painter.setFont(extensionFont);
        painter.drawText(bounds,Qt::AlignCenter,displayType());
    }
    QLinearGradient shade(bounds.topLeft(),bounds.bottomRight());
    shade.setColorAt(0,QColor(4,7,14,38));
    shade.setColorAt(.55,QColor(4,7,14,5));
    shade.setColorAt(1,QColor(2,4,10,m_hovered?56:88));
    painter.fillRect(bounds,shade);
    painter.restore();
}

QTransform GalleryTile::perspectiveTransform(const QRectF &bounds) const {
    const qreal phase=m_timeScale;
    const qreal tiltLimit=2.0+6.0*m_hoverIntensity;
    const qreal rotX=std::sin(phase*.70+.32)*tiltLimit;
    const qreal rotY=std::sin(phase*1.11+1.07)*tiltLimit;
    const qreal drift=1.0+.012*(std::sin(phase*.83)*.5+.5)+.025*m_hoverIntensity;
    const QPointF center=bounds.center();
    QTransform perspective;
    perspective.translate(center.x(),center.y());
    perspective.rotate(rotX,Qt::XAxis,650.0);
    perspective.rotate(rotY,Qt::YAxis,650.0);
    perspective.scale(drift,drift);
    perspective.translate(-center.x(),-center.y());
    return perspective;
}

void GalleryTile::paintEvent(QPaintEvent *){
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF bounds=rect().adjusted(8,8,-8,-8);
    const qreal radius=10.0;
    const int shadowAlpha=m_hovered?60:0;
    for(int pass=4;pass>0;--pass){
        const qreal offset=pass*(m_hovered?2.5:1.5);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0,0,0,shadowAlpha/pass));
        painter.drawRoundedRect(bounds.translated(0,offset),radius+pass,radius+pass);
    }
    paintThumbnail(painter,bounds);
    QPainterPath glass;glass.addRoundedRect(bounds,radius,radius);
    painter.save();
    painter.setClipPath(glass);
    painter.setTransform(perspectiveTransform(bounds),true);
    QLinearGradient sheen(bounds.topLeft(),bounds.bottomRight());
    sheen.setColorAt(0,QColor(255,255,255,42));
    sheen.setColorAt(.20,QColor(255,255,255,14));
    sheen.setColorAt(.43,QColor(255,255,255,0));
    sheen.setColorAt(.68,QColor(255,255,255,18));
    sheen.setColorAt(1,QColor(255,255,255,0));
    painter.fillRect(bounds,sheen);
    const QRectF titleBounds(bounds.left()+12,bounds.bottom()-62,bounds.width()-24,38);
    QLinearGradient titleShade(titleBounds.topLeft(),titleBounds.bottomLeft());
    titleShade.setColorAt(0,QColor(1,4,10,0));
    titleShade.setColorAt(.33,QColor(1,4,10,115));
    titleShade.setColorAt(1,QColor(1,4,10,190));
    painter.fillRect(QRectF(bounds.left(),bounds.bottom()-80,bounds.width(),80),titleShade);
    painter.restore();
    QLinearGradient rim(bounds.topLeft(),bounds.bottomRight());
    rim.setColorAt(0,QColor(255,255,255,m_hovered?230:150));
    rim.setColorAt(.48,accentColor().lighter(130));
    rim.setColorAt(1,QColor(255,255,255,82));
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(rim,m_hovered?1.6:1.0));
    painter.drawRoundedRect(bounds.adjusted(.5,.5,-.5,-.5),radius,radius);
    painter.setPen(QColor(255,255,255,m_hovered?220:156));
    QFont titleFont("Segoe UI",qMax(9,qMin(13,height()/11)),QFont::DemiBold);
    painter.setFont(titleFont);
    const QString name=QFontMetrics(titleFont).elidedText(displayName(),Qt::ElideRight,qRound(titleBounds.width()));
    painter.drawText(titleBounds,Qt::AlignBottom|Qt::AlignLeft,name);
    QFont metaFont("Segoe UI",8);
    painter.setFont(metaFont);
    painter.setPen(QColor(234,242,255,176));
    painter.drawText(QRectF(titleBounds.left(),titleBounds.top(),titleBounds.width(),15),Qt::AlignLeft|Qt::AlignVCenter,displayType());
}

void GalleryTile::enterEvent(QEnterEvent *event){
    QWidget::enterEvent(event);
    if(m_hovered)return;
    m_hovered=true;
    m_hoverAnimation->stop();
    m_hoverAnimation->setStartValue(m_hoverIntensity);
    m_hoverAnimation->setEndValue(1.0);
    m_hoverAnimation->start();
    emit hoverStarted(this);
    update();
}

void GalleryTile::leaveEvent(QEvent *event){
    QWidget::leaveEvent(event);
    if(!m_hovered)return;
    m_hovered=false;
    m_hoverAnimation->stop();
    m_hoverAnimation->setStartValue(m_hoverIntensity);
    m_hoverAnimation->setEndValue(0.0);
    m_hoverAnimation->start();
    emit hoverEnded(this);
    update();
}

void GalleryTile::mouseReleaseEvent(QMouseEvent *event){
    QWidget::mouseReleaseEvent(event);
    if(event->button()==Qt::LeftButton&&rect().contains(event->position().toPoint())&&!m_projectPath.startsWith("aspectra://test_item_"))emit openProjectRequested(m_projectPath);
}
