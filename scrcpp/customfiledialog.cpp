#include "customfiledialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QTreeView>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QDir>
#include <QStringList>
#include <QHeaderView>

CustomFileDialog::CustomFileDialog(QWidget *parent)
    : QDialog(parent), m_acceptMode(AcceptOpen)
{
    setWindowFlags(windowFlags() & ~Qt::WindowCloseButtonHint);
    // 1. Het model aanmaken dat het bestandssysteem leest
    m_model = new QFileSystemModel(this);
    m_model->setRootPath(QDir::rootPath());
    // Filter om mappen en bestanden te tonen, maar geen '.' of '..'

    // Vertel het model dat het onze provider moet gebruiken
    // voor het ophalen van alle iconen.
    m_model->setIconProvider(&m_iconProvider);

    m_model->setFilter(QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot);

    // We geven de kolommen die we gaan tonen een duidelijke naam
    m_model->setHeaderData(0, Qt::Horizontal, "Bestandsnaam");
    m_model->setHeaderData(1, Qt::Horizontal, "Grootte");

    // 1. De TreeView (de bestandsbrowser)
    m_treeView = new QTreeView(this);
    m_treeView->setModel(m_model);
    // 2. Stel de lettergrootte en het type in
    m_treeView->setFont(QFont("Roboto", 10)); // Gebruik 10pt Roboto

    // 3. Stel de icoongrootte in (dit bepaalt ook de minimale rijhoogte)
    m_treeView->setIconSize(QSize(32, 32));

    m_treeView->setRootIndex(m_model->index(QDir::homePath())); // Start in de home-map

    m_treeView->setColumnHidden(1, false); // Toon 'Grootte' <-- NIEUW
    m_treeView->setColumnHidden(2, true);  // Verberg 'Type'
    m_treeView->setColumnHidden(3, true);  // Verberg 'Date Modified'
    m_treeView->header()->setVisible(true); // Toon de header (voor kolomnamen) <-- NIEUW

    m_treeView->setSortingEnabled(true);
    m_treeView->sortByColumn(0, Qt::AscendingOrder);

    m_treeView->header()->setSectionResizeMode(0, QHeaderView::Fixed);
    m_treeView->setColumnWidth(0, 500); // Stel hier je vaste breedte in    // Maak "Grootte" zo breed als nodig
    m_treeView->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);

    m_treeView->setAlternatingRowColors(true);

    // Definieer al onze kleuren
    QColor m_rowLight = QColor(60, 60, 60);
    QColor m_rowDark  = QColor(40, 40, 40);
    QColor m_selActiveBg = QColor(51, 153, 255); // Standaard selectie-blauw
    QColor m_selActiveText = QColor(Qt::black);
    QColor m_selInactiveBg = QColor(217, 217, 217); // Standaard inactief-grijs
    QColor m_selInactiveText = QColor(Qt::black);

    // Pas de kleuren toe via een stylesheet
    m_treeView->setStyleSheet(QString(
                                  "QTreeView {"
                                  "    background-color: %1;"           // Standaard achtergrond (licht)
                                  "    border: none;"                    // Geen lelijke rand
                                  "}"
                                  "QTreeView::item:alternate {"
                                  "    background-color: %2;"           // Alternatieve achtergrond (donker)
                                  "}"
                                  "QTreeView::item:selected:active {"    // --- NIEUW ---
                                  "    background-color: %3;"           // Actieve selectie (blauw)
                                  "    color: %4;"                      // Tekstkleur bij actieve selectie (wit)
                                  "}"
                                  "QTreeView::item:selected:!active {"   // --- NIEUW ---
                                  "    background-color: %5;"           // Inactieve selectie (grijs)
                                  "    color: %6;"                      // Tekstkleur bij inactieve selectie (zwart)
                                  "}"
                                  ).arg(m_rowLight.name(),         // %1
                                       m_rowDark.name(),          // %2
                                       m_selActiveBg.name(),      // %3
                                       m_selActiveText.name(),    // %4
                                       m_selInactiveBg.name(),    // %5
                                       m_selInactiveText.name()   // %6
                                       ));
    // 3. Huidige pad (alleen-lezen)
    m_pathEdit = new QLineEdit(this);
    m_pathEdit->setReadOnly(true);
    m_pathEdit->setText(QDir::homePath());

    // 4. Bestandsnaam (voor typen of selectie)
    m_fileNameEdit = new QLineEdit(this);

    // 5. Filter ComboBox
    m_filterComboBox = new QComboBox(this);

    // 6. Knoppen
    m_okButton = new QPushButton(this);
    m_cancelButton = new QPushButton(this);

    // 1. Laad de iconen EN de pixmaps (om de grootte te lezen)
    QIcon okIcon(":/images/images/OK.png");
    QIcon cancelIcon(":/images/images/CANCEL.png");
    QPixmap okPixmap(":/images/images/OK.png");
    QPixmap cancelPixmap(":/images/images/CANCEL.png");

    // Optionele check (altijd goed om te doen)
    if (okIcon.isNull()) { qWarning() << "CustomFileDialog: Kon OK.png niet laden."; }
    if (cancelIcon.isNull()) { qWarning() << "CustomFileDialog: Kon CANCEL.png niet laden."; }

    // 2. Krijg de 'originele' groottes van de PNGs
    QSize okSize = okPixmap.size();
    QSize cancelSize = cancelPixmap.size();

    // 3. Pas de OK-knop aan
    m_okButton->setIcon(okIcon);
    m_okButton->setIconSize(okSize);      // Gebruik PNG-grootte
    m_okButton->setFixedSize(okSize);     // Gebruik PNG-grootte
    m_okButton->setText("");
    m_okButton->setFlat(true);
    m_okButton->setStyleSheet(
        "QPushButton { border: none; background: transparent; }"
        "QPushButton:pressed { padding-top: 2px; padding-left: 2px; }"
        );

    // 4. Pas de Cancel-knop aan
    m_cancelButton->setIcon(cancelIcon);
    m_cancelButton->setIconSize(cancelSize);      // Gebruik PNG-grootte
    m_cancelButton->setFixedSize(cancelSize);     // Gebruik PNG-grootte
    m_cancelButton->setText("");
    m_cancelButton->setFlat(true);
    m_cancelButton->setStyleSheet(
        "QPushButton { border: none; background: transparent; }"
        "QPushButton:pressed { padding-top: 2px; padding-left: 2px; }"
        );

    // --- Layout opbouwen  ---
    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    // Bovenste deel: Pad
    mainLayout->addWidget(new QLabel("Map:", this));
    mainLayout->addWidget(m_pathEdit);

    // Middelste deel: Browser
    mainLayout->addWidget(m_treeView, 1); // 1 = stretch factor

    // Onderste deel: Selectieformulier
    QGridLayout *formLayout = new QGridLayout();
    formLayout->addWidget(new QLabel("Bestandsnaam:", this), 0, 0);
    formLayout->addWidget(m_fileNameEdit, 0, 1);
    formLayout->addWidget(new QLabel("Filter:", this), 1, 0);
    formLayout->addWidget(m_filterComboBox, 1, 1);
    mainLayout->addLayout(formLayout);

    // Knoppenbalk onderaan
    QHBoxLayout *buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();
    buttonLayout->addWidget(m_okButton);
    buttonLayout->addWidget(m_cancelButton);
    mainLayout->addLayout(buttonLayout);

    // --- Connecties  ---
    connect(m_treeView, &QTreeView::clicked, this, &CustomFileDialog::onTreeViewClicked);
    connect(m_treeView, &QTreeView::doubleClicked, this, &CustomFileDialog::onTreeViewDoubleClicked);
    connect(m_filterComboBox, &QComboBox::currentTextChanged, this, &CustomFileDialog::onFilterChanged);
    connect(m_okButton, &QPushButton::clicked, this, &CustomFileDialog::onOkButtonClicked);
    connect(m_cancelButton, &QPushButton::clicked, this, &QDialog::reject);

    // Standaard grootte
    resize(700, 500);
}
// --- Static Functies (de publieke API) ---

