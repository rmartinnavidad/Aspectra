#pragma once

#include <QtWidgets>

// A self-painting project tile. It owns the visual glass treatment, hover
// geometry animation, and procedural Ken Burns thumbnail animation.
class GalleryTile final : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal kenBurnsProgress READ kenBurnsProgress WRITE setKenBurnsProgress NOTIFY kenBurnsProgressChanged)
public:
    explicit GalleryTile(QString projectPath, QWidget *parent=nullptr);

    QString projectPath() const { return m_projectPath; }
    QRect baseGeometry() const { return m_baseGeometry; }
    qreal kenBurnsProgress() const { return m_kenBurnsProgress; }

    void setBaseGeometry(const QRect &geometry, bool animate=false);
    void animateTo(const QRect &geometry, const QEasingCurve &curve);
    void setKenBurnsProgress(qreal progress);

signals:
    void hoverStarted(GalleryTile *tile);
    void hoverEnded(GalleryTile *tile);
    void openProjectRequested(const QString &path);
    void kenBurnsProgressChanged(qreal progress);

protected:
    void paintEvent(QPaintEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    QString m_projectPath;
    QImage m_thumbnail;
    QRect m_baseGeometry;
    QVector<QPointF> m_focalPoints;
    QVariantAnimation *m_geometryAnimation=nullptr;
    QVariantAnimation *m_kenBurnsAnimation=nullptr;
    qreal m_kenBurnsProgress=0.0;
    bool m_hovered=false;

    void loadThumbnail();
    void buildFocalPoints();
    void paintThumbnail(QPainter &painter, const QRectF &bounds) const;
    QColor accentColor() const;
};
