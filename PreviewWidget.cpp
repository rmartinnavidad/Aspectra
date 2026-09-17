#include <optional>
#include <cmath>

#include "PreviewWidget.h"

PreviewWidget::PreviewWidget(QWidget *parent):QWidget(parent) {

    setMinimumHeight(220); setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Expanding);setFocusPolicy(Qt::StrongFocus);

    refresh.setSingleShot(true); refresh.setInterval(0);

    connect(&refresh,&QTimer::timeout,this,&PreviewWidget::rebuild);
    // Canvas navigation must not invoke the full adjustment renderer on each
    // wheel tick or animation frame.  Rebuild once input has settled instead.
    zoomSettleRefresh.setSingleShot(true);zoomSettleRefresh.setInterval(90);
    connect(&zoomSettleRefresh,&QTimer::timeout,this,&PreviewWidget::rebuild);
    selectionAnimation.setInterval(80);
    connect(&selectionAnimation,&QTimer::timeout,this,qOverload<>(&PreviewWidget::update));

}

void PreviewWidget::setFrame(const QImage &image) { frame=image; refresh.start(); }
void PreviewWidget::setArtboards(const QVector<PreviewArtboard> &items)
{
    workspaceArtboards = items;

    for (const PreviewArtboard &artboard : workspaceArtboards) {
        if (artboard.active) {
            activeArtboardId = artboard.id;
            break;
        }
    }

    update();
}

void PreviewWidget::clearArtboards()
{
    workspaceArtboards.clear();
    activeArtboardId.clear();
    update();
}

void PreviewWidget::setActiveArtboard(const QString &id)
{
    activeArtboardId = id;

    for (PreviewArtboard &artboard : workspaceArtboards)
        artboard.active = (artboard.id == id);

    update();
}

QRectF PreviewWidget::workspaceBounds() const
{
    QRectF bounds;
    bool first = true;

    for (const PreviewArtboard &artboard : workspaceArtboards) {
        if (!artboard.visible || !artboard.size.isValid())
            continue;

        QRectF rect(
            artboard.position,
            QSizeF(artboard.size)
        );

        if (first) {
            bounds = rect;
            first = false;
        } else {
            bounds = bounds.united(rect);
        }
    }

    return bounds;
}

QRectF PreviewWidget::artboardScreenRect(
    const PreviewArtboard &artboard) const
{
    QRectF bounds = workspaceBounds();

    if (!bounds.isValid())
        return {};

    const double availableWidth  = qMax(1, width()  - 80);
    const double availableHeight = qMax(1, height() - 80);

    double baseScale = qMin(
        availableWidth / bounds.width(),
        availableHeight / bounds.height()
    );

    if (!std::isfinite(baseScale) || baseScale <= 0.0)
        baseScale = 1.0;

    const double scale = baseScale * viewZoom;

    const QPointF center(
        width() * 0.5,
        height() * 0.5
    );

    const QPointF boundsCenter = bounds.center();

    QPointF topLeft =
        center +
        viewPan +
        QPointF(
            (artboard.position.x() - boundsCenter.x()) * scale,
            (artboard.position.y() - boundsCenter.y()) * scale
        );

    return QRectF(
        topLeft,
        QSizeF(
            artboard.size.width() * scale,
            artboard.size.height() * scale
        )
    );
}
void PreviewWidget::setTextureFrame(const QImage &image) { textureFrame=image; textureActive=!image.isNull(); refresh.start(); }
void PreviewWidget::clearTextureFrame() { textureActive=false; textureFrame={}; refresh.start(); }
void PreviewWidget::setVectorTraceFrame(const QImage &image) { vectorTraceFrame=image; vectorTraceActive=!image.isNull(); refresh.start(); }
void PreviewWidget::clearVectorTraceFrame() { vectorTraceActive=false; vectorTraceFrame={}; refresh.start(); }
void PreviewWidget::setViewZoom(double value) {
    const double previous=viewZoom;
    viewZoom=std::clamp(value,.25,16.0);
    // Returning to the native 1:1 view is also a recenter operation.  This is
    // what lets a tap outside the rails collapse back to the centered gallery
    // view after the user has panned around a zoomed image.
    if(viewZoom<=1.0)viewPan={};
    // Crossing into/out of focus needs an immediate layout-quality rebuild.
    // Continuous zoom inside focus uses the cached image this frame and
    // schedules one refinement after the gesture stops.
    if((previous>1.04)!=(viewZoom>1.04))refresh.start();else zoomSettleRefresh.start();
    emit viewZoomChanged(viewZoom);
    update();
}
void PreviewWidget::setLayerMask(const QImage &image) { layerMask=image; refresh.start(); }
void PreviewWidget::setSelectionMask(const QImage &image) { selectionMask=image; if(selectionMask.isNull())selectionAnimation.stop();else selectionAnimation.start();update(); }
void PreviewWidget::clearSelectionMask() { selectionMask={};selectionAnimation.stop();update(); }

