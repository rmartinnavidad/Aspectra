#pragma once

#include <QtWidgets>

#include "GalleryTile.h"

// Dedicated no-document workspace. The gallery owns the packing algorithm and
// coordinates cross-tile radial repulsion; GalleryTile owns its individual art.
class AspectraGalleryScreen final : public QWidget {
    Q_OBJECT
public:
    enum class Presentation { FullWelcome, ProjectSelector };
    explicit AspectraGalleryScreen(Presentation presentation=Presentation::FullWelcome,QWidget *parent=nullptr);

    void setProjectPaths(const QStringList &paths);
    QStringList projectPaths() const { return m_projectPaths; }

signals:
    void openProjectRequested(const QString &path);
    void importProjectRequested();

protected:
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    struct Span { int columns=1; int rows=1; };
    QWidget *m_mosaicCanvas=nullptr;
    QScrollArea *m_scrollArea=nullptr;
    QLabel *m_emptyLabel=nullptr;
    QVector<GalleryTile*> m_tiles;
    QStringList m_projectPaths;
    QHash<QString,Span> m_spans;
    GalleryTile *m_hoveredTile=nullptr;
    bool m_repackQueued=false;
    Presentation m_presentation=Presentation::FullWelcome;

    void scheduleMosaicLayout();
    void layoutMosaic();
    Span randomSpan(bool hero,int availableColumns) const;
    void applyRadialRepulsion(GalleryTile *hovered);
    void clearRadialRepulsion(GalleryTile *leavingTile);
};
