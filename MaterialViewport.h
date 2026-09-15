#pragma once
#include <QtWidgets>
#include "ModelImporter.h"
class MaterialViewport : public QWidget {
    Q_OBJECT
public:
    explicit MaterialViewport(QWidget *parent=nullptr);
    void setMaterial(const QImage &image); void setLighting(int azimuth,int elevation,int intensity,int ambient); void setModelTransform(double scale,double uvScale,double uvX,double uvY);
    void setMesh(const ModelAsset *asset);
    QImage renderPreview(QSize size) const;
    void orbitBy(QPoint delta,bool pan); void zoomBy(int wheelDelta);
protected:
    void paintEvent(QPaintEvent *) override; void mousePressEvent(QMouseEvent *) override; void mouseMoveEvent(QMouseEvent *) override; void mouseReleaseEvent(QMouseEvent *) override; void wheelEvent(QWheelEvent *) override;
private:
    void scheduleHighQuality();
    QImage material; const ModelAsset *meshAsset=nullptr; QString meshSourcePath; QVector3D meshCenter; double meshRadius=1.; QPoint last; QTimer highQualityTimer; bool interactionActive=false; double yaw=0,pitch=0,distance=2.5,panX=0,panY=0,modelScale=1.,uvScale=1.,uvX=0.,uvY=0.; int lightAzimuth=35,lightElevation=45,lightIntensity=100,ambient=20;
};