void PreviewWidget::setAdjustments(const Adjustments &a,QSize s,const RecolorPreset *p) {

    settings=a; target=s; recolor=p?std::optional<RecolorPreset>(*p):std::nullopt; refresh.start();

}

void PreviewWidget::resizeEvent(QResizeEvent *) {
    // A layout change must redraw the fitted image before the next paint.
    // Deferring normal-view rendering left the previous, taller frame cached
    // after the layer card opened, which made its lower edge look clipped.
    if(viewZoom>1.04) zoomSettleRefresh.start();
    else rebuild();
}

void PreviewWidget::rebuild() {

    const QSize viewport(qMax(1,width()-8),qMax(1,height()-8));
    // Once the user zooms, Aspectra behaves like a phone gallery: the media
    // covers the entire available stage and is rendered at that cover size.
    // Normal view remains fully fitted and uncropped.
    const QSize size=target.scaled(viewport,viewZoom>1.04?Qt::KeepAspectRatioByExpanding:Qt::KeepAspectRatio);

    try { const QImage &source=vectorTraceActive?vectorTraceFrame:(textureActive?textureFrame:frame); processed=ImageProcessor::render(source,size,settings,recolor?&*recolor:nullptr); if(!vectorTraceActive&&!textureActive&&!processed.isNull()&&!layerMask.isNull()){QImage alpha=layerMask.scaled(processed.size(),Qt::IgnoreAspectRatio,Qt::SmoothTransformation).convertToFormat(QImage::Format_Grayscale8);processed=processed.convertToFormat(QImage::Format_ARGB32);for(int y=0;y<processed.height();++y){QRgb *pixels=reinterpret_cast<QRgb*>(processed.scanLine(y));const uchar *mask=alpha.constScanLine(y);for(int x=0;x<processed.width();++x)pixels[x]=qRgba(qRed(pixels[x]),qGreen(pixels[x]),qBlue(pixels[x]),qAlpha(pixels[x])*mask[x]/255);}} } catch(...) { processed={}; }

    update();

}

