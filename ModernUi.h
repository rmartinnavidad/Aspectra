#pragma once

#include <QtWidgets>

// Structural UI primitives for the portrait Aspectra X workspace.  They keep
// presentation separate from MainWindow's media and export controllers.
class AspectraWindow : public QWidget {
public:
    using QWidget::QWidget;
};

class TopBar final : public QWidget {
public:
    explicit TopBar(QWidget *parent=nullptr);
    QToolButton *backButton=nullptr;
    QToolButton *undoButton=nullptr,*redoButton=nullptr;
    QLabel *titleLabel=nullptr;
    QToolButton *moreButton=nullptr;
    QToolButton *infoButton=nullptr;
    QPushButton *exportButton=nullptr;
protected:
    void resizeEvent(QResizeEvent *event) override;
private:
    // Header contract: do not alter these placements without updating the UI test.
    static constexpr int HeaderHeight=48;
    static constexpr int ActionClusterWidth=178;
    static constexpr int WordmarkWidth=140;
    static constexpr int WordmarkHeight=34;
    QWidget *leftGroup=nullptr;
    QWidget *rightGroup=nullptr;
    QLabel *brandIcon=nullptr;
};

class MainPreview final : public QWidget {
public:
    explicit MainPreview(QWidget *content,QWidget *parent=nullptr);
    QWidget *content() const { return previewContent; }
private:
    QWidget *previewContent=nullptr;
};

class BottomDock final : public QWidget {
    Q_OBJECT
public:
    explicit BottomDock(QWidget *parent=nullptr);
    void setCurrentTool(int index);
signals:
    void toolActivated(int index);
private:
    QButtonGroup *tools=nullptr;
};

class InspectorPanel final : public QFrame {
    Q_OBJECT
    Q_PROPERTY(int panelHeight READ panelHeight WRITE setPanelHeight)
public:
    explicit InspectorPanel(QWidget *parent=nullptr);
    QTabWidget *pages() const { return stack; }
    void openPage(int index);
    void closePanel();
    bool isOpen() const;
    int panelHeight() const { return currentHeight; }
    void setPanelHeight(int height);
protected:
    bool eventFilter(QObject *watched,QEvent *event) override;
private:
    QTabWidget *stack=nullptr;
    QPropertyAnimation *animation=nullptr;
    int currentHeight=0;
    void place();
};

class FilmstripPanel final : public QFrame {
public:
    explicit FilmstripPanel(QWidget *parent=nullptr);
    QComboBox *selector=nullptr;
    QLabel *countLabel=nullptr;
    QPushButton *previousButton=nullptr;
    QPushButton *nextButton=nullptr;
};
