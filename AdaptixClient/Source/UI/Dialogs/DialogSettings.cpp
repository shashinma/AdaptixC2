#include <UI/MainUI.h>
#include <UI/Widgets/AdaptixWidget.h>
#include <UI/Dialogs/DialogSettings.h>
#include <Client/Requestor.h>
#include <Client/AuthProfile.h>
#include <Utils/Logs.h>
#include <QJsonParseError>
#include <QPointer>
#include <UI/Widgets/DockWidgetRegister.h>
#include <MainAdaptix.h>
#include <Client/Settings.h>
#include <Client/ConsoleTheme.h>
#include <Utils/TitleBarStyle.h>
#include <QShowEvent>
#include <QFileDialog>
#include <algorithm>
#include <oclero/qlementine.hpp>

DialogSettings::DialogSettings(Settings* s)
{
    settings = s;

    this->createUI();

    connect(themeCombo,         &QComboBox::currentTextChanged, buttonApply, [this](const QString &text){buttonApply->setEnabled(true);} );
    connect(fontFamilyCombo,    &QComboBox::currentTextChanged, buttonApply, [this](const QString &text){buttonApply->setEnabled(true);} );
    connect(fontSizeSpin,       &QSpinBox::valueChanged,        buttonApply, [this](int){buttonApply->setEnabled(true);} );
    connect(sessionsCoafSpin,   &QDoubleSpinBox::valueChanged,  buttonApply, [this](double){buttonApply->setEnabled(true);} );
    connect(sessionsOffsetSpin, &QSpinBox::valueChanged,        buttonApply, [this](int){buttonApply->setEnabled(true);} );
    connect(terminalSizeSpin,   &QSpinBox::valueChanged,        buttonApply, [this](int){buttonApply->setEnabled(true);} );
    connect(consoleSizeSpin,    &QSpinBox::valueChanged,        buttonApply, [this](int){buttonApply->setEnabled(true);} );
    connect(consoleThemeCombo, &QComboBox::currentTextChanged, buttonApply, [this](const QString &){buttonApply->setEnabled(true);} );

    connect(consoleTimeCheckbox,           &oclero::qlementine::Switch::toggled, buttonApply, [this](bool){buttonApply->setEnabled(true);} );
    connect(consoleNoWrapCheckbox,         &oclero::qlementine::Switch::toggled, buttonApply, [this](bool){buttonApply->setEnabled(true);} );
    connect(consoleAutoScrollCheckbox,     &oclero::qlementine::Switch::toggled, buttonApply, [this](bool){buttonApply->setEnabled(true);} );
    connect(consoleShowBackgroundCheckbox, &oclero::qlementine::Switch::toggled, buttonApply, [this](bool){buttonApply->setEnabled(true);} );
    connect(sessionsHealthCheck,           &oclero::qlementine::Switch::toggled, this, &DialogSettings::onHealthChange );

    for ( int i = 0; i < sessionsCheckCount; i++)
        connect(sessionsCheck[i],  &QCheckBox::checkStateChanged, buttonApply, [this](int){buttonApply->setEnabled(true);} );

    connect(graphCombo1, &QComboBox::currentTextChanged, buttonApply, [this](const QString &text){buttonApply->setEnabled(true);} );

    connect(tabblinkEnabledCheckbox, &oclero::qlementine::Switch::toggled, this, &DialogSettings::onBlinkChange );

    for (auto* check : m_tabblinkChecks)
        connect(check, &QCheckBox::checkStateChanged, buttonApply, [this](int){buttonApply->setEnabled(true);} );


    for ( int i = 0; i < 11; i++)
        connect(tasksCheck[i],  &QCheckBox::checkStateChanged, buttonApply, [this](int){buttonApply->setEnabled(true);} );


    connect(listSettings, &QListWidget::currentRowChanged, this, &DialogSettings::onStackChange);
    connect(buttonApply,  &QPushButton::clicked,           this, &DialogSettings::onApply);
    connect(buttonClose,  &QPushButton::clicked,           this, &DialogSettings::onClose);
}

