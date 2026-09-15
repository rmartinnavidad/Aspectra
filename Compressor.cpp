#include "Compressor.h"
#include <cmath>
#include <stdexcept>
#include <cstring>
#include <QPdfWriter>
#include <QPainter>
namespace {
void fail(QString s){throw std::runtime_error(s.toUtf8().constData());}
void check(std::atomic_bool &c){if(c)fail("Cancelled");}
void run(QString tool,QStringList args,std::atomic_bool &cancel){QProcess p;p.start(tool,args);if(!p.waitForStarted(5000))fail(p.errorString());QByteArray errors;while(p.state()!=QProcess::NotRunning){if(cancel){p.kill();p.waitForFinished();check(cancel);}p.waitForFinished(100);errors+=p.readAllStandardError();if(errors.size()>16000)errors=errors.right(8000);}errors+=p.readAllStandardError();if(p.exitCode()!=0)fail(QString::fromUtf8(errors).right(1800));}

QByteArray vectorSvg(const QImage &input){QImage image=input.scaled(QSize(256,256),Qt::KeepAspectRatio,Qt::SmoothTransformation).convertToFormat(QImage::Format_RGBA8888);QByteArray out=QString("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%1\" height=\"%2\" viewBox=\"0 0 %1 %2\">\n").arg(image.width()).arg(image.height()).toUtf8();for(int y=0;y<image.height();++y){int x=0;while(x<image.width()){QColor c=QColor::fromRgba(image.pixel(x,y));if(c.alpha()<16){++x;continue;}int r=qBound(0,qRound(c.red()/32.)*32,255),g=qBound(0,qRound(c.green()/32.)*32,255),b=qBound(0,qRound(c.blue()/32.)*32,255),start=x;++x;while(x<image.width()){QColor n=QColor::fromRgba(image.pixel(x,y));if(n.alpha()<16||qBound(0,qRound(n.red()/32.)*32,255)!=r||qBound(0,qRound(n.green()/32.)*32,255)!=g||qBound(0,qRound(n.blue()/32.)*32,255)!=b)break;++x;}out+=QString("<path d=\"M%1 %2h%3v1H%1z\" fill=\"#%4\"/>\n").arg(start).arg(y).arg(x-start).arg(QString("%1%2%3").arg(r,2,16,QChar('0')).arg(g,2,16,QChar('0')).arg(b,2,16,QChar('0'))).toUtf8();}}return out+"</svg>\n";}
QByteArray vectorEps(const QImage &input){QImage image=input.scaled(QSize(256,256),Qt::KeepAspectRatio,Qt::SmoothTransformation).convertToFormat(QImage::Format_RGBA8888);QByteArray out="%!PS-Adobe-3.0 EPSF-3.0\n";out+=QString("%%BoundingBox: 0 0 %1 %2\n%%LanguageLevel: 2\n%%EndComments\n").arg(image.width()).arg(image.height()).toUtf8();for(int y=0;y<image.height();++y){int x=0;while(x<image.width()){QColor c=QColor::fromRgba(image.pixel(x,y));if(c.alpha()<16){++x;continue;}int r=qBound(0,qRound(c.red()/32.)*32,255),g=qBound(0,qRound(c.green()/32.)*32,255),b=qBound(0,qRound(c.blue()/32.)*32,255),start=x;++x;while(x<image.width()){QColor n=QColor::fromRgba(image.pixel(x,y));if(n.alpha()<16||qBound(0,qRound(n.red()/32.)*32,255)!=r||qBound(0,qRound(n.green()/32.)*32,255)!=g||qBound(0,qRound(n.blue()/32.)*32,255)!=b)break;++x;}out+=QString("%1 %2 %3 setrgbcolor newpath %4 %5 moveto %6 0 rlineto 0 1 rlineto %7 0 rlineto closepath fill\n").arg(r/255.).arg(g/255.).arg(b/255.).arg(start).arg(image.height()-y-1).arg(x-start).arg(-(x-start)).toUtf8();}}return out+"showpage\n%%EOF\n";}

void exrPut32(QByteArray &b,quint32 v){for(int i=0;i<4;++i)b.append(char((v>>(8*i))&255));}void exrPut64(QByteArray &b,quint64 v){for(int i=0;i<8;++i)b.append(char((v>>(8*i))&255));}void exrFloat(QByteArray &b,float v){quint32 n;memcpy(&n,&v,4);exrPut32(b,n);}
QByteArray exrBytes(const QImage &input){QImage image=input.convertToFormat(QImage::Format_RGBA32FPx4);int w=image.width(),h=image.height();QByteArray out;exrPut32(out,0x01312f76);exrPut32(out,2);auto attr=[&](const QByteArray &name,const QByteArray &type,const QByteArray &data){out+=name;out.append('\0');out+=type;out.append('\0');exrPut32(out,data.size());out+=data;};QByteArray channels;for(const QByteArray &name:{QByteArray("B"),QByteArray("G"),QByteArray("R"),QByteArray("A")}){channels+=name;channels.append('\0');exrPut32(channels,2);channels.append(char(0));channels.append(char(0));exrPut32(channels,1);exrPut32(channels,1);}channels.append('\0');attr("channels","chlist",channels);QByteArray box;exrPut32(box,0);exrPut32(box,0);exrPut32(box,w-1);exrPut32(box,h-1);attr("dataWindow","box2",box);attr("displayWindow","box2",box);QByteArray order(1,char(0));attr("lineOrder","lineOrder",order);QByteArray aspect;exrFloat(aspect,1.f);attr("pixelAspectRatio","float",aspect);QByteArray center;exrFloat(center,0.f);exrFloat(center,0.f);attr("screenWindowCenter","v2f",center);QByteArray sw;exrFloat(sw,1.f);attr("screenWindowWidth","float",sw);out.append('\0');int headerSize=out.size(),lineBytes=w*4*4,recordBytes=8+lineBytes;for(int y=0;y<h;++y)exrPut64(out,headerSize+h*8+y*recordBytes);for(int y=0;y<h;++y){exrPut32(out,y);exrPut32(out,lineBytes);for(int channel=0;channel<4;++channel)for(int x=0;x<w;++x){QColor p=image.pixelColor(x,y);exrFloat(out,channel==0?p.blueF():channel==1?p.greenF():channel==2?p.redF():p.alphaF());}}return out;}
QByteArray encoded(QImage image,QString format,int quality,int compression){
    format=format.toUpper();
    if(format=="SVG")return vectorSvg(image);
    if(format=="EPS")return vectorEps(image);
    if(format=="EXR")return exrBytes(image);
    if(format=="JPG"||format=="JPEG"||format=="BMP"){QImage solid(image.size(),QImage::Format_RGB32);solid.fill(Qt::white);QPainter p(&solid);p.drawImage(0,0,image);p.end();image=solid;}
    QByteArray bytes;QBuffer buffer(&bytes);buffer.open(QIODevice::WriteOnly);QImageWriter writer(&buffer,format.toLatin1());writer.setQuality(quality);if(format=="PNG")writer.setCompression(compression);if(!writer.write(image))fail(writer.errorString());return bytes;
}
void replace(QString temp,QString destination){QFile source(temp);if(!source.open(QIODevice::ReadOnly))fail(source.errorString());QSaveFile out(destination);if(!out.open(QIODevice::WriteOnly))fail(out.errorString());while(!source.atEnd()){auto chunk=source.read(1024*1024);if(chunk.isEmpty()&&source.error()!=QFile::NoError)fail(source.errorString());if(out.write(chunk)!=chunk.size())fail(out.errorString());}if(!out.commit())fail(out.errorString());}
}
void Compressor::image(const QImage &input,const QString &format,const QString &output,const ExportJob &job,std::atomic_bool &cancel){
    QImage working=input;QByteArray best;bool lossy=format=="JPG"||format=="JPEG"||format=="WEBP";
    for(int resize=0;resize<12;++resize){check(cancel);best=encoded(working,format,job.quality,job.targetBytes?9:job.pngCompression);if(!job.targetBytes||best.size()<=job.targetBytes)break;
        if(lossy){QByteArray candidate;int lo=1,hi=job.quality;while(lo<=hi){check(cancel);int q=(lo+hi)/2;auto bytes=encoded(working,format,q,9);if(bytes.size()<=job.targetBytes){candidate=bytes;lo=q+1;}else hi=q-1;}if(!candidate.isEmpty()){best=candidate;break;}}
        if(!job.allowDownscale||std::min(working.width(),working.height())<16)fail("Target size cannot be met for "+QFileInfo(output).fileName()+" at the chosen dimensions. Allow dimension reduction, choose JPG/WebP, or raise the target.");
        working=working.scaled(std::max(1,qRound(working.width()*.75)),std::max(1,qRound(working.height()*.75)),Qt::KeepAspectRatio,Qt::SmoothTransformation);
    }
    if(job.targetBytes&&best.size()>job.targetBytes)fail("Target size remains too small for this image.");
    QSaveFile file(output);if(!file.open(QIODevice::WriteOnly)||file.write(best)!=best.size()||!file.commit())fail("Could not save compressed image.");
}
void Compressor::video(const QString &output,const ExportJob &job,std::atomic_bool &cancel,const std::function<void(QString)> &progress){
    if(!job.targetBytes||QFileInfo(output).size()<=job.targetBytes)return;
    auto info=VideoProcessor::inspect(job.ffmpeg,output);if(info.duration<=0)fail("Cannot target file size without a known video duration.");QTemporaryDir temp(QFileInfo(output).absolutePath()+"/.compress-XXXXXX");if(!temp.isValid())fail("Cannot create compression workspace.");
    QString format=QFileInfo(output).suffix().toLower(),candidate=temp.filePath("candidate."+format);double scale=1;int audioRate=job.audioKbps;double budget=job.targetBytes*.94*8/info.duration;
    for(int attempt=0;attempt<6;++attempt){check(cancel);progress(QString("Target-size compression · attempt %1/6").arg(attempt+1));QStringList args{"-v","error","-nostdin","-y","-i",output};if(job.stripMetadata)args<<"-map_metadata"<<"-1";
        int w=std::max(2,int(info.size.width()*scale)/2*2),h=std::max(2,int(info.size.height()*scale)/2*2);
        if(format=="gif"){
            int colors=std::max(8,job.gifColors>>std::min(attempt,4));double fps=std::max(3.,std::min(info.fps,20.)*(job.allowDownscale?std::pow(.82,attempt):1));
            args<<"-vf"<<QString("scale=%1:%2:flags=lanczos,fps=%3,split[a][b];[a]palettegen=max_colors=%4[p];[b][p]paletteuse").arg(w).arg(h).arg(fps).arg(colors)<<"-loop"<<"0";
        }else{
            audioRate=std::max(16,std::min(job.audioKbps,int(budget*.2/1000)));int videoRate=int(budget)-audioRate*1000;
            if(videoRate<12000)fail("Target is too small for this clip's duration. Raise the limit or trim a shorter clip.");
            QString codec=format=="webm"?"libvpx-vp9":"libx264";QStringList common{"-vf",QString("scale=%1:%2,format=yuv420p").arg(w).arg(h),"-c:v",codec,"-b:v",QString::number(videoRate),"-threads","2"};
            if(format=="webm")common<<"-deadline"<<"good"<<"-cpu-used"<<"5";else common<<"-preset"<<job.encoderPreset;
            QString passlog=temp.filePath("pass");QStringList first=args;first<<"-map"<<"0:v:0";first<<common;first<<"-pass"<<"1"<<"-passlogfile"<<passlog<<"-an"<<"-f"<<"null"<<"-";run(job.ffmpeg,first,cancel);
            args<<"-map"<<"0:v:0"<<"-map"<<"0:a:0?";args<<common;args<<"-pass"<<"2"<<"-passlogfile"<<passlog<<"-c:a"<<(format=="webm"?"libopus":"aac")<<"-b:a"<<QString::number(audioRate*1000);if(format!="webm")args<<"-movflags"<<"+faststart";
        }
        args<<candidate;run(job.ffmpeg,args,cancel);qint64 bytes=QFileInfo(candidate).size();if(bytes>0&&bytes<=job.targetBytes){replace(candidate,output);return;}
        budget*=std::min(.85,double(job.targetBytes)/std::max<qint64>(1,bytes)*.92);if(job.allowDownscale)scale*=.85;
    }
    fail("The encoded file exceeds the requested size. Increase the target or allow smaller dimensions/frame rate.");
}
void Compressor::audio(const QString &source,const QString &output,const QString &format,const ExportJob &job,std::atomic_bool &cancel){
    int bitrate=job.audioKbps;double duration=job.duration;if(duration<=0)duration=VideoProcessor::inspect(job.ffmpeg,source).duration-job.start;
    if(job.targetBytes&&duration>0)bitrate=std::min(bitrate,int(job.targetBytes*.9*8/duration/1000));
    QTemporaryDir temp(QFileInfo(output).absolutePath()+"/.audio-XXXXXX");if(!temp.isValid())fail("Cannot create audio workspace.");QString candidate=temp.filePath("audio."+format.toLower());
    for(int i=0;i<5;++i){check(cancel);QStringList args{"-v","error","-nostdin","-y","-ss",QString::number(job.start),"-i",source};if(job.duration>0)args<<"-t"<<QString::number(job.duration);args<<"-map"<<"0:a:0"<<"-vn";if(job.stripMetadata)args<<"-map_metadata"<<"-1";
        QString codec=format=="MP3"?"libmp3lame":format=="M4A"?"aac":format=="OGG"?"libvorbis":format=="FLAC"?"flac":"pcm_s16le";args<<"-c:a"<<codec;
        if(format!="WAV"&&format!="FLAC"){if(bitrate<16)fail("Target is too small for this audio duration.");int rate=bitrate;if(format=="MP3"){const QList<int> rates{16,24,32,40,48,56,64,80,96,112,128,160,192,224,256,320};rate=16;for(int value:rates)if(value<=bitrate)rate=value;}args<<"-b:a"<<QString::number(rate*1000);}
        args<<candidate;run(job.ffmpeg,args,cancel);qint64 bytes=QFileInfo(candidate).size();if(!job.targetBytes||bytes<=job.targetBytes){replace(candidate,output);return;}if(format=="WAV"||format=="FLAC")fail("Lossless audio exceeds the target. Choose MP3/M4A/OGG or a larger target.");bitrate=int(bitrate*.75);
    }fail("Could not fit audio within the requested file size.");
}
