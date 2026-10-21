#include "ControlsPage.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QCompleter>
#include <QDateTimeEdit>
#include <QDial>
#include <QDoubleSpinBox>
#include <QFontDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLCDNumber>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollBar>
#include <QSlider>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextEdit>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {

QIcon swatch(const QColor &color)
{
    QPixmap pixmap(32, 32);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setBrush(color);
    painter.setPen(QPen(color.darker(160), 2));
    painter.drawRoundedRect(QRectF(3, 3, 26, 26), 6, 6);
    return QIcon(pixmap);
}

} // namespace

ControlsPage::ControlsPage(QWidget *parent)
    : QScrollArea(parent)
{
    setWidgetResizable(true);
    setFrameShape(QFrame::NoFrame);

    auto *content = new QWidget;
    auto *grid = new QGridLayout(content);
    grid->setContentsMargins(16, 16, 16, 16);
    grid->setHorizontalSpacing(16);
    grid->setVerticalSpacing(16);
    grid->addWidget(buttonsGroup(), 0, 0);
    grid->addWidget(textGroup(), 0, 1);
    grid->addWidget(rangesGroup(), 1, 0);
    grid->addWidget(viewsGroup(), 1, 1);
    grid->setColumnStretch(0, 1);
    grid->setColumnStretch(1, 1);
    grid->setRowStretch(2, 1);
    setWidget(content);
}

QGroupBox *ControlsPage::buttonsGroup()
{
    auto *group = new QGroupBox(QStringLiteral("Buttons, menus and dialogs"));
    auto *layout = new QVBoxLayout(group);

    auto *row = new QHBoxLayout;
    auto *push = new QPushButton(QStringLiteral("Push button"));
    push->setToolTip(QStringLiteral("A plain QPushButton. Hover long enough and this tooltip, "
                                    "a top-level window of its own, is composited too."));
    connect(push, &QPushButton::clicked, this, [this, clicks = 0]() mutable {
        emit message(QStringLiteral("Push button clicked %1 time(s)").arg(++clicks));
    });
    row->addWidget(push);

    auto *toggle = new QPushButton(QStringLiteral("Toggle"));
    toggle->setCheckable(true);
    connect(toggle, &QPushButton::toggled, this, [this, toggle](bool on) {
        toggle->setText(on ? QStringLiteral("Toggled on") : QStringLiteral("Toggle"));
        emit message(on ? QStringLiteral("Toggle is on") : QStringLiteral("Toggle is off"));
    });
    row->addWidget(toggle);

    auto *menuButton = new QToolButton;
    menuButton->setText(QStringLiteral("Menu"));
    menuButton->setPopupMode(QToolButton::InstantPopup);
    menuButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    auto *menu = new QMenu(menuButton);
    for (const QString &name : { QStringLiteral("New"), QStringLiteral("Open"),
                                 QStringLiteral("Duplicate") }) {
        menu->addAction(name, this, [this, name] { emit message(name + QStringLiteral(" chosen")); });
    }
    QMenu *sub = menu->addMenu(QStringLiteral("Export as"));
    for (const QString &name : { QStringLiteral("PNG"), QStringLiteral("PDF"), QStringLiteral("SVG") })
        sub->addAction(name, this, [this, name] { emit message(QStringLiteral("Export as ") + name); });
    menu->addSeparator();
    QAction *checkable = menu->addAction(QStringLiteral("Show grid"));
    checkable->setCheckable(true);
    connect(checkable, &QAction::toggled, this, [this](bool on) {
        emit message(on ? QStringLiteral("Grid shown") : QStringLiteral("Grid hidden"));
    });
    menuButton->setMenu(menu);
    row->addWidget(menuButton);
    row->addStretch();
    layout->addLayout(row);

    auto *checks = new QHBoxLayout;
    auto *check = new QCheckBox(QStringLiteral("Check box"));
    check->setChecked(true);
    auto *tristate = new QCheckBox(QStringLiteral("Tristate"));
    tristate->setTristate(true);
    tristate->setCheckState(Qt::PartiallyChecked);
    for (QCheckBox *box : { check, tristate }) {
        connect(box, &QCheckBox::checkStateChanged, this, [this, box](Qt::CheckState state) {
            static const char *names[] = { "unchecked", "partially checked", "checked" };
            emit message(box->text() + QStringLiteral(" is ") + QLatin1String(names[state]));
        });
        checks->addWidget(box);
    }
    checks->addStretch();
    layout->addLayout(checks);

    auto *radios = new QHBoxLayout;
    auto *radioGroup = new QButtonGroup(group);
    const QStringList sizes = { QStringLiteral("Small"), QStringLiteral("Medium"),
                                QStringLiteral("Large") };
    for (int i = 0; i < sizes.size(); ++i) {
        auto *radio = new QRadioButton(sizes.at(i));
        radio->setChecked(i == 1);
        radioGroup->addButton(radio, i);
        radios->addWidget(radio);
    }
    connect(radioGroup, &QButtonGroup::idClicked, this, [this, sizes](int id) {
        emit message(QStringLiteral("Size: ") + sizes.at(id));
    });
    radios->addStretch();
    layout->addLayout(radios);

    // Each of these opens a top-level dialog. They are composited into the
    // tab like the main window and block it while they are open.
    auto *dialogs = new QHBoxLayout;
    auto *question = new QPushButton(QStringLiteral("Message box…"));
    connect(question, &QPushButton::clicked, this, [this] {
        const auto answer = QMessageBox::question(
                this, QStringLiteral("Question"),
                QStringLiteral("Does this dialog look like part of the tab?"),
                QMessageBox::Yes | QMessageBox::No);
        emit message(answer == QMessageBox::Yes ? QStringLiteral("Answered yes")
                                                : QStringLiteral("Answered no"));
    });
    auto *color = new QPushButton(swatch(QColor(QStringLiteral("#3f6bd8"))),
                                  QStringLiteral("Color…"));
    connect(color, &QPushButton::clicked, this, [this, color] {
        const QColor chosen = QColorDialog::getColor(QColor(QStringLiteral("#3f6bd8")), this,
                                                     QStringLiteral("Pick a color"));
        if (!chosen.isValid())
            return;
        color->setIcon(swatch(chosen));
        emit message(QStringLiteral("Picked ") + chosen.name());
    });
    auto *font = new QPushButton(QStringLiteral("Font…"));
    connect(font, &QPushButton::clicked, this, [this] {
        bool ok = false;
        const QFont chosen = QFontDialog::getFont(&ok, this->font(), this,
                                                  QStringLiteral("Pick a font"));
        if (ok)
            emit message(QStringLiteral("Picked %1, %2 pt").arg(chosen.family()).arg(chosen.pointSize()));
    });
    auto *input = new QPushButton(QStringLiteral("Input…"));
    connect(input, &QPushButton::clicked, this, [this] {
        bool ok = false;
        const QString text = QInputDialog::getText(this, QStringLiteral("Your name"),
                                                   QStringLiteral("Name (any script):"),
                                                   QLineEdit::Normal, QString(), &ok);
        if (ok)
            emit message(QStringLiteral("Hello, ") + text);
    });
    for (QPushButton *button : { question, color, font, input })
        dialogs->addWidget(button);
    dialogs->addStretch();
    layout->addLayout(dialogs);
    layout->addStretch();
    return group;
}