void DialogSettings::createUI()
{
    this->setWindowTitle("Adaptix Settings");
    this->resize(600, 300);
    this->setProperty("Main", "base");

    mainSettingWidget = new QWidget(this);
    mainSettingLayout = new QGridLayout(mainSettingWidget);

    themeLabel = new QLabel("Main theme: ", mainSettingWidget);
    themeCombo = new QComboBox(mainSettingWidget);
    refreshAppThemeCombo();
    themeImportBtn = new QPushButton("Import", mainSettingWidget);
    themeImportBtn->setFixedWidth(80);
    connect(themeImportBtn, &QPushButton::clicked, this, [this]() {
        QString filePath = QFileDialog::getOpenFileName(this, "Import Application Theme", QString(), "JSON files (*.json)");
        if (filePath.isEmpty()) return;
        if (importAppTheme(filePath)) {
            QString name = QFileInfo(filePath).baseName();
            refreshAppThemeCombo();
            themeCombo->setCurrentText(name);
            buttonApply->setEnabled(true);
        }
    });

    fontSizeLabel = new QLabel("Font size: ", mainSettingWidget);
    fontSizeSpin  = new QSpinBox(mainSettingWidget);
    fontSizeSpin->setMinimum(7);
    fontSizeSpin->setMaximum(30);

    fontFamilyLabel = new QLabel("Font family: ", mainSettingWidget);
    fontFamilyCombo = new QComboBox(mainSettingWidget);
    fontFamilyCombo->addItem("Adaptix - JetBrains Mono");
    fontFamilyCombo->addItem("Adaptix - Hack");
    fontFamilyCombo->addItem("Qlementine - Inter");
    fontFamilyCombo->addItem("Qlementine - Roboto Mono");

    graphLabel1 = new QLabel("Session Graph version:", mainSettingWidget);
    graphCombo1 = new QComboBox(mainSettingWidget);
    graphCombo1->addItem("Version 1");
    graphCombo1->addItem("Version 2");
    graphCombo1->addItem("Version 3");

    terminalSizeLabel = new QLabel("RemoteTerminal buffer (lines):", mainSettingWidget);
    terminalSizeSpin  = new QSpinBox(mainSettingWidget);
    terminalSizeSpin->setMinimum(1);
    terminalSizeSpin->setMaximum(100000);

    consoleGroup  = new QGroupBox("Agent Console", mainSettingWidget);

    consoleSizeLabel = new QLabel("Buffer size (lines):", consoleGroup);
    consoleSizeSpin  = new QSpinBox(consoleGroup);
    consoleSizeSpin->setMinimum(10000);
    consoleSizeSpin->setMaximum(1000000);

    consoleTimeCheckbox           = new oclero::qlementine::Switch(consoleGroup);
    consoleTimeCheckbox->setText("Print date and time");
    consoleNoWrapCheckbox         = new oclero::qlementine::Switch(consoleGroup);
    consoleNoWrapCheckbox->setText("No Wrap mode");
    consoleAutoScrollCheckbox     = new oclero::qlementine::Switch(consoleGroup);
    consoleAutoScrollCheckbox->setText("Auto Scroll mode");
    consoleShowBackgroundCheckbox = new oclero::qlementine::Switch(consoleGroup);
    consoleShowBackgroundCheckbox->setText("Show background image");

    consoleThemeLabel = new QLabel("Console theme:", consoleGroup);
    consoleThemeCombo = new QComboBox(consoleGroup);
    refreshConsoleThemeCombo();
    consoleThemeImportBtn = new QPushButton("Import", consoleGroup);
    consoleThemeImportBtn->setFixedWidth(80);
    connect(consoleThemeImportBtn, &QPushButton::clicked, this, [this]() {
        QString filePath = QFileDialog::getOpenFileName(this, "Import Console Theme", QString(), "JSON files (*.json)");
        if (filePath.isEmpty()) return;
        if (ConsoleThemeManager::instance().importTheme(filePath)) {
            QString name = QFileInfo(filePath).baseName();
            refreshConsoleThemeCombo();
            consoleThemeCombo->setCurrentText(name);
            buttonApply->setEnabled(true);
        }
    });

    consoleGroupLayout = new QGridLayout(consoleGroup);
    consoleGroupLayout->addWidget(consoleSizeLabel,              0, 0, 1, 1);
    consoleGroupLayout->addWidget(consoleSizeSpin,               0, 1, 1, 2);
    consoleGroupLayout->addWidget(consoleTimeCheckbox,           1, 0, 1, 3);
    consoleGroupLayout->addWidget(consoleNoWrapCheckbox,         2, 0, 1, 3);
    consoleGroupLayout->addWidget(consoleAutoScrollCheckbox,     3, 0, 1, 3);
    consoleGroupLayout->addWidget(consoleShowBackgroundCheckbox, 4, 0, 1, 3);
    consoleGroupLayout->addWidget(consoleThemeLabel,             5, 0, 1, 1);
    consoleGroupLayout->addWidget(consoleThemeCombo,             5, 1, 1, 1);
    consoleGroupLayout->addWidget(consoleThemeImportBtn,         5, 2, 1, 1);
    consoleGroup->setLayout(consoleGroupLayout);

    mainSettingLayout->addWidget(themeLabel,        0, 0, 1, 1);
    mainSettingLayout->addWidget(themeCombo,        0, 1, 1, 1);
    mainSettingLayout->addWidget(themeImportBtn,    0, 2, 1, 1);
    mainSettingLayout->addWidget(fontFamilyLabel,   1, 0, 1, 1);
    mainSettingLayout->addWidget(fontFamilyCombo,   1, 1, 1, 2);
    mainSettingLayout->addWidget(fontSizeLabel,     2, 0, 1, 1);
    mainSettingLayout->addWidget(fontSizeSpin,      2, 1, 1, 2);
    mainSettingLayout->addWidget(graphLabel1,       3, 0, 1, 1);
    mainSettingLayout->addWidget(graphCombo1,       3, 1, 1, 2);
    mainSettingLayout->addWidget(terminalSizeLabel, 4, 0, 1, 1);
    mainSettingLayout->addWidget(terminalSizeSpin,  4, 1, 1, 2);
    mainSettingLayout->addWidget(consoleGroup,      5, 0, 1, 3);

    mainSettingWidget->setLayout(mainSettingLayout);

    sessionsWidget = new QWidget(this);
    sessionsLayout = new QGridLayout(sessionsWidget);
    sessionsGroup  = new QGroupBox("Columns", sessionsWidget);

    QStringList sessionsCheckboxLabels = {
        "Agent ID", "Agent Type", "External", "Listener", "Internal",
        "Domain", "Computer", "User", "OS", "Process",
        "PID", "TID", "Tags", "Created", "Last", "Sleep"
    };

    for (int i = 0; i < sessionsCheckCount; ++i)
        sessionsCheck[i] = new QCheckBox(sessionsCheckboxLabels[i], sessionsGroup);

    sessionsGroupLayout = new QGridLayout(sessionsGroup);
    sessionsGroupLayout->addWidget(sessionsCheck[0],  0, 0, 1, 1);
    sessionsGroupLayout->addWidget(sessionsCheck[1],  0, 1, 1, 1);
    sessionsGroupLayout->addWidget(sessionsCheck[2],  1, 0, 1, 1);
    sessionsGroupLayout->addWidget(sessionsCheck[3],  1, 1, 1, 1);
    sessionsGroupLayout->addWidget(sessionsCheck[4],  2, 0, 1, 1);
    sessionsGroupLayout->addWidget(sessionsCheck[5],  2, 1, 1, 1);
    sessionsGroupLayout->addWidget(sessionsCheck[6],  3, 0, 1, 1);
    sessionsGroupLayout->addWidget(sessionsCheck[7],  3, 1, 1, 1);
    sessionsGroupLayout->addWidget(sessionsCheck[8],  4, 0, 1, 1);
    sessionsGroupLayout->addWidget(sessionsCheck[9],  4, 1, 1, 1);
    sessionsGroupLayout->addWidget(sessionsCheck[10], 5, 0, 1, 1);
    sessionsGroupLayout->addWidget(sessionsCheck[11], 5, 1, 1, 1);
    sessionsGroupLayout->addWidget(sessionsCheck[12], 6, 0, 1, 1);
    sessionsGroupLayout->addWidget(sessionsCheck[13], 6, 1, 1, 1);
    sessionsGroupLayout->addWidget(sessionsCheck[14], 7, 0, 1, 1);
    sessionsGroupLayout->addWidget(sessionsCheck[15], 7, 1, 1, 1);
    sessionsGroup->setLayout(sessionsGroupLayout);

    sessionsHealthCheck = new oclero::qlementine::Switch(sessionsWidget);
    sessionsHealthCheck->setText("Check Health");

    QSpacerItem* horizontalSpacer = new QSpacerItem(40, 20, QSizePolicy::Expanding, QSizePolicy::Minimum);

    sessionsLabel1 = new QLabel(sessionsWidget);
    sessionsLabel1->setText("Sleeptime *");

    sessionsLabel2 = new QLabel(sessionsWidget);
    sessionsLabel2->setText("+");

    sessionsLabel3 = new QLabel(sessionsWidget);
    sessionsLabel3->setText("sec");

    sessionsCoafSpin = new QDoubleSpinBox(sessionsWidget);
    sessionsCoafSpin->setMinimum(1.0);
    sessionsCoafSpin->setMaximum(5.0);
    sessionsCoafSpin->setSingleStep(0.1);

    sessionsOffsetSpin = new QSpinBox(sessionsWidget);
    sessionsOffsetSpin->setMinimum(1);
    sessionsOffsetSpin->setMaximum(10000);

    sessionsLayout->addWidget(sessionsGroup, 0, 0, 1, 7);
    sessionsLayout->addWidget(sessionsHealthCheck, 1, 0, 1, 1);
    sessionsLayout->addItem(horizontalSpacer, 1, 1, 1, 1);
    sessionsLayout->addWidget(sessionsLabel1, 1, 2, 1, 1);
    sessionsLayout->addWidget(sessionsCoafSpin, 1, 3, 1, 1);
    sessionsLayout->addWidget(sessionsLabel2, 1, 4, 1, 1);
    sessionsLayout->addWidget(sessionsOffsetSpin, 1, 5, 1, 1);
    sessionsLayout->addWidget(sessionsLabel3, 1, 6, 1, 1);

    sessionsWidget->setLayout(sessionsLayout);


    tasksWidget = new QWidget(this);
    tasksLayout = new QGridLayout(tasksWidget);
    tasksGroup  = new QGroupBox("Columns", tasksWidget);

    QStringList tasksCheckboxLabels = {
        "Task ID", "Task Type", "Agent ID", "Client", "User",
        "Computer", "Start Time", "Finish Time", "Commandline",
        "Result", "Output"
    };

    for (int i = 0; i < 11; ++i)
        tasksCheck[i] = new QCheckBox(tasksCheckboxLabels[i], tasksGroup);

    tasksGroupLayout = new QGridLayout(tasksGroup);
    tasksGroupLayout->addWidget(tasksCheck[0], 0, 0, 1, 1);
    tasksGroupLayout->addWidget(tasksCheck[1], 0, 1, 1, 1);
    tasksGroupLayout->addWidget(tasksCheck[2], 1, 0, 1, 1);
    tasksGroupLayout->addWidget(tasksCheck[3], 1, 1, 1, 1);
    tasksGroupLayout->addWidget(tasksCheck[4], 2, 0, 1, 1);
    tasksGroupLayout->addWidget(tasksCheck[5], 2, 1, 1, 1);
    tasksGroupLayout->addWidget(tasksCheck[6], 3, 0, 1, 1);
    tasksGroupLayout->addWidget(tasksCheck[7], 3, 1, 1, 1);
    tasksGroupLayout->addWidget(tasksCheck[8], 4, 0, 1, 1);
    tasksGroupLayout->addWidget(tasksCheck[9], 4, 1, 1, 1);
    tasksGroupLayout->addWidget(tasksCheck[10], 5, 0, 1, 1);

    tasksGroup->setLayout(tasksGroupLayout);
    tasksLayout->addWidget(tasksGroup, 0, 0, 1, 1);
    tasksWidget->setLayout(tasksLayout);

    tabblinkWidget = new QWidget(this);
    tabblinkEnabledCheckbox = new oclero::qlementine::Switch(tabblinkWidget);
    tabblinkEnabledCheckbox->setText("Enable tab blink");

    tabblinkGroup = new QGroupBox("Blinking tabs", tabblinkWidget);
    tabblinkGroupLayout = new QGridLayout(tabblinkGroup);
    tabblinkGroupLayout->setContentsMargins(15, 15, 15, 15);
    tabblinkGroupLayout->setHorizontalSpacing(40);
    tabblinkGroupLayout->setVerticalSpacing(12);

    auto widgetsList = WidgetRegistry::instance().widgets();
    std::ranges::sort(widgetsList, [](const auto& a, const auto& b) { return a.displayName < b.displayName; });

    // Dynamically create checkboxes from registry
    int row = 0, col = 0;
    for (const auto& info : widgetsList) {
        auto* check = new QCheckBox(info.displayName, tabblinkGroup);
        check->setChecked(info.defaultState);
        tabblinkGroupLayout->addWidget(check, row, col);
        m_tabblinkChecks[info.className] = check;

        col++;
        if (col > 1) { col = 0; row++; }
    }
    tabblinkGroup->setLayout(tabblinkGroupLayout);

    tabblinkLayout = new QGridLayout(tabblinkWidget);
    tabblinkLayout->addWidget(tabblinkEnabledCheckbox, 0, 0, 1, 1);
    tabblinkLayout->addWidget(tabblinkGroup,           1, 0, 1, 1);
    tabblinkLayout->setRowStretch(3, 1);

    tabblinkWidget->setLayout(tabblinkLayout);

    servicesWidget = new QWidget(this);
    servicesLayout = new QGridLayout(servicesWidget);
    servicesHintLabel = new QLabel(servicesWidget);
    servicesHintLabel->setWordWrap(true);
    servicesComboLabel = new QLabel("Service:", servicesWidget);
    servicesCombo = new QComboBox(servicesWidget);
    servicesScroll = new QScrollArea(servicesWidget);
    servicesScroll->setWidgetResizable(true);
    servicesScroll->setFrameShape(QFrame::NoFrame);
    servicesFormHost = new QWidget(servicesScroll);
    servicesFormLayout = new QFormLayout(servicesFormHost);
    servicesFormLayout->setContentsMargins(8, 8, 8, 8);
    servicesScroll->setWidget(servicesFormHost);

    servicesLayout->addWidget(servicesHintLabel, 0, 0, 1, 2);
    servicesLayout->addWidget(servicesComboLabel, 1, 0);
    servicesLayout->addWidget(servicesCombo, 1, 1);
    servicesLayout->addWidget(servicesScroll, 2, 0, 1, 2);
    servicesLayout->setRowStretch(2, 1);
    servicesWidget->setLayout(servicesLayout);

    connect(servicesCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        if (!m_lastServicesComboService.isEmpty())
            captureServiceFormDraft(m_lastServicesComboService);
        m_lastServicesComboService = servicesCombo->currentText();
        rebuildServiceFormForSelection();
    });

    listSettings = new QListWidget(this);
    listSettings->setFixedWidth(150);
    listSettings->setSpacing(3);
    listSettings->addItem("Main settings");
    listSettings->addItem("Sessions table");
    listSettings->addItem("Tasks table");
    listSettings->addItem("Blinking tabs");
    listSettings->addItem("Services");
    listSettings->setCurrentRow(0);

    labelHeader = new QLabel(this);
    QFont headerFont = labelHeader->font();
    headerFont.setPointSize(14);
    headerFont.setBold(true);
    labelHeader->setFont(headerFont);
    labelHeader->setContentsMargins(0, 0, 0, 10);
    labelHeader->setText("Main settings");

    lineFrame = new QFrame(this);
    lineFrame->setFrameShape(QFrame::HLine);
    lineFrame->setFrameShadow(QFrame::Plain);

    headerLayout = new QVBoxLayout();
    headerLayout->addWidget(labelHeader);
    headerLayout->addWidget(lineFrame);
    headerLayout->setSpacing(0);

    hSpacer     = new QSpacerItem(40, 20, QSizePolicy::Expanding, QSizePolicy::Minimum);
    buttonClose = new QPushButton("Close", this);

    buttonApply = new QPushButton("Apply ", this);
    buttonApply->setDefault(true);
    buttonApply->setEnabled(false);

    stackSettings = new QStackedWidget(this);
    stackSettings->addWidget(mainSettingWidget);
    stackSettings->addWidget(sessionsWidget);
    stackSettings->addWidget(tasksWidget);
    stackSettings->addWidget(tabblinkWidget);
    stackSettings->addWidget(servicesWidget);

    layoutMain = new QGridLayout(this);
    layoutMain->setContentsMargins(4, 4, 4, 4);
    layoutMain->addWidget(listSettings, 0, 0, 3, 1);
    layoutMain->addLayout(headerLayout, 0, 1, 1, 3);
    layoutMain->addWidget(stackSettings, 1, 1, 1, 3);
    layoutMain->addItem(hSpacer, 2, 1, 1, 1);
    layoutMain->addWidget(buttonApply, 2, 2, 1, 1);
    layoutMain->addWidget(buttonClose, 2, 3, 1, 1);

    this->setLayout(layoutMain);

    int buttonWidth = buttonApply->width();
    buttonApply->setFixedWidth(buttonWidth);
    buttonClose->setFixedWidth(buttonWidth);

    int buttonHeight = buttonClose->height();
    buttonApply->setFixedHeight(buttonHeight);
    buttonClose->setFixedHeight(buttonHeight);
}