void PreviewWidget::paintEvent(QPaintEvent *) {

    QPainter p(this); p.setRenderHint(QPainter::Antialiasing); p.setRenderHint(QPainter::SmoothPixmapTransform);

    // Keep the preview surface unframed.  The media/checkerboard, selection
    // guides and active tool are the only visible canvas boundaries.
    QRectF outer=rect();

    if(processed.isNull()) {

        p.setPen(QColor("#6c7b78"));

        QFont f=font(); f.setPointSize(12); p.setFont(f);

        p.drawText(rect(),Qt::AlignCenter,"Drop an image or video here\n\nShape + color, in your control"); return;

    }

    const QRectF box=frameBox();

    QPainterPath clip; clip.addRoundedRect(box,10,10); p.setClipPath(clip);

    for(int y=int(box.top());y<box.bottom();y+=12) for(int x=int(box.left());x<box.right();x+=12)

        p.fillRect(QRect(x,y,12,12),((x-int(box.left()))/12+(y-int(box.top()))/12)%2?QColor("#1c2223"):QColor("#14191a"));

    p.drawImage(box,processed); p.setClipping(false);
    const auto paintMarchingAnts=[&p,&box,this](const QImage &source){const QImage mask=source.scaled(processed.size(),Qt::IgnoreAspectRatio,Qt::FastTransformation).convertToFormat(QImage::Format_Grayscale8);for(int y=1;y<mask.height()-1;++y)for(int x=1;x<mask.width()-1;++x){const int value=mask.constScanLine(y)[x];if(value<128)continue;if(mask.constScanLine(y-1)[x]>=128&&mask.constScanLine(y+1)[x]>=128&&mask.constScanLine(y)[x-1]>=128&&mask.constScanLine(y)[x+1]>=128)continue;const bool light=((x+y+QTime::currentTime().msec()/90)/3)%2==0;p.setPen(light?Qt::white:Qt::black);p.drawPoint(QPointF(box.left()+(x+.5)*box.width()/mask.width(),box.top()+(y+.5)*box.height()/mask.height()));}};
    if(!vectorTraceActive&&!textureActive&&!layerMask.isNull())paintMarchingAnts(layerMask);
    if(!selectionMask.isNull())paintMarchingAnts(selectionMask);

    // Do not draw a permanent blue frame around media.  Guides and selections
    // provide their own contextual outlines when enabled.

    if(guides){

        QRectF safe=box.adjusted(safeMargins.left()*box.width(),safeMargins.top()*box.height(),-safeMargins.right()*box.width(),-safeMargins.bottom()*box.height());

        QPainterPath warning;warning.addRect(box);warning.addRect(safe);warning.setFillRule(Qt::OddEvenFill);p.fillPath(warning,QColor(238,70,88,28));

        p.setPen(QPen(QColor("#ed4658"),1,Qt::DashLine));p.drawRect(box.adjusted(2,2,-2,-2));

        p.setPen(QPen(QColor("#57dd7b"),1.5,Qt::DashLine));p.drawRect(safe);

        p.setBrush(QColor("#57dd7b"));for(auto point:{QPointF(safe.left(),safe.center().y()),QPointF(safe.right(),safe.center().y()),QPointF(safe.center().x(),safe.top()),QPointF(safe.center().x(),safe.bottom())})p.drawEllipse(point,3,3);

        QRectF pad=box.adjusted(settings.padding.left()*box.width(),settings.padding.top()*box.height(),-settings.padding.right()*box.width(),-settings.padding.bottom()*box.height());

        if(settings.padding!=QMarginsF()){p.setPen(QPen(QColor("#fac665"),1,Qt::DotLine));p.setBrush(Qt::NoBrush);p.drawRect(pad);}

        p.setFont(QFont("Segoe UI",8));p.setPen(QColor("#57dd7b"));p.drawText(safe.topLeft()+QPointF(5,13),"SAFE");

        p.setPen(QColor("#ed6675"));p.drawText(box.bottomLeft()+QPointF(6,-6),"OUTSIDE SAFE | guides do not export");

    }

}



QRectF PreviewWidget::frameBox() const {
    QSizeF displaySize(processed.width(),processed.height());
    double scale=viewZoom;
    // Focus view uses the live viewport as the source of truth.  It therefore
    // covers the whole stage even during the layout/re-render handoff, rather
    // than briefly retaining the previous fitted-image rectangle.
    if(viewZoom>1.04&&!displaySize.isEmpty())
        scale*=qMax(double(width())/displaySize.width(),double(height())/displaySize.height());
    displaySize*=scale;
    return QRectF(QPointF((width()-displaySize.width())/2.,(height()-displaySize.height())/2.)+viewPan,displaySize);
}

void PreviewWidget::adjustViewZoomFromInput(int angleDelta,int pixelDelta,Qt::KeyboardModifiers modifiers){
    if(processed.isNull())return;
    const int delta=angleDelta?angleDelta:pixelDelta;
    if(!delta)return;
    if(sphereInteractive&&modifiers.testFlag(Qt::ControlModifier)){emit sphereZoomed(delta);return;}
    // Standard wheels report ±120 angle units; precision touchpads report
    // pixel deltas instead.  Treat both as directional continuous zoom input.
    const double units=angleDelta?qMax(1.0,qAbs(angleDelta)/120.0):qMax(1.0,qAbs(pixelDelta)/15.0);
    const double step=modifiers.testFlag(Qt::ControlModifier)?1.035:1.20;
    const double factor=std::pow(step,delta>0?units:-units);
    const double next=std::clamp(viewZoom*factor,.25,16.0);
    // Do not leave focus mode stranded infinitesimally above its threshold.
    if(delta<0&&next<=1.08)setViewZoom(1.0);else setViewZoom(next);
}