QString CustomFileDialog::getOpenFileName(QWidget *parent, const QString &caption,
                                          const QString &dir, const QString &filter)
{
    CustomFileDialog dlg(parent);
    dlg.m_acceptMode = AcceptOpen;              // <-- TOEVOEGEN
    dlg.setWindowTitle(caption);
    dlg.setInitialDirectory(dir);
    dlg.setNameFilters(filter);

    if (dlg.exec() == QDialog::Accepted) {
        return dlg.selectedFile();
    }
    return QString();
}

QString CustomFileDialog::getSaveFileName(QWidget *parent, const QString &caption,
                                          const QString &dir, const QString &filter)
{
    CustomFileDialog dlg(parent);
    dlg.m_acceptMode = AcceptSave;              // <-- TOEVOEGEN
    dlg.setWindowTitle(caption);
    dlg.setInitialDirectory(dir);
    dlg.setNameFilters(filter);

    if (dlg.exec() == QDialog::Accepted) {
        return dlg.selectedFile();
    }
    return QString();
}

// --- Public Methods ---

QString CustomFileDialog::selectedFile() const
{
    // Combineer het huidige pad met de bestandsnaam in het invoerveld
    return QDir(m_pathEdit->text()).filePath(m_fileNameEdit->text());
}

// --- Protected Methods (Setup) ---

