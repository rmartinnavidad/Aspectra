#include "ScreenCaptureManager.h"
#ifdef Q_OS_WIN
#define NOMINMAX
#include <windows.h>
#include <dwmapi.h>
#endif
QImage ScreenCaptureManager::grabArea(QRect area){
    if(area.isEmpty())return {};
    // Compose in logical pixels. Mixing a virtual desktop rectangle with one
    // monitor's device-pixel ratio was unstable on mixed-DPI Windows setups.
    QImage result(area.size(),QImage::Format_ARGB32_Premultiplied);result.fill(Qt::transparent);QPainter painter(&result);
    for(auto *screen:QGuiApplication::screens()){
        const QRect part=area.intersected(screen->geometry());if(part.isEmpty())continue;
        const QPixmap shot=screen->grabWindow(0);if(shot.isNull())continue;
        const QRect geometry=screen->geometry();
        const qreal sx=qreal(shot.width())/qMax(1,geometry.width()),sy=qreal(shot.height())/qMax(1,geometry.height());
        const QRectF source((part.x()-geometry.x())*sx,(part.y()-geometry.y())*sy,part.width()*sx,part.height()*sy);
        painter.drawPixmap(QRectF(part.translated(-area.topLeft())),shot,source);
    }
    return result;
}
ScreenCaptureManager::ScreenCaptureManager(bool record,QWidget *parent,bool selectWindow):QWidget(parent),recording(record),windowSelection(selectWindow){
    setAttribute(Qt::WA_DeleteOnClose);setWindowFlags(Qt::FramelessWindowHint|Qt::WindowStaysOnTopHint|Qt::Tool);setCursor(Qt::CrossCursor);setMouseTracking(true);
    for(auto *screen:QGuiApplication::screens()){virtualRect=virtualRect.united(screen->geometry());scale=std::max(scale,screen->devicePixelRatio());}
    desktop=QPixmap::fromImage(grabArea(virtualRect));setGeometry(virtualRect);show();raise();activateWindow();
}
void ScreenCaptureManager::paintEvent(QPaintEvent *){
    QPainter p(this);p.setRenderHint(QPainter::Antialiasing);p.drawPixmap(0,0,desktop);p.fillRect(rect(),QColor(0,0,0,150));QRect selection=windowSelection?hoverWindow.translated(-virtualRect.topLeft()):QRect(anchor,cursor).normalized();
    if(dragging||windowSelection){p.save();p.setClipRect(selection);p.drawPixmap(0,0,desktop);p.restore();p.fillRect(selection,QColor(61,145,251,24));p.setPen(QPen(QColor("#57a6ff"),2));p.drawRect(selection.adjusted(1,1,-1,-1));p.setPen(Qt::white);p.drawText(selection.topLeft()+QPoint(7,-10),QString("%1 × %2 px").arg(qRound(selection.width()*scale)).arg(qRound(selection.height()*scale)));}
    const QString instruction=windowSelection?"WINDOW  ·  click a window":recording?"RECORDING  ·  drag a region":"SCREENSHOT  ·  drag a region";const QString help="Esc cancel";const QSize panelSize(360,54);const QPoint panelTop((width()-panelSize.width())/2,28);p.setPen(Qt::NoPen);p.setBrush(QColor("#18202d"));p.drawRoundedRect(QRect(panelTop,panelSize),8,8);p.setPen(QColor("#dfeeff"));QFont title=p.font();title.setBold(true);title.setPointSize(10);p.setFont(title);p.drawText(QRect(panelTop+QPoint(16,7),QSize(230,20)),Qt::AlignVCenter,"ASPECTRA CAPTURE · "+instruction);QFont hint=p.font();hint.setBold(false);hint.setPointSize(9);p.setFont(hint);p.setPen(QColor("#9db0c9"));p.drawText(QRect(panelTop+QPoint(16,27),QSize(230,20)),Qt::AlignVCenter,"Release to confirm selection");p.setPen(QColor("#57a6ff"));p.drawText(QRect(panelTop+QPoint(260,0),QSize(84,54)),Qt::AlignVCenter|Qt::AlignRight,help);
}
void ScreenCaptureManager::mousePressEvent(QMouseEvent *e){if(e->button()==Qt::LeftButton){anchor=cursor=e->position().toPoint();dragging=true;update();}}
void ScreenCaptureManager::mouseMoveEvent(QMouseEvent *e){cursor=e->position().toPoint();
#ifdef Q_OS_WIN
    if(windowSelection){QPoint logical=e->globalPosition().toPoint();QScreen *screen=QGuiApplication::screenAt(logical);if(screen){double dpr=screen->devicePixelRatio();QPoint origin=screen->geometry().topLeft();DEVMODEW device{};device.dmSize=sizeof(device);if(EnumDisplaySettingsW(reinterpret_cast<LPCWSTR>(screen->name().utf16()),ENUM_CURRENT_SETTINGS,&device))origin={device.dmPosition.x,device.dmPosition.y};
        struct Search{POINT point;RECT rect{};DWORD own;bool found=false;};Search search{{origin.x()+qRound((logical.x()-screen->geometry().x())*dpr),origin.y()+qRound((logical.y()-screen->geometry().y())*dpr)},{},GetCurrentProcessId(),false};
        EnumWindows([](HWND window,LPARAM data)->BOOL{auto *s=reinterpret_cast<Search*>(data);DWORD process=0;GetWindowThreadProcessId(window,&process);if(process==s->own||!IsWindowVisible(window)||IsIconic(window))return TRUE;RECT r{};GetWindowRect(window,&r);if(PtInRect(&r,s->point)){s->rect=r;s->found=true;return FALSE;}return TRUE;},reinterpret_cast<LPARAM>(&search));
        if(search.found)hoverWindow=QRect(screen->geometry().x()+qRound((search.rect.left-origin.x())/dpr),screen->geometry().y()+qRound((search.rect.top-origin.y())/dpr),qRound((search.rect.right-search.rect.left)/dpr),qRound((search.rect.bottom-search.rect.top)/dpr));}}
#endif
    update();
}
void ScreenCaptureManager::mouseReleaseEvent(QMouseEvent *e){if(e->button()!=Qt::LeftButton||!dragging)return;QRect area=windowSelection?hoverWindow.translated(-virtualRect.topLeft()).intersected(rect()):QRect(anchor,e->position().toPoint()).normalized().intersected(rect());if(area.width()<4||area.height()<4){emit cancelled();close();return;}
    emit areaSelected(area.translated(virtualRect.topLeft()));if(!recording){QImage result=desktop.copy(area).toImage();result.setDevicePixelRatio(1);if(result.isNull()){emit cancelled();}else emit captured(result);}close();
}
void ScreenCaptureManager::keyPressEvent(QKeyEvent *e){if(e->key()==Qt::Key_Escape){emit cancelled();close();}}
