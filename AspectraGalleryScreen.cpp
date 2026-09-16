#include "AspectraGalleryScreen.h"

#include <algorithm>
#include <cmath>
#include <random>

namespace {
constexpr int TileUnit = 138;
constexpr int GridGap = 0;
QDateTime projectTime(const QString &path) {
    const QFileInfo info(path);
    return info.birthTime().isValid() ? info.birthTime() : info.lastModified();
}
}

GalleryMosaicCanvas::GalleryMosaicCanvas(QWidget *parent) : QWidget(parent) {
    setMouseTracking(true);
    setCursor(Qt::OpenHandCursor);
}

void GalleryMosaicCanvas::mousePressEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton) { QWidget::mousePressEvent(event); return; }
    m_dragging = true;
    m_dragStart = event->position().toPoint();
    m_lastPosition = m_dragStart;
    m_releaseVelocity = 0.0;
    m_dragClock.restart();
    setCursor(Qt::ClosedHandCursor);
    emit dragStarted();
    event->accept();
}

void GalleryMosaicCanvas::mouseMoveEvent(QMouseEvent *event) {
    if (!m_dragging) { QWidget::mouseMoveEvent(event); return; }
    const QPoint position = event->position().toPoint();
    const QPoint delta = position - m_lastPosition;
    m_lastPosition = position;
    const qint64 elapsed=qMax<qint64>(1,m_dragClock.restart());
    if (std::abs(delta.x())>=1) {
        m_releaseVelocity=qreal(delta.x())/elapsed;
        emit dragMoved(delta);
    }
    event->accept();
}

void GalleryMosaicCanvas::mouseReleaseEvent(QMouseEvent *event) {
    if (!m_dragging || event->button() != Qt::LeftButton) { QWidget::mouseReleaseEvent(event); return; }
    m_dragging = false;
    setCursor(Qt::OpenHandCursor);
    emit dragReleased(qBound<qreal>(-2.5,m_releaseVelocity,2.5));
    event->accept();
}

