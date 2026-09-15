#include "MainWindow.h"
#include <QSplashScreen>
int runUpgradeTests();
int runUiTests(MainWindow &);
int runFocusZoomTest(MainWindow &);
int main(int argc,char *argv[]) {
    QApplication app(argc,argv);
    QCoreApplication::setOrganizationName("Aspectra"); QCoreApplication::setApplicationName("Aspectra");
    QApplication::setWindowIcon(QIcon(":/brand/logo.png"));
    QApplication::setStyle("Fusion");
    if(app.arguments().contains("--self-test")){int result=runSelfTests();return result?result:runUpgradeTests();}
    QPixmap splashCanvas(360,640);splashCanvas.fill(Qt::black);{QPainter painter(&splashCanvas);painter.setRenderHint(QPainter::Antialiasing);QPixmap logo(":/brand/logo.png");if(!logo.isNull())painter.drawPixmap(QRect(126,238,108,108),logo);painter.setPen(Qt::white);QFont title("Segoe UI",18,QFont::DemiBold);painter.setFont(title);painter.drawText(QRect(0,366,360,30),Qt::AlignCenter,"ASPECTRA X");painter.setPen(QColor("#aeb0b5"));painter.setFont(QFont("Segoe UI",10));painter.drawText(QRect(0,400,360,24),Qt::AlignCenter,"Preparing your workspace");}QSplashScreen splash(splashCanvas);splash.show();app.processEvents();
    MainWindow window;window.show();QTimer::singleShot(350,&window,[&splash,&window]{splash.finish(&window);});
    if(app.arguments().contains("--ui-test"))return runUiTests(window);
    if(app.arguments().contains("--focus-zoom-test"))return runFocusZoomTest(window);
    if(app.arguments().contains("--header-screenshot")){QTimer::singleShot(750,&window,[&]{window.grab().save(QDir::current().filePath("Aspectra-header.png"));app.quit();});return app.exec();}
    if(app.arguments().contains("--layout-screenshot")){QTimer::singleShot(750,&window,[&window]{for(auto *button:window.findChildren<QToolButton*>("CarouselTab"))if(button->toolTip()=="Adjust settings"){button->click();break;}QTimer::singleShot(250,&window,[&window]{window.grab().save(QDir::current().filePath("Aspectra-layout.png"));QCoreApplication::quit();});});return app.exec();}
    const QString importedLayoutArg=app.arguments().filter(QRegularExpression("^--import-layout-screenshot=")).value(0);
    if(!importedLayoutArg.isEmpty()){const QString imagePath=importedLayoutArg.section('=',1);window.loadFiles({imagePath});QTimer::singleShot(1800,&window,[&window]{for(auto *button:window.findChildren<QToolButton*>("CarouselTab"))if(button->toolTip()=="Adjust settings"){button->click();break;}QTimer::singleShot(350,&window,[&window]{window.grab().save(QDir::current().filePath("Aspectra-import-layout.png"));QCoreApplication::quit();});});return app.exec();}
    QTimer::singleShot(420,&window,[&window]{window.showWelcomeScreen();});
    QStringList files;for(int i=1;i<app.arguments().size();++i)if(!app.arguments()[i].startsWith("--"))files<<app.arguments()[i];
    if(app.arguments().contains("--screenshot")) {
        window.loadFiles({QCoreApplication::applicationDirPath()+"/sample.png"});
        QTimer::singleShot(1500,&window,[&]{window.savePreview(QDir::current().filePath("Aspectra-preview.png"));app.quit();});
    } else if(!files.isEmpty())window.loadFiles(files);
    return app.exec();
}
