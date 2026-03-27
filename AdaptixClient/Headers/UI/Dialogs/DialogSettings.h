#ifndef ADAPTIXCLIENT_DIALOGSETTINGS_H
#define ADAPTIXCLIENT_DIALOGSETTINGS_H

#include <main.h>
#include <oclero/qlementine/widgets/Switch.hpp>
#include <QFormLayout>
#include <QScrollArea>

class Settings;
class AdaptixWidget;

class DialogSettings : public QWidget
{
Q_OBJECT

    Settings* settings = nullptr;

    QGridLayout*    layoutMain    = nullptr;
    QListWidget*    listSettings  = nullptr;
    QVBoxLayout*    headerLayout  = nullptr;
    QLabel*         labelHeader   = nullptr;
    QFrame*         lineFrame     = nullptr;
    QStackedWidget* stackSettings = nullptr;
    QSpacerItem*    hSpacer       = nullptr;
    QPushButton*    buttonApply   = nullptr;
    QPushButton*    buttonClose   = nullptr;

    QWidget*     mainSettingWidget = nullptr;
    QGridLayout* mainSettingLayout = nullptr;
    QLabel*      themeLabel        = nullptr;
    QComboBox*   themeCombo        = nullptr;
    QPushButton* themeImportBtn    = nullptr;
    QLabel*      fontSizeLabel     = nullptr;
    QSpinBox*    fontSizeSpin      = nullptr;
    QLabel*      fontFamilyLabel   = nullptr;
    QComboBox*   fontFamilyCombo   = nullptr;
    QLabel*      graphLabel1       = nullptr;
    QComboBox*   graphCombo1       = nullptr;
    QLabel*      terminalSizeLabel = nullptr;
    QSpinBox*    terminalSizeSpin  = nullptr;

    QGroupBox*   consoleGroup              = nullptr;
    QGridLayout* consoleGroupLayout        = nullptr;
    QLabel*      consoleSizeLabel          = nullptr;
    QSpinBox*    consoleSizeSpin           = nullptr;
    oclero::qlementine::Switch* consoleTimeCheckbox           = nullptr;
    oclero::qlementine::Switch* consoleNoWrapCheckbox         = nullptr;
    oclero::qlementine::Switch* consoleAutoScrollCheckbox     = nullptr;
    oclero::qlementine::Switch* consoleShowBackgroundCheckbox = nullptr;
    QLabel*      consoleThemeLabel         = nullptr;
    QComboBox*   consoleThemeCombo         = nullptr;
    QPushButton* consoleThemeImportBtn     = nullptr;

    QWidget*     sessionsWidget       = nullptr;
    QGridLayout* sessionsLayout       = nullptr;
    QGroupBox*   sessionsGroup        = nullptr;
    QGridLayout* sessionsGroupLayout  = nullptr;
    int          sessionsCheckCount   = 16;
    QCheckBox*   sessionsCheck[16];
    oclero::qlementine::Switch* sessionsHealthCheck = nullptr;
    QLabel*      sessionsLabel1       = nullptr;
    QLabel*      sessionsLabel2       = nullptr;
    QLabel*      sessionsLabel3       = nullptr;
    QDoubleSpinBox* sessionsCoafSpin  = nullptr;
    QSpinBox*    sessionsOffsetSpin   = nullptr;

    QWidget*     tasksWidget      = nullptr;
    QGridLayout* tasksLayout      = nullptr;
    QGroupBox*   tasksGroup       = nullptr;
    QGridLayout* tasksGroupLayout = nullptr;
    QCheckBox*   tasksCheck[11];

    QWidget*     tabblinkWidget          = nullptr;
    QGridLayout* tabblinkLayout          = nullptr;
    oclero::qlementine::Switch* tabblinkEnabledCheckbox = nullptr;
    QGroupBox*   tabblinkGroup           = nullptr;
    QGridLayout* tabblinkGroupLayout     = nullptr;
    QMap<QString, QCheckBox*> m_tabblinkChecks;  // className -> checkbox

    QWidget*     servicesWidget       = nullptr;
    QGridLayout* servicesLayout       = nullptr;
    QLabel*      servicesHintLabel    = nullptr;
    QLabel*      servicesComboLabel   = nullptr;
    QComboBox*   servicesCombo        = nullptr;
    QScrollArea* servicesScroll       = nullptr;
    QWidget*     servicesFormHost     = nullptr;
    QFormLayout* servicesFormLayout   = nullptr;
    QMap<QString, QLineEdit*> m_serviceFieldEdits;
    bool         m_serviceFormDirty   = false;
    QMap<QString, QJsonObject> m_serviceFormDrafts;
    QString      m_lastServicesComboService;
    AdaptixWidget* m_servicesDraftsAdaptix = nullptr;

    void captureServiceFormDraft(const QString &serviceName);
    void persistServiceDraftField(const QString &serviceName, const QString &key, const QString &value);
    static QJsonObject mergeServiceFormValues(const QJsonObject &defaults, const QJsonObject &cached, const QJsonObject &draft);
    void triggerServiceAction(const QString& command);

    void createUI();
    void loadSettings();
    void refreshAppThemeCombo();
    void refreshConsoleThemeCombo();
    void refreshServicesPanel();
    void rebuildServiceFormForSelection();
    bool tryApplyServiceConfig();

    static QString userAppThemeDir();
    static bool importAppTheme(const QString& filePath);

protected:
    void showEvent(QShowEvent* event) override;

public:
    DialogSettings(Settings* s);

public Q_SLOTS:
    void onStackChange(int index);
    void onHealthChange() const;
    void onBlinkChange() const;
    void onApply();
    void onClose();

private Q_SLOTS:
    void markServiceFormDirty();
};

#endif