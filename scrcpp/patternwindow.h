#ifndef PATTERNWINDOW_H
#define PATTERNWINDOW_H

#include <QMainWindow>
#include <QPixmap>

class QAction;
class QMenu;
class QMenuBar;
class QLabel;
class QCheckBox;
class QRadioButton;
class QScrollBar;
class QGroupBox;


extern int cv_pal32[16*4];

class PatternWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit PatternWindow(QWidget *parent = nullptr);
    ~PatternWindow();

signals:
    void windowClosed();

public slots:
    void doRefresh();
    void onRefresh();

protected:
    void closeEvent(QCloseEvent *event) override;
    void showEvent(QShowEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void onSaveAs();
    void onCopyToClipboard();
    void onAutoRefreshToggled(bool checked);
    void onOptionToggled();
    void onScrollChanged(int value);

private:
    void setupUI();
    void setupMenus();

    void createVramPixmap(QPaintDevice *device, int w, int h);
    void createTilePixmap();
    void updateChanges();
    void smallUpdateChanges();
    void updateTileInfo(int x, int y);
    void updateOfsText();

    QString intToHex(int value, int width);

    QMenuBar *m_menuBar;
    QMenu *m_fileMenu;
    QMenu *m_viewMenu;
    QAction *m_copyAction;
    QAction *m_saveAction;
    QAction *m_exitAction;
    QAction *m_autoRefreshAction;
    QAction *m_refreshAction;

    QLabel *m_vramLabel;
    QScrollBar *m_vramScroll;
    QLabel *m_vramOfsLabel;
    QLabel *m_vramTxtLabel;

    QGroupBox *m_tileInfoBox;
    QLabel *m_tileIndexLabel;
    QLabel *m_tileAddrLabel;

    QGroupBox *m_tileViewBox;
    QLabel *m_tileLabel;
    QLabel *m_tileValueLabels[8];
    QCheckBox *m_gridCheck;
    QRadioButton *m_colorRadio;
    QRadioButton *m_bwRadio;

    QPixmap m_vramPixmap;
    QPixmap m_tilePixmap;
    int m_baseVram;
    int m_vramTile;
};

#endif
