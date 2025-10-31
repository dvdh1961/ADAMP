#ifndef SPRITEWINDOW_H
#define SPRITEWINDOW_H

#include <QMainWindow>
#include <QPixmap>

// Forward declarations
class QAction;
class QMenu;
class QMenuBar;
class QLabel;
class QTableWidget;
class QTableWidgetItem;
class QGroupBox;

// Nodig voor cv_pal32
extern int cv_pal32[16*4];

class SpriteWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit SpriteWindow(QWidget *parent = nullptr);
    ~SpriteWindow();

signals:
    void windowClosed();

public slots:
    void doRefresh();
    void onRefresh();

protected:
    void closeEvent(QCloseEvent *event) override;
    void showEvent(QShowEvent *event) override;

private slots:
    void onSaveAs();
    void onCopyToClipboard();
    void onAutoRefreshToggled(bool checked);
    void onSpriteSelected(QTableWidgetItem *current, QTableWidgetItem *previous);

private:
    void setupUI();
    void setupMenus();

    // Core logica poorten
    void updateChanges();
    void refreshSprite(int spriteIndex, QPixmap &pixmap);
    void updateSpriteList();
    void updateSpriteTable();
    void updateSpriteView();

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
    QTableWidget *m_spriteListWidget; // Vervangt LVSprites

    QGroupBox *m_spriteTableBox;
    QLabel *m_spriteTableLabel;     // Vervangt sprTable (de 4x8 grid)

    QGroupBox *m_spriteInfoBox;
    QLabel *m_attrAddrLabel;        // Vervangt eVVSprCurSAddr
    QLabel *m_tileAddrLabel;        // Vervangt eVVSprCurTAddr
    QLabel *m_tiledisableLabel;
    QLabel *m_tileenableLabel;

    QGroupBox *m_spriteViewBox;
    QLabel *m_spriteViewLabel;      // Vervangt SpriteAlone (96x96 view)
    QLabel *m_spriteSizeLabel;      // Vervangt sprSize

    // Member variabelen
    QPixmap m_spriteTablePixmap;    // Offscreen buffer voor de 4x8 grid
    QPixmap m_spriteViewPixmap;     // Offscreen buffer voor de 16x16 sprite (geschaald)

    int m_selectedSprite;           // sprAct (-1 voor geen)
    bool m_is8x8;                   // spr8x8
    bool m_isZoomed;                // sprzoom
    bool m_spritesDisabled;         // sprdisable
};

#endif // SPRITEWINDOW_H