void PreviewWidget::wheelEvent(QWheelEvent *event){
    if(processed.isNull()){event->ignore();return;}
    const int angleDelta=event->angleDelta().y();
    const int pixelDelta=event->pixelDelta().y();
    if(!(angleDelta||pixelDelta)){event->ignore();return;}
    adjustViewZoomFromInput(angleDelta,pixelDelta,event->modifiers());
    event->accept();
}

void PreviewWidget::keyPressEvent(QKeyEvent *event){
    if(event->key()==Qt::Key_Escape||event->key()==Qt::Key_0){if(viewZoom>1.04)setViewZoom(1.0);event->accept();return;}
    if(event->key()==Qt::Key_Minus){adjustViewZoomFromInput(-120,0,event->modifiers());event->accept();return;}
    if(event->key()==Qt::Key_Plus||event->key()==Qt::Key_Equal){adjustViewZoomFromInput(120,0,event->modifiers());event->accept();return;}
    QWidget::keyPressEvent(event);
}

void PreviewWidget::mouseDoubleClickEvent(QMouseEvent *event){
    if(event->button()==Qt::LeftButton){emit importRequested();event->accept();return;}
    QWidget::mouseDoubleClickEvent(event);
}

void PreviewWidget::setGuides(bool visible,QMarginsF safe){guides=visible;safeMargins=safe;update();}

void PreviewWidget::mousePressEvent(QMouseEvent *e){
    if(e->button()==Qt::LeftButton)setFocus(Qt::MouseFocusReason);
    if(sphereInteractive&&e->button()==Qt::LeftButton){sphereDragging=true;sphereAnchor=e->position();tapCandidate=false;setCursor(Qt::ClosedHandCursor);return;}
    // In focus canvas, a plain drag is always navigation when no edit tool is
    // active.  It deliberately bypasses safe-guide handles, which otherwise
    // turned an image drag into a guide resize near an edge.
    if(viewZoom>1.04&&canvasTool==CanvasTool::None&&!eyedropper&&!maskPainting&&e->button()==Qt::LeftButton&&!processed.isNull()){
        tapCandidate=true;tapStart=e->position();panning=true;panAnchor=e->position();setCursor(Qt::ClosedHandCursor);grabMouse();return;
    }
    if(eyedropper&&e->button()==Qt::LeftButton&&!processed.isNull()){auto box=frameBox();int px=qBound(0,qRound((e->position().x()-box.left())/box.width()*processed.width()),processed.width()-1),py=qBound(0,qRound((e->position().y()-box.top())/box.height()*processed.height()),processed.height()-1);emit colorSampled(processed.pixelColor(px,py));setEyedropper(false);return;}
    if(maskPainting&&e->button()==Qt::LeftButton&&!processed.isNull()){auto box=frameBox();emit maskBrushed(QPointF(qBound(0.,(e->position().x()-box.left())/box.width(),1.),qBound(0.,(e->position().y()-box.top())/box.height(),1.)),maskAdd,maskSize);paintingMask=true;return;}
    if(canvasTool!=CanvasTool::None&&e->button()==Qt::LeftButton&&!processed.isNull()){auto box=frameBox();const QPointF point(qBound(0.,(e->position().x()-box.left())/box.width(),1.),qBound(0.,(e->position().y()-box.top())/box.height(),1.));toolPath={point};toolInteracting=true;if(RasterToolEngine::isStrokeTool(canvasTool))emit toolStroke(point,canvasTool,toolDiameter,e->modifiers());return;}
    if(e->button()==Qt::RightButton){QMenu menu(this);auto *reset=menu.addAction("Reset view");auto *toggleGuides=menu.addAction(guides?"Hide guides":"Show guides");auto *chosen=menu.exec(e->globalPosition().toPoint());if(chosen==reset){setViewZoom(1.0);}else if(chosen==toggleGuides){guides=!guides;update();}return;}
    tapCandidate=e->button()==Qt::LeftButton;tapStart=e->position();
    if(!guides||processed.isNull()||e->button()!=Qt::LeftButton){if(!processed.isNull()&&e->button()==Qt::LeftButton){panning=true;panAnchor=e->position();setCursor(Qt::ClosedHandCursor);}return;}

    draggingPadding=e->modifiers().testFlag(Qt::AltModifier);auto m=draggingPadding?settings.padding:safeMargins;auto box=frameBox();auto region=box.adjusted(m.left()*box.width(),m.top()*box.height(),-m.right()*box.width(),-m.bottom()*box.height());

    QPointF p=e->position();double distances[]{qAbs(p.x()-region.left()),qAbs(p.y()-region.top()),qAbs(p.x()-region.right()),qAbs(p.y()-region.bottom())};double nearest=12;

    for(int i=0;i<4;++i)if(distances[i]<nearest){nearest=distances[i];dragEdge=i;}

    if(dragEdge>=0)setCursor(dragEdge%2?Qt::SizeVerCursor:Qt::SizeHorCursor);else{panning=true;panAnchor=e->position();setCursor(Qt::ClosedHandCursor);}

}

