#include "Processing.h"
#include "LayerEffects.h"
#include <cmath>
#include <stdexcept>
#include <algorithm>

namespace {
void fail(const QString &s) { throw std::runtime_error(s.toUtf8().constData()); }
double bound(double v) { return std::clamp(v, 0.0, 1.0); }
QByteArray command(const QString &tool, const QStringList &args, int timeout = 30000, bool allowFailure = false, QByteArray *err = nullptr) {
    if (tool.isEmpty()) fail("FFmpeg was not found. Select ffmpeg.exe in Settings.");
    QProcess p;
    p.start(tool, args);
    if (!p.waitForStarted(5000)) fail(p.errorString());
    if (!p.waitForFinished(timeout)) { p.kill(); p.waitForFinished(); fail("Media operation timed out."); }
    QByteArray e = p.readAllStandardError();
    if (err) *err = e;
    if (!allowFailure && (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0)) fail(QString::fromUtf8(e).right(2000));
    return p.readAllStandardOutput();
}
void checkCancel(std::atomic_bool &cancel) { if (cancel) fail("Cancelled"); }
quint32 crc32(const QByteArray &data) {
    quint32 crc = 0xffffffffu;
    for (unsigned char b : data) {
        crc ^= b;
        for (int j = 0; j < 8; ++j) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}
}

QRectF ImageProcessor::cropRect(QSize source, QSize target, double zoom) {
    if (source.isEmpty() || target.isEmpty()) return {};
    double ratio = double(target.width()) / target.height();
    double w = source.width(), h = source.height();
    if (w / h > ratio) w = h * ratio; else h = w / ratio;
    w /= std::max(1.0, zoom); h /= std::max(1.0, zoom);
    return {(source.width()-w)/2, (source.height()-h)/2, w, h};
}
QImage ImageProcessor::read(const QString &path) {
    QImageReader reader(path); reader.setAutoTransform(true);
    QImage image = reader.read();
    if (image.isNull()) fail("Cannot read " + QFileInfo(path).fileName() + ": " + reader.errorString());
    return image;
}
QImage ImageProcessor::render(const QImage &input, QSize size, const Adjustments &a, const RecolorPreset *preset) {
    QImage source=a.rotation?input.transformed(QTransform().rotate(a.rotation),Qt::SmoothTransformation):input;
    if(source.isNull()||size.isEmpty())return {};
    QImage result(size,QImage::Format_ARGB32);if(result.isNull())fail("Not enough memory for output dimensions.");result.fill(Qt::transparent);
    QRectF content(a.padding.left()*size.width(),a.padding.top()*size.height(),size.width()*(1-a.padding.left()-a.padding.right()),size.height()*(1-a.padding.top()-a.padding.bottom()));
    if(content.width()<1||content.height()<1)fail("Padding leaves no room for the image.");
    {QPainter p(&result);p.setRenderHint(QPainter::SmoothPixmapTransform);p.drawImage(content,source,cropRect(source.size(),content.size().toSize(),a.zoom));}
    if(a.chromaKey){const QColor key=a.keyColor;const double low=a.keyTolerance+a.keyMatteBias-a.keyContract,soft=qMax(.001,a.keySoftness+(a.keyRadius+a.keySmooth+a.keySmartRadius)*.002);for(int y=0;y<result.height();++y)for(int x=0;x<result.width();++x){QColor pixel=result.pixelColor(x,y);double distance=std::sqrt(std::pow((pixel.red()-key.red())/255.,2)+std::pow((pixel.green()-key.green())/255.,2)+std::pow((pixel.blue()-key.blue())/255.,2));double alpha=qBound(0.,(distance-low)/soft,1.);if(a.keyEdgeContrast!=0){const double power=std::exp(-a.keyEdgeContrast*1.8);alpha=std::pow(alpha,power);}alpha=qBound(0.,alpha*a.keyDensity+a.keyShiftEdge,1.);if(a.keyCleanBlack>0)alpha=alpha<a.keyCleanBlack?0.:alpha;if(a.keyCleanWhite>0)alpha=alpha>1-a.keyCleanWhite?1.:alpha;double luminance=qGray(pixel.rgb())/255.;if(a.keyLumaProtect>0&&luminance<a.keyLumaProtect)alpha=1.;if(a.keyDespill>0&&alpha<1){int neutral=qMax(pixel.red(),pixel.blue());pixel.setGreen(qRound(pixel.green()*(1-a.keyDespill*(1-alpha))+neutral*a.keyDespill*(1-alpha)));}pixel.setAlpha(qRound(pixel.alpha()*alpha));result.setPixelColor(x,y,pixel);}}
    if(!a.hasColor()&&!preset)return applyLayerEffects(result,a);
    int lut[3][256];const double balances[]{a.red,a.green,a.blue};
    for(int ch=0;ch<3;++ch)for(int i=0;i<256;++i){double v=bound((i/255.*std::exp2(a.exposure)-.5)*a.contrast+.5);v=bound((v-a.blacks)/std::max(.001,a.whites-a.blacks));v=bound(std::pow(v,1/std::max(.1,a.gamma))+balances[ch]);if(a.flat)v=std::round(v*3)/3;lut[ch][i]=qRound(v*255);}
    bool hsv=a.hue!=0||a.saturation!=1||a.brightness!=0;
    // Rows are independent. Expensive powers are evaluated 768 times per image, not per pixel.
    uchar *base=result.bits();const int stride=result.bytesPerLine();
    #pragma omp parallel for if(size.width()*size.height()>262144) num_threads(4)
    for(int y=0;y<result.height();++y){QRgb *line=(QRgb*)(base+y*stride);
        for(int x=0;x<result.width();++x){QRgb pixel=line[x];if(!qAlpha(pixel))continue;
            if(hsv){QColor c=QColor::fromRgba(pixel);float h,s,v;c.getHsvF(&h,&s,&v);h=float(std::fmod(std::max(0.f,h)+a.hue/360.+1,1));c.setHsvF(h,float(bound(s*a.saturation)),float(bound(v+a.brightness)),c.alphaF());pixel=c.rgba();}
            int r=lut[0][qRed(pixel)],g=lut[1][qGreen(pixel)],b=lut[2][qBlue(pixel)],alpha=qAlpha(pixel);
            if(preset){if((.2126*r+.7152*g+.0722*b)/255.<preset->threshold)alpha=0;alpha=qRound(std::pow(alpha/255.,1/preset->gamma)*255);r=preset->color.red();g=preset->color.green();b=preset->color.blue();}
            line[x]=qRgba(r,g,b,alpha);
        }
    }
    return applyLayerEffects(result,a);
}
bool VideoProcessor::isVideo(const QString &p) {
    return QStringList{"mp4","mov","avi","mkv","webm","gif"}.contains(QFileInfo(p).suffix().toLower());
}
QImage ImageProcessor::compositeTimeline(const QImage &source,const QVector<TimelineTrack> &tracks,double time){
    if(source.isNull()||tracks.isEmpty())return source;
    QImage out=source.convertToFormat(QImage::Format_ARGB32);
    QPainter painter(&out);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    for(const auto &track:tracks){
        if(!track.enabled||time<track.start||time>track.end)continue;
        QPointF position=track.position,scale=track.scale;
        double opacity=track.opacity,rotation=track.rotation;
        if(!track.keyframes.isEmpty()){
            const TimelineKeyframe *before=&track.keyframes.first(),*after=&track.keyframes.last();
            for(const auto &key:track.keyframes){if(key.time<=time)before=&key;if(key.time>=time){after=&key;break;}}
            const double blend=after->time>before->time?std::clamp((time-before->time)/(after->time-before->time),0.,1.):0.;
            position=before->position*(1-blend)+after->position*blend;
            scale=before->scale*(1-blend)+after->scale*blend;
            rotation=before->rotation*(1-blend)+after->rotation*blend;
            opacity=before->opacity*(1-blend)+after->opacity*blend;
        }
        opacity*=track.fill;
        if(track.transitionIn>0)opacity*=std::clamp((time-track.start)/track.transitionIn,0.,1.);
        if(track.transitionOut>0)opacity*=std::clamp((track.end-time)/track.transitionOut,0.,1.);
        painter.save();
        if(track.blendMode=="Screen")painter.setCompositionMode(QPainter::CompositionMode_Screen);
        else if(track.blendMode=="Multiply")painter.setCompositionMode(QPainter::CompositionMode_Multiply);
        else if(track.blendMode=="Overlay")painter.setCompositionMode(QPainter::CompositionMode_Overlay);
        else if(track.blendMode=="Color Dodge")painter.setCompositionMode(QPainter::CompositionMode_ColorDodge);
        else if(track.blendMode=="Lighten")painter.setCompositionMode(QPainter::CompositionMode_Lighten);
        painter.setOpacity(std::clamp(opacity,0.,1.));
        painter.translate(position);painter.rotate(rotation);painter.scale(std::max(.01,scale.x()),std::max(.01,scale.y()));
        if(track.type==TimelineTrack::Text){
            QFont font(track.fontFamily);font.setStyleName(track.fontStyle);font.setPixelSize(track.fontSize);font.setBold(track.fontBold);font.setItalic(track.fontItalic);font.setUnderline(track.underline);font.setStretch(qBound(1,qRound(track.horizontalScale),400));font.setCapitalization(track.allCaps?QFont::AllUppercase:(track.smallCaps?QFont::SmallCaps:QFont::MixedCase));font.setLetterSpacing(QFont::AbsoluteSpacing,track.letterSpacing+track.kerning);
            painter.setFont(font);const QStringList lines=track.text.split('\n');const QFontMetricsF metrics(font);const double lineAdvance=metrics.height()*track.lineSpacing/100.;const double baseline=track.baseline+(track.superscript?-track.fontSize*.32:(track.subscript?track.fontSize*.22:0));
            painter.save();painter.scale(1.,qBound(.1,track.verticalScale/100.,4.));
            for(int line=0;line<lines.size();++line){const QPointF point(0,baseline+line*lineAdvance);const QString value=lines[line];if(track.highlightColor.alpha()>0&&track.highlightWidth>0){const double h=qMax(1.,metrics.height()*track.highlightWidth/100.);painter.fillRect(QRectF(point.x(),point.y()-metrics.ascent(),metrics.horizontalAdvance(value),h),track.highlightColor);}if(track.textStroke){QPainterPath path;path.addText(point,font,value);painter.fillPath(path,track.color);painter.strokePath(path,QPen(track.color.lighter(170),qMax(1.,track.fontSize/26.)));}else{painter.setPen(track.color);painter.drawText(point,value);}}
            painter.restore();
        }
        else if(!track.image.isNull())painter.drawImage(QPointF(0,0),track.image);
        painter.restore();
    }
    return out;
}
QString VideoProcessor::findFfmpeg() {
    QString path=QSettings().value("ffmpeg").toString();
    if (QFileInfo::exists(path)) return path;
    for (const auto &p : QStringList{QCoreApplication::applicationDirPath()+"/ffmpeg.exe", QStandardPaths::findExecutable("ffmpeg"), "C:/Program Files/ShareX/ffmpeg.exe", "C:/Program Files/File Converter/ffmpeg.exe"})
        if (!p.isEmpty() && QFileInfo::exists(p)) return p;
    return {};
}
QString VideoProcessor::findRar() {
    QString path=QSettings().value("rar").toString();
    if (QFileInfo::exists(path)) return path;
    for (const auto &p : QStringList{QStandardPaths::findExecutable("rar"), "C:/Program Files/WinRAR/Rar.exe", "C:/Program Files (x86)/WinRAR/Rar.exe"})
        if (!p.isEmpty() && QFileInfo::exists(p)) return p;
    return {};
}
QImage VideoProcessor::firstFrame(const QString &tool, const QString &file, double seconds) {
    QByteArray png=command(tool,{"-v","error","-ss",QString::number(seconds,'f',3),"-i",file,"-frames:v","1","-f","image2pipe","-vcodec","png","-"});
    QImage image=QImage::fromData(png,"PNG");
    if (image.isNull()) fail("No video frame was decoded.");
    return image;
}
MediaInfo VideoProcessor::inspect(const QString &tool, const QString &file) {
    QByteArray err;
    command(tool,{"-hide_banner","-i",file},30000,true,&err);
    QString info=QString::fromUtf8(err);
    MediaInfo m; m.size=firstFrame(tool,file).size();
    auto duration=QRegularExpression("Duration: (\\d+):(\\d+):(\\d+(?:\\.\\d+)?)").match(info);
    if (duration.hasMatch()) m.duration=duration.captured(1).toDouble()*3600+duration.captured(2).toDouble()*60+duration.captured(3).toDouble();
    auto fps=QRegularExpression("([0-9]+(?:\\.[0-9]+)?) fps").match(info);
    if (fps.hasMatch()) m.fps=std::clamp(fps.captured(1).toDouble(),1.0,120.0);
    return m;
}
void VideoProcessor::render(const ExportJob &job,const QString &input,const QString &output,QSize size,const RecolorPreset *preset,std::atomic_bool &cancel,const std::function<void(QString)> &progress) {
    MediaInfo info=inspect(job.ffmpeg,input);
    if(info.duration>0&&(job.start>=info.duration||job.start<0||(job.duration>0&&job.start+job.duration>info.duration+.02)))fail("Clip range is outside the video duration.");
    const auto &a=job.adjustments;
    QString rotationFilter;int angle=((a.rotation%360)+360)%360;if(angle==90){rotationFilter="transpose=1,";info.size.transpose();}else if(angle==180)rotationFilter="hflip,vflip,";else if(angle==270){rotationFilter="transpose=2,";info.size.transpose();}
    int left=qRound(a.padding.left()*size.width()),top=qRound(a.padding.top()*size.height());
    QSize content(std::max(1,qRound(size.width()*(1-a.padding.left()-a.padding.right()))),std::max(1,qRound(size.height()*(1-a.padding.top()-a.padding.bottom()))));
    QRectF crop=ImageProcessor::cropRect(info.size,content,a.zoom);
    QString filter=rotationFilter+QString("format=rgba,crop=%1:%2:%3:%4:exact=1,scale=%5:%6:flags=bicubic,pad=%7:%8:%9:%10:color=black@0")
        .arg(std::max(1,int(crop.width()))).arg(std::max(1,int(crop.height()))).arg(int(crop.x())).arg(int(crop.y())).arg(content.width()).arg(content.height()).arg(size.width()).arg(size.height()).arg(left).arg(top);
    double fps=job.outputFps>0?job.outputFps:(job.videoFormat=="GIF"?std::min(20.,info.fps):info.fps);
    bool direct=!a.hasColor()&&!a.hasStyles()&&!preset;
    auto outputArgs=[&](){QStringList args;
        if(job.videoFormat=="GIF")args<<"-an"<<"-loop"<<"0";
        else if(job.videoFormat=="WEBM")args<<"-c:v"<<"libvpx-vp9"<<"-deadline"<<"realtime"<<"-cpu-used"<<"6"<<"-crf"<<QString::number(job.crf)<<"-b:v"<<"0"<<"-c:a"<<"libopus";
        else args<<"-c:v"<<"libx264"<<"-preset"<<job.encoderPreset<<"-crf"<<QString::number(job.crf)<<"-pix_fmt"<<"yuv420p"<<"-c:a"<<"aac"<<"-movflags"<<"+faststart";
        args<<"-b:a"<<QString::number(job.audioKbps*1000);if(job.stripMetadata)args<<"-map_metadata"<<"-1";args<<"-threads"<<"2"<<output;return args;};
    QString finalFilter=job.videoFormat=="GIF"?QString("fps=%1,split[a][b];[a]palettegen=stats_mode=single:max_colors=%2[p];[b][p]paletteuse=new=1").arg(fps).arg(job.gifColors):"pad=ceil(iw/2)*2:ceil(ih/2)*2,format=yuv420p";
    if(direct){
        QProcess p;QStringList args{"-v","error","-nostdin","-n","-ss",QString::number(job.start),"-i",input};
        if(job.duration>0)args<<"-t"<<QString::number(job.duration);
        args<<"-map"<<"0:v:0";if(job.videoFormat!="GIF")args<<"-map"<<"0:a:0?";
        args<<"-vf"<<filter+QString(",fps=%1,").arg(fps)+finalFilter<<"-filter_threads"<<"2"<<"-progress"<<"pipe:1";args<<outputArgs();
        p.start(job.ffmpeg,args);if(!p.waitForStarted())fail(p.errorString());QByteArray error,lines;
        while(p.state()!=QProcess::NotRunning){if(cancel){p.kill();p.waitForFinished();QFile::remove(output);checkCancel(cancel);}p.waitForFinished(100);error+=p.readAllStandardError();lines+=p.readAllStandardOutput();
            auto match=QRegularExpression("out_time_us=(\\d+)").globalMatch(QString::fromUtf8(lines));QString last;while(match.hasNext())last=match.next().captured(1);if(!last.isEmpty())progress(QString("Fast export · %1 s").arg(last.toDouble()/1000000.,0,'f',1));if(lines.size()>4096)lines=lines.right(1024);}
        error+=p.readAllStandardError();if(p.exitCode()!=0){QFile::remove(output);fail(QString::fromUtf8(error).right(2000));}return;
    }
    QProcess decoder,encoder;QStringList dec{"-v","error","-ss",QString::number(job.start),"-i",input};
    if(job.duration>0)dec<<"-t"<<QString::number(job.duration);
    dec<<"-an"<<"-vf"<<filter+QString(",fps=%1").arg(fps)<<"-filter_threads"<<"2"<<"-threads"<<"2"<<"-f"<<"rawvideo"<<"-pix_fmt"<<"bgra"<<"-";
    QStringList enc{"-v","error","-n","-f","rawvideo","-pixel_format","bgra","-video_size",QString("%1x%2").arg(size.width()).arg(size.height()),"-framerate",QString::number(fps),"-i","-"};
    if(job.videoFormat!="GIF"){enc<<"-ss"<<QString::number(job.start)<<"-i"<<input<<"-map"<<"0:v:0"<<"-map"<<"1:a:0?"<<"-shortest";if(job.duration>0)enc<<"-t"<<QString::number(job.duration);}
    enc<<"-vf"<<finalFilter<<"-filter_threads"<<"2";enc<<outputArgs();
    decoder.start(job.ffmpeg,dec);encoder.start(job.ffmpeg,enc);
    auto stop=[&]{decoder.kill();encoder.kill();decoder.waitForFinished(3000);encoder.waitForFinished(3000);};
    if(!decoder.waitForStarted(5000)||!encoder.waitForStarted(5000)){stop();fail("Could not start FFmpeg.");}
    QByteArray buffer,error;qint64 bytes=qint64(size.width())*size.height()*4;int count=0;QElapsedTimer watchdog;watchdog.start();
    Adjustments pixelAdjust=a;pixelAdjust.zoom=1;pixelAdjust.padding={};pixelAdjust.rotation=0;
    try{
        while(decoder.state()!=QProcess::NotRunning||decoder.bytesAvailable()>0||buffer.size()>=bytes){checkCancel(cancel);decoder.waitForReadyRead(25);auto chunk=decoder.readAllStandardOutput();if(!chunk.isEmpty())watchdog.restart();buffer+=chunk;error+=decoder.readAllStandardError();if(watchdog.elapsed()>60000)fail("Video decoder stalled.");
            qsizetype consumed=0;
            while(buffer.size()-consumed>=bytes){checkCancel(cancel);QImage frame((const uchar*)buffer.constData()+consumed,size.width(),size.height(),size.width()*4,QImage::Format_ARGB32);QImage rendered=ImageProcessor::compositeTimeline(ImageProcessor::render(frame,size,pixelAdjust,preset),job.timelineTracks,job.start+count/fps);
                if(encoder.write((const char*)rendered.constBits(),rendered.sizeInBytes())!=rendered.sizeInBytes())fail("Encoder stopped accepting frames.");
                while(encoder.bytesToWrite()>4*1024*1024){checkCancel(cancel);if(!encoder.waitForBytesWritten(5000))fail("Video encoder stalled: "+QString::fromUtf8(encoder.readAllStandardError()));}
                consumed+=bytes;if(++count%15==0)progress(QString("Color + effects · %1 s").arg(count/fps,0,'f',1));
            }buffer.remove(0,consumed);
        }
        if(decoder.exitCode()!=0)fail(QString::fromUtf8(error));if(!count)fail("No frames in this segment. Check its start/end.");
        encoder.closeWriteChannel();QElapsedTimer timer;timer.start();while(encoder.state()!=QProcess::NotRunning){checkCancel(cancel);encoder.waitForFinished(100);if(timer.elapsed()>60000)fail("Video encoder timed out while finishing.");}
        if(encoder.exitCode()!=0)fail(QString::fromUtf8(encoder.readAllStandardError()));
    }catch(...){stop();QFile::remove(output);throw;}
}

void ExportManager::zip(const QString &folder,const QString &archive,std::atomic_bool &cancel,int level) {
    // Standard ZIP with raw DEFLATE, UTF-8 names and CRC32; ZIP64 is rejected explicitly.
    struct Entry { QByteArray name; quint32 crc,packed,raw,offset; };
    QVector<Entry> entries;
    QSaveFile file(archive);
    if(!file.open(QIODevice::WriteOnly)) fail(file.errorString());
    QDataStream out(&file); out.setByteOrder(QDataStream::LittleEndian);
    QDir root(folder); QDirIterator it(folder,QDir::Files,QDirIterator::Subdirectories);
    while(it.hasNext()) {
        checkCancel(cancel); QString path=it.next();
        if(QFileInfo(path).size()>512*1024*1024) fail("ZIP supports files up to 512 MiB in this build. Export without ZIP for larger videos.");
        QFile source(path); if(!source.open(QIODevice::ReadOnly)) fail(source.errorString());
        QByteArray raw=source.readAll(); QByteArray z=qCompress(raw,level); QByteArray packed=z.mid(6,z.size()-10);
        if(file.pos()+packed.size()>0xffffffffLL || entries.size()>=65534) fail("ZIP64 is required for this archive. Export without ZIP.");
        Entry e{root.relativeFilePath(path).toUtf8(),crc32(raw),quint32(packed.size()),quint32(raw.size()),quint32(file.pos())};
        out << quint32(0x04034b50) << quint16(20) << quint16(0x800) << quint16(8) << quint16(0) << quint16(33)
            << e.crc << e.packed << e.raw << quint16(e.name.size()) << quint16(0);
        out.writeRawData(e.name.constData(),e.name.size()); out.writeRawData(packed.constData(),packed.size()); entries << e;
    }
    quint32 start=quint32(file.pos());
    for(const auto &e:entries) {
        out << quint32(0x02014b50) << quint16(20) << quint16(20) << quint16(0x800) << quint16(8) << quint16(0) << quint16(33)
            << e.crc << e.packed << e.raw << quint16(e.name.size()) << quint16(0) << quint16(0) << quint16(0) << quint16(0) << quint32(0) << e.offset;
        out.writeRawData(e.name.constData(),e.name.size());
    }
    quint32 central=quint32(file.pos())-start;
    out << quint32(0x06054b50) << quint16(0) << quint16(0) << quint16(entries.size()) << quint16(entries.size()) << central << start << quint16(0);
    if(out.status()!=QDataStream::Ok || !file.commit()) fail("Could not finish ZIP archive.");
}
bool BatchManager::supported(const QString &path) {
    return VideoProcessor::isVideo(path)||QStringList{"png","jpg","jpeg","bmp","webp","tif","tiff","psd","psb"}.contains(QFileInfo(path).suffix().toLower());
}
void BatchManager::add(const QStringList &paths) {
    for(const auto &path:paths) { QString p=QFileInfo(path).absoluteFilePath(); if(QFileInfo(p).isFile()&&supported(p)&&!files.contains(p,Qt::CaseInsensitive)) files << p; }
}

int runSelfTests() {
    try {
        auto require=[](bool pass,const char *message) { if(!pass) fail(message); };
        QImage image(120,80,QImage::Format_ARGB32); image.fill(QColor(220,60,30,128));
        auto crop=ImageProcessor::cropRect(image.size(),QSize(40,40),2);
        require(crop==QRectF(40,20,40,40),"Centered crop failed");
        Adjustments a;
        auto output=ImageProcessor::render(image,QSize(32,32),a);
        require(output.size()==QSize(32,32)&&qAlpha(output.pixel(10,10))==128,"Dimensions/alpha failed");
        a.saturation=0;
        auto gray=ImageProcessor::render(image,QSize(32,32),a); auto c=gray.pixelColor(10,10);
        require(c.red()==c.green()&&c.green()==c.blue(),"Desaturation failed");
        RecolorPreset black{"Black",Qt::black,0,1,true};
        auto recolor=ImageProcessor::render(image,QSize(32,32),a,&black);
        require(recolor.pixelColor(10,10)==QColor(0,0,0,128),"Recolor alpha failed");
        QTemporaryDir temp;
        require(temp.isValid(),"Temporary directory failed");
        image.save(temp.filePath("sample.png"));
        ExportJob job; job.files << temp.filePath("sample.png"); job.sizes << QSize(32,32) << QSize(64,64); job.outputRoot=temp.path(); job.presets << black;
        std::atomic_bool cancel=false;
        auto result=ExportManager::run(job,cancel,[](int,int,QString){});
        require(result.error.isEmpty()&&result.written==4,"Batch export failed");
        require(QFileInfo(result.folder+".zip").size()>0,"ZIP failed");
        auto check=ImageProcessor::read(result.folder+"/64/sample_black.png");
        require(check.size()==QSize(64,64)&&check.pixelColor(20,20).red()==0,"Written variant failed");
        cancel=true; auto cancelled=ExportManager::run(job,cancel,[](int,int,QString){});
        require(cancelled.cancelled,"Cancellation failed");
        QString testTool=qEnvironmentVariable("ASPECTRA_TEST_FFMPEG");
        if(!testTool.isEmpty()) {
            QString testRoot=qEnvironmentVariable("ASPECTRA_TEST_OUTPUT",temp.path());
            QDir().mkpath(testRoot);
            QString video=QDir(testRoot).filePath("video-test-source.mp4");
            command(testTool,{"-v","error","-y","-f","lavfi","-i","testsrc2=size=160x120:rate=12","-f","lavfi","-i","sine=frequency=440:sample_rate=44100","-t","1","-c:v","libx264","-pix_fmt","yuv420p","-c:a","aac",video});
            cancel=false;
            ExportJob vjob; vjob.files={video}; vjob.sizes={QSize(64,36)}; vjob.outputRoot=testRoot; vjob.ffmpeg=testTool; vjob.duration=.5; vjob.start=.1; vjob.adjustments.zoom=1.4; vjob.adjustments.saturation=.7;
            auto vr=ExportManager::run(vjob,cancel,[](int,int,QString){});
            require(vr.error.isEmpty()&&vr.written==1,"MP4 export failed");
            auto vi=VideoProcessor::inspect(testTool,vr.folder+"/64/video-test-source.mp4");
            require(vi.size==QSize(64,36)&&vi.duration>0&&vi.duration<1,"MP4 dimensions/segment failed");
            QByteArray audioInfo; command(testTool,{"-i",vr.folder+"/64/video-test-source.mp4"},30000,true,&audioInfo);
            require(audioInfo.contains("Audio:"),"MP4 audio retention failed");
            vjob.videoFormat="GIF";vjob.zip=false;
            auto gr=ExportManager::run(vjob,cancel,[](int,int,QString){});
            require(gr.error.isEmpty()&&gr.written==1,"GIF export failed");
            require(VideoProcessor::firstFrame(testTool,gr.folder+"/64/video-test-source.gif").size()==QSize(64,36),"GIF decoding failed");
        }
        QFile report(QDir::current().filePath("self-test-result.txt"));
        if(report.open(QIODevice::WriteOnly)) report.write(testTool.isEmpty()?"PASS: crop, dimensions, alpha, color, recolor, multi-size batch, ZIP and cancellation\n":"PASS: crop, dimensions, alpha, color, recolor, multi-size batch, ZIP, cancellation, MP4 encode/decode, segment trimming, audio retention and GIF export\n");
        return 0;
    } catch(const std::exception &e) {
        QFile report(QDir::current().filePath("self-test-result.txt"));
        if(report.open(QIODevice::WriteOnly)) report.write(QByteArray("FAIL: ")+e.what());
        return 1;
    }
}
