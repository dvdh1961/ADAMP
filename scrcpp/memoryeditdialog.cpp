#include "memoryeditdialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpressionValidator>
#include <QMessageBox>
#include <QFontDatabase>
#include <QHeaderView>
#include <QIcon>
#include <QPixmap>
#include <QDebug>
#include <QClipboard>
#include <QGuiApplication>
#include <algorithm>
#include <QScreen>
#include <QtGlobal>
#include <QColor>

// Constants
static const int BYTES_PER_LINE = 16;
static const int MAX_BYTES = 16;

// =========================================================================
// Custom Delegate & Helpers
// =========================================================================

class HexItemDelegate : public QStyledItemDelegate
{
public:
    explicit HexItemDelegate(QObject *parent = nullptr) : QStyledItemDelegate(parent) {}

    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QLineEdit *editor = new QLineEdit(parent);

        QRegularExpression hexRegex("^[0-9A-F]{1,2}$");

        QRegularExpressionValidator *validator = new QRegularExpressionValidator(hexRegex, editor);
        editor->setValidator(validator);
        editor->setMaxLength(2);
        editor->setAlignment(Qt::AlignCenter);
        return editor;
    }

    void setModelData(QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const override
    {
        QLineEdit *lineEdit = qobject_cast<QLineEdit *>(editor);
        if (lineEdit) {
            QString text = lineEdit->text().toUpper();
            if (text.length() == 1) {
                text.prepend('0');
            }
            model->setData(index, text, Qt::EditRole);
        }
    }
};

// Converteert 16 bytes naar een enkele hexadecimale string gescheiden door spaties (voor Kopiëren)
static QString bytesToHexString(const uint8_t *data)
{
    QString s;
    for (int i = 0; i < BYTES_PER_LINE; ++i) {
        s.append(QString("%1").arg(data[i], 2, 16, QChar('0')).toUpper());
        if (i < BYTES_PER_LINE - 1) {
            s.append(" ");
        }
    }
    return s;
}

static bool hexStringToBytes(const QString &hexString, uint8_t *data)
{
    QString s = hexString.toUpper().simplified();
    QStringList parts = s.split(' ');

    if (parts.size() != BYTES_PER_LINE) {
        return false;
    }

    bool ok;
    for (int i = 0; i < BYTES_PER_LINE; ++i) {
        if (parts[i].length() == 1) {
            parts[i].prepend('0');
        } else if (parts[i].length() != 2) {
            return false;
        }

        uint value = parts[i].toUInt(&ok, 16);
        if (!ok) return false;
        data[i] = static_cast<uint8_t>(value);
    }
    return true;
}

// =========================================================================
// MemoryEditDialog Implementatie
// =========================================================================

// Statische helper functie
bool MemoryEditDialog::editMemoryLine(QWidget *parent, uint32_t address, uint8_t *data16)
{
    MemoryEditDialog dialog(parent, address, data16);
    if (dialog.exec() == QDialog::Accepted) {
        std::copy(dialog.m_data, dialog.m_data + BYTES_PER_LINE, data16);
        return true;
    }
    return false;
}

MemoryEditDialog::MemoryEditDialog(QWidget *parent, uint32_t address, const uint8_t *data16)
    : QDialog(parent)
    , m_address(address)
{
    std::copy(data16, data16 + BYTES_PER_LINE, m_data);

    setupUi();
    loadData(m_data);

    // Koppel de signalen
    connect(m_okButton, &QPushButton::clicked, this, &MemoryEditDialog::onOkClicked);
    connect(m_cancelButton, &QPushButton::clicked, this, &MemoryEditDialog::onCancelClicked);
    connect(m_copyButton, &QPushButton::clicked, this, &MemoryEditDialog::onCopyClicked);
    connect(m_pasteButton, &QPushButton::clicked, this, &MemoryEditDialog::onPasteClicked);
}

