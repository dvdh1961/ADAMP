#ifndef NTABLEWINDOW_H
#define NTABLEWINDOW_H

#include <QMainWindow>
#include <QPixmap>

// Forward declarations voor Qt UI-componenten
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

// De NTableWindow-klasse, een port van Tnametabviewer
class NTableWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit NTableWindow(QWidget *parent = nullptr);
    ~NTableWindow();

public slots:
    // Slot voor de MainWindow om auto-refresh aan te roepen
    void doRefresh();

signals:
    // Signaal om de MainWindow te laten weten dat het venster is gesloten
    void windowClosed();

protected:
    // Re-implementeerde event handlers
    void closeEvent(QCloseEvent *event) override;
    void showEvent(QShowEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    // Slots voor menu-acties
    void onSaveAs();
    void onCopyToClipboard();
    void onRefresh();
    void onAutoRefreshToggled(bool checked);

    // Slots voor UI-interacties
    void onOptionToggled();
    //void onRestoreMap();
    void onEditNamTabAddr();

private:
    // UI Setup
    void setupUI();
    void setupMenus();

    // Port van de originele VCL-tekenmethoden
    void createMap(QPaintDevice *device, int w, int h);
    void createTile(int screen_y);

    // Update-methoden
    void updateChanges();
    void smallUpdateChanges(int screen_y);
    void updateTileInfo(int ix, int iy);
    void updateVdpRegisters();

    // Helper voor hex-conversie
    QString intToHex(int value, int width);
    void updatePalette();

    // UI-componenten
    QMenuBar *m_menuBar;
    QMenu *m_fileMenu;
    QMenu *m_viewMenu;

    QAction *m_copyAction;
    QAction *m_saveAction;
    QAction *m_exitAction;
    QAction *m_autoRefreshAction;
    QAction *m_refreshAction;

    // Hoofd-layout widgets
    QLabel *m_nameTableLabel; // Vervangt VRam (TPaintBox)
    QLabel *m_tileLabel;      // Vervangt TileAlone (TPaintBox)

    // Checkboxes
    QCheckBox *m_gridCheck;
    QCheckBox *m_tilesCheck;
    QCheckBox *m_bwCheck;

    // Tile Info Groep
    QGroupBox *m_tileInfoBox;
    QLabel *m_locLabel;
    QLabel *m_tileIndexLabel;
    QLabel *m_namTabAddrLabel;
    QLabel *m_patGenAddrLabel;
    QLabel *m_colTabAddrLabel;
    QLabel *m_tileValueLabels[8]; // Vervangt idTiValue
    //QPushButton *m_restoreMapButton;

    QGroupBox *m_vdpRegBox;
    QLabel *m_vdpRegLabel[8]; // Voor R0 t/m R7
    QLabel *m_vdpRegDesc[8];  // Voor de beschrijvingen
    QGroupBox *m_paletteBox;
    QLabel *m_paletteLabels[16]; // De 16 kleurstalen

    // Off-screen pixmaps (vervangen TBitmap)
    QPixmap m_nameTablePixmap;
    QPixmap m_tilePixmap; // De 8x8 ruwe tile

    // Statusvariabelen
    int m_vramTile;
    int m_namTabVal;
    int m_lastScreenY;
};

#endif // NTABLEWINDOW_H