AspectraGalleryScreen::AspectraGalleryScreen(Presentation presentation, QWidget *parent)
    : QWidget(parent), m_presentation(presentation) {
    setObjectName("AspectraGalleryScreen");
    setStyleSheet("QWidget#AspectraGalleryScreen{background:transparent;}QScrollArea#LivingGalleryRail{background:transparent;border:0;}");
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(presentation == Presentation::FullWelcome ? QMargins(24,18,24,18) : QMargins());
    root->setSpacing(presentation == Presentation::FullWelcome ? 12 : 0);
    if (presentation == Presentation::FullWelcome) {
        auto *header = new QWidget(this);
        auto *headerLayout = new QHBoxLayout(header);
        headerLayout->setContentsMargins(0,0,0,0); headerLayout->setSpacing(10);
        auto *brand = new QLabel(header);
        QPixmap wordmark(":/brand/wordmark.png");
        if (!wordmark.isNull()) brand->setPixmap(wordmark.scaled(220,44,Qt::KeepAspectRatio,Qt::SmoothTransformation)); else brand->setText("ASPECTRA");
        brand->setAlignment(Qt::AlignLeft|Qt::AlignVCenter);
        auto *subhead = new QLabel("LIVING ART GALLERY", header);
        subhead->setStyleSheet("color:#d8e1ee;font-size:11px;font-weight:800;letter-spacing:3px;");
        auto *importButton = new QPushButton("Import project", header);
        importButton->setStyleSheet("background:#111721;border:1px solid #42516a;border-radius:14px;padding:8px 13px;color:#fff;font-weight:700;");
        headerLayout->addWidget(brand); headerLayout->addWidget(subhead); headerLayout->addStretch(); headerLayout->addWidget(importButton);
        root->addWidget(header);
        auto *caption = new QLabel("Drag to travel through your projects. Hover to bring a project into focus.", this);
        caption->setStyleSheet("color:#8f9caf;font-size:11px;"); root->addWidget(caption);
        connect(importButton, &QPushButton::clicked, this, &AspectraGalleryScreen::importProjectRequested);
    }
    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setObjectName("LivingGalleryRail");
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_scrollArea->setWidgetResizable(false);
    m_scrollArea->setAlignment(Qt::AlignLeft|Qt::AlignTop);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_mosaicCanvas = new GalleryMosaicCanvas(m_scrollArea);
    m_mosaicCanvas->setObjectName("LivingGalleryCanvas");
    m_mosaicCanvas->setStyleSheet("QWidget#LivingGalleryCanvas{background:transparent;}");
    m_scrollArea->setWidget(m_mosaicCanvas);
    root->addWidget(m_scrollArea, 1);
    m_emptyLabel = new QLabel("No recent projects yet\nImport a project to start your gallery.", m_mosaicCanvas);
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->setStyleSheet("color:#8d9cad;font-size:15px;line-height:1.5;");
    m_emptyLabel->hide();

    m_zoomAnimation = new QVariantAnimation(this);
    m_zoomAnimation->setDuration(280); m_zoomAnimation->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_zoomAnimation, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) { setGlobalZoomFactor(value.toReal()); });
    m_repulsionAnimation = new QVariantAnimation(this);
    m_repulsionAnimation->setDuration(240);
    connect(m_repulsionAnimation, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) { m_repulsionAmount=value.toReal(); applyCameraGeometry(); });
    connect(m_mosaicCanvas, &GalleryMosaicCanvas::dragStarted, this, [this] { stopKineticMotion(); });
    connect(m_mosaicCanvas, &GalleryMosaicCanvas::dragMoved, this, [this](const QPoint &delta) {
        if (std::abs(delta.x())<1) return;
        auto *bar=m_scrollArea->horizontalScrollBar(); bar->setValue(bar->value()-qRound(delta.x()*.35));
    });
    connect(m_mosaicCanvas, &GalleryMosaicCanvas::dragReleased, this, [this](qreal velocity) { m_kineticVelocity=-velocity; if (std::abs(m_kineticVelocity)>.08) m_kineticTimer.start(); });
    m_kineticTimer.setInterval(16);
    connect(&m_kineticTimer, &QTimer::timeout, this, [this] {
        auto *bar=m_scrollArea->horizontalScrollBar();
        bar->setValue(qBound(bar->minimum(), bar->value()+qRound(m_kineticVelocity*8.0), bar->maximum()));
        m_kineticVelocity *= .85;
        if (std::abs(m_kineticVelocity)<.04 || bar->value()==bar->minimum() || bar->value()==bar->maximum()) stopKineticMotion();
    });
}

void AspectraGalleryScreen::setProjectPaths(const QStringList &paths) {
    QStringList valid;
    for (const QString &path : paths) {
        const QString canonical=QFileInfo(path).absoluteFilePath();
        if (QFileInfo::exists(canonical) && !valid.contains(canonical)) valid.push_back(canonical);
    }
    std::sort(valid.begin(), valid.end(), [](const QString &left,const QString &right){ return projectTime(left)>projectTime(right); });
    if (valid.size()>100) valid=valid.mid(0,100);
    for (int index=valid.size(); index<100; ++index) valid.push_back(QStringLiteral("aspectra://test_item_%1").arg(index+1,3,10,QChar('0')));
    m_projectPaths=valid; m_hoveredTile=nullptr; stopKineticMotion();
    for (GalleryTile *tile:m_tiles) tile->deleteLater();
    m_tiles.clear(); m_spans.clear();
    for (const QString &path:m_projectPaths) {
        auto *tile=new GalleryTile(path,m_mosaicCanvas); m_tiles.push_back(tile);
        connect(tile,&GalleryTile::hoverStarted,this,[this](GalleryTile *hovered){ applyRadialRepulsion(hovered); });
        connect(tile,&GalleryTile::hoverEnded,this,[this](GalleryTile *leaving){ clearRadialRepulsion(leaving); });
        connect(tile,&GalleryTile::openProjectRequested,this,&AspectraGalleryScreen::openProjectRequested);
    }
    scheduleMosaicLayout();
}

