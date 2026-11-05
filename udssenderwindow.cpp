#include "udssenderwindow.h"
#include "ui_udssenderwindow.h"

#include <QHeaderView>
#include <QRegularExpression>
#include <QSignalBlocker>

static QString normalizeRequest(const QString &input)
{
    static const QRegularExpression hexBytes(QStringLiteral("^[0-9a-fA-F]{2}(?:\\s*[0-9a-fA-F]{2})*$"));
    static const QRegularExpression whitespace(QStringLiteral("\\s+"));
    QString request = input.trimmed();
    if (!hexBytes.match(request).hasMatch()) return QString();

    request.remove(whitespace);
    return QString::fromLatin1(QByteArray::fromHex(request.toLatin1()).toHex(' ').toUpper());
}

UdsSenderWindow::UdsSenderWindow(QWidget *parent) :
    QDialog(parent, Qt::Window),
    ui(new Ui::UdsSenderWindow)
{
    ui->setupUi(this);
    setAutoFillBackground(true);
    ui->splitter->setSizes({240, 680});
    ui->splitter->setStretchFactor(1, 1);
    ui->requestSplitter->setSizes({370, 200});
    ui->tableQueue->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    ui->tableQueue->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);

    auto addService = [this](const QString &name, const QString &request) {
        auto *item = new QTreeWidgetItem(ui->treeServices, QStringList{name});
        item->setData(0, Qt::UserRole, request);
        item->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
        return item;
    };
    addService(tr("Raw request"), QString());
    auto *session = addService(tr("Diagnostic Session Control (10)"), QStringLiteral("10 03"));
    addService(tr("ECU Reset (11)"), QStringLiteral("11 01"));
    addService(tr("Read DTC Information (19)"), QStringLiteral("19 02 FF"));
    auto *readDid = addService(tr("Read Data by Identifier (22)"), QStringLiteral("22 F1 90"));
    addService(tr("Write Data by Identifier (2E)"), QStringLiteral("2E"));
    addService(tr("Routine Control (31)"), QStringLiteral("31"));
    addService(tr("Tester Present (3E)"), QStringLiteral("3E 00"));

    auto addSubService = [](QTreeWidgetItem *service, const QString &name, const QString &request) {
        auto *item = new QTreeWidgetItem(service, QStringList{name});
        item->setData(0, Qt::UserRole, request);
        return item;
    };
    addSubService(session, tr("Default Session"), QStringLiteral("10 01"));
    addSubService(session, tr("Extended Session"), QStringLiteral("10 03"));
    addSubService(readDid, tr("Read VIN (F190)"), QStringLiteral("22 F1 90"));

    connect(ui->treeServices, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *item) {
        if (!item) return;
        ui->txtName->setText(item->text(0));
        ui->txtRequest->setText(item->data(0, Qt::UserRole).toString());
        ui->lblError->clear();
        ui->txtRequest->setFocus();
    });
    connect(ui->btnAddSubService, &QPushButton::clicked, this, [this, addSubService] {
        auto *service = ui->treeServices->currentItem();
        if (!service) return;
        if (service->parent()) service = service->parent();

        const QString payload = normalizeRequest(ui->txtRequest->text());
        if (payload.isEmpty())
        {
            ui->lblError->setText(tr("Enter complete hexadecimal bytes, for example 22 F1 90."));
            ui->txtRequest->setFocus();
            return;
        }

        const QString name = ui->txtName->text().trimmed();
        auto *item = addSubService(service, name.isEmpty() ? payload : name, payload);
        service->setExpanded(true);
        ui->treeServices->setCurrentItem(item);
    });
    connect(ui->tableQueue, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *item) {
        if (item->column() != 2) return;
        const QString payload = normalizeRequest(item->text());
        const QSignalBlocker blocker(ui->tableQueue);
        if (payload.isEmpty())
        {
            item->setText(item->data(Qt::UserRole).toString());
            ui->lblError->setText(tr("Enter complete hexadecimal bytes, for example 22 F1 90."));
            return;
        }

        item->setText(payload);
        item->setData(Qt::UserRole, payload);
        if (item->row() == ui->tableQueue->currentRow()) ui->txtRequest->setText(payload);
        ui->lblError->clear();
    });
    connect(ui->btnAdd, &QPushButton::clicked, this, [this] { writeRequest(-1); });
    connect(ui->btnUpdate, &QPushButton::clicked, this, [this] {
        int row = ui->tableQueue->currentRow();
        if (row >= 0) writeRequest(row);
    });
    connect(ui->btnRemove, &QPushButton::clicked, this, [this] {
        int row = ui->tableQueue->currentRow();
        if (row < 0) return;
        ui->tableQueue->setCurrentCell(-1, -1);
        ui->tableQueue->removeRow(row);
        int remaining = ui->tableQueue->rowCount();
        if (remaining > 0) ui->tableQueue->selectRow(qMin(row, remaining - 1));
    });
    connect(ui->btnUp, &QPushButton::clicked, this, [this] { moveRequest(-1); });
    connect(ui->btnDown, &QPushButton::clicked, this, [this] { moveRequest(1); });
    connect(ui->tableQueue, &QTableWidget::currentCellChanged, this, [this](int row) {
        const bool selected = row >= 0;
        ui->btnUpdate->setEnabled(selected);
        ui->btnRemove->setEnabled(selected);
        ui->btnUp->setEnabled(row > 0);
        ui->btnDown->setEnabled(selected && row + 1 < ui->tableQueue->rowCount());
        if (!selected) return;
        ui->txtName->setText(ui->tableQueue->item(row, 1)->text());
        ui->txtRequest->setText(ui->tableQueue->item(row, 2)->text());
        ui->lblError->clear();
    });
    ui->treeServices->setCurrentItem(ui->treeServices->topLevelItem(0));
}