void DialogSettings::onStackChange(int index)
{
    QString text = listSettings->item(index)->text();
    labelHeader->setText(text);
    stackSettings->setCurrentIndex(index);
    if (index == stackSettings->count() - 1)
        refreshServicesPanel();
}

void DialogSettings::onHealthChange() const
{
    buttonApply->setEnabled(true);
    bool active = sessionsHealthCheck->isChecked();
    sessionsLabel1->setEnabled(active);
    sessionsLabel2->setEnabled(active);
    sessionsLabel3->setEnabled(active);
    sessionsCoafSpin->setEnabled(active);
    sessionsOffsetSpin->setEnabled(active);
}

void DialogSettings::onBlinkChange() const
{
    buttonApply->setEnabled(true);
    bool active = tabblinkEnabledCheckbox->isChecked();
    tabblinkGroup->setEnabled(active);
}

void DialogSettings::onApply()
{
    buttonApply->setEnabled(false);

    bool themeChanged = settings->data.MainTheme != themeCombo->currentText();
    bool fontChanged  = settings->data.FontSize != fontSizeSpin->value() || settings->data.FontFamily != fontFamilyCombo->currentText();

    if(themeChanged) {
        settings->data.MainTheme = themeCombo->currentText();
        
        if (auto* style = settings->getMainAdaptix()->qlementineStyle) {
            QString userPath = userAppThemeDir() + "/" + settings->data.MainTheme + ".json";
            QString themePath = QFile::exists(userPath) ? userPath : QString(":/qlementine-themes/%1").arg(settings->data.MainTheme);
            style->setThemeJsonPath(themePath);
        }
        
        TitleBarStyle::applyForTheme(settings->getMainAdaptix()->mainUI, settings->data.MainTheme);
    }

    if(fontChanged) {
        settings->data.FontSize   = fontSizeSpin->value();
        settings->data.FontFamily = fontFamilyCombo->currentText();
    }

    if(themeChanged || fontChanged) {
        settings->getMainAdaptix()->ApplyApplicationFont();
    }

    if (settings->data.GraphVersion != graphCombo1->currentText()) {
        settings->data.GraphVersion = graphCombo1->currentText();
        settings->getMainAdaptix()->mainUI->UpdateGraphIcons();
    }

    settings->data.RemoteTerminalBufferSize = terminalSizeSpin->value();

    settings->data.ConsoleBufferSize = consoleSizeSpin->value();
    settings->data.ConsoleTime = consoleTimeCheckbox->isChecked();
    settings->data.ConsoleNoWrap = consoleNoWrapCheckbox->isChecked();
    settings->data.ConsoleAutoScroll = consoleAutoScrollCheckbox->isChecked();

    bool bgChanged = settings->data.ConsoleShowBackground != consoleShowBackgroundCheckbox->isChecked();
    settings->data.ConsoleShowBackground = consoleShowBackgroundCheckbox->isChecked();

    if (settings->data.ConsoleTheme != consoleThemeCombo->currentText() || bgChanged) {
        settings->data.ConsoleTheme = consoleThemeCombo->currentText();
        ConsoleThemeManager::instance().loadTheme(settings->data.ConsoleTheme);
    }

    bool updateTable = false;
    for ( int i = 0; i < sessionsCheckCount; i++) {
        if (settings->data.SessionsTableColumns[i] != sessionsCheck[i]->isChecked()) {
            settings->data.SessionsTableColumns[i] = sessionsCheck[i]->isChecked();
            updateTable = true;
        }
    }
    if (updateTable)
        settings->getMainAdaptix()->mainUI->UpdateSessionsTableColumns();

    settings->data.CheckHealth = sessionsHealthCheck->isChecked();
    settings->data.HealthCoaf = sessionsCoafSpin->value();
    settings->data.HealthOffset = sessionsOffsetSpin->value();

    updateTable = false;
    for ( int i = 0; i < 11; i++) {
        if (settings->data.TasksTableColumns[i] != tasksCheck[i]->isChecked()) {
            settings->data.TasksTableColumns[i] = tasksCheck[i]->isChecked();
            updateTable = true;
        }
    }
    if (updateTable)
        settings->getMainAdaptix()->mainUI->UpdateTasksTableColumns();

    for (auto it = m_tabblinkChecks.begin(); it != m_tabblinkChecks.end(); ++it)
        settings->data.BlinkWidgets[it.key()] = it.value()->isChecked();

    settings->SaveToDB();

    if (m_serviceFormDirty) {
        if (!tryApplyServiceConfig()) {
            buttonApply->setEnabled(true);
            return;
        }
    }
}