void AspectraGalleryScreen::scheduleMosaicLayout() {
    if (m_repackQueued) return;
    m_repackQueued=true;
    QTimer::singleShot(0,this,[this]{m_repackQueued=false; layoutMosaic();});
}

void AspectraGalleryScreen::setGlobalZoomFactor(qreal factor) {
    factor=qBound<qreal>(1.0,factor,1.15);
    if (qFuzzyCompare(m_globalZoomFactor,factor)) return;
    m_globalZoomFactor=factor; applyCameraGeometry(); emit globalZoomFactorChanged(factor);
}

AspectraGalleryScreen::Span AspectraGalleryScreen::randomSpan(bool hero,int maxRows) const {
    if (hero) return maxRows>=4 ? Span{4,4} : Span{3,qMin(2,maxRows)};
    static const QVector<Span> choices{{1,1},{2,1},{1,2},{2,2},{3,2},{4,4}};
    static const std::vector<double> weights{44.0,23.0,16.0,11.0,5.0,1.0};
    static thread_local std::mt19937 generator(std::random_device{}());
    std::discrete_distribution<int> distribution(weights.begin(),weights.end());
    for (int attempts=0;attempts<12;++attempts) { const Span span=choices.value(distribution(generator)); if (span.rows<=maxRows) return span; }
    return {1,1};
}

void AspectraGalleryScreen::layoutMosaic() {
    const int viewportHeight=qMax(260,m_scrollArea->viewport()->height());
    const int viewportWidth=qMax(360,m_scrollArea->viewport()->width());
    const int maxRows=qMax(2,(viewportHeight+GridGap)/(TileUnit+GridGap));
    if (m_tiles.isEmpty()) {
        m_baseCanvasSize=QSize(viewportWidth,viewportHeight); m_mosaicCanvas->setFixedSize(m_baseCanvasSize);
        m_emptyLabel->setGeometry(m_mosaicCanvas->rect()); m_emptyLabel->show(); return;
    }
    m_emptyLabel->hide();
    QVector<QVector<bool>> occupiedColumns;
    auto ensureColumns=[&](int count){while(occupiedColumns.size()<count) occupiedColumns.push_back(QVector<bool>(maxRows,false));};
    auto fits=[&](int column,int row,const Span &span){
        if (row+span.rows>maxRows) return false; ensureColumns(column+span.columns);
        for(int x=column;x<column+span.columns;++x) for(int y=row;y<row+span.rows;++y) if (occupiedColumns[x][y]) return false;
        return true;
    };
    auto reserve=[&](int column,int row,const Span &span){for(int x=column;x<column+span.columns;++x)for(int y=row;y<row+span.rows;++y)occupiedColumns[x][y]=true;};
    int furthestColumn=0;
    for (int index=0;index<m_tiles.size();++index) {
        GalleryTile *tile=m_tiles[index];
        Span span=m_spans.contains(tile->projectPath()) ? m_spans.value(tile->projectPath()) : randomSpan(index==0,maxRows);
        // A resized selector can expose fewer rows than a cached tile span.
        // Clamp before attempting placement so every candidate can fit.
        span.rows=qMin(span.rows,maxRows);
        span.columns=qMin(span.columns,span.rows);
        m_spans.insert(tile->projectPath(),span);
        int placedColumn=0,placedRow=0; bool placed=false;
        for (int column=0;!placed;++column) for (int row=0;row<maxRows;++row) {
            if (!fits(column,row,span)) continue;
            placedColumn=column; placedRow=row; reserve(column,row,span); placed=true; break;
        }
        const int px=placedColumn*(TileUnit+GridGap), py=placedRow*(TileUnit+GridGap);
        const int width=span.columns*TileUnit+(span.columns-1)*GridGap;
        const int height=span.rows*TileUnit+(span.rows-1)*GridGap;
        tile->setBaseGeometry(QRect(px,py,width,height),false); tile->show();
        furthestColumn=qMax(furthestColumn,placedColumn+span.columns);
    }
    m_baseCanvasSize=QSize(qMax(viewportWidth,furthestColumn*TileUnit+(furthestColumn-1)*GridGap),maxRows*TileUnit+(maxRows-1)*GridGap);
    applyCameraGeometry();
}

