#include "ModernUi.h"

namespace {
QPixmap dockGlyph(const QString &glyph, const QColor &color) {
    QPixmap pixmap(32,32); pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap); painter.setRenderHint(QPainter::Antialiasing);
    QFont font("Segoe UI Symbol"); font.setPointSize(19); font.setBold(true);
    painter.setFont(font); painter.setPen(color); painter.drawText(pixmap.rect(),Qt::AlignCenter,glyph);
    return pixmap;
}
QIcon whiteToolIcon(const QString &name){
    const QString root="C:/Users/rmart/Reign of Glory/blender/00 Addons/custom add ons/utilities/ASPECTRA/tools/";
    QImage image(root+name);if(image.isNull())return {};
    image=image.convertToFormat(QImage::Format_ARGB32);
    for(int y=0;y<image.height();++y){auto *pixels=reinterpret_cast<QRgb*>(image.scanLine(y));for(int x=0;x<image.width();++x)pixels[x]=qRgba(255,255,255,qAlpha(pixels[x]));}
    return QIcon(QPixmap::fromImage(image));
}
QPixmap whitePixmap(const QString &path, const QSize &size) {
    QImage image(path); if(image.isNull()) return {};
    image=image.convertToFormat(QImage::Format_ARGB32);
    for(int y=0;y<image.height();++y){auto *pixels=reinterpret_cast<QRgb*>(image.scanLine(y));for(int x=0;x<image.width();++x)pixels[x]=qRgba(255,255,255,qAlpha(pixels[x]));}
    return QPixmap::fromImage(image).scaled(size,Qt::KeepAspectRatio,Qt::SmoothTransformation);
}
}

TopBar::TopBar(QWidget *parent):QWidget(parent) {
    setObjectName("TopBar"); setFixedHeight(HeaderHeight);
    backButton=new QToolButton(this); backButton->setObjectName("BackButton"); backButton->setIcon(whiteToolIcon("back.png")); backButton->setIconSize(QSize(22,22)); backButton->setToolButtonStyle(Qt::ToolButtonIconOnly); backButton->setToolTip("Previous item"); backButton->setFixedSize(36,36);
    undoButton=new QToolButton(this);undoButton->setObjectName("UndoButton");undoButton->setIcon(whiteToolIcon("undo.png"));undoButton->setIconSize(QSize(20,20));undoButton->setToolButtonStyle(Qt::ToolButtonIconOnly);undoButton->setToolTip("Undo · Ctrl+Z / Ctrl+Alt+Z");undoButton->setFixedSize(36,36);
    redoButton=new QToolButton(this);redoButton->setObjectName("RedoButton");redoButton->setIcon(whiteToolIcon("redo.png"));redoButton->setIconSize(QSize(20,20));redoButton->setToolButtonStyle(Qt::ToolButtonIconOnly);redoButton->setToolTip("Redo · Ctrl+Shift+Z");redoButton->setFixedSize(36,36);
    titleLabel=new QLabel(this); titleLabel->setObjectName("TitleLabel"); titleLabel->setVisible(false);
    brandIcon=new QLabel(this);brandIcon->setObjectName("HeaderLogo");brandIcon->setPixmap(QPixmap(":/brand/wordmark.png").scaled(QSize(WordmarkWidth,30),Qt::KeepAspectRatio,Qt::SmoothTransformation));brandIcon->setAlignment(Qt::AlignCenter);brandIcon->setFixedSize(WordmarkWidth,WordmarkHeight);
    rightGroup=new QWidget(this); rightGroup->setObjectName("HeaderActions"); auto *rightLayout=new QHBoxLayout(rightGroup); rightLayout->setContentsMargins(0,0,0,0); rightLayout->setSpacing(8); rightLayout->addStretch(1);
    infoButton=new QToolButton(rightGroup); infoButton->setObjectName("InfoButton"); infoButton->setIcon(whiteToolIcon("info.png")); infoButton->setIconSize(QSize(22,22)); infoButton->setToolButtonStyle(Qt::ToolButtonIconOnly); infoButton->setToolTip("Current media information"); infoButton->setFixedSize(36,36); rightLayout->addWidget(infoButton);
    moreButton=new QToolButton(rightGroup); moreButton->setObjectName("MoreMenuButton"); moreButton->setIcon(whiteToolIcon("panel settings.png")); moreButton->setIconSize(QSize(19,19)); moreButton->setToolButtonStyle(Qt::ToolButtonIconOnly); moreButton->setToolTip("More actions"); moreButton->setFixedSize(36,36); rightLayout->addWidget(moreButton);
    exportButton=new QPushButton("Export",rightGroup); exportButton->setObjectName("TopExportButton"); exportButton->setIcon(whiteToolIcon("trim_save frame.png")); exportButton->setIconSize(QSize(17,17)); exportButton->setToolTip("Export all selected media"); exportButton->setFixedHeight(36); rightLayout->addWidget(exportButton);
    resizeEvent(nullptr);
}