// Helper functie om de data uit de tabel naar de buffer te halen
void MemoryEditDialog::retrieveData(uint8_t *data16)
{
    bool ok;
    for (int i = 0; i < MAX_BYTES; ++i) {
        // Row 0 is the read-only position header (0..F).
        // Row 1 contains the editable byte values.
        QTableWidgetItem *item = m_byteTable->item(1, i);
        if (item) {
            QString text = item->text().trimmed().toUpper();
            uint value = text.toUInt(&ok, 16);
            if (ok) {
                data16[i] = static_cast<uint8_t>(value);
            } else {
                data16[i] = 0x00;
            }
        }
    }
}

void MemoryEditDialog::onCopyClicked()
{
    uint8_t data16[MAX_BYTES];
    retrieveData(data16);

    QString hexString = bytesToHexString(data16);

    QClipboard *clipboard = QGuiApplication::clipboard();
    if (clipboard) {
        clipboard->setText(hexString);
    }
}

void MemoryEditDialog::onPasteClicked()
{
    QClipboard *clipboard = QGuiApplication::clipboard();
    if (!clipboard) return;

    QString clipboardText = clipboard->text();
    uint8_t data16[MAX_BYTES];

    // Controleer of de data geldig is (16 bytes in hexadecimaal)
    if (hexStringToBytes(clipboardText, data16)) {
        for (int i = 0; i < MAX_BYTES; ++i) {
            QString hexValue = QString("%1").arg(data16[i], 2, 16, QChar('0')).toUpper();
            QTableWidgetItem *item = m_byteTable->item(1, i);

            if (!item) {
                item = new QTableWidgetItem();
                m_byteTable->setItem(1, i, item);
            }

            item->setText(hexValue);
            item->setTextAlignment(Qt::AlignCenter);
        }

        m_byteTable->setCurrentCell(1, 0);
    } else {
        QMessageBox::warning(this, tr("Invalid Content!"),
                             tr("The clipboard content is not a valid memory row (16 bytes). Ensure the data consists of 16 groups of 2 hexadecimal characters separated by spaces."));
    }
}