UdsSenderWindow::~UdsSenderWindow()
{
    delete ui;
}

void UdsSenderWindow::writeRequest(int row)
{
    const QString payload = normalizeRequest(ui->txtRequest->text());
    if (payload.isEmpty())
    {
        ui->lblError->setText(tr("Enter complete hexadecimal bytes, for example 22 F1 90."));
        ui->txtRequest->setFocus();
        return;
    }

    QString name = ui->txtName->text().trimmed();
    if (name.isEmpty()) name = tr("Raw request");

    if (row < 0)
    {
        row = ui->tableQueue->rowCount();
        ui->tableQueue->insertRow(row);
        auto *enabled = new QTableWidgetItem;
        enabled->setFlags(enabled->flags() & ~Qt::ItemIsEditable);
        enabled->setCheckState(Qt::Checked);
        ui->tableQueue->setItem(row, 0, enabled);
        auto *nameItem = new QTableWidgetItem(name);
        nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
        ui->tableQueue->setItem(row, 1, nameItem);
        auto *payloadItem = new QTableWidgetItem(payload);
        payloadItem->setData(Qt::UserRole, payload);
        ui->tableQueue->setItem(row, 2, payloadItem);
    }

    ui->tableQueue->item(row, 1)->setText(name);
    ui->tableQueue->item(row, 2)->setText(payload);
    ui->tableQueue->selectRow(row);
    ui->txtName->setText(name);
    ui->txtRequest->setText(payload);
    ui->lblError->clear();
}

void UdsSenderWindow::moveRequest(int offset)
{
    const int row = ui->tableQueue->currentRow();
    const int destination = row + offset;
    if (row < 0 || destination < 0 || destination >= ui->tableQueue->rowCount()) return;

    for (int column = 0; column < ui->tableQueue->columnCount(); ++column)
    {
        auto *item = ui->tableQueue->takeItem(row, column);
        ui->tableQueue->setItem(row, column, ui->tableQueue->takeItem(destination, column));
        ui->tableQueue->setItem(destination, column, item);
    }
    ui->tableQueue->selectRow(destination);
}
