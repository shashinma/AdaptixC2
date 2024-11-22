#include <UI/Widgets/LogsWidget.h>
#include <Utils/Convert.h>

LogsWidget::LogsWidget()
{
    this->createUI();
}

LogsWidget::~LogsWidget() = default;

void LogsWidget::createUI()
{
    /// Logs
    logsLabel = new QLabel(this);
    logsLabel->setText("Logs");
    logsLabel->setAlignment(Qt::AlignCenter);

    logsConsoleTextEdit = new QTextEdit(this);
    logsConsoleTextEdit->setReadOnly(true);

    logsGridLayout = new QGridLayout(this);
    logsGridLayout->setContentsMargins(1, 1, 1, 1);
    logsGridLayout->setVerticalSpacing(1);
    logsGridLayout->addWidget(logsLabel, 0, 0, 1, 1);
    logsGridLayout->addWidget(logsConsoleTextEdit, 1, 0, 1, 1);

    logsWidget = new QWidget(this);
    logsWidget->setLayout(logsGridLayout);


    /// ToDo: todo list + sync chat
    todoLabel = new QLabel(this);
    todoLabel->setText("ToDo notes");
    todoLabel->setAlignment(Qt::AlignCenter);

    todoGridLayout = new QGridLayout(this);
    todoGridLayout->setContentsMargins(1, 1, 1, 1);
    todoGridLayout->setVerticalSpacing(1);
    todoGridLayout->setHorizontalSpacing(2);

    todoGridLayout->addWidget(todoLabel, 0, 0, 1, 1);

    todoWidget = new QWidget(this);
    todoWidget->setLayout(todoGridLayout);


    /// Main
    mainHSplitter = new QSplitter( Qt::Horizontal, this );
    mainHSplitter->setHandleWidth(3);
    mainHSplitter->addWidget(logsWidget);
    mainHSplitter->addWidget(todoWidget);
    mainHSplitter->setSizes(QList<int>({200, 40}));

    mainGridLayout = new QGridLayout( this );
    mainGridLayout->setContentsMargins(0, 0, 0, 0);
    mainGridLayout->setVerticalSpacing(0);
    mainGridLayout->addWidget( mainHSplitter, 0, 0, 1, 1);

    this->setLayout( mainGridLayout );
}

void LogsWidget::AddLogs( int type, qint64 time, QString message )
{
    QString sTime = UnixTimestampGlobalToStringLocal(time);
    QString log = QString("[%1] -> ").arg(sTime);

    if( type == TYPE_CLIENT_CONNECT ) {
        log += TextColorHtml(message, COLOR_ChiliPepper);
    }
    else if( type == TYPE_CLIENT_DISCONNECT ) {
        log += TextColorHtml(message, COLOR_Berry);
    }
    else if( type == TYPE_LISTENER_START || TYPE_LISTENER_STOP ) {
        log += TextColorHtml(message, COLOR_BrightOrange);
    }
    else if( type == TYPE_AGENT_NEW ) {
        log += TextColorHtml(message, COLOR_NeonGreen);
    }
    else {
        log += message;
    }

    logsConsoleTextEdit->append( log );
}

void LogsWidget::Clear()
{
    logsConsoleTextEdit->clear();
}