void TopBar::resizeEvent(QResizeEvent *event) {
    if(backButton) backButton->move(0,qRound((height()-backButton->height())*.5));
    if(undoButton)undoButton->move(40,qRound((height()-undoButton->height())*.5));
    if(redoButton)redoButton->move(80,qRound((height()-redoButton->height())*.5));
    if(rightGroup) rightGroup->setGeometry(qMax(0,width()-ActionClusterWidth),0,ActionClusterWidth,height());
    if(brandIcon) brandIcon->move(qRound((width()-brandIcon->width())*.5),qRound((height()-brandIcon->height())*.5));
    if(event) QWidget::resizeEvent(event);
}

MainPreview::MainPreview(QWidget *content,QWidget *parent):QWidget(parent),previewContent(content) {
    setObjectName("MainPreview");
    // The stage is the canvas, not a framed card.  Giving its child the full
    // host rect restores the preview's intended large portrait footprint
    // without moving any of the rails or the bottom layer card.
    auto *layout=new QVBoxLayout(this); layout->setContentsMargins(0,0,0,0); layout->addWidget(content,1);
}

BottomDock::BottomDock(QWidget *parent):QWidget(parent) {
    setObjectName("BottomDock"); setFixedHeight(72);
    auto *layout=new QHBoxLayout(this); layout->setContentsMargins(16,4,16,4); layout->setSpacing(12);
    auto *scroll=new QScrollArea(this); scroll->setObjectName("DockScroll"); scroll->setFrameShape(QFrame::NoFrame); scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff); scroll->setWidgetResizable(true); layout->addWidget(scroll);
    auto *host=new QWidget; auto *buttons=new QHBoxLayout(host); buttons->setContentsMargins(0,0,0,0); buttons->setSpacing(12); scroll->setWidget(host);
    tools=new QButtonGroup(this); tools->setExclusive(true);
    const QStringList names{"Canvas","Adjust","Select","Trace","Effects","Video","Texture","Pattern","Export"};
    const QStringList glyphs{"▣","◐","⌁","✦","✧","▶","◉","▧","⇧"};
    for(int i=0;i<names.size();++i){auto *button=new QToolButton(host);button->setObjectName("DockButton");button->setText(names[i]);button->setIcon(QIcon(dockGlyph(glyphs[i],QColor("#b9ceff"))));button->setIconSize(QSize(32,32));button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);button->setCheckable(true);button->setToolTip(names[i]+" tools");button->setFixedSize(58,62);buttons->addWidget(button);tools->addButton(button,i);}
    tools->button(0)->setChecked(true); connect(tools,&QButtonGroup::idClicked,this,&BottomDock::toolActivated);
}
void BottomDock::setCurrentTool(int index){if(auto *button=tools->button(index))button->setChecked(true);}

InspectorPanel::InspectorPanel(QWidget *parent):QFrame(parent) {
    setObjectName("InspectorPanel"); setFrameShape(QFrame::NoFrame); setVisible(false); if(parent)parent->installEventFilter(this);
    auto *layout=new QVBoxLayout(this);layout->setContentsMargins(0,0,0,0);stack=new QTabWidget(this);stack->setObjectName("InspectorPages");stack->tabBar()->hide();layout->addWidget(stack);
    animation=new QPropertyAnimation(this,"panelHeight",this);animation->setDuration(250);animation->setEasingCurve(QEasingCurve::OutCubic);connect(animation,&QPropertyAnimation::finished,this,[this]{if(currentHeight==0)hide();});
}
void InspectorPanel::place(){if(!parentWidget())return;setGeometry(0,qMax(0,parentWidget()->height()-currentHeight),parentWidget()->width(),currentHeight);}
void InspectorPanel::setPanelHeight(int height){currentHeight=qMax(0,height);place();}
bool InspectorPanel::eventFilter(QObject *watched,QEvent *event){if(watched==parentWidget()&&event->type()==QEvent::Resize)place();return QFrame::eventFilter(watched,event);}
void InspectorPanel::openPage(int index){stack->setCurrentIndex(index);show();raise();animation->stop();const int maximum=qMax(360,qRound(parentWidget()->height()*.85));animation->setStartValue(currentHeight);animation->setEndValue(maximum);animation->start();}
void InspectorPanel::closePanel(){animation->stop();animation->setStartValue(currentHeight);animation->setEndValue(0);animation->start();}
bool InspectorPanel::isOpen() const{return currentHeight>0;}

FilmstripPanel::FilmstripPanel(QWidget *parent):QFrame(parent) {
    setObjectName("FilmstripPanel");setFrameShape(QFrame::NoFrame);setFixedHeight(72);
    auto *layout=new QHBoxLayout(this);layout->setContentsMargins(12,8,12,8);layout->setSpacing(12);
    previousButton=new QPushButton("‹",this);previousButton->setObjectName("FilmstripArrow");previousButton->setFixedSize(40,48);layout->addWidget(previousButton);
    selector=new QComboBox(this);selector->setObjectName("FilmstripSelector");selector->setSizeAdjustPolicy(QComboBox::AdjustToContents);selector->setMinimumHeight(48);layout->addWidget(selector,1);
    nextButton=new QPushButton("›",this);nextButton->setObjectName("FilmstripArrow");nextButton->setFixedSize(40,48);layout->addWidget(nextButton);
    countLabel=new QLabel("0 / 0",this);countLabel->setObjectName("FilmstripCount");countLabel->setMinimumWidth(44);countLabel->setAlignment(Qt::AlignCenter);layout->addWidget(countLabel);
}
