/// ELF inspector — thin wrapper over binutils' readelf. Tabbed view.

#include "gui/elfinspector.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QFileDialog>
#include <QFont>
#include <QProcess>
#include <QStandardPaths>

namespace ce::gui {

ElfInspector::ElfInspector(const QString& initialPath, QWidget* parent) : QDialog(parent) {
    setWindowTitle(QObject::tr("ELF Inspector"));
    resize(900, 640);

    auto* root = new QVBoxLayout(this);

    auto* row = new QHBoxLayout;
    row->addWidget(new QLabel(QObject::tr("ELF file:")));
    pathEdit_ = new QLineEdit;
    pathEdit_->setText(initialPath);
    row->addWidget(pathEdit_, /*stretch=*/1);
    auto* browseBtn = new QPushButton(QObject::tr("Browse…"));
    auto* loadBtn   = new QPushButton(QObject::tr("Load"));
    row->addWidget(browseBtn);
    row->addWidget(loadBtn);
    root->addLayout(row);

    connect(browseBtn, &QPushButton::clicked, this, &ElfInspector::onBrowse);
    connect(loadBtn,   &QPushButton::clicked, this, &ElfInspector::onLoad);

    tabs_ = new QTabWidget;
    auto makePane = [&](const QString& title) {
        auto* edit = new QPlainTextEdit;
        edit->setReadOnly(true);
        edit->setFont(QFont("Monospace", 9));
        tabs_->addTab(edit, title);
        return edit;
    };
    headerView_  = makePane(QObject::tr("Header"));
    sectionView_ = makePane(QObject::tr("Sections"));
    programView_ = makePane(QObject::tr("Segments"));
    dynamicView_ = makePane(QObject::tr("Dynamic"));
    symbolView_  = makePane(QObject::tr("Symbols"));
    notesView_   = makePane(QObject::tr("Notes"));

    root->addWidget(tabs_, 1);

    auto* btnRow = new QHBoxLayout;
    btnRow->addStretch();
    auto* closeBtn = new QPushButton(QObject::tr("Close"));
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    btnRow->addWidget(closeBtn);
    root->addLayout(btnRow);

    if (!initialPath.isEmpty()) onLoad();
}

void ElfInspector::onBrowse() {
    auto path = QFileDialog::getOpenFileName(this, QObject::tr("Choose ELF file"),
        pathEdit_->text(), QObject::tr("All Files (*)"));
    if (!path.isEmpty()) pathEdit_->setText(path);
}

void ElfInspector::runReadelf(QPlainTextEdit* sink, const QStringList& args) {
    sink->setPlainText(QObject::tr(""));
    // Resolve readelf to a trusted absolute path once, so a poisoned $PATH
    // (cecore commonly runs as root) cannot substitute a malicious binary.
    static const QString readelfPath = QStandardPaths::findExecutable("readelf");
    if (readelfPath.isEmpty()) {
        sink->setPlainText(QObject::tr("readelf not found in PATH (install binutils)."));
        return;
    }
    QProcess p;
    p.setProgram(readelfPath);
    p.setArguments(args);
    p.start();
    if (!p.waitForFinished(10'000)) {
        sink->setPlainText(QObject::tr("readelf timed out or is not installed (install binutils)."));
        return;
    }
    QByteArray out = p.readAllStandardOutput();
    QByteArray err = p.readAllStandardError();
    QString text = QString::fromUtf8(out);
    if (!err.isEmpty())
        text += QObject::tr("\n--- stderr ---\n") + QString::fromUtf8(err);
    if (text.isEmpty())
        text = QObject::tr("(readelf produced no output)");
    sink->setPlainText(text);
}

void ElfInspector::onLoad() {
    auto path = pathEdit_->text();
    if (path.isEmpty()) return;
    runReadelf(headerView_,  {"-hW",  path});
    runReadelf(sectionView_, {"-SW",  path});
    runReadelf(programView_, {"-lW",  path});
    runReadelf(dynamicView_, {"-dW",  path});
    runReadelf(symbolView_,  {"-sW",  path});
    runReadelf(notesView_,   {"-nW",  path});
}

} // namespace ce::gui