void PreviewWidget::mouseMoveEvent(QMouseEvent *e){
    if(sphereDragging&&e->buttons()&Qt::LeftButton){const QPoint delta=(e->position()-sphereAnchor).toPoint();sphereAnchor=e->position();emit sphereOrbited(delta,e->modifiers().testFlag(Qt::ShiftModifier));return;}
    if(tapCandidate&&QLineF(e->position(),tapStart).length()>4)tapCandidate=false;
    if(paintingMask&&maskPainting&&e->buttons()&Qt::LeftButton){auto box=frameBox();emit maskBrushed(QPointF(qBound(0.,(e->position().x()-box.left())/box.width(),1.),qBound(0.,(e->position().y()-box.top())/box.height(),1.)),maskAdd,maskSize);return;}
    if(toolInteracting&&e->buttons()&Qt::LeftButton){auto box=frameBox();const QPointF point(qBound(0.,(e->position().x()-box.left())/box.width(),1.),qBound(0.,(e->position().y()-box.top())/box.height(),1.));toolPath<<point;if(RasterToolEngine::isStrokeTool(canvasTool))emit toolStroke(point,canvasTool,toolDiameter,e->modifiers());update();return;}
    if(panning&&e->buttons()&Qt::LeftButton){viewPan+=e->position()-panAnchor;panAnchor=e->position();update();return;}
    if(dragEdge<0)return;auto box=frameBox();auto m=draggingPadding?settings.padding:safeMargins;double x=(e->position().x()-box.left())/box.width(),y=(e->position().y()-box.top())/box.height();

    if(dragEdge==0)m.setLeft(std::clamp(x,0.,.45));if(dragEdge==1)m.setTop(std::clamp(y,0.,.45));if(dragEdge==2)m.setRight(std::clamp(1-x,0.,.45));if(dragEdge==3)m.setBottom(std::clamp(1-y,0.,.45));

    if(draggingPadding)settings.padding=m;else safeMargins=m;

    emit marginsDragged(safeMargins,settings.padding);update();

}

void PreviewWidget::mouseReleaseEvent(QMouseEvent *event){if(sphereDragging&&event->button()==Qt::LeftButton){sphereDragging=false;unsetCursor();return;}if(toolInteracting&&event->button()==Qt::LeftButton){toolInteracting=false;emit toolFinished(toolPath,canvasTool,event->modifiers());toolPath.clear();return;}const bool tapped=tapCandidate&&event->button()==Qt::LeftButton;const bool wasPanning=panning;tapCandidate=false;paintingMask=false;panning=false;dragEdge=-1;if(wasPanning&&mouseGrabber()==this)releaseMouse();if(!maskPainting)unsetCursor();if(tapped)emit canvasTapped();}

