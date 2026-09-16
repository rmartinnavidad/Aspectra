#pragma once

#include <QtWidgets>

#include "GalleryTile.h"

class GradientSlider;

class GalleryMosaicCanvas final : public QWidget {
    Q_OBJECT
public:
    explicit GalleryMosaicCanvas(QWidget *parent=nullptr);
signals:
    void dragStarted();
    void dragMoved(const QPoint &delta);
    void dragReleased(qreal horizontalVelocity);
protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
private:
    QPoint m_dragStart,m_lastPosition;
    QElapsedTimer m_dragClock;
    qreal m_releaseVelocity=0.0;
    bool m_dragging=false;
};

// Dedicated no-document workspace. The gallery owns the packing algorithm and
// coordinates cross-tile radial repulsion; GalleryTile owns its individual art.
class AspectraGalleryScreen final : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal globalZoomFactor READ globalZoomFactor WRITE setGlobalZoomFactor NOTIFY globalZoomFactorChanged)
public:
    enum class Presentation { FullWelcome, ProjectSelector };
    explicit AspectraGalleryScreen(Presentation presentation=Presentation::FullWelcome,QWidget *parent=nullptr);

    void setProjectPaths(const QStringList &paths);
    QStringList projectPaths() const { return m_projectPaths; }
    qreal globalZoomFactor() const { return m_globalZoomFactor; }
    void setGlobalZoomFactor(qreal factor);

signals:
    void openProjectRequested(const QString &path);
    void importProjectRequested();
    void globalZoomFactorChanged(qreal factor);

protected:
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    struct Span { int columns=1; int rows=1; };
    GalleryMosaicCanvas *m_mosaicCanvas=nullptr;
    QScrollArea *m_scrollArea=nullptr;
    GradientSlider *m_gallerySlider=nullptr;
    QLabel *m_emptyLabel=nullptr;
    QVector<GalleryTile*> m_tiles;
    QStringList m_projectPaths;
    QHash<QString,Span> m_spans;
    GalleryTile *m_hoveredTile=nullptr;
    bool m_repackQueued=false;
    Presentation m_presentation=Presentation::FullWelcome;
    QSize m_baseCanvasSize;
    qreal m_globalZoomFactor=1.0;
    qreal m_repulsionAmount=0.0;
    QVariantAnimation *m_zoomAnimation=nullptr;
    QVariantAnimation *m_repulsionAnimation=nullptr;
    QTimer m_kineticTimer;
    qreal m_kineticVelocity=0.0;

    void scheduleMosaicLayout();
    void layoutMosaic();
    Span randomSpan(bool hero,int availableColumns) const;
    void applyRadialRepulsion(GalleryTile *hovered);
    void clearRadialRepulsion(GalleryTile *leavingTile);
    void applyCameraGeometry();
    void animateCameraZoom(qreal target);
    void animateRepulsion(qreal target,const QEasingCurve &curve);
    void centerHoveredTile();
    void stopKineticMotion();
};
