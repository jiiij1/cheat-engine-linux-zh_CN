#include "gui/breakpointlist.hpp"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QFont>

namespace ce::gui {

BreakpointListWindow::BreakpointListWindow(BreakpointManager* mgr, QWidget* parent)
    : QMainWindow(parent), mgr_(mgr) {
    setWindowTitle(QObject::tr("Breakpoint List"));
    resize(700, 300);

    auto* central = new QWidget;
    auto* layout = new QVBoxLayout(central);

    table_ = new QTableWidget;
    table_->setColumnCount(6);
    table_->setHorizontalHeaderLabels({"ID", QObject::tr("Address"), QObject::tr("Type"), QObject::tr("Action"), QObject::tr("Hits"), QObject::tr("Enabled")});
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setFont(QFont("Monospace", 9));
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(table_);

    auto* btnRow = new QHBoxLayout;
    auto* removeBtn = new QPushButton(QObject::tr("Remove"));
    connect(removeBtn, &QPushButton::clicked, this, &BreakpointListWindow::onRemove);
    auto* toggleBtn = new QPushButton(QObject::tr("Enable/Disable"));
    connect(toggleBtn, &QPushButton::clicked, this, &BreakpointListWindow::onToggle);
    btnRow->addWidget(removeBtn);
    btnRow->addWidget(toggleBtn);
    btnRow->addStretch();
    layout->addLayout(btnRow);

    setCentralWidget(central);

    refreshTimer_ = new QTimer(this);
    connect(refreshTimer_, &QTimer::timeout, this, &BreakpointListWindow::refresh);
    refreshTimer_->start(500);
    refresh();
}

void BreakpointListWindow::refresh() {
    auto bps = mgr_->list();
    table_->setRowCount(bps.size());
    for (size_t i = 0; i < bps.size(); ++i) {
        auto& bp = bps[i];
        table_->setItem(i, 0, new QTableWidgetItem(QString::number(bp.id)));
        table_->setItem(i, 1, new QTableWidgetItem(QString("0x%1").arg(bp.address, 0, 16)));

        QString type;
        switch (bp.type) {
            case BpType::Execute: type = QObject::tr("Execute"); break;
            case BpType::Write:   type = QObject::tr("Write"); break;
            case BpType::Read:    type = QObject::tr("Read"); break;
            case BpType::Access:  type = QObject::tr("Access"); break;
        }
        table_->setItem(i, 2, new QTableWidgetItem(type));

        QString action;
        switch (bp.action) {
            case BpAction::Break:      action = QObject::tr("Break"); break;
            case BpAction::FindCode:   action = QObject::tr("Find Code"); break;
            case BpAction::FindAccess: action = QObject::tr("Find Access"); break;
            case BpAction::Trace:      action = QObject::tr("Trace"); break;
            default: action = "?"; break;
        }
        table_->setItem(i, 3, new QTableWidgetItem(action));
        table_->setItem(i, 4, new QTableWidgetItem(QString::number(bp.hitCount)));
        table_->setItem(i, 5, new QTableWidgetItem(bp.enabled ? QObject::tr("Yes") : QObject::tr("No")));
    }
}

void BreakpointListWindow::onRemove() {
    auto row = table_->currentRow();
    if (row < 0) return;
    int id = table_->item(row, 0)->text().toInt();
    mgr_->remove(id);
    refresh();
}

void BreakpointListWindow::onToggle() {
    auto row = table_->currentRow();
    if (row < 0) return;
    int id = table_->item(row, 0)->text().toInt();
    auto* bp = mgr_->get(id);
    if (bp) mgr_->setEnabled(id, !bp->enabled);
    refresh();
}

} // namespace ce::gui
