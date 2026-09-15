#include "GalleryTile.h"

#include <algorithm>
#include <cmath>
#include <random>

namespace {
QPointF interpolate(const QPointF &a,const QPointF &b,qreal t){return a+(b-a)*t;}
}

GalleryTile::GalleryTile(QString projectPath,QWidget *parent)
    : QWidget(parent),m_projectPath(std::move(projectPath)) {
    setAttribute(Qt::WA_Hover,true);
    setMouseTracking(true);
    setCursor(Qt::PointingHandCursor);
    setSizePolicy(QSizePolicy::Fixed,QSizePolicy::Fixed);
    loadThumbnail();
    buildFocalPoints();

    m_geometryAnimation=new QVariantAnimation(this);
    m_geometryAnimation->setDuration(240);
    connect(m_geometryAnimation,&QVariantAnimation::valueChanged,this,[this](const QVariant &value){
        setGeometry(value.toRect());
        update();
    });

    m_kenBurnsAnimation=new QVariantAnimation(this);
    m_kenBurnsAnimation->setStartValue(0.0);
    m_kenBurnsAnimation->setEndValue(1.0);
    m_kenBurnsAnimation->setDuration(6800);
    m_kenBurnsAnimation->setLoopCount(-1);
    m_kenBurnsAnimation->setEasingCurve(QEasingCurve::InOutSine);
    connect(m_kenBurnsAnimation,&QVariantAnimation::valueChanged,this,[this](const QVariant &value){
        setKenBurnsProgress(value.toReal());
    });
}

void GalleryTile::loadThumbnail(){
    const QFileInfo info(m_projectPath);
    m_thumbnail=QImage(info.absoluteFilePath());
    if(!m_thumbnail.isNull())m_thumbnail=m_thumbnail.convertToFormat(QImage::Format_RGBA8888);
}

void GalleryTile::buildFocalPoints(){
    std::mt19937 generator(qHash(m_projectPath));
    std::uniform_real_distribution<qreal> horizontal(.18,.82);
    std::uniform_real_distribution<qreal> vertical(.18,.82);
    m_focalPoints.reserve(4);
    // Four centers produce three cinematic moves while staying within the source.
    for(int point=0;point<4;++point)m_focalPoints.push_back({horizontal(generator),vertical(generator)});
}

QColor GalleryTile::accentColor() const {
    const QString suffix=QFileInfo(m_projectPath).suffix().toLower();
    if(suffix=="psd"||suffix=="psb")return QColor("#3c86d8");
    if(suffix=="ai")return QColor("#efae37");
    if(suffix=="xd")return QColor("#d55895");
    if(suffix=="aspectra")return QColor("#45d7c1");
    return QColor("#65a9ed");
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

void GalleryTile::setKenBurnsProgress(qreal progress){
    progress=qBound<qreal>(0.0,progress,1.0);
    if(qFuzzyCompare(m_kenBurnsProgress,progress))return;
    m_kenBurnsProgress=progress;
    emit kenBurnsProgressChanged(progress);
    update();
}

void GalleryTile::paintThumbnail(QPainter &painter,const QRectF &bounds) const {
    QPainterPath clip;clip.addRoundedRect(bounds,10,10);
    painter.save();
    painter.setClipPath(clip);
    if(!m_thumbnail.isNull()){
        const int segments=qMax(1,m_focalPoints.size()-1);
        const qreal scaled=m_kenBurnsProgress*segments;
        const int index=qBound(0,int(std::floor(scaled)),segments-1);
        const qreal local=scaled-index;
        const QPointF focal=interpolate(m_focalPoints[index],m_focalPoints[index+1],local);
        const qreal zoom=1.0+.25*m_kenBurnsProgress;
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
        painter.drawText(bounds,Qt::AlignCenter,QFileInfo(m_projectPath).suffix().toUpper());
    }
    QLinearGradient shade(bounds.topLeft(),bounds.bottomRight());
    shade.setColorAt(0,QColor(4,7,14,38));
    shade.setColorAt(.55,QColor(4,7,14,5));
    shade.setColorAt(1,QColor(2,4,10,m_hovered?56:88));
    painter.fillRect(bounds,shade);
    painter.restore();
}

void GalleryTile::paintEvent(QPaintEvent *){
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF bounds=rect().adjusted(3,3,-3,-3);
    const qreal radius=10.0;
    const int shadowAlpha=m_hovered?50:25;
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
    QLinearGradient sheen(bounds.topLeft(),bounds.bottomRight());
    sheen.setColorAt(0,QColor(255,255,255,42));
    sheen.setColorAt(.20,QColor(255,255,255,14));
    sheen.setColorAt(.43,QColor(255,255,255,0));
    sheen.setColorAt(.68,QColor(255,255,255,18));
    sheen.setColorAt(1,QColor(255,255,255,0));
    painter.fillRect(bounds,sheen);
    const QRectF titleBounds(bounds.left()+10,bounds.bottom()-46,bounds.width()-20,36);
    QLinearGradient titleShade(titleBounds.topLeft(),titleBounds.bottomLeft());
    titleShade.setColorAt(0,QColor(1,4,10,0));
    titleShade.setColorAt(.33,QColor(1,4,10,115));
    titleShade.setColorAt(1,QColor(1,4,10,190));
    painter.fillRect(QRectF(bounds.left(),bounds.bottom()-62,bounds.width(),62),titleShade);
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
    const QString name=QFontMetrics(titleFont).elidedText(QFileInfo(m_projectPath).completeBaseName(),Qt::ElideRight,qRound(titleBounds.width()));
    painter.drawText(titleBounds,Qt::AlignBottom|Qt::AlignLeft,name);
    QFont metaFont("Segoe UI",8);
    painter.setFont(metaFont);
    painter.setPen(QColor(234,242,255,176));
    painter.drawText(QRectF(titleBounds.left(),titleBounds.top(),titleBounds.width(),15),Qt::AlignLeft|Qt::AlignVCenter,QFileInfo(m_projectPath).suffix().toUpper());
}

void GalleryTile::enterEvent(QEnterEvent *event){
    QWidget::enterEvent(event);
    if(m_hovered)return;
    m_hovered=true;
    m_kenBurnsAnimation->start();
    emit hoverStarted(this);
    update();
}

void GalleryTile::leaveEvent(QEvent *event){
    QWidget::leaveEvent(event);
    if(!m_hovered)return;
    m_hovered=false;
    m_kenBurnsAnimation->stop();
    setKenBurnsProgress(0.0);
    emit hoverEnded(this);
    update();
}

void GalleryTile::mouseReleaseEvent(QMouseEvent *event){
    QWidget::mouseReleaseEvent(event);
    if(event->button()==Qt::LeftButton&&rect().contains(event->position().toPoint()))emit openProjectRequested(m_projectPath);
}
