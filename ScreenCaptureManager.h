#pragma once
#include <QtWidgets>
class ScreenCaptureManager : public QWidget {
    Q_OBJECT
public:
    explicit ScreenCaptureManager(bool record,QWidget *parent=nullptr,bool windowSelection=false);
    static QImage grabArea(QRect globalRect);
signals:
    void captured(QImage image);
    void areaSelected(QRect globalRect);
    void cancelled();
protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
private:
    QPixmap desktop;
    QPoint anchor, cursor;
    QRect virtualRect;
    bool dragging=false, recording=false;
    bool windowSelection=false;
    QRect hoverWindow;
    qreal scale=1;
};
