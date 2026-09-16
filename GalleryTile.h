#pragma once

#include <QtWidgets>

// A self-painting project tile with a procedural, continuously moving 2.5D
// thumbnail surface. The tile owns its geometry and visual animation.
class GalleryTile final : public QWidget {
    Q_OBJECT
public:
    explicit GalleryTile(QString projectPath, QWidget *parent=nullptr);

    QString projectPath() const { return m_projectPath; }
    QRect baseGeometry() const { return m_baseGeometry; }
    qreal timeScale() const { return m_timeScale; }

    void setBaseGeometry(const QRect &geometry, bool animate=false);
    void animateTo(const QRect &geometry, const QEasingCurve &curve);

signals:
    void hoverStarted(GalleryTile *tile);
    void hoverEnded(GalleryTile *tile);
    void openProjectRequested(const QString &path);

protected:
    void paintEvent(QPaintEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    QString m_projectPath;
    QImage m_thumbnail;
    QRect m_baseGeometry;
    QVariantAnimation *m_geometryAnimation=nullptr;
    QVariantAnimation *m_hoverAnimation=nullptr;
    QTimer m_motionTimer;
    qreal m_timeScale=0.0;
    qreal m_hoverIntensity=0.0;
    bool m_hovered=false;

    void loadThumbnail();
    void paintThumbnail(QPainter &painter, const QRectF &bounds) const;
    QTransform perspectiveTransform(const QRectF &bounds) const;
    QColor accentColor() const;
    QString displayName() const;
    QString displayType() const;
};