QGroupBox *ControlsPage::textGroup()
{
    auto *group = new QGroupBox(QStringLiteral("Text input"));
    auto *form = new QFormLayout(group);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    auto *line = new QLineEdit;
    line->setPlaceholderText(QStringLiteral("Type here; IME works too (你好, こんにちは)"));
    line->setClearButtonEnabled(true);
    connect(line, &QLineEdit::returnPressed, this, [this, line] {
        emit message(QStringLiteral("Entered \"%1\"").arg(line->text()));
    });
    form->addRow(QStringLiteral("Line edit"), line);

    auto *password = new QLineEdit;
    password->setEchoMode(QLineEdit::Password);
    password->setPlaceholderText(QStringLiteral("The IME stays off here"));
    form->addRow(QStringLiteral("Password"), password);

    auto *combo = new QComboBox;
    combo->addItems({ QStringLiteral("Tokyo"), QStringLiteral("Beijing"), QStringLiteral("Paris"),
                      QStringLiteral("New York"), QStringLiteral("Hà Nội"), QStringLiteral("Cairo"),
                      QStringLiteral("Lima"), QStringLiteral("Oslo") });
    connect(combo, &QComboBox::currentTextChanged, this,
            [this](const QString &text) { emit message(QStringLiteral("City: ") + text); });
    form->addRow(QStringLiteral("Combo box"), combo);

    auto *editable = new QComboBox;
    editable->setEditable(true);
    editable->addItems({ QStringLiteral("apple"), QStringLiteral("apricot"), QStringLiteral("banana"),
                         QStringLiteral("blueberry"), QStringLiteral("cherry"), QStringLiteral("北京"),
                         QStringLiteral("東京") });
    editable->setCurrentText(QString());
    editable->lineEdit()->setPlaceholderText(QStringLiteral("Type to complete"));
    editable->completer()->setCompletionMode(QCompleter::PopupCompletion);
    form->addRow(QStringLiteral("Editable"), editable);

    auto *numbers = new QHBoxLayout;
    auto *spin = new QSpinBox;
    spin->setRange(0, 1000);
    spin->setValue(42);
    auto *doubleSpin = new QDoubleSpinBox;
    doubleSpin->setRange(0, 10);
    doubleSpin->setSingleStep(0.25);
    doubleSpin->setSuffix(QStringLiteral(" s"));
    doubleSpin->setValue(1.5);
    numbers->addWidget(spin);
    numbers->addWidget(doubleSpin);
    form->addRow(QStringLiteral("Spin boxes"), numbers);

    auto *date = new QDateTimeEdit(QDateTime::currentDateTime());
    date->setCalendarPopup(true);
    date->setDisplayFormat(QStringLiteral("yyyy-MM-dd  HH:mm"));
    connect(date, &QDateTimeEdit::dateTimeChanged, this, [this](const QDateTime &value) {
        emit message(QStringLiteral("Date: ") + value.toString(QStringLiteral("yyyy-MM-dd HH:mm")));
    });
    form->addRow(QStringLiteral("Date (popup)"), date);

    auto *rich = new QTextEdit;
    rich->setHtml(QStringLiteral(
            "<p><b>Rich text</b> in a <span style='color:#8ab4ff'>QTextEdit</span>. "
            "Select, scroll, and compose text with an input method.</p>"
            "<ul><li>第一行 / 一行目</li><li><i>Italic</i> and <u>underlined</u></li>"
            "<li>Second line in English</li></ul>"));
    rich->setMinimumHeight(110);
    form->addRow(QStringLiteral("Text edit"), rich);
    return group;
}

QGroupBox *ControlsPage::rangesGroup()
{
    auto *group = new QGroupBox(QStringLiteral("Ranges"));
    auto *layout = new QGridLayout(group);

    auto *slider = new QSlider(Qt::Horizontal);
    slider->setRange(0, 100);
    slider->setValue(35);
    slider->setTickPosition(QSlider::TicksBelow);
    slider->setTickInterval(10);

    auto *progress = new QProgressBar;
    progress->setRange(0, 100);
    progress->setValue(slider->value());

    auto *dial = new QDial;
    dial->setRange(0, 100);
    dial->setNotchesVisible(true);
    dial->setValue(slider->value());
    dial->setFixedSize(84, 84);

    auto *lcd = new QLCDNumber(3);
    lcd->setSegmentStyle(QLCDNumber::Flat);
    lcd->display(slider->value());
    lcd->setMinimumHeight(48);

    auto *vertical = new QSlider(Qt::Vertical);
    vertical->setRange(0, 100);
    vertical->setValue(slider->value());
    vertical->setMinimumHeight(84);

    auto *scroll = new QScrollBar(Qt::Horizontal);
    scroll->setRange(0, 100);
    scroll->setPageStep(20);
    scroll->setValue(slider->value());

    // One value, five views of it.
    const auto sync = [=](int value) {
        for (QAbstractSlider *s : { static_cast<QAbstractSlider *>(slider),
                                    static_cast<QAbstractSlider *>(dial),
                                    static_cast<QAbstractSlider *>(vertical),
                                    static_cast<QAbstractSlider *>(scroll) }) {
            if (s->value() != value)
                s->setValue(value);
        }
        progress->setValue(value);
        lcd->display(value);
    };
    for (QAbstractSlider *s : { static_cast<QAbstractSlider *>(slider),
                                static_cast<QAbstractSlider *>(dial),
                                static_cast<QAbstractSlider *>(vertical),
                                static_cast<QAbstractSlider *>(scroll) }) {
        connect(s, &QAbstractSlider::valueChanged, this, sync);
    }
    connect(slider, &QSlider::sliderReleased, this, [this, slider] {
        emit message(QStringLiteral("Value %1").arg(slider->value()));
    });

    layout->addWidget(new QLabel(QStringLiteral("Slider")), 0, 0);
    layout->addWidget(slider, 0, 1, 1, 3);
    layout->addWidget(new QLabel(QStringLiteral("Progress")), 1, 0);
    layout->addWidget(progress, 1, 1, 1, 3);
    layout->addWidget(new QLabel(QStringLiteral("Scroll bar")), 2, 0);
    layout->addWidget(scroll, 2, 1, 1, 3);
    layout->addWidget(dial, 3, 1, Qt::AlignLeft);
    layout->addWidget(vertical, 3, 2, Qt::AlignHCenter);
    layout->addWidget(lcd, 3, 3);
    layout->setColumnStretch(3, 1);
    layout->setRowStretch(4, 1);
    return group;
}