void DialogSettings::onClose()
{
    this->close();
}

void DialogSettings::loadSettings()
{
    themeCombo->setCurrentText(settings->data.MainTheme);
    fontFamilyCombo->setCurrentText(settings->data.FontFamily);
    fontSizeSpin->setValue(settings->data.FontSize);
    graphCombo1->setCurrentText(settings->data.GraphVersion);
    terminalSizeSpin->setValue(settings->data.RemoteTerminalBufferSize);

    consoleSizeSpin->setValue(settings->data.ConsoleBufferSize);
    consoleTimeCheckbox->setChecked(settings->data.ConsoleTime);
    consoleNoWrapCheckbox->setChecked(settings->data.ConsoleNoWrap);
    consoleAutoScrollCheckbox->setChecked(settings->data.ConsoleAutoScroll);
    consoleShowBackgroundCheckbox->setChecked(settings->data.ConsoleShowBackground);
    consoleThemeCombo->setCurrentText(settings->data.ConsoleTheme);

    for (int i = 0; i < sessionsCheckCount; i++)
        sessionsCheck[i]->setChecked(settings->data.SessionsTableColumns[i]);

    sessionsHealthCheck->setChecked(settings->data.CheckHealth);
    sessionsCoafSpin->setValue(settings->data.HealthCoaf);
    sessionsOffsetSpin->setValue(settings->data.HealthOffset);

    for (int i = 0; i < 11; i++)
        tasksCheck[i]->setChecked(settings->data.TasksTableColumns[i]);

    tabblinkEnabledCheckbox->setChecked(settings->data.TabBlinkEnabled);

    for (auto it = m_tabblinkChecks.begin(); it != m_tabblinkChecks.end(); ++it) {
        if ( settings->data.BlinkWidgets.contains(it.key()) ) {
            bool enabled = settings->data.BlinkWidgets[it.key()];
            it.value()->setChecked(enabled);
        }
    }

    buttonApply->setEnabled(false);
}

