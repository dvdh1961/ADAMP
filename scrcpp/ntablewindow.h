#ifndef NTABLEWINDOW_H
#define NTABLEWINDOW_H

#include <QMainWindow>
#include <QPixmap>

class QAction;
class QMenu;
class QMenuBar;
class QLabel;
class QCheckBox;
class QPushButton;
class QGroupBox;
class QCloseEvent;
class QShowEvent;
class QPaintDevice;
class QMouseEvent;

class NTableWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit NTableWindow(QWidget *parent = nullptr);
    ~NTableWindow();

public slots:
    void doRefresh();

signals:
    void windowClosed();

protected:
    void closeEvent(QCloseEvent *event) override;
    void showEvent(QShowEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void onSaveAs();
    void onCopyToClipboard();
    void onRefresh();
    void onAutoRefreshToggled(bool checked);

    void onOptionToggled();
    void onEditNamTabAddr();

private:
    void setupUI();
    void setupMenus();

    void createMap(QPaintDevice *device, int w, int h);
    void createTile(int screen_y);

    void updateChanges();
    void smallUpdateChanges(int screen_y);
    void updateTileInfo(int ix, int iy);
    void updateVdpRegisters();

    QString intToHex(int value, int width);
    void updatePalette();

    QMenuBar *m_menuBar;
    QMenu *m_fileMenu;
    QMenu *m_viewMenu;

    QAction *m_copyAction;
    QAction *m_saveAction;
    QAction *m_exitAction;
    QAction *m_autoRefreshAction;
    QAction *m_refreshAction;

    QLabel *m_nameTableLabel;
    QLabel *m_tileLabel;

    QCheckBox *m_gridCheck;
    QCheckBox *m_tilesCheck;
    QCheckBox *m_bwCheck;

    QGroupBox *m_tileInfoBox;
    QLabel *m_locLabel;
    QLabel *m_tileIndexLabel;
    QLabel *m_namTabAddrLabel;
    QLabel *m_patGenAddrLabel;
    QLabel *m_colTabAddrLabel;
    QLabel *m_tileValueLabels[8];

    QGroupBox *m_vdpRegBox;
    QLabel *m_vdpRegLabel[8]; // Voor R0 t/m R7
    QLabel *m_vdpRegDesc[8];
    QGroupBox *m_paletteBox;
    QLabel *m_paletteLabels[16];

    QPixmap m_nameTablePixmap;
    QPixmap m_tilePixmap; // 8x8 tile

    int m_vramTile;
    int m_namTabVal;
    int m_lastScreenY;
};

#endif
