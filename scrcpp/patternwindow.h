#ifndef PATTERNWINDOW_H
#define PATTERNWINDOW_H

#include <QMainWindow>
#include <QPixmap>

// Forward declarations voor Qt-widgets
class QAction;
class QMenu;
class QMenuBar;
class QLabel;
class QCheckBox;
class QRadioButton;
class QScrollBar;
class QGroupBox;


// Nodig voor cv_pal32
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

    // Core logica poorten
    void createVramPixmap(QPaintDevice *device, int w, int h);
    void createTilePixmap();
    void updateChanges();
    void smallUpdateChanges();
    void updateTileInfo(int x, int y);
    void updateOfsText();

    // Helper
    QString intToHex(int value, int width);

    // Menu Bar
    QMenuBar *m_menuBar;
    QMenu *m_fileMenu;
    QMenu *m_viewMenu;
    QAction *m_copyAction;
    QAction *m_saveAction;
    QAction *m_exitAction;
    QAction *m_autoRefreshAction;
    QAction *m_refreshAction;

    // UI-componenten
    QLabel *m_vramLabel;      // De grote "VRam" TPaintBox
    QScrollBar *m_vramScroll;   // "scrVRAM"
    QLabel *m_vramOfsLabel;   // "eVRAMOfs"
    QLabel *m_vramTxtLabel;   // "eVRAMTxt"

    // Tile Info Group
    QGroupBox *m_tileInfoBox;
    QLabel *m_tileIndexLabel; // "eBGTTileNo"
    QLabel *m_tileAddrLabel;  // "eBGTTileAdr"

    // Tile View Group
    QGroupBox *m_tileViewBox;
    QLabel *m_tileLabel;      // "TileAlone" TPaintBox
    QLabel *m_tileValueLabels[8]; // Voor "idTiValue"
    QCheckBox *m_gridCheck;     // "chkGrid"
    QRadioButton *m_colorRadio;   // "rCol"
    QRadioButton *m_bwRadio;      // "rBW"

    // Member variabelen
    QPixmap m_vramPixmap;       // "mOffscreenBitmap"
    QPixmap m_tilePixmap;       // "mOfftileBitmap"
    int m_baseVram;             // Huidige scroll-offset
    int m_vramTile;             // Geselecteerd tile-adres (voor 8x8 view)
};

#endif // PATTERNWINDOW_H