void DialogSettings::showEvent(QShowEvent* event)
{
    loadSettings();
    refreshServicesPanel();
    QWidget::showEvent(event);
}

void DialogSettings::refreshServicesPanel()
{
    MainAdaptix* ma = settings->getMainAdaptix();
    MainUI* mainUI = ma ? ma->mainUI : nullptr;
    AdaptixWidget* aw = mainUI ? mainUI->GetCurrentAdaptixWidget() : nullptr;

    if (aw != m_servicesDraftsAdaptix) {
        m_serviceFormDrafts.clear();
        m_servicesDraftsAdaptix = aw;
    }

    const QString prevSvc = servicesCombo->currentText();
    if (!prevSvc.isEmpty())
        captureServiceFormDraft(prevSvc);

    m_serviceFormDirty = false;
    m_serviceFieldEdits.clear();

    servicesCombo->blockSignals(true);
    const QString prev = servicesCombo->currentText();
    servicesCombo->clear();

    if (!aw) {
        servicesHintLabel->setText(QStringLiteral("Open a project tab to configure teamserver services."));
        servicesCombo->setEnabled(false);
        servicesComboLabel->setEnabled(false);
        servicesScroll->setVisible(false);
        servicesCombo->blockSignals(false);
        while (servicesFormLayout->count()) {
            QLayoutItem* it = servicesFormLayout->takeAt(0);
            if (QWidget* w = it->widget())
                delete w;
            delete it;
        }
        return;
    }

    if (!aw->IsSynchronized()) {
        servicesHintLabel->setText(QStringLiteral("Wait until the project finishes synchronizing."));
        servicesCombo->setEnabled(false);
        servicesComboLabel->setEnabled(false);
        servicesScroll->setVisible(false);
        servicesCombo->blockSignals(false);
        while (servicesFormLayout->count()) {
            QLayoutItem* it = servicesFormLayout->takeAt(0);
            if (QWidget* w = it->widget())
                delete w;
            delete it;
        }
        return;
    }

    int visibleCount = 0;
    for (const auto& s : aw->GetRegisteredServices()) {
        if (s.configurable)
            ++visibleCount;
    }
    if (visibleCount == 0) {
        servicesHintLabel->setText(
            QStringLiteral("No services enable client configuration (teamserver extender: client_configurable: true)."));
        servicesCombo->setEnabled(false);
        servicesComboLabel->setEnabled(false);
        servicesScroll->setVisible(false);
        servicesCombo->blockSignals(false);
        while (servicesFormLayout->count()) {
            QLayoutItem* it = servicesFormLayout->takeAt(0);
            if (QWidget* w = it->widget())
                delete w;
            delete it;
        }
        return;
    }

    servicesHintLabel->setText(
        QStringLiteral("Services registered on the teamserver for the current project tab. Apply posts the form as JSON to /service/call (command=config)."));
    servicesCombo->setEnabled(true);
    servicesComboLabel->setEnabled(true);
    servicesScroll->setVisible(true);

    for (const auto& s : aw->GetRegisteredServices()) {
        if (s.configurable)
            servicesCombo->addItem(s.name);
    }

    if (!prev.isEmpty()) {
        const int idx = servicesCombo->findText(prev);
        if (idx >= 0)
            servicesCombo->setCurrentIndex(idx);
    }

    servicesCombo->blockSignals(false);
    rebuildServiceFormForSelection();
    m_lastServicesComboService = servicesCombo->currentText();
}