QGroupBox *ControlsPage::viewsGroup()
{
    auto *group = new QGroupBox(QStringLiteral("Item views"));
    auto *layout = new QVBoxLayout(group);
    auto *tabs = new QTabWidget;

    auto *list = new QListWidget;
    const QList<QPair<QString, QColor>> colors = {
        { QStringLiteral("Azure"), QColor(QStringLiteral("#8ab4ff")) },
        { QStringLiteral("Mint"), QColor(QStringLiteral("#7ce3b0")) },
        { QStringLiteral("Amber"), QColor(QStringLiteral("#f0b775")) },
        { QStringLiteral("Rose"), QColor(QStringLiteral("#ff8095")) },
        { QStringLiteral("Lilac"), QColor(QStringLiteral("#c9a2ff")) },
        { QStringLiteral("Slate"), QColor(QStringLiteral("#8d97ac")) },
    };
    for (const auto &entry : colors) {
        auto *item = new QListWidgetItem(swatch(entry.second), entry.first, list);
        item->setFlags(item->flags() | Qt::ItemIsEditable);
    }
    list->setDragDropMode(QAbstractItemView::InternalMove);
    connect(list, &QListWidget::currentTextChanged, this,
            [this](const QString &text) { emit message(QStringLiteral("List: ") + text); });
    tabs->addTab(list, QStringLiteral("List"));

    auto *tree = new QTreeWidget;
    tree->setHeaderLabels({ QStringLiteral("Name"), QStringLiteral("Kind"), QStringLiteral("Size") });
    const auto add = [](QTreeWidgetItem *parent, const QStringList &columns) {
        auto *item = new QTreeWidgetItem(parent, columns);
        item->setFlags(item->flags() | Qt::ItemIsEditable);
        return item;
    };
    auto *root = new QTreeWidgetItem(tree, { QStringLiteral("QMLBrowser"), QStringLiteral("Folder"), QString() });
    auto *client = add(root, { QStringLiteral("client"), QStringLiteral("Folder"), QString() });
    add(client, { QStringLiteral("BrowserTab.cpp"), QStringLiteral("C++"), QStringLiteral("11 KB") });
    add(client, { QStringLiteral("TabViewport.cpp"), QStringLiteral("C++"), QStringLiteral("12 KB") });
    auto *renderer = add(root, { QStringLiteral("renderer"), QStringLiteral("Folder"), QString() });
    add(renderer, { QStringLiteral("OffscreenPage.cpp"), QStringLiteral("C++"), QStringLiteral("19 KB") });
    auto *widgetapp = add(root, { QStringLiteral("widgetapp"), QStringLiteral("Folder"), QString() });
    add(widgetapp, { QStringLiteral("BrowserBridge.cpp"), QStringLiteral("C++"), QStringLiteral("24 KB") });
    add(widgetapp, { QStringLiteral("GalleryWindow.cpp"), QStringLiteral("C++"), QStringLiteral("8 KB") });
    tree->expandAll();
    tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    tabs->addTab(tree, QStringLiteral("Tree"));

    auto *table = new QTableWidget(8, 4);
    table->setHorizontalHeaderLabels({ QStringLiteral("Item"), QStringLiteral("Qty"),
                                       QStringLiteral("Price"), QStringLiteral("Total") });
    const QStringList items = { QStringLiteral("Paper"), QStringLiteral("Pens"), QStringLiteral("Ink"),
                                QStringLiteral("Stapler"), QStringLiteral("Folders"),
                                QStringLiteral("Tape"), QStringLiteral("Clips"), QStringLiteral("Labels") };
    for (int row = 0; row < items.size(); ++row) {
        const int qty = 1 + (row * 7) % 9;
        const double price = 0.75 + row * 1.35;
        table->setItem(row, 0, new QTableWidgetItem(items.at(row)));
        table->setItem(row, 1, new QTableWidgetItem(QString::number(qty)));
        table->setItem(row, 2, new QTableWidgetItem(QString::number(price, 'f', 2)));
        auto *total = new QTableWidgetItem(QString::number(qty * price, 'f', 2));
        total->setFlags(total->flags() & ~Qt::ItemIsEditable);
        table->setItem(row, 3, total);
    }
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table->setAlternatingRowColors(true);
    connect(table, &QTableWidget::cellChanged, this, [this, table](int row, int column) {
        if (column != 1 && column != 2)
            return;
        const double qty = table->item(row, 1)->text().toDouble();
        const double price = table->item(row, 2)->text().toDouble();
        QSignalBlocker block(table);
        table->item(row, 3)->setText(QString::number(qty * price, 'f', 2));
        emit message(QStringLiteral("Row %1 updated").arg(row + 1));
    });
    tabs->addTab(table, QStringLiteral("Table"));

    layout->addWidget(tabs);
    auto *hint = new QLabel(QStringLiteral("Double-click a list, tree or table entry to edit it in place."));
    hint->setWordWrap(true);
    hint->setForegroundRole(QPalette::PlaceholderText);
    layout->addWidget(hint);
    return group;
}
