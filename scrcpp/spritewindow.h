#ifndef SPRITEWINDOW_H
#define SPRITEWINDOW_H

#include <QMainWindow>
#include <QPixmap>

class QAction;
class QMenu;
class QMenuBar;
class QLabel;
class QTableWidget;
class QTableWidgetItem;
class QGroupBox;

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

    void updateChanges();
    void refreshSprite(int spriteIndex, QPixmap &pixmap);
    void updateSpriteList();
    void updateSpriteTable();
    void updateSpriteView();

    QString intToHex(int value, int width);

    QMenuBar *m_menuBar;
    QMenu *m_fileMenu;
    QMenu *m_viewMenu;
    QAction *m_copyAction;
    QAction *m_saveAction;
    QAction *m_exitAction;
    QAction *m_autoRefreshAction;
    QAction *m_refreshAction;

    QTableWidget *m_spriteListWidget;
    QGroupBox *m_spriteTableBox;
    QLabel *m_spriteTableLabel;

    QGroupBox *m_spriteInfoBox;
    QLabel *m_attrAddrLabel;
    QLabel *m_tileAddrLabel;
    QLabel *m_tiledisableLabel;
    QLabel *m_tileenableLabel;

    QGroupBox *m_spriteViewBox;
    QLabel *m_spriteViewLabel;
    QLabel *m_spriteSizeLabel;

    QPixmap m_spriteTablePixmap;
    QPixmap m_spriteViewPixmap;

    int m_selectedSprite;
    bool m_is8x8;
    bool m_isZoomed;
    bool m_spritesDisabled;
};

#endif