void CustomFileDialog::setInitialDirectory(const QString &dir)
{
    QString path = dir;
    QString fileName;

    if (!dir.isEmpty()) {
        QFileInfo fi(dir);

        if (fi.isDir()) {
            // dir is een map
            path = fi.absoluteFilePath();
        } else {
            // dir is (waarschijnlijk) een bestandspad
            path = fi.absolutePath();
            fileName = fi.fileName();
        }
    }

    // Fallback als path niet bestaat
    if (!QDir(path).exists()) {
        path = QDir::homePath();
        fileName.clear();
    }

    QModelIndex index = m_model->index(path);
    if (index.isValid()) {
        m_treeView->setRootIndex(index);
        m_treeView->scrollTo(index);
        m_pathEdit->setText(path);
    }

    if (!fileName.isEmpty()) {
        m_fileNameEdit->setText(fileName);
    }
}

void CustomFileDialog::setNameFilters(const QString &filter)
{
    // Voorbeeld filter: "ROM Bestanden (*.rom *.col);;Alle bestanden (*.*)"
    QStringList filters = filter.split(";;");
    m_filterComboBox->addItems(filters);
    onFilterChanged(filters.at(0)); // Activeer de eerste filter
}

// --- Slots (Event Handlers) ---

void CustomFileDialog::onTreeViewClicked(const QModelIndex &index)
{
    if (!index.isValid()) return;

    QString filePath = m_model->filePath(index);

    if (m_model->isDir(index)) {
        // Geklikt op een map: maak het tekstvak leeg
        m_fileNameEdit->clear();
    } else {
        // Geklikt op een bestand: vul het tekstvak
        QFileInfo fi(filePath);
        m_fileNameEdit->setText(fi.fileName());
    }
}

void CustomFileDialog::onTreeViewDoubleClicked(const QModelIndex &index)
{
    if (!index.isValid()) return;

    if (m_model->isDir(index)) {
        // Dubbelklik op map: navigeer erin
        m_treeView->setRootIndex(index);
        m_pathEdit->setText(m_model->filePath(index));
        m_fileNameEdit->clear();
    } else {
        // Dubbelklik op bestand: accepteer dialoog
        onTreeViewClicked(index); // Zorg dat filename gevuld is
        onOkButtonClicked();
    }
}

void CustomFileDialog::onFilterChanged(const QString &filter)
{
    // Parse de filter string, bv: "ROM Bestanden (*.rom *.col)"
    m_filterPatterns.clear();
    int start = filter.indexOf("(*");
    int end = filter.indexOf(")", start);
    if (start != -1 && end != -1) {
        QString patterns = filter.mid(start + 1, end - start - 1);
        //QString patterns = filter.substring(start + 1, end - start - 1);
        m_filterPatterns = patterns.split(" ", Qt::SkipEmptyParts);
    }

    if (m_filterPatterns.contains("*.*") || m_filterPatterns.isEmpty()) {
        m_model->setNameFilters(QStringList()); // Geen filter
    } else {
        m_model->setNameFilters(m_filterPatterns);
    }
}

void CustomFileDialog::onOkButtonClicked()
{
    // Basisvalidatie
    if (m_fileNameEdit->text().isEmpty()) {
        // Geen bestand geselecteerd/getypt
        return;
    }

    m_selectedFile = selectedFile();
    QFileInfo fi(m_selectedFile);

    // Controleer of het bestand de juiste extensie heeft
    if (m_acceptMode == AcceptSave && !m_filterPatterns.isEmpty() && !m_filterPatterns.contains("*.*")) {
        bool extensionMatch = false;
        QString suffix = fi.suffix();
        for (const QString &pattern : m_filterPatterns) {
            if (pattern.endsWith(suffix, Qt::CaseInsensitive)) {
                extensionMatch = true;
                break;
            }
        }
        // Voeg automatisch de eerste extensie toe als er geen is
        if (!extensionMatch && fi.suffix().isEmpty()) {
            //QString defaultExt = m_filterPatterns.at(0).remove(0, 1); // Verwijder '*'
            QString defaultExt = m_filterPatterns.at(0).mid(1);
            m_selectedFile += defaultExt;
        }
    }

    // Controleer bij 'Open' of het bestand bestaat
    if (m_acceptMode == AcceptOpen) {
        if (!fi.exists() || !fi.isFile()) {
            // TODO: Je kunt hier een QMessageBox tonen
            return;
        }
    }

    accept();
}