void DialogSettings::captureServiceFormDraft(const QString &serviceName)
{
    if (serviceName.isEmpty() || m_serviceFieldEdits.isEmpty())
        return;
    QJsonObject o;
    for (auto it = m_serviceFieldEdits.constBegin(); it != m_serviceFieldEdits.constEnd(); ++it)
        o[it.key()] = it.value()->text();
    m_serviceFormDrafts[serviceName] = o;
}

void DialogSettings::persistServiceDraftField(const QString &serviceName, const QString &key, const QString &value)
{
    QJsonObject d = m_serviceFormDrafts.value(serviceName);
    d[key] = value;
    m_serviceFormDrafts[serviceName] = d;
}

QJsonObject DialogSettings::mergeServiceFormValues(const QJsonObject &defaults, const QJsonObject &cached, const QJsonObject &draft)
{
    QJsonObject out = defaults;
    for (auto it = cached.begin(); it != cached.end(); ++it)
        out[it.key()] = it.value();
    for (auto it = draft.begin(); it != draft.end(); ++it)
        out[it.key()] = it.value();
    return out;
}

void DialogSettings::rebuildServiceFormForSelection()
{
    m_serviceFieldEdits.clear();
    while (servicesFormLayout->count()) {
        QLayoutItem* it = servicesFormLayout->takeAt(0);
        if (QWidget* w = it->widget())
            delete w;
        delete it;
    }

    MainAdaptix* ma = settings->getMainAdaptix();
    MainUI* mainUI = ma ? ma->mainUI : nullptr;
    AdaptixWidget* aw = mainUI ? mainUI->GetCurrentAdaptixWidget() : nullptr;
    if (!aw || !aw->IsSynchronized()) {
        return;
    }

    const QString svc = servicesCombo->currentText();
    if (svc.isEmpty()) {
        auto* lb = new QLabel(QStringLiteral("No services registered."), servicesFormHost);
        servicesFormLayout->addRow(lb);
        return;
    }

    RegServiceInfo info;
    bool found = false;
    for (const auto& s : aw->GetRegisteredServices()) {
        if (s.name == svc) {
            info = s;
            found = true;
            break;
        }
    }
    if (!found) {
        auto* lb = new QLabel(QStringLiteral("Service not found in registration list."), servicesFormHost);
        servicesFormLayout->addRow(lb);
        return;
    }

    if (!info.configurable) {
        auto* lb = new QLabel(QStringLiteral("This service does not expose client-editable settings."), servicesFormHost);
        servicesFormLayout->addRow(lb);
        return;
    }

    QJsonParseError pe;
    const QJsonDocument sd = QJsonDocument::fromJson(info.configSchema.toUtf8(), &pe);
    if (pe.error != QJsonParseError::NoError || !sd.isArray()) {
        auto* lb = new QLabel(QStringLiteral("Invalid config_schema from teamserver (expected JSON array)."), servicesFormHost);
        servicesFormLayout->addRow(lb);
        return;
    }

    QJsonObject defaultsObj;
    QJsonParseError pedef;
    const QJsonDocument defDoc = QJsonDocument::fromJson(info.configDefaults.toUtf8(), &pedef);
    if (pedef.error == QJsonParseError::NoError && defDoc.isObject())
        defaultsObj = defDoc.object();

    const QJsonObject cached = aw->GetServiceConfigData(svc);
    const QJsonObject draft = m_serviceFormDrafts.value(svc);
    const QJsonObject merged = mergeServiceFormValues(defaultsObj, cached, draft);

    for (const QJsonValue& v : sd.array()) {
        if (!v.isObject())
            continue;
        const QJsonObject fo = v.toObject();
        const QString key = fo[QStringLiteral("key")].toString();
        if (key.isEmpty())
            continue;

        QString label = fo[QStringLiteral("label")].toString();
        if (label.isEmpty())
            label = key;

        QString type = fo[QStringLiteral("type")].toString();
        if (type.isEmpty())
            type = QStringLiteral("string");

        if (type == QStringLiteral("button")) {
            const QString cmd = key;
            auto* btn = new QPushButton(label, servicesFormHost);
            connect(btn, &QPushButton::clicked, this, [this, cmd]() {
                triggerServiceAction(cmd);
            });
            servicesFormLayout->addRow(btn);
            continue;
        }

        auto* le = new QLineEdit(servicesFormHost);
        if (type == QStringLiteral("secret"))
            le->setEchoMode(QLineEdit::Password);
        le->setPlaceholderText(fo[QStringLiteral("placeholder")].toString());

        le->blockSignals(true);
        if (merged.contains(key)) {
            const QJsonValue cv = merged[key];
            if (cv.isString())
                le->setText(cv.toString());
            else if (!cv.isNull() && !cv.isUndefined())
                le->setText(cv.toVariant().toString());
        }
        le->blockSignals(false);

        connect(le, &QLineEdit::textChanged, this, [this, svc, key, le](const QString &) {
            persistServiceDraftField(svc, key, le->text());
            markServiceFormDirty();
        });
        servicesFormLayout->addRow(label, le);
        m_serviceFieldEdits[key] = le;
    }

    if (!m_serviceFieldEdits.isEmpty())
        captureServiceFormDraft(svc);

    if (m_serviceFieldEdits.isEmpty()) {
        auto* lb = new QLabel(QStringLiteral("Config schema has no fields."), servicesFormHost);
        servicesFormLayout->addRow(lb);
    }
}

