#include "MainWindow.h"
#include "GradientSlider.h"
#define QSlider(...) GradientSlider(__VA_ARGS__)
#include "ScreenCaptureManager.h"
#include "AnnotationEditor.h"
#include <QMediaDevices>
#include <QAudioDevice>
#include <algorithm>
#include <cmath>
#ifdef Q_OS_WIN
#define NOMINMAX
#include <windows.h>
#endif
namespace {
const QString kToolIconRoot="C:/Users/rmart/Reign of Glory/blender/00 Addons/custom add ons/utilities/ASPECTRA/tools/";
QIcon actionIcon(const QString &text){
    const QString label=text.toLower();
    QString file;
    if(label.contains("video")) file="video.png";
    else if(label.contains("image")) file="frame tool.png";
    else if(label.contains("text")) file="warped text.png";
    else if(label.contains("effect")) file="brush tool.png";
    else if(label.contains("keyframe")) file="trim_save frame.png";
    else if(label.contains("composition")) file="artboard tool.png";
    else if(label.contains("remove")||label.contains("delete")) file="delete anchor point tool.png";
    else if(label.contains("full screen")) file="video editor_full screen.png";
    else if(label.contains("play")) file="playback_play.png";
    else if(label.contains("capture")) file="crop tool.png";
    else if(label.contains("annotate")) file="brush tool.png";
    else if(label.contains("save")||label.contains("export")) file="trim_save frame.png";
    else if(label.contains("rotate")) file="video editor_rotate.png";
    else if(label.contains("smaller")||label.contains("balanced")||label.contains("fastest")) file="panel settings.png";
    else if(label.contains("up")||label.contains("down")) file="move tool.png";
    if(file.isEmpty()) return {};
    QImage image(kToolIconRoot+file);
    if(image.isNull()) return {};
    image=image.convertToFormat(QImage::Format_ARGB32);
    for(int y=0;y<image.height();++y){auto *pixels=reinterpret_cast<QRgb*>(image.scanLine(y));for(int x=0;x<image.width();++x)pixels[x]=qRgba(255,255,255,qAlpha(pixels[x]));}
    return QIcon(QPixmap::fromImage(image));
}
QPushButton *action(QString text,QBoxLayout *layout,QString tone={}){
    auto *b=new QPushButton(text);
    if(!tone.isEmpty())b->setProperty("tone",tone);
    const QIcon icon=actionIcon(text);if(!icon.isNull()){b->setIcon(icon);b->setIconSize(QSize(17,17));}
    b->setToolTip(text);
    layout->addWidget(b);return b;
}
QHBoxLayout *line(QVBoxLayout *v){auto *h=new QHBoxLayout;h->setSpacing(6);v->addLayout(h);return h;}
QSpinBox *integer(int minimum,int maximum,int value){auto *n=new QSpinBox;n->setRange(minimum,maximum);n->setValue(value);return n;}
QVBoxLayout *inspector(QTabWidget *tabs,int index){auto *area=qobject_cast<QScrollArea*>(tabs->widget(index));return qobject_cast<QVBoxLayout*>(area->widget()->layout());}
bool isGif(QString path){return QFileInfo(path).suffix().compare("gif",Qt::CaseInsensitive)==0;}
QString timeCode(double seconds){int minutes=int(seconds)/60;return QString("%1:%2").arg(minutes,2,10,QChar('0')).arg(seconds-minutes*60,6,'f',3,QChar('0'));}
QColor pickCharacterColor(QWidget *parent,const QString &title,QColor initial){
    QDialog dialog(parent);dialog.setWindowTitle(title);dialog.setModal(true);dialog.setObjectName("CharacterColorPicker");auto *layout=new QVBoxLayout(&dialog);layout->setContentsMargins(16,14,16,14);layout->setSpacing(8);
    auto *palette=new QWidget;auto *paletteRow=new QHBoxLayout(palette);paletteRow->setContentsMargins(0,0,0,0);paletteRow->setSpacing(5);const QList<QColor> quick{QString("#000000"),QString("#ffffff"),QString("#e53935"),QString("#fb8c00"),QString("#fdd835"),QString("#43a047"),QString("#00acc1"),QString("#1e88e5"),QString("#5e35b1"),QString("#d81b60"),QString("#795548"),QString("#90a4ae")};for(const QColor &shade:quick){auto *chip=new QToolButton;chip->setFixedSize(22,22);chip->setProperty("swatchColor",shade);chip->setStyleSheet("background:"+shade.name()+"; border-radius:6px;");paletteRow->addWidget(chip);}layout->addWidget(palette,0,Qt::AlignHCenter);
    auto *picker=new QToolButton;picker->setObjectName("ColorPickerSurface");picker->setText("COLOR PICKER");picker->setFixedHeight(92);picker->setToolTip("Color field · click a swatch or use the precision controls below");picker->setStyleSheet("QToolButton { border-radius:12px; background:qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 #ffffff, stop:.42 #ff4d8d, stop:1 #2400ff); color:#ffffff; font-weight:700; }");layout->addWidget(picker);
    auto *hueRow=new QHBoxLayout;hueRow->addWidget(new QLabel("Hue"));auto *hue=new GradientSlider;hue->setRange(0,359);hueRow->addWidget(hue,1);layout->addLayout(hueRow);
    QHash<QString,QSpinBox*> fields;QHash<QString,QSlider*> sliders;auto makeGroup=[&](const QString &heading,const QStringList &keys,const QList<QPair<int,int>> &ranges){auto *box=new QWidget;auto *boxLayout=new QVBoxLayout(box);boxLayout->setContentsMargins(0,1,0,1);boxLayout->setSpacing(4);auto *caption=new QLabel(heading);caption->setObjectName("muted");boxLayout->addWidget(caption);for(int i=0;i<keys.size();++i){auto *row=new QHBoxLayout;row->setContentsMargins(0,0,0,0);auto *label=new QLabel(keys[i]);label->setFixedWidth(46);label->setAlignment(Qt::AlignRight|Qt::AlignVCenter);auto *slider=new GradientSlider;slider->setRange(ranges[i].first,ranges[i].second);auto *spin=new QSpinBox;spin->setRange(ranges[i].first,ranges[i].second);spin->setAlignment(Qt::AlignCenter);spin->setFixedWidth(58);spin->setObjectName("Color"+keys[i]);row->addWidget(label);row->addWidget(slider,1);row->addWidget(spin);boxLayout->addLayout(row);fields.insert(keys[i],spin);sliders.insert(keys[i],slider);QObject::connect(slider,&QSlider::valueChanged,spin,[spin](int value){QSignalBlocker guard(spin);spin->setValue(value);});QObject::connect(spin,qOverload<int>(&QSpinBox::valueChanged),slider,[slider](int value){QSignalBlocker guard(slider);slider->setValue(value);});}layout->addWidget(box);};
    makeGroup("RGB",{"R","G","B"},{{0,255},{0,255},{0,255}});makeGroup("CMYK",{"C","M","Y","K"},{{0,100},{0,100},{0,100},{0,100}});makeGroup("LAB",{"Lab L","Lab A","Lab B"},{{0,100},{-128,127},{-128,127}});makeGroup("HSB",{"H","S","Bv"},{{0,359},{0,100},{0,100}});
    auto *hexRow=new QHBoxLayout;hexRow->addWidget(new QLabel("HEX"));auto *hex=new QLineEdit;hex->setMaxLength(9);hex->setAlignment(Qt::AlignCenter);hex->setPlaceholderText("#RRGGBB");hexRow->addWidget(hex,1);layout->addLayout(hexRow);auto *currentRow=new QHBoxLayout;auto *current=new QLabel("CURRENT");auto *next=new QLabel("NEW");for(auto *view:{current,next}){view->setAlignment(Qt::AlignCenter);view->setFixedHeight(30);view->setStyleSheet("border-radius:8px;");}currentRow->addWidget(current);currentRow->addWidget(next);layout->addLayout(currentRow);
    bool syncing=false;QColor color=initial.isValid()?initial:Qt::white;
    auto update=[&]{if(syncing)return;syncing=true;const int r=color.red(),g=color.green(),b=color.blue();fields["R"]->setValue(r);fields["G"]->setValue(g);fields["B"]->setValue(b);sliders["R"]->setValue(r);sliders["G"]->setValue(g);sliders["B"]->setValue(b);int h,s,v,a;color.getHsv(&h,&s,&v,&a);fields["H"]->setValue(qMax(0,h));fields["S"]->setValue(qRound(s*100./255));fields["Bv"]->setValue(qRound(v*100./255));sliders["H"]->setValue(qMax(0,h));sliders["S"]->setValue(qRound(s*100./255));sliders["Bv"]->setValue(qRound(v*100./255));hue->setValue(qMax(0,h));int c,m,y,k;color.getCmyk(&c,&m,&y,&k);fields["C"]->setValue(qRound(c*100./255));fields["M"]->setValue(qRound(m*100./255));fields["Y"]->setValue(qRound(y*100./255));fields["K"]->setValue(qRound(k*100./255));auto lin=[](double value){value/=255.;return value<=.04045?value/12.92:std::pow((value+.055)/1.055,2.4);};double rr=lin(r),gg=lin(g),bb=lin(b),x=(rr*.4124+gg*.3576+bb*.1805)/.95047,z=(rr*.0193+gg*.1192+bb*.9505)/1.08883,yy=rr*.2126+gg*.7152+bb*.0722;auto f=[](double value){return value>.008856?std::cbrt(value):7.787*value+16./116.;};const double fx=f(x),fy=f(yy),fz=f(z);fields["Lab L"]->setValue(qRound(116*fy-16));fields["Lab A"]->setValue(qRound(500*(fx-fy)));fields["Lab B"]->setValue(qRound(200*(fy-fz)));hex->setText(color.name(QColor::HexRgb).toUpper());picker->setStyleSheet("QToolButton { border-radius:12px; background:"+color.name()+"; color:"+(color.lightness()>140?QString("#000000"):QString("#ffffff"))+"; font-weight:700; }");next->setStyleSheet("background:"+color.name()+"; border-radius:8px;");current->setStyleSheet("background:"+initial.name()+"; border-radius:8px;");syncing=false;};
    auto setRgb=[&](int r,int g,int b){if(syncing)return;color=QColor(qBound(0,r,255),qBound(0,g,255),qBound(0,b,255));update();};
    for(const auto &key:QStringList{"R","G","B"})QObject::connect(fields[key],qOverload<int>(&QSpinBox::valueChanged),&dialog,[&]{setRgb(fields["R"]->value(),fields["G"]->value(),fields["B"]->value());});
    for(const auto &chip:palette->findChildren<QToolButton*>())QObject::connect(chip,&QToolButton::clicked,&dialog,[&,chip]{color=chip->property("swatchColor").value<QColor>();update();});
    for(const auto &key:QStringList{"H","S","Bv"})QObject::connect(fields[key],qOverload<int>(&QSpinBox::valueChanged),&dialog,[&]{if(syncing)return;color=QColor::fromHsv(fields["H"]->value(),qRound(fields["S"]->value()*2.55),qRound(fields["Bv"]->value()*2.55));update();});
    for(const auto &key:QStringList{"C","M","Y","K"})QObject::connect(fields[key],qOverload<int>(&QSpinBox::valueChanged),&dialog,[&]{if(syncing)return;color=QColor::fromCmyk(qRound(fields["C"]->value()*2.55),qRound(fields["M"]->value()*2.55),qRound(fields["Y"]->value()*2.55),qRound(fields["K"]->value()*2.55));update();});
    for(const auto &key:QStringList{"Lab L","Lab A","Lab B"})QObject::connect(fields[key],qOverload<int>(&QSpinBox::valueChanged),&dialog,[&]{if(syncing)return;const double fy=(fields["Lab L"]->value()+16.)/116.,fx=fields["Lab A"]->value()/500.+fy,fz=fy-fields["Lab B"]->value()/200.;auto inv=[](double n){const double cube=n*n*n;return cube>.008856?cube:(n-16./116.)/7.787;};const double x=.95047*inv(fx),y=inv(fy),z=1.08883*inv(fz);auto gamma=[](double n){n=n<=.0031308?12.92*n:1.055*std::pow(n,1./2.4)-.055;return qBound(0,qRound(n*255),255);};color=QColor(gamma(3.2406*x-1.5372*y-.4986*z),gamma(-.9689*x+1.8758*y+.0415*z),gamma(.0557*x-.204*y+1.057*z));update();});
    QObject::connect(hue,&QSlider::valueChanged,&dialog,[&](int value){if(syncing)return;int h,s,v,a;color.getHsv(&h,&s,&v,&a);color=QColor::fromHsv(value,s,v,a);update();});QObject::connect(hex,&QLineEdit::editingFinished,&dialog,[&]{if(syncing)return;QColor next(hex->text());if(next.isValid()){color=next;update();}});
    auto *buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);layout->addWidget(buttons);QObject::connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);QObject::connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);update();return dialog.exec()==QDialog::Accepted?color:QColor{};
}
}
void MainWindow::buildPowerFeatures(){
    QTimer::singleShot(0,this,[this]{const QString root="C:/Users/rmart/Reign of Glory/blender/00 Addons/custom add ons/utilities/ASPECTRA/tools/";const QStringList corrected{"icon_canvas.png","editor_adjustments.png","editor_select all.png","pen tool.png","editor_glow.png","video.png","icon_textures.png","icon_patterns.png","trim_save frame.png"};auto white=[](const QString &path){QImage image(path);if(image.isNull())return QIcon{};image=image.convertToFormat(QImage::Format_ARGB32);for(int y=0;y<image.height();++y){auto *pixels=reinterpret_cast<QRgb*>(image.scanLine(y));for(int x=0;x<image.width();++x)pixels[x]=qRgba(255,255,255,qAlpha(pixels[x]));}return QIcon(QPixmap::fromImage(image));};QList<QToolButton*> rail;for(auto *button:tabCarousel->findChildren<QToolButton*>("CarouselTab"))if(button->toolTip().endsWith("settings"))rail<<button;for(int i=0;i<qMin(rail.size(),corrected.size());++i)rail[i]->setIcon(white(root+corrected[i]));});
    auto hookSubRail=[this]{if(auto *rail=findChild<QScrollArea*>("SubSettingsRail")){rail->setWidgetResizable(false);rail->viewport()->setObjectName("SubSettingsViewport");rail->viewport()->installEventFilter(this);if(auto *content=rail->widget()){content->adjustSize();content->setMinimumWidth(qMax(rail->viewport()->width(),content->sizeHint().width()));}for(auto *icon:rail->findChildren<QToolButton*>("SubRailIcon"))icon->installEventFilter(this);}};QTimer::singleShot(0,this,hookSubRail);connect(tabs,&QTabWidget::currentChanged,this,[this,hookSubRail](int){QTimer::singleShot(0,this,hookSubRail);});
    // Every horizontally moving rail uses the same soft edge treatment.  The
    // content stays scrollable, but it never ends in a hard visual cut.
    auto addRailFades=[](QScrollArea *rail){if(!rail||rail->property("edgeFades").toBool())return;rail->setProperty("edgeFades",true);auto *left=new QWidget(rail->viewport()),*right=new QWidget(rail->viewport());for(auto *fade:{left,right}){fade->setAttribute(Qt::WA_TransparentForMouseEvents);fade->raise();}left->setStyleSheet("background:qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 #000000,stop:.60 rgba(0,0,0,215),stop:1 rgba(0,0,0,0));");right->setStyleSheet("background:qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 rgba(0,0,0,0),stop:.40 rgba(0,0,0,215),stop:1 #000000);");auto update=[rail,left,right]{auto *view=rail->viewport();left->setGeometry(0,0,38,view->height());right->setGeometry(view->width()-38,0,38,view->height());auto *bar=rail->horizontalScrollBar();left->setVisible(bar->value()>0);right->setVisible(bar->value()<bar->maximum());left->raise();right->raise();};connect(rail->horizontalScrollBar(),&QScrollBar::valueChanged,rail,[update](int){update();});QTimer::singleShot(0,rail,update);};QTimer::singleShot(0,this,[this,addRailFades]{addRailFades(findChild<QScrollArea*>("SubSettingsRail"));});
    gif=new QMovie(this);gif->setCacheMode(QMovie::CacheAll);
    connect(gif,&QMovie::frameChanged,this,[this](int frame){if(!isGif(currentFile))return;deliverFrame(gif->currentImage());double pos=gifTimes.value(frame)/1000.;timeline->setPosition(pos);timeLabel->setText(QString::number(pos,'f',2)+" / "+QString::number(media.duration,'f',2)+" s · GIF");if(rangePlayback&&pos>=outMarker->value()){rangePlayback=false;gif->setPaused(true);}});
    connect(gif,&QMovie::stateChanged,this,[this](QMovie::MovieState state){if(isGif(currentFile))playButton->setText(state==QMovie::Running?"Ⅱ Pause":"▶ Play");});
    connect(gif,qOverload<QImageReader::ImageReaderError>(&QMovie::error),this,[this](QImageReader::ImageReaderError){status->setText("GIF preview error: "+gif->lastErrorString());});
    connect(preview,&PreviewWidget::fullScreenRequested,this,&MainWindow::fullScreenPlayer);auto *colorTabs=qobject_cast<QTabWidget*>(tabs->widget(1));if(colorTabs&&colorTabs->count()>4){auto *keyLayout=qobject_cast<QVBoxLayout*>(colorTabs->widget(4)->layout());auto addKeyControl=[&](QString title,QSlider **target,int value,QString tip){auto *line=new QHBoxLayout;line->addWidget(new QLabel(title));*target=new QSlider(Qt::Horizontal);(*target)->setRange(0,100);(*target)->setValue(value);(*target)->setToolTip(tip);line->addWidget(*target,1);keyLayout->insertLayout(qMax(0,keyLayout->count()-1),line);connect(*target,&QSlider::valueChanged,this,&MainWindow::refresh);};addKeyControl("Green spill suppression",&keyDespillSlider,0,"Neutralizes residual key-color spill only on partially transparent edge pixels.");addKeyControl("Protect dark subject detail",&keyLumaProtectSlider,0,"Prevents dark subject regions from being removed by the color key.");auto *eye=new QPushButton("Eyedropper: click main preview");eye->setToolTip("Activates the native in-preview color sampler. Click the background color in the main preview.");keyLayout->insertWidget(qMax(0,keyLayout->count()-1),eye);connect(eye,&QPushButton::clicked,this,[this]{preview->setEyedropper(true);status->setText("Eyedropper active · click the background in the main preview");});connect(preview,&PreviewWidget::colorSampled,this,[this](QColor color){layerStyle.keyColor=color;chromaKeyEnabled->setChecked(true);status->setText("Key color sampled from preview");refresh();});}
    auto *frame=inspector(tabs,0);auto *top=new QHBoxLayout;frame->insertLayout(0,top);followSource=new QCheckBox("Use media dimensions");followSource->setChecked(true);top->addWidget(followSource);auto *left=action("↶ 90°",top),*right=action("↷ 90°",top),*annotate=action("Annotate…",top);connect(left,&QPushButton::clicked,this,[this]{rotateMedia(-90);});connect(right,&QPushButton::clicked,this,[this]{rotateMedia(90);});connect(annotate,&QPushButton::clicked,this,&MainWindow::annotateMedia);
    connect(followSource,&QCheckBox::toggled,this,[this](bool on){if(on&&!original.isNull()){QSize native=media.size;if(layerStyle.rotation%180)native.transpose();lockedRatio=double(native.width())/native.height();setDimensions(native.width(),native.height());}});
    auto *units=new QHBoxLayout;frame->insertLayout(1,units);units->addWidget(new QLabel("Guide & padding units"));marginUnits=new QComboBox;marginUnits->addItems({"% of output canvas","Output pixels"});units->addWidget(marginUnits);connect(marginUnits,&QComboBox::currentIndexChanged,this,&MainWindow::changeMarginUnits);
    auto *layerStack=new QListWidget;layerStack->setObjectName("LayerStack");layerStack->setMinimumHeight(104);layerStack->setToolTip("Layer order. Higher rows render over lower rows.");auto refreshLayers=[this,layerStack]{const int selected=layerStack->currentRow();QSignalBlocker block(layerStack);layerStack->clear();auto *base=new QListWidgetItem("Background · "+QFileInfo(currentFile).fileName(),layerStack);base->setData(Qt::UserRole,-1);base->setFlags(base->flags()&~Qt::ItemIsDragEnabled);for(int i=0;i<timelineTracks.size();++i){const auto &track=timelineTracks[i];if(track.type==TimelineTrack::Image||track.type==TimelineTrack::Text){auto *item=new QListWidgetItem((track.type==TimelineTrack::Image?"Image · ":"Text · ")+track.name,layerStack);item->setData(Qt::UserRole,i);}}layerStack->setCurrentRow(qBound(0,selected,layerStack->count()-1));};auto *layerHeader=new QHBoxLayout;layerHeader->addWidget(new QLabel("LAYERS"));auto *addLayerImage=action("+ Image",layerHeader,"blue"),*addLayerText=action("+ Text",layerHeader),*layerUp=action("↑",layerHeader),*layerDown=action("↓",layerHeader),*removeLayer=action("Remove",layerHeader,"pink");frame->insertLayout(2,layerHeader);frame->insertWidget(3,layerStack);auto *layerTransform=new QHBoxLayout;layerTransform->addWidget(new QLabel("X"));auto *layerX=integer(-8192,8192,24);layerTransform->addWidget(layerX);layerTransform->addWidget(new QLabel("Y"));auto *layerY=integer(-8192,8192,56);layerTransform->addWidget(layerY);layerTransform->addWidget(new QLabel("Opacity"));auto *layerOpacity=integer(0,100,100);layerOpacity->setSuffix("%");layerTransform->addWidget(layerOpacity);auto *snapLayers=new QCheckBox("Snap");snapLayers->setChecked(true);layerTransform->addWidget(snapLayers);frame->insertLayout(4,layerTransform);
    auto loadLayer=[this,layerStack,layerX,layerY,layerOpacity]{const int index=layerStack->currentItem()?layerStack->currentItem()->data(Qt::UserRole).toInt():-1;const bool valid=index>=0&&index<timelineTracks.size();layerX->setEnabled(valid);layerY->setEnabled(valid);layerOpacity->setEnabled(valid);if(!valid)return;const auto &track=timelineTracks[index];QSignalBlocker x(layerX),y(layerY),opacity(layerOpacity);layerX->setValue(qRound(track.position.x()));layerY->setValue(qRound(track.position.y()));layerOpacity->setValue(qRound(track.opacity*100));if(trackList)trackList->setCurrentRow(index);};connect(layerStack,&QListWidget::currentRowChanged,this,[loadLayer](int){loadLayer();});connect(addLayerImage,&QPushButton::clicked,this,[this,refreshLayers]{QString path=QFileDialog::getOpenFileName(this,"Add image layer",QSettings().value("lastImportFolder").toString(),"Images (*.png *.jpg *.jpeg *.webp *.bmp *.tif *.tiff)");if(path.isEmpty())return;TimelineTrack track;track.type=TimelineTrack::Image;track.name=QFileInfo(path).fileName();track.source=path;track.image=ImageProcessor::read(path);track.start=0;track.end=qMax(1.,media.duration);timelineTracks<<track;refreshLayers();updateTrackPanel();refresh();});connect(addLayerText,&QPushButton::clicked,this,[this,refreshLayers]{bool ok=false;QString text=QInputDialog::getText(this,"Add text layer","Text",QLineEdit::Normal,"Text",&ok);if(!ok||text.isEmpty())return;TimelineTrack track;track.type=TimelineTrack::Text;track.name=text.left(24);track.text=text;track.start=0;track.end=qMax(1.,media.duration);timelineTracks<<track;refreshLayers();updateTrackPanel();refresh();});connect(removeLayer,&QPushButton::clicked,this,[this,layerStack,refreshLayers]{const int index=layerStack->currentItem()?layerStack->currentItem()->data(Qt::UserRole).toInt():-1;if(index<0||index>=timelineTracks.size())return;timelineTracks.removeAt(index);refreshLayers();updateTrackPanel();refresh();});auto moveLayer=[this,layerStack,refreshLayers](int direction){const int index=layerStack->currentItem()?layerStack->currentItem()->data(Qt::UserRole).toInt():-1;const int target=index+direction;if(index<0||target<0||target>=timelineTracks.size())return;timelineTracks.move(index,target);refreshLayers();layerStack->setCurrentRow(target+1);updateTrackPanel();refresh();};connect(layerUp,&QPushButton::clicked,this,[moveLayer]{moveLayer(1);});connect(layerDown,&QPushButton::clicked,this,[moveLayer]{moveLayer(-1);});auto applyLayer=[this,layerStack,layerX,layerY,layerOpacity,snapLayers]{const int index=layerStack->currentItem()?layerStack->currentItem()->data(Qt::UserRole).toInt():-1;if(index<0||index>=timelineTracks.size())return;int x=layerX->value(),y=layerY->value();if(snapLayers->isChecked()){if(qAbs(x)<12)x=0;if(qAbs(y)<12)y=0;if(qAbs(x-widthInput->value()/2)<12)x=widthInput->value()/2;if(qAbs(y-heightInput->value()/2)<12)y=heightInput->value()/2;}auto &track=timelineTracks[index];track.position={double(x),double(y)};track.opacity=layerOpacity->value()/100.;refresh();updateTrackPanel();};connect(layerX,qOverload<int>(&QSpinBox::valueChanged),this,[applyLayer](int){applyLayer();});connect(layerY,qOverload<int>(&QSpinBox::valueChanged),this,[applyLayer](int){applyLayer();});connect(layerOpacity,qOverload<int>(&QSpinBox::valueChanged),this,[applyLayer](int){applyLayer();});refreshLayers();
    auto *videoLayout=qobject_cast<QVBoxLayout*>(videoRow->layout());auto *full=action("⛶ Full screen",qobject_cast<QHBoxLayout*>(videoLayout->itemAt(0)->layout()));connect(full,&QPushButton::clicked,this,&MainWindow::fullScreenPlayer);auto *audioRow=new QHBoxLayout;audioRow->addWidget(new QLabel("AUDIO"));auto *volume=new GradientSlider;volume->setRange(0,100);volume->setValue(70);volume->setToolTip("Playback volume for the active video track.");audioRow->addWidget(volume,1);auto *rate=new QComboBox;rate->addItems({"0.5×","0.75×","1.0×","1.25×","1.5×","2.0×"});rate->setCurrentText("1.0×");rate->setToolTip("Playback speed for previewing and keyframe timing.");audioRow->addWidget(rate);videoLayout->insertLayout(1,audioRow);connect(volume,&QSlider::valueChanged,this,[this](int value){audio->setVolume(value/100.);});connect(rate,&QComboBox::currentTextChanged,this,[this](const QString &value){player->setPlaybackRate(value.left(value.size()-1).toDouble());});
    videoWorkspace=new QWidget(videoRow);videoWorkspace->setObjectName("VideoWorkspace");auto *workspaceLayout=new QVBoxLayout(videoWorkspace);workspaceLayout->setContentsMargins(0,4,0,4);workspaceLayout->setSpacing(4);
    auto *trackActions=new QHBoxLayout;auto *addVideoTrack=action("+ Video",trackActions),*addImageTrack=action("+ Image",trackActions),*addTextTrack=action("+ Text",trackActions,"blue"),*addEffectTrack=action("+ Effect",trackActions),*removeTrack=action("Remove",trackActions),*keyframe=action("◇ Keyframe",trackActions,"gold"),*composition=action("Composition",trackActions);workspaceLayout->addLayout(trackActions);
    trackList=new QListWidget(videoWorkspace);trackList->hide();
    auto *transformRow=new QHBoxLayout;transformRow->addWidget(new QLabel("X"));trackX=integer(-8192,8192,24);transformRow->addWidget(trackX);transformRow->addWidget(new QLabel("Y"));trackY=integer(-8192,8192,56);transformRow->addWidget(trackY);transformRow->addWidget(new QLabel("Scale"));trackScale=integer(1,800,100);trackScale->setSuffix("%");transformRow->addWidget(trackScale);transformRow->addWidget(new QLabel("Rotation"));trackRotation=integer(-360,360,0);trackRotation->setSuffix("°");transformRow->addWidget(trackRotation);transformRow->addWidget(new QLabel("Opacity"));trackOpacity=integer(0,100,100);trackOpacity->setSuffix("%");transformRow->addWidget(trackOpacity);workspaceLayout->addLayout(transformRow);
    auto *compositeRow=new QHBoxLayout;compositeRow->addWidget(new QLabel("Blend"));trackBlend=new QComboBox;trackBlend->addItems({"Normal","Screen","Multiply","Lighten"});compositeRow->addWidget(trackBlend);compositeRow->addWidget(new QLabel("Transition"));trackTransition=new QComboBox;trackTransition->addItems({"Cut","Fade"});compositeRow->addWidget(trackTransition);workspaceLayout->addLayout(compositeRow);
    auto applyTrack=[this]{int index=trackList?trackList->currentRow():-1;if(index<0||index>=timelineTracks.size())return;auto &track=timelineTracks[index];track.position={double(trackX->value()),double(trackY->value())};const double scale=trackScale->value()/100.;track.scale={scale,scale};track.rotation=trackRotation->value();track.opacity=trackOpacity->value()/100.;track.blendMode=trackBlend->currentText();track.transition=trackTransition->currentText();track.transitionIn=track.transition=="Fade"?.25:0;track.transitionOut=track.transition=="Fade"?.25:0;refresh();updateTrackPanel();};
    connect(trackX,qOverload<int>(&QSpinBox::valueChanged),this,[applyTrack](int){applyTrack();});connect(trackY,qOverload<int>(&QSpinBox::valueChanged),this,[applyTrack](int){applyTrack();});connect(trackScale,qOverload<int>(&QSpinBox::valueChanged),this,[applyTrack](int){applyTrack();});connect(trackRotation,qOverload<int>(&QSpinBox::valueChanged),this,[applyTrack](int){applyTrack();});connect(trackOpacity,qOverload<int>(&QSpinBox::valueChanged),this,[applyTrack](int){applyTrack();});connect(trackBlend,&QComboBox::currentTextChanged,this,[applyTrack](const QString &){applyTrack();});connect(trackTransition,&QComboBox::currentTextChanged,this,[applyTrack](const QString &){applyTrack();});connect(trackList,&QListWidget::currentRowChanged,this,[this](int){updateTrackPanel();});
    connect(addVideoTrack,&QPushButton::clicked,this,[this]{QString path=QFileDialog::getOpenFileName(this,"Replace primary video",{},"Videos (*.mp4 *.mov *.avi *.mkv *.webm)");if(!path.isEmpty())loadFiles({path});});
    connect(addTextTrack,&QPushButton::clicked,this,[this]{bool ok=false;QString text=QInputDialog::getText(this,"Add text track","Text",QLineEdit::Normal,"Text",&ok);if(!ok||text.isEmpty())return;TimelineTrack track;track.type=TimelineTrack::Text;track.name="Text · "+text.left(20);track.text=text;track.start=0;track.end=qMax(1.,media.duration);timelineTracks<<track;updateTrackPanel();refresh();});
    connect(addImageTrack,&QPushButton::clicked,this,[this]{QString path=QFileDialog::getOpenFileName(this,"Add image track",{},"Images (*.png *.jpg *.jpeg *.webp *.bmp *.tif *.tiff)");if(path.isEmpty())return;TimelineTrack track;track.type=TimelineTrack::Image;track.name="Image · "+QFileInfo(path).fileName();track.source=path;track.image=ImageProcessor::read(path);track.start=0;track.end=qMax(1.,media.duration);timelineTracks<<track;updateTrackPanel();refresh();});
    connect(addEffectTrack,&QPushButton::clicked,this,[this]{TimelineTrack track;track.type=TimelineTrack::Effect;track.name="Effects · active color and texture";track.start=0;track.end=qMax(1.,media.duration);timelineTracks<<track;updateTrackPanel();refresh();});
    connect(removeTrack,&QPushButton::clicked,this,[this]{int index=trackList?trackList->currentRow():-1;if(index<0||index>=timelineTracks.size()||timelineTracks[index].type==TimelineTrack::Video)return;timelineTracks.removeAt(index);updateTrackPanel();refresh();});
    connect(keyframe,&QPushButton::clicked,this,[this]{int index=trackList?trackList->currentRow():-1;if(index<0||index>=timelineTracks.size())return;auto &track=timelineTracks[index];track.keyframes<<TimelineKeyframe{player?qMax(0.,player->position()/1000.):0.,track.position,track.scale,track.rotation,track.opacity};std::sort(track.keyframes.begin(),track.keyframes.end(),[](const TimelineKeyframe &a,const TimelineKeyframe &b){return a.time<b.time;});status->setText("Keyframe saved · "+track.name);updateTrackPanel();});
    connect(composition,&QPushButton::clicked,this,[this]{QDialog dialog(this);dialog.setWindowTitle("Composition");auto *form=new QFormLayout(&dialog);auto *width=integer(64,8192,widthInput->value()),*height=integer(64,8192,heightInput->value()),*fps=integer(1,120,qRound(media.fps));form->addRow("Width",width);form->addRow("Height",height);form->addRow("Frame rate",fps);auto *buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);form->addRow(buttons);connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);if(dialog.exec()==QDialog::Accepted){setDimensions(width->value(),height->value());media.fps=fps->value();status->setText(QString("Composition · %1×%2 · %3 fps").arg(width->value()).arg(height->value()).arg(fps->value()));refresh();}});
    videoLayout->insertWidget(2,videoWorkspace);videoWorkspace->hide();
    // Output is the final inspector tab. Texture and Patterns are inserted before it.
    auto *output=inspector(tabs,8);auto *sizeRow=new QHBoxLayout;output->insertLayout(0,sizeRow);sizeRow->addWidget(new QLabel("Media size"));sizingMode=new QComboBox;sizingMode->addItems({"Current frame dimensions","Each source's original dimensions","Multiple checked widths"});sizeRow->addWidget(sizingMode,1);sizes->setEnabled(false);connect(sizingMode,&QComboBox::currentIndexChanged,this,[this](int index){sizes->setEnabled(index==2);});
    auto *targetRow=new QHBoxLayout;output->insertLayout(1,targetRow);targetEnabled=new QCheckBox("Target file size");targetRow->addWidget(targetEnabled);targetSize=new QDoubleSpinBox;targetSize->setRange(.1,1000000);targetSize->setValue(2);targetSize->setDecimals(2);targetRow->addWidget(targetSize);targetUnit=new QComboBox;targetUnit->addItem("MB",1000000);targetUnit->addItem("KB",1000);targetRow->addWidget(targetUnit);targetSize->setEnabled(false);targetUnit->setEnabled(false);connect(targetEnabled,&QCheckBox::toggled,targetSize,&QWidget::setEnabled);connect(targetEnabled,&QCheckBox::toggled,targetUnit,&QWidget::setEnabled);
    allowSmaller=new QCheckBox("Allow smaller dimensions / GIF frame rate to reach target");output->insertWidget(2,allowSmaller);auto *hint=new QLabel("Target is a maximum per output file. Results are measured before success.");hint->setObjectName("muted");hint->setWordWrap(true);output->insertWidget(3,hint);
    audioFormatRow=new QWidget;auto *audioLine=new QHBoxLayout(audioFormatRow);audioLine->setContentsMargins(0,0,0,0);audioLine->addWidget(new QLabel("Extract audio"));for(QString format:{"MP3","M4A","WAV","FLAC","OGG"}){auto *check=new QCheckBox(format);audioLine->addWidget(check);audioFormats[format]=check;}output->addWidget(audioFormatRow);
    auto *form=new QFormLayout;output->addLayout(form);encodeSpeed=new QComboBox;encodeSpeed->addItems({"ultrafast","veryfast","fast","medium","slow"});encodeSpeed->setCurrentText("veryfast");form->addRow("Encoding speed / efficiency",encodeSpeed);pngLevel=new QComboBox;pngLevel->addItem("Fast",1);pngLevel->addItem("Balanced",6);pngLevel->addItem("Maximum lossless",9);form->addRow("PNG compression",pngLevel);fpsLimit=integer(0,120,0);fpsLimit->setSpecialValueText("Keep source");form->addRow("Video / GIF frames per second",fpsLimit);paletteSize=new QComboBox;paletteSize->addItems({"256","128","64","32","16"});form->addRow("GIF colors",paletteSize);audioRate=integer(16,320,128);audioRate->setSuffix(" kbps");form->addRow("Audio bitrate",audioRate);stripMetadata=new QCheckBox("Remove metadata on export");stripMetadata->setChecked(true);output->addWidget(stripMetadata);
    auto *quick=line(output);auto *smallerPreset=action("Smaller files",quick,"pink"),*balanced=action("Balanced",quick,"blue"),*speed=action("Fastest",quick,"gold");connect(smallerPreset,&QPushButton::clicked,this,[this]{quality->setValue(72);crf->setValue(30);encodeSpeed->setCurrentText("medium");pngLevel->setCurrentIndex(2);audioRate->setValue(96);paletteSize->setCurrentText("128");});connect(balanced,&QPushButton::clicked,this,[this]{quality->setValue(85);crf->setValue(23);encodeSpeed->setCurrentText("veryfast");pngLevel->setCurrentIndex(1);audioRate->setValue(128);paletteSize->setCurrentText("256");});connect(speed,&QPushButton::clicked,this,[this]{quality->setValue(85);crf->setValue(25);encodeSpeed->setCurrentText("ultrafast");pngLevel->setCurrentIndex(0);});
    // Phone-style focus mode promotes this same live preview into the client
    // surface.  It is restored to this holder when zoom returns to 1:1.
    auto *normalPreviewHost=preview?preview->parentWidget():nullptr;
    auto *focusShell=findChild<QWidget*>("shell");
    if(focusShell)focusShell->installEventFilter(this);
    connect(preview,&PreviewWidget::viewZoomChanged,this,[this,normalPreviewHost,focusShell](double value){
        // Preview zoom is a deliberate focus mode: the image owns the portrait
        // workspace.  The tool rail remains available, but the settings drawer
        // never sits over the media.
        const bool immersive=value>1.04;
        auto *toolRail=findChild<QScrollArea*>("MainToolRail");
        auto *subRail=findChild<QScrollArea*>("SubSettingsRail");
        auto *addRail=findChild<QWidget*>("AddIconRail");
        auto *settingsHost=tabs?tabs->parentWidget():nullptr;
        auto *workspace=settingsHost?qobject_cast<QSplitter*>(settingsHost->parentWidget()):nullptr;
        auto *stage=findChild<QWidget*>("StageHost");
        auto *mainPreview=findChild<QWidget*>("MainPreview");
        auto setFocusSurface=[](QWidget *surface,bool on){
            if(!surface)return;
            if(on){
                if(!surface->property("focusSurfaceStyleSaved").toBool()){surface->setProperty("focusSurfaceStyleSaved",true);surface->setProperty("focusSurfaceStyle",surface->styleSheet());}
                surface->setStyleSheet("background:transparent;border:none;");
                surface->setAttribute(Qt::WA_TranslucentBackground,true);
            }else if(surface->property("focusSurfaceStyleSaved").toBool()){
                surface->setStyleSheet(surface->property("focusSurfaceStyle").toString());
                surface->setProperty("focusSurfaceStyleSaved",false);
                surface->setAttribute(Qt::WA_TranslucentBackground,false);
            }
        };
        auto setFocusLogoWhite=[](QWidget *top,bool on){
            if(!top)return;
            auto *logo=top->findChild<QLabel*>("HeaderLogo");if(!logo)return;
            QImage image(":/brand/wordmark.png");if(image.isNull())return;
            image=image.convertToFormat(QImage::Format_ARGB32);
            if(on)for(int y=0;y<image.height();++y){auto *pixels=reinterpret_cast<QRgb*>(image.scanLine(y));for(int x=0;x<image.width();++x)pixels[x]=qRgba(255,255,255,qAlpha(pixels[x]));}
            logo->setPixmap(QPixmap::fromImage(image).scaled(QSize(140,30),Qt::KeepAspectRatio,Qt::SmoothTransformation));
        };
        if(immersive){
            if(settingsHost&&!settingsHost->property("focusModeSaved").toBool()){
                settingsHost->setProperty("focusModeSaved",true);
                settingsHost->setProperty("focusTabsVisible",tabs&&tabs->isVisible());
                settingsHost->setProperty("focusCarouselVisible",tabCarousel&&tabCarousel->isVisible());
                settingsHost->setProperty("focusSubRailVisible",subRail&&subRail->isVisible());
            }
            if(tabs)tabs->hide();
            // Focus is immersive, not a reduced-feature mode.  Rails stay
            // reachable; only their surfaces become transparent over media.
            if(tabCarousel)tabCarousel->show();
            if(subRail)subRail->show();
            if(addRail)addRail->show();
            setFocusSurface(findChild<QWidget*>("TopBar"),true);
            setFocusSurface(mainPreview,true);setFocusSurface(stage,true);setFocusSurface(settingsHost,true);setFocusSurface(workspace,true);setFocusSurface(addRail,true);setFocusSurface(bottomDock,true);
            if(auto *top=findChild<QWidget*>("TopBar")){top->setStyleSheet("QWidget#TopBar,QWidget#HeaderActions,QLabel#HeaderLogo,QToolButton,QPushButton{background:transparent;border:none;color:#ffffff;}");setFocusSurface(top->findChild<QWidget*>("HeaderActions"),true);setFocusSurface(top->findChild<QWidget*>("HeaderLogo"),true);setFocusLogoWhite(top,true);for(auto *control:top->findChildren<QAbstractButton*>()){control->show();control->raise();}}
            if(preview&&focusShell&&!preview->property("focusPreviewAttached").toBool()){
                const QRect startRect(normalPreviewHost?QRect(normalPreviewHost->mapTo(focusShell,QPoint{}),preview->size()):focusShell->rect());
                // Focus opens as navigation first: a plain drag must pan the
                // zoomed media, never continue a stale brush/eyedropper mode.
                preview->setCanvasTool(CanvasTool::None);
                preview->setProperty("focusPreviewAttached",true);
                preview->setParent(focusShell);
                // mediaContext gives the normal preview a compact fixed
                // height.  Clear both sides of that constraint for focus.
                preview->setMinimumHeight(0);
                preview->setMaximumHeight(QWIDGETSIZE_MAX);
                preview->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Expanding);
                preview->setGeometry(startRect);
                preview->show();
                preview->lower();
                auto *transition=new QPropertyAnimation(preview,"geometry",preview);transition->setDuration(240);transition->setEasingCurve(QEasingCurve::OutCubic);transition->setStartValue(startRect);transition->setEndValue(focusShell->rect());transition->start(QAbstractAnimation::DeleteWhenStopped);
            }
            if(toolRail)toolRail->show();
            if(toolRail){
                if(!toolRail->property("focusRailStyleSaved").toBool()){toolRail->setProperty("focusRailStyleSaved",true);toolRail->setProperty("focusRailStyle",toolRail->styleSheet());}
                toolRail->setStyleSheet("QScrollArea,QWidget,QToolButton#MainToolButton,QToolButton#MainToolButton:hover{background:transparent;border:1px solid transparent;border-radius:20px;} QToolButton#MainToolButton:checked{background:transparent;border:2px solid #7a4dff;}");
                if(auto *fade=toolRail->findChild<QWidget*>("MainToolRailLeftFade"))fade->hide();
                if(auto *fade=toolRail->findChild<QWidget*>("MainToolRailRightFade"))fade->hide();
            }
            // The vertical canvas control stays usable while immersed, but it
            // must not introduce a black strip over the edge-to-edge image.
            for(auto *slider:findChildren<QSlider*>("SafeZoneSlider")){
                if(!slider->property("focusSliderStyleSaved").toBool()){
                    slider->setProperty("focusSliderStyleSaved",true);
                    slider->setProperty("focusSliderStyle",slider->styleSheet());
                }
                slider->setStyleSheet("QSlider#SafeZoneSlider::groove:vertical{background:transparent;border:none;} "
                                      "QSlider#SafeZoneSlider::add-page:vertical{background:transparent;border:none;} "
                                      "QSlider#SafeZoneSlider::sub-page:vertical{background:qlineargradient(x1:0,y1:1,x2:0,y2:0,stop:0 #57dd7b,stop:.55 #3ddcff,stop:1 #3d91fb);border-radius:4px;} "
                                      "QSlider#SafeZoneSlider::handle:vertical{height:16px;margin:0 -5px;border-radius:8px;background:#ffffff;border:2px solid #3d91fb;}");
                slider->style()->unpolish(slider);slider->style()->polish(slider);slider->update();
                // The focus canvas has no side gutter.  Guides remain in the
                // preview itself; their normal-layout slider returns on exit.
                if(!slider->property("focusSliderWidthSaved").isValid())slider->setProperty("focusSliderWidthSaved",slider->width());
                slider->setFixedWidth(0);
                slider->hide();
            }
            const QList<QWidget*> focusRails{toolRail,tabCarousel,subRail,addRail,bottomDock};
            for(auto *rail:focusRails)if(rail)for(auto *control:rail->findChildren<QToolButton*>()){
                if(!control->property("focusIconStyleSaved").toBool()){control->setProperty("focusIconStyleSaved",true);control->setProperty("focusIconStyle",control->styleSheet());}
                control->setStyleSheet("QToolButton{background:transparent;border:1px solid transparent;border-radius:20px;color:#ffffff;} QToolButton:hover{background:transparent;border:1px solid #ffffff;} QToolButton:checked{background:transparent;border:2px solid #eb4790;}");
            }
            const int focusRailHeight=qMax(52,(toolRail?toolRail->height():0)+(tabCarousel?tabCarousel->height():0)+(subRail?subRail->height():0));
            if(settingsHost)settingsHost->setFixedHeight(focusRailHeight);
            if(bottomDock)bottomDock->show();
            if(stage){stage->setMinimumHeight(0);stage->setMaximumHeight(QWIDGETSIZE_MAX);}
            if(workspace)workspace->setSizes({qMax(300,workspace->height()-focusRailHeight),focusRailHeight});
            // The live canvas stays lowest.  Header actions and tool glyphs
            // float over it without bringing back a panel background.
            if(auto *top=findChild<QWidget*>("TopBar")){top->show();top->raise();for(auto *control:top->findChildren<QAbstractButton*>()){control->show();control->raise();}}
            if(settingsHost){settingsHost->show();settingsHost->raise();}
            if(toolRail){toolRail->show();toolRail->raise();}
            if(tabCarousel){tabCarousel->show();tabCarousel->raise();}
            if(subRail){subRail->show();subRail->raise();}
            if(addRail){addRail->show();addRail->raise();}
            if(bottomDock){bottomDock->show();bottomDock->raise();}
        }else if(settingsHost&&settingsHost->property("focusModeSaved").toBool()){
            const bool tabsWereVisible=settingsHost->property("focusTabsVisible").toBool();
            const bool carouselWasVisible=settingsHost->property("focusCarouselVisible").toBool();
            const bool subRailWasVisible=settingsHost->property("focusSubRailVisible").toBool();
            settingsHost->setProperty("focusModeSaved",false);
            if(tabCarousel)tabCarousel->setVisible(carouselWasVisible);
            if(subRail)subRail->setVisible(subRailWasVisible);
            if(tabs)tabs->setVisible(tabsWereVisible);
            if(addRail)addRail->show();
            setFocusSurface(findChild<QWidget*>("TopBar"),false);
            setFocusSurface(mainPreview,false);setFocusSurface(stage,false);setFocusSurface(settingsHost,false);setFocusSurface(workspace,false);setFocusSurface(addRail,false);setFocusSurface(bottomDock,false);
            if(auto *top=findChild<QWidget*>("TopBar")){top->setStyleSheet({});setFocusSurface(top->findChild<QWidget*>("HeaderActions"),false);setFocusSurface(top->findChild<QWidget*>("HeaderLogo"),false);setFocusLogoWhite(top,false);}
            if(toolRail&&toolRail->property("focusRailStyleSaved").toBool()){toolRail->setStyleSheet(toolRail->property("focusRailStyle").toString());toolRail->setProperty("focusRailStyleSaved",false);if(auto *fade=toolRail->findChild<QWidget*>("MainToolRailLeftFade"))fade->show();if(auto *fade=toolRail->findChild<QWidget*>("MainToolRailRightFade"))fade->show();}
            for(auto *slider:findChildren<QSlider*>("SafeZoneSlider"))if(slider->property("focusSliderStyleSaved").toBool()){
                slider->setStyleSheet(slider->property("focusSliderStyle").toString());
                slider->setProperty("focusSliderStyleSaved",false);
                slider->style()->unpolish(slider);slider->style()->polish(slider);slider->update();
                slider->setFixedWidth(slider->property("focusSliderWidthSaved").toInt());
                slider->setProperty("focusSliderWidthSaved",QVariant{});
                slider->show();
            }
            for(auto *control:findChildren<QToolButton*>())if(control->property("focusIconStyleSaved").toBool()){control->setStyleSheet(control->property("focusIconStyle").toString());control->setProperty("focusIconStyleSaved",false);}
            if(preview&&normalPreviewHost&&preview->property("focusPreviewAttached").toBool()){
                preview->setProperty("focusPreviewAttached",false);
                preview->setParent(normalPreviewHost);
                if(auto *normalLayout=normalPreviewHost->layout();normalLayout&&normalLayout->indexOf(preview)<0)normalLayout->addWidget(preview);
                preview->show();
                preview->setFixedHeight(VideoProcessor::isVideo(currentFile)?qBound(190,height()-640,360):360);
            }
            if(bottomDock)bottomDock->show();
            const int restoredHeight=tabsWereVisible?300:110;
            settingsHost->setFixedHeight(restoredHeight);
            if(stage){stage->setMinimumHeight(240);stage->setMaximumHeight(QWIDGETSIZE_MAX);}
            if(workspace)workspace->setSizes({qMax(240,workspace->height()-restoredHeight),restoredHeight});
        }
        const QList<QWidget*> surfaces{bottomDock,tabCarousel,toolRail,subRail,findChild<QWidget*>("TopBar"),stage,mainPreview,settingsHost,workspace};
        for(auto *surface:surfaces)if(surface){surface->setProperty("immersivePreview",immersive);surface->style()->unpolish(surface);surface->style()->polish(surface);surface->update();}
        if(settingsHost){settingsHost->setProperty("immersivePreview",immersive);settingsHost->style()->unpolish(settingsHost);settingsHost->style()->polish(settingsHost);settingsHost->update();}
        if(auto *top=findChild<QWidget*>("TopBar"))for(const auto &name:{QString("HeaderActions"),QString("HeaderLogo")})if(auto *surface=top->findChild<QWidget*>(name)){surface->setProperty("immersivePreview",immersive);surface->style()->unpolish(surface);surface->style()->polish(surface);surface->update();}
    });
    configureCapture();mediaContext();
    auto *clearShortcut=new QShortcut(QKeySequence("Ctrl+Shift+Delete"),this);connect(clearShortcut,&QShortcut::activated,this,&MainWindow::clearMedia);
    auto *openProjectShortcut=new QShortcut(QKeySequence::Open,this);connect(openProjectShortcut,&QShortcut::activated,this,&MainWindow::loadProject);
    auto *captureShortcut=new QShortcut(QKeySequence("Ctrl+Shift+S"),this);connect(captureShortcut,&QShortcut::activated,this,&MainWindow::startCapture);
}
QMarginsF MainWindow::normalizedMargins(const QVector<QDoubleSpinBox*>& values)const{
    auto part=[&](int i){double unit=pixelMargins?(i%2?heightInput->value():widthInput->value()):100.;return std::clamp(values[i]->value()/std::max(1.,unit),0.,.45);};return {part(0),part(1),part(2),part(3)};
}
void MainWindow::changeMarginUnits(int index){auto safe=normalizedMargins(safeInputs),pad=normalizedMargins(paddingInputs);pixelMargins=index==1;double s[]{safe.left(),safe.top(),safe.right(),safe.bottom()},p[]{pad.left(),pad.top(),pad.right(),pad.bottom()};for(int i=0;i<4;++i){double unit=pixelMargins?(i%2?heightInput->value():widthInput->value()):100.;for(auto *n:{safeInputs[i],paddingInputs[i]}){QSignalBlocker block(n);n->setSuffix(pixelMargins?" px":"%");n->setMaximum(pixelMargins?8192:45);}QSignalBlocker a(safeInputs[i]),b(paddingInputs[i]);safeInputs[i]->setValue(s[i]*unit);paddingInputs[i]->setValue(p[i]*unit);}refresh();}
void MainWindow::rotateMedia(int delta){layerStyle.rotation=(layerStyle.rotation+delta+360)%360;bool follow=followSource->isChecked();setDimensions(heightInput->value(),widthInput->value());lockedRatio=double(widthInput->value())/heightInput->value();followSource->setChecked(follow);refresh();}
void MainWindow::deliverFrame(const QImage &image){if(image.isNull())return;original=image;preview->setFrame(image);if(fullPreview)fullPreview->setFrame(image);}
void MainWindow::seekMedia(double seconds){if(isGif(currentFile)){if(gifTimes.isEmpty())return;int index=int(std::upper_bound(gifTimes.begin(),gifTimes.end(),qRound(seconds*1000))-gifTimes.begin())-1;gif->jumpToFrame(std::clamp(index,0,int(gifTimes.size())-1));}else player->setPosition(qRound64(seconds*1000));}
void MainWindow::clearMedia(){
    ++loadGeneration;loading=false;compositionMode=false;compositionPosition=0;stopPlayback();gif->stop();gif->setFileName({});player->stop();player->setSource({});gifTimes.clear();if(recorder&&recorder->state()!=QProcess::NotRunning){discardRecording=true;recorder->write("q\n");}
    if(fullscreen)fullscreen->close();batch.files.clear();clips.clear();draftRanges.clear();inputAliases.clear();canvasLayers.clear();currentFile.clear();original={};media={};preview->setFrame({});updateBatchLabel();if(batchStrip){batchStrip->hide();if(auto *dock=batchStrip->parentWidget())dock->setFixedHeight(64);}if(exportButton)exportButton->setEnabled(false);updateClipTable();timeline->setPosition(0);timeline->setDuration(1);timeline->setRange(0,1);thumbnail->clear();info->setText("No media");filename->setText("PREVIEW · Import images, GIFs, PSDs or videos");status->setText("Media cleared · source files unchanged");modes->button(0)->setChecked(true);captureRow->hide();mediaContext();
}
void MainWindow::openImageInCompositor(){
    if(original.isNull()||currentFile.isEmpty()){showError("Import an image before opening the compositor.");return;}
    if(VideoProcessor::isVideo(currentFile)){mediaContext();return;}
    stopPlayback();player->setSource({});compositionMode=true;compositionPlaying=false;compositionTimer.stop();compositionPosition=0;media.duration=5.;
    timelineTracks.clear();TimelineTrack image;image.type=TimelineTrack::Image;image.name=QFileInfo(inputTitles.value(currentFile,currentFile)).fileName();image.source=currentFile;image.image=original;image.start=0;image.end=media.duration;timelineTracks<<image;
    if(!compositionTimer.property("aspectraCompositionClock").toBool()){compositionTimer.setInterval(16);connect(&compositionTimer,&QTimer::timeout,this,[this]{if(!compositionMode||VideoProcessor::isVideo(currentFile))return;compositionPosition+=.016;if(compositionPosition>=media.duration)compositionPosition=0;timeline->setPosition(compositionPosition);timeLabel->setText(timeCode(compositionPosition)+" / "+timeCode(media.duration));refresh();});compositionTimer.setProperty("aspectraCompositionClock",true);}
    if(!timeline->property("aspectraCompositionSeek").toBool()){connect(timeline,&TimelineWidget::seek,this,[this](double time){if(compositionMode&&!VideoProcessor::isVideo(currentFile)){compositionPosition=time;timeline->setPosition(time);timeLabel->setText(timeCode(time)+" / "+timeCode(media.duration));refresh();}});timeline->setProperty("aspectraCompositionSeek",true);}
    timeline->setDuration(media.duration);timeline->setPosition(0);timeLabel->setText(timeCode(0)+" / "+timeCode(media.duration));mediaContext();updateTrackPanel();refresh();status->setText("Image placed as the first clip · 5-second composition ready");
}
void MainWindow::mediaContext(){
    bool motion=VideoProcessor::isVideo(currentFile)||compositionMode,hasImages=false,hasVideos=compositionMode;for(auto *control:tabCarousel->findChildren<QToolButton*>("CarouselTab")){if(control->toolTip()=="Video settings")control->setVisible(motion);}for(auto path:batch.files){if(VideoProcessor::isVideo(path))hasVideos=true;else hasImages=true;}
    auto *settingsHost=tabs->parentWidget();tabs->setTabVisible(5,motion);videoRow->setVisible(motion);if(videoWorkspace)videoWorkspace->setVisible(motion);preview->setFixedHeight(motion&&!isGif(currentFile)?qBound(190,height()-640,360):360);if(motion){modes->button(1)->setChecked(true);QTimer::singleShot(0,this,[this,settingsHost]{if(tabCarousel->parentWidget()!=videoRow){if(auto *old=qobject_cast<QBoxLayout*>(tabCarousel->parentWidget()->layout()))old->removeWidget(tabCarousel);if(auto *videoLayout=qobject_cast<QVBoxLayout*>(videoRow->layout()))videoLayout->insertWidget(2,tabCarousel);}tabs->hide();if(auto *activeCard=settingsHost->findChild<QWidget*>("ActiveSettingCard"))activeCard->hide();if(auto *subRail=settingsHost->findChild<QScrollArea*>("SubSettingsRail"))subRail->hide();settingsHost->setMinimumHeight(0);settingsHost->setMaximumHeight(0);if(auto *split=qobject_cast<QSplitter*>(settingsHost->parentWidget()))split->setSizes({qMax(240,split->height()),0});});}else{if(tabCarousel->parentWidget()!=settingsHost){if(auto *old=qobject_cast<QBoxLayout*>(tabCarousel->parentWidget()->layout()))old->removeWidget(tabCarousel);if(auto *settingsLayout=qobject_cast<QVBoxLayout*>(settingsHost->layout()))settingsLayout->insertWidget(0,tabCarousel);}settingsHost->show();settingsHost->setMinimumHeight(58);settingsHost->setMaximumHeight(58);if(!captureRow->isVisible())modes->button(0)->setChecked(true);}
    if(imageFormatRow)imageFormatRow->setVisible(hasImages||!hasVideos);if(videoFormatRow)videoFormatRow->setVisible(hasVideos);if(audioFormatRow)audioFormatRow->setVisible(hasVideos&&!isGif(currentFile));
    if(fpsLimit)fpsLimit->setEnabled(hasVideos);if(paletteSize)paletteSize->setEnabled(hasVideos);if(!motion&&tabs->currentIndex()==5)tabs->setCurrentIndex(0);
}
void MainWindow::routeMode(int id){
    stopPlayback();captureRow->setVisible(id==2);if(id==2){videoRow->hide();preview->setFixedHeight(220);tabs->setCurrentIndex(0);return;}
    auto matches=[&](QString path){return id==1?(compositionMode||VideoProcessor::isVideo(path)&&!isGif(path)):!VideoProcessor::isVideo(path)||isGif(path);};
    if(!matches(currentFile)||currentFile.isEmpty()){for(auto path:batch.files)if(matches(path)){selectFile(path);break;}}
    if(id==1){mediaContext();tabs->setTabVisible(5,true);videoRow->show();preview->setFixedHeight(qBound(190,height()-640,360));if(!VideoProcessor::isVideo(currentFile)&&!compositionMode)status->setText("Import a video or open an image in the compositor.");}else{mediaContext();tabs->setCurrentIndex(0);}
}
void MainWindow::fullScreenPlayer(){
    if(original.isNull())return;if(fullscreen){fullscreen->raise();return;}fullscreen=new QDialog(this);fullscreen->setAttribute(Qt::WA_DeleteOnClose);fullscreen->setWindowTitle("Aspectra · Full screen");auto *v=new QVBoxLayout(fullscreen);v->setContentsMargins(0,0,0,0);fullPreview=new PreviewWidget;fullPreview->setMinimumHeight(1);fullPreview->setMaximumHeight(QWIDGETSIZE_MAX);fullPreview->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Expanding);fullPreview->setGuides(false,{});v->addWidget(fullPreview,1);auto *bar=line(v);auto *play=action("Play / Pause",bar,"blue"),*exit=action("Exit full screen · Esc",bar);connect(play,&QPushButton::clicked,this,&MainWindow::togglePlayback);connect(exit,&QPushButton::clicked,fullscreen,&QDialog::close);connect(fullPreview,&PreviewWidget::fullScreenRequested,fullscreen,&QDialog::close);connect(fullscreen,&QDialog::finished,this,[this]{fullPreview=nullptr;fullscreen=nullptr;});fullPreview->setFrame(original);refresh();fullscreen->showFullScreen();
}
void MainWindow::annotateMedia(){if(original.isNull())return;stopPlayback();QImage image=AnnotationEditor::edit(this,original);if(!image.isNull())ingestCapture(image);}
void MainWindow::ingestCapture(QImage image){show();if(image.isNull()){showError("The capture returned no image. Try Whole monitor or select a different region.");return;}if(copyCapture&&copyCapture->isChecked())QGuiApplication::clipboard()->setImage(image);QString path=captures.filePath("Capture-"+QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss-zzz")+".png");if(!image.save(path)){showError("Could not save capture.");return;}loadFiles({path});}
void MainWindow::configureCapture(){
    if(captureKind)return;delete captureRow->layout();auto *v=new QVBoxLayout(captureRow);v->setContentsMargins(0,0,0,0);const auto children=captureRow->findChildren<QWidget*>(QString(),Qt::FindDirectChildrenOnly);for(auto *child:children)child->deleteLater();auto *r=line(v);captureKind=new QComboBox;captureKind->addItems({"Select region","Select window","Whole monitor","Repeat last region"});captureKind->setToolTip("Select what the Capture button grabs. Window and region selections use the on-screen overlay.");r->addWidget(captureKind,1);auto *mode=new QComboBox;mode->addItems({"Image capture","Video recording"});mode->setToolTip("Choose a still image or an FFmpeg-powered screen recording.");r->addWidget(mode);connect(mode,&QComboBox::currentIndexChanged,this,[this](int i){recordMode=i==1;});captureButton=action("Capture",r,"blue");captureButton->setToolTip("Start a screen capture. Press Esc in the selection overlay to cancel.");connect(captureButton,&QPushButton::clicked,this,&MainWindow::startCapture);
    r=line(v);captureScreen=new QComboBox;for(auto *screen:QGuiApplication::screens())captureScreen->addItem(screen->name());captureScreen->setToolTip("Monitor used by Whole monitor capture and as the reference for recording.");r->addWidget(captureScreen,1);r->addWidget(new QLabel("Delay"));captureDelay=integer(0,10,0);captureDelay->setSuffix(" s");captureDelay->setToolTip("Wait before capturing, so you can prepare the screen.");r->addWidget(captureDelay);copyCapture=new QCheckBox("Copy image");copyCapture->setToolTip("Also place the captured image on the clipboard.");r->addWidget(copyCapture);
    r=line(v);r->addWidget(new QLabel("Record FPS"));captureFps=integer(5,60,30);r->addWidget(captureFps);r->addWidget(new QLabel("Stop after"));captureLimit=integer(0,3600,0);captureLimit->setSpecialValueText("Manual");captureLimit->setSuffix(" s");r->addWidget(captureLimit);captureAudio=new QComboBox;captureAudio->addItem("No microphone");for(auto device:QMediaDevices::audioInputs())captureAudio->addItem(device.description());r->addWidget(captureAudio,1);
    r=line(v);auto *annotate=action("Annotate / redact current image…",r,"gold");connect(annotate,&QPushButton::clicked,this,&MainWindow::annotateMedia);auto *note=new QLabel("Ctrl+Shift+S · capture while Aspectra is focused");note->setObjectName("muted");r->addWidget(note);
}
void MainWindow::startCapture(){
    if(recorder&&recorder->state()!=QProcess::NotRunning){captureButton->setEnabled(false);recorder->write("q\n");status->setText("Finishing recording…");return;}if(capturePending)return;
    if(recordMode&&VideoProcessor::findFfmpeg().isEmpty()){showError("Select ffmpeg.exe in Settings to record.");return;}int kind=captureKind->currentIndex();if(kind==3&&lastCaptureArea.isEmpty()){showError("Capture a region first.");return;}stopPlayback();hide();capturePending=true;
    QTimer::singleShot(250+captureDelay->value()*1000,this,[this,kind]{capturePending=false;if(kind==2||kind==3){auto *screen=QGuiApplication::screens().value(captureScreen->currentIndex(),QGuiApplication::primaryScreen());QRect area=kind==3?lastCaptureArea:screen->geometry();lastCaptureArea=area;if(recordMode)startRecording(area);else ingestCapture(ScreenCaptureManager::grabArea(area));return;}
        auto *overlay=new ScreenCaptureManager(recordMode,nullptr,kind==1);connect(overlay,&ScreenCaptureManager::cancelled,this,[this]{show();});connect(overlay,&ScreenCaptureManager::areaSelected,this,[this](QRect area){lastCaptureArea=area;if(recordMode)startRecording(area);});connect(overlay,&ScreenCaptureManager::captured,this,&MainWindow::ingestCapture);
    });
}
void MainWindow::startRecording(QRect area){
    QScreen *screen=QGuiApplication::screenAt(area.center());if(!screen||!screen->geometry().contains(area)){show();showError("Choose a recording area within one monitor.");return;}double scale=screen->devicePixelRatio();QPoint origin=screen->geometry().topLeft();
#ifdef Q_OS_WIN
    DEVMODEW device{};device.dmSize=sizeof(device);if(EnumDisplaySettingsW(reinterpret_cast<LPCWSTR>(screen->name().utf16()),ENUM_CURRENT_SETTINGS,&device))origin={device.dmPosition.x,device.dmPosition.y};
#endif
    QRect native(origin+QPoint(qRound((area.x()-screen->geometry().x())*scale),qRound((area.y()-screen->geometry().y())*scale)),QSize(qRound(area.width()*scale),qRound(area.height()*scale)));
    recordFile=captures.filePath("Recording-"+QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss-zzz")+".mp4");discardRecording=false;recorder=new QProcess(this);QStringList args{"-hide_banner","-loglevel","error","-n","-thread_queue_size","512","-f","gdigrab","-framerate",QString::number(captureFps->value()),"-offset_x",QString::number(native.x()),"-offset_y",QString::number(native.y()),"-video_size",QString("%1x%2").arg(native.width()).arg(native.height()),"-i","desktop"};
    bool microphone=captureAudio->currentIndex()>0;if(microphone)args<<"-thread_queue_size"<<"512"<<"-f"<<"dshow"<<"-i"<<"audio="+captureAudio->currentText()<<"-c:a"<<"aac";if(captureLimit->value())args<<"-t"<<QString::number(captureLimit->value());args<<"-vf"<<"pad=ceil(iw/2)*2:ceil(ih/2)*2"<<"-c:v"<<"libx264"<<"-preset"<<"ultrafast"<<"-crf"<<"23"<<recordFile;
    connect(recorder,&QProcess::errorOccurred,this,[this](QProcess::ProcessError error){if(error==QProcess::FailedToStart){show();captureButton->setEnabled(true);showError("Recorder could not start.");recorder->deleteLater();recorder=nullptr;}});
    connect(recorder,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this](int code,QProcess::ExitStatus){QString error=QString::fromUtf8(recorder->readAllStandardError());recorder->deleteLater();recorder=nullptr;captureButton->setEnabled(true);captureButton->setText("Capture");show();if(discardRecording){discardRecording=false;return;}if(code==0)loadFiles({recordFile});else showError(error.right(1800));});
    recorder->start(VideoProcessor::findFfmpeg(),args);QTimer::singleShot(500,this,[this,microphone]{if(!recorder)return;show();modes->button(2)->setChecked(true);captureRow->show();captureButton->setText("■ Stop");status->setText(microphone?"Recording with microphone":"Recording · no audio");});
}