void MemoryEditDialog::setupUi()
{
    setWindowTitle(tr("Edit data"));

    // Wider dialog so all 16 byte fields fit cleanly.
    setFixedSize(760, 185);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    QGridLayout *dataLayout = new QGridLayout();

    // Adres veld
    m_addressLabel = new QLabel(tr("Address:"), this);
    m_addressEdit = new QLineEdit(this);
    m_addressEdit->setReadOnly(true);
    m_addressEdit->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));

    // Geheugentabel
    QLabel *dataLabel = new QLabel(tr("16 Bytes (HEX):"), this);
    m_byteTable = new QTableWidget(2, MAX_BYTES, this);
    m_byteTable->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_byteTable->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_byteTable->setFixedSize(650, 58);

    // Configureer de tabel
    m_byteTable->horizontalHeader()->hide();
    m_byteTable->verticalHeader()->hide();
    m_byteTable->setShowGrid(true);
    m_byteTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_byteTable->setSelectionBehavior(QAbstractItemView::SelectItems);
    m_byteTable->setEditTriggers(QAbstractItemView::DoubleClicked |
                                 QAbstractItemView::SelectedClicked |
                                 QAbstractItemView::AnyKeyPressed);

    const int columnWidth = 38;
    for (int i = 0; i < MAX_BYTES; ++i) {
        m_byteTable->setColumnWidth(i, columnWidth);
    }

    m_byteTable->setRowHeight(0, 22);
    m_byteTable->setRowHeight(1, 28);
    m_byteTable->setItemDelegate(new HexItemDelegate(m_byteTable));

    // Read-only position header row: 0..F in green.
    for (int i = 0; i < MAX_BYTES; ++i) {
        QTableWidgetItem *posItem = new QTableWidgetItem(QString("%1").arg(i, 1, 16).toUpper());
        posItem->setTextAlignment(Qt::AlignCenter);
        posItem->setForeground(QColor("#00FF66"));
        posItem->setBackground(QColor("#202020"));
        posItem->setFlags(Qt::ItemIsEnabled); // readonly, not selectable/editable
        m_byteTable->setItem(0, i, posItem);
    }

    // Layout opbouw
    dataLayout->addWidget(m_addressLabel, 0, 0);
    dataLayout->addWidget(m_addressEdit, 0, 1);
    dataLayout->addWidget(dataLabel, 1, 0);
    dataLayout->addWidget(m_byteTable, 1, 1);

    mainLayout->addLayout(dataLayout);
    mainLayout->addSpacing(10);

    QHBoxLayout *buttonLayout = new QHBoxLayout();

    // Iconen laden
    QIcon okIcon(":/images/images/OK.png");
    QIcon cancelIcon(":/images/images/CANCEL.png");
    QIcon copyIcon(":/images/images/COPY.png");
    QIcon pasteIcon(":/images/images/PASTE.png");

    QPixmap okPixmap(":/images/images/OK.png");
    QPixmap cancelPixmap(":/images/images/CANCEL.png");
    QPixmap copyPixmap(":/images/images/COPY.png");
    QPixmap pastePixmap(":/images/images/PASTE.png");

    int commonWidth  = qMax(okPixmap.width(), qMax(cancelPixmap.width(), qMax(copyPixmap.width(), pastePixmap.width())));
    int commonHeight = qMax(okPixmap.height(), qMax(cancelPixmap.height(), qMax(copyPixmap.height(), pastePixmap.height())));
    QSize commonSize(commonWidth, commonHeight);

    QString buttonStyle =
        "QPushButton { border: none; background: transparent; }"
        "QPushButton:pressed { padding-top: 2px; padding-left: 2px; }";

    // --- Knoppen Initialisatie ---

    // Kopieer Knop
    m_copyButton = new QPushButton("", this);
    m_copyButton->setIcon(copyIcon);
    m_copyButton->setIconSize(copyPixmap.size());
    m_copyButton->setFixedSize(commonSize);
    m_copyButton->setStyleSheet(buttonStyle);
    m_copyButton->setFlat(true);

    // Plak Knop
    m_pasteButton = new QPushButton("", this);
    m_pasteButton->setIcon(pasteIcon);
    m_pasteButton->setIconSize(pastePixmap.size());
    m_pasteButton->setFixedSize(commonSize);
    m_pasteButton->setStyleSheet(buttonStyle);
    m_pasteButton->setFlat(true);

    // OK Knop
    m_okButton = new QPushButton("", this);
    m_okButton->setIcon(okIcon);
    m_okButton->setIconSize(okPixmap.size());
    m_okButton->setFixedSize(commonSize);
    m_okButton->setStyleSheet(buttonStyle);
    m_okButton->setFlat(true);
    m_okButton->setDefault(true);

    // Annuleren Knop
    m_cancelButton = new QPushButton("", this);
    m_cancelButton->setIcon(cancelIcon);
    m_cancelButton->setIconSize(cancelPixmap.size());
    m_cancelButton->setFixedSize(commonSize);
    m_cancelButton->setStyleSheet(buttonStyle);
    m_cancelButton->setFlat(true);
    m_cancelButton->setShortcut(QKeySequence(Qt::Key_Escape));

    // Layout opbouw van de knoppen
    buttonLayout->addStretch();
    buttonLayout->addWidget(m_copyButton);
    buttonLayout->addWidget(m_pasteButton);
    buttonLayout->addSpacing(15);
    buttonLayout->addWidget(m_okButton);
    buttonLayout->addWidget(m_cancelButton);

    mainLayout->addLayout(buttonLayout);
    setLayout(mainLayout);
}

void MemoryEditDialog::loadData(const uint8_t *data16)
{
    m_addressEdit->setText(QString("0x%1").arg(m_address, 4, 16, QChar('0')).toUpper());

    for (int i = 0; i < MAX_BYTES; ++i) {
        QString hexValue = QString("%1").arg(data16[i], 2, 16, QChar('0')).toUpper();
        QTableWidgetItem *item = new QTableWidgetItem(hexValue);

        item->setTextAlignment(Qt::AlignCenter);

        m_byteTable->setItem(1, i, item);
    }

    m_byteTable->setCurrentCell(1, 0);
    m_okButton->setEnabled(true);
}

void MemoryEditDialog::onOkClicked()
{
    retrieveData(m_data);
    accept();
}

void MemoryEditDialog::onCancelClicked()
{
    reject();
}