void DialogSettings::markServiceFormDirty()
{
    m_serviceFormDirty = true;
    buttonApply->setEnabled(true);
}

bool DialogSettings::tryApplyServiceConfig()
{
    if (!m_serviceFormDirty)
        return true;

    MainAdaptix* ma = settings->getMainAdaptix();
    MainUI* mainUI = ma ? ma->mainUI : nullptr;
    QPointer<AdaptixWidget> aw(mainUI ? mainUI->GetCurrentAdaptixWidget() : nullptr);
    if (!aw || !aw->IsSynchronized()) {
        MessageError(QStringLiteral("Connect to a project and wait for sync before applying service settings."));
        return false;
    }

    const QString svc = servicesCombo->currentText().trimmed();
    if (svc.isEmpty()) {
        m_serviceFormDirty = false;
        return true;
    }

    RegServiceInfo info;
    bool found = false;
    for (const auto& s : aw->GetRegisteredServices()) {
        if (s.name == svc) {
            info = s;
            found = true;
            break;
        }
    }
    if (!found || !info.configurable) {
        m_serviceFormDirty = false;
        return true;
    }

    QJsonParseError pe;
    const QJsonDocument sd = QJsonDocument::fromJson(info.configSchema.toUtf8(), &pe);
    if (pe.error != QJsonParseError::NoError || !sd.isArray()) {
        MessageError(QStringLiteral("Invalid service config schema from teamserver."));
        return false;
    }

    QJsonObject out;
    for (const QJsonValue& v : sd.array()) {
        if (!v.isObject())
            continue;
        const QJsonObject fo = v.toObject();
        const QString key = fo[QStringLiteral("key")].toString();
        if (key.isEmpty())
            continue;

        const bool optional = fo[QStringLiteral("optional")].toBool();
        QLineEdit* le = m_serviceFieldEdits.value(key);
        if (!le)
            continue;

        const QString val = le->text().trimmed();
        if (!optional && val.isEmpty()) {
            QString fieldLabel = fo[QStringLiteral("label")].toString();
            if (fieldLabel.isEmpty())
                fieldLabel = key;
            MessageError(QStringLiteral("Field \"%1\" is required.").arg(fieldLabel));
            return false;
        }
        out[key] = val;
    }

    AuthProfile* prof = aw->GetProfile();
    if (!prof)
        return false;

    const QByteArray args = QJsonDocument(out).toJson(QJsonDocument::Compact);
    QPointer<DialogSettings> self(this);
    HttpReqServiceCallAsync(svc, QStringLiteral("config"), QString::fromUtf8(args), *prof,
                            [self, aw, svc, out](bool ok, const QString& msg, const QJsonObject&) {
                                if (!self)
                                    return;
                                if (!aw)
                                    return;
                                if (ok) {
                                    aw->SetServiceConfigData(svc, out);
                                    self->m_serviceFormDrafts.remove(svc);
                                    self->m_serviceFormDirty = false;
                                    MessageSuccess(QStringLiteral("Service configuration applied."));
                                } else {
                                    MessageError(msg.isEmpty() ? QStringLiteral("Service configuration failed.") : msg);
                                    self->buttonApply->setEnabled(true);
                                }
                            });

    return true;
}