void AspectraGalleryScreen::applyCameraGeometry() {
    if (m_baseCanvasSize.isEmpty()) return;
    const qreal zoom=m_globalZoomFactor;
    m_mosaicCanvas->setFixedSize(qCeil(m_baseCanvasSize.width()*zoom),qCeil(m_baseCanvasSize.height()*zoom));
    m_emptyLabel->setGeometry(m_mosaicCanvas->rect());
    const QPointF focus=m_hoveredTile ? m_hoveredTile->baseGeometry().center() : QPointF(m_baseCanvasSize.width()/2.0,m_baseCanvasSize.height()/2.0);
    for (GalleryTile *tile:m_tiles) {
        const QRect base=tile->baseGeometry();
        QRectF geometry(base.x()*zoom,base.y()*zoom,base.width()*zoom,base.height()*zoom);
        if (m_hoveredTile && tile!=m_hoveredTile && m_repulsionAmount>0) {
            const QPointF delta=QPointF(base.center())-focus;
            const qreal distance=qMax<qreal>(1.0,std::hypot(delta.x(),delta.y()));
            const qreal magnitude=distance>560.0?0.0:qBound<qreal>(0.0,14.0,1450.0/distance)*m_repulsionAmount;
            geometry.translate(delta.x()/distance*magnitude,delta.y()/distance*magnitude);
        }
        if (tile==m_hoveredTile) {
            const qreal emphasis=.04*m_repulsionAmount;
            geometry.adjust(-geometry.width()*emphasis,-geometry.height()*emphasis,geometry.width()*emphasis,geometry.height()*emphasis);
        }
        tile->setGeometry(geometry.toAlignedRect());
    }
    if (m_hoveredTile) centerHoveredTile();
}

void AspectraGalleryScreen::centerHoveredTile() {
    if (!m_hoveredTile) return;
    const QRect geometry=m_hoveredTile->geometry(); const QSize viewport=m_scrollArea->viewport()->size();
    auto *horizontal=m_scrollArea->horizontalScrollBar(); auto *vertical=m_scrollArea->verticalScrollBar();
    horizontal->setValue(qBound(horizontal->minimum(),geometry.center().x()-viewport.width()/2,horizontal->maximum()));
    vertical->setValue(qBound(vertical->minimum(),geometry.center().y()-viewport.height()/2,vertical->maximum()));
}

void AspectraGalleryScreen::animateCameraZoom(qreal endValue) {
    m_zoomAnimation->stop(); m_zoomAnimation->setStartValue(m_globalZoomFactor); m_zoomAnimation->setEndValue(endValue); m_zoomAnimation->start();
}
void AspectraGalleryScreen::animateRepulsion(qreal endValue,const QEasingCurve &curve) {
    m_repulsionAnimation->stop(); m_repulsionAnimation->setEasingCurve(curve); m_repulsionAnimation->setStartValue(m_repulsionAmount); m_repulsionAnimation->setEndValue(endValue); m_repulsionAnimation->start();
}
void AspectraGalleryScreen::applyRadialRepulsion(GalleryTile *hovered) {
    if (!hovered) return; stopKineticMotion(); m_hoveredTile=hovered; hovered->raise();
    animateRepulsion(1.0,QEasingCurve(QEasingCurve::OutBack)); animateCameraZoom(1.15); applyCameraGeometry();
}
void AspectraGalleryScreen::clearRadialRepulsion(GalleryTile *leavingTile) {
    if (m_hoveredTile!=leavingTile) return;
    m_hoveredTile=nullptr; animateRepulsion(0.0,QEasingCurve(QEasingCurve::OutQuad)); animateCameraZoom(1.0); applyCameraGeometry();
}
void AspectraGalleryScreen::stopKineticMotion() { m_kineticTimer.stop(); m_kineticVelocity=0.0; }
void AspectraGalleryScreen::resizeEvent(QResizeEvent *event) { QWidget::resizeEvent(event); scheduleMosaicLayout(); }
void AspectraGalleryScreen::showEvent(QShowEvent *event) { QWidget::showEvent(event); scheduleMosaicLayout(); }