void DialogSettings::triggerServiceAction(const QString& command)
{
    MainAdaptix* ma = settings->getMainAdaptix();
    MainUI* mainUI = ma ? ma->mainUI : nullptr;
    QPointer<AdaptixWidget> aw(mainUI ? mainUI->GetCurrentAdaptixWidget() : nullptr);
    if (!aw || !aw->IsSynchronized()) {
        MessageError(QStringLiteral("Connect to a project and wait for sync before running service actions."));
        return;
    }

    const QString svc = servicesCombo->currentText().trimmed();
    if (svc.isEmpty())
        return;

    QJsonObject args;
    for (auto it = m_serviceFieldEdits.constBegin(); it != m_serviceFieldEdits.constEnd(); ++it)
        args[it.key()] = it.value()->text().trimmed();

    AuthProfile* prof = aw->GetProfile();
    if (!prof)
        return;

    const QByteArray argsData = QJsonDocument(args).toJson(QJsonDocument::Compact);
    HttpReqServiceCallAsync(svc, command, QString::fromUtf8(argsData), *prof,
                            [](bool ok, const QString& msg, const QJsonObject&) {
                                if (ok)
                                    MessageSuccess(QStringLiteral("Action completed successfully."));
                                else
                                    MessageError(msg.isEmpty() ? QStringLiteral("Action failed.") : msg);
                            });
}

QString DialogSettings::userAppThemeDir()
{
    QString dir = QDir(QDir::homePath()).filePath(".adaptix/themes/app");
    QDir().mkpath(dir);
    return dir;
}

bool DialogSettings::importAppTheme(const QString& filePath)
{
    QFileInfo fi(filePath);
    if (!fi.exists() || fi.suffix().toLower() != "json")
        return false;

    QString destDir = userAppThemeDir();
    QString destPath = destDir + "/" + fi.fileName();
    if (QFile::exists(destPath))
        QFile::remove(destPath);

    return QFile::copy(filePath, destPath);
}

void DialogSettings::refreshAppThemeCombo()
{
    QString current = themeCombo->currentText();
    themeCombo->clear();

    QStringList builtIn = {
        "Adaptix_Dark", "Adaptix_Light", "Adaptix_Dracula",
        "Dark_Ice", "Glass_Morph", "Hacker_Tech",
        "Fallout", "Light_Arc", "Dark_Classic", "Dracula"
    };
    themeCombo->addItems(builtIn);

    QDir userDir(userAppThemeDir());
    for (const auto& entry : userDir.entryList({"*.json"}, QDir::Files)) {
        QString name = QFileInfo(entry).baseName();
        if (!builtIn.contains(name))
            themeCombo->addItem(name);
    }

    if (!current.isEmpty())
        themeCombo->setCurrentText(current);
}

void DialogSettings::refreshConsoleThemeCombo()
{
    QString current = consoleThemeCombo->currentText();
    consoleThemeCombo->clear();
    consoleThemeCombo->addItems(ConsoleThemeManager::instance().availableThemes());
    if (!current.isEmpty())
        consoleThemeCombo->setCurrentText(current);
}
