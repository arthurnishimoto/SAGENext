#include "settingstackeddialog.h"
#include "ui_settingstackeddialog.h"

#include "ui_generalsettingdialog.h"
#include "ui_systemsettingdialog.h"
#include "ui_graphicssettingdialog.h"
#include "ui_guisettingdialog.h"

#include <QSettings>
#include <QtCore>


GeneralSettingDialog::GeneralSettingDialog(QSettings *s, QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::GeneralSettingDialog)
    , _settings(s)
{
    ui->setupUi(this);
	
	// fill the GUI components with value read from _settings
	ui->wallIPLineEdit->setInputMask("000.000.000.000;_");
	ui->wallPortLineEdit->setInputMask("00000;_");
	ui->fsManagerIPLineEdit->setInputMask("000.000.000.000;_");
	ui->fsManagerPortLineEdit->setInputMask("00000;_");

	ui->wallIPLineEdit->setText( _settings->value("general/wallip", "127.0.0.1").toString() );
	ui->wallPortLineEdit->setText( _settings->value("general/wallport", 30003).toString());

	ui->fsManagerIPLineEdit->setText( _settings->value("general/fsmip", "127.0.0.1").toString() );
	ui->fsManagerPortLineEdit->setText( _settings->value("general/fsmport", 20002).toString());

	ui->widthLineEdit->setText(_settings->value("general/width", 0).toString());
	ui->heightLineEdit->setText(_settings->value("general/height", 0).toString());
	ui->offsetx->setText(_settings->value("general/offsetx", 0).toString());
	ui->offsety->setText(_settings->value("general/offsety",0).toString());
}

void GeneralSettingDialog::accept() {
	Q_ASSERT(_settings);
	/* save(overwrite) settings using QSetting */
//	_settings->setValue("general/wallname", ui->wallNameLineEdit->text());
	_settings->setValue("general/wallip", ui->wallIPLineEdit->text() );
	_settings->setValue("general/wallport", ui->wallPortLineEdit->text().toInt() );
	_settings->setValue("general/fsmip", ui->fsManagerIPLineEdit->text() );
	_settings->setValue("general/fsmport", ui->fsManagerPortLineEdit->text().toInt() );
	_settings->setValue("general/fsmstreambaseport",  ui->fsManagerPortLineEdit->text().toInt() + 3);

	_settings->setValue("general/width", ui->widthLineEdit->text().toInt());
	_settings->setValue("general/height", ui->heightLineEdit->text().toInt());
	_settings->setValue("general/offsetx", ui->offsetx->text().toInt());
	_settings->setValue("general/offsety", ui->offsety->text().toInt());
//	_settings->setValue("general/fontpointsize", ui->fontpointsize->text().toInt());
}

GeneralSettingDialog::~GeneralSettingDialog() { delete ui;}

/* System */
SystemSettingDialog::SystemSettingDialog(QSettings *s, QWidget *parent)
	: QDialog(parent)
	, ui(new Ui::SystemSettingDialog)
	, _settings(s)
{
	ui->setupUi(this);

	if ( _settings->value("system/numa").toBool())
		ui->isNumaAvailLabel->setText("Yes");
	else
		ui->isNumaAvailLabel->setText("No");
	ui->numNumaNodesLabel->setText(_settings->value("system/numnumanodes", -1).toString());

	ui->numCpuPerNumaLineEdit->setText(_settings->value("system/cpupernumanode", -1).toString());
	ui->numHWThreadLineEdit->setText(_settings->value("system/threadpercpu", -1).toString());
	ui->numSWThreadLineEdit->setText(_settings->value("system/swthreadpercpu", -1).toString());

	if (_settings->value("system/resourcemonitor", false).toBool()) {
		ui->rmonitorCheckBox->setCheckState(Qt::Checked);
		if (_settings->value("system/scheduler", false).toBool()) {
			ui->schedulerCheckBox->setCheckState(Qt::Checked);
			ui->schedFreqLineEdit->setEnabled(true);
		}
		else {
			ui->schedulerCheckBox->setCheckState(Qt::Unchecked);
			ui->schedFreqLineEdit->setDisabled(true);
		}
	}
	else {
		ui->rmonitorCheckBox->setCheckState(Qt::Unchecked);
		ui->schedulerCheckBox->setCheckState(Qt::Unchecked);
	}

	ui->schedFreqLineEdit->setText(_settings->value("system/scheduler_freq", 100).toString());
}

void SystemSettingDialog::on_schedulerCheckBox_stateChanged(int state) {
	if (state == Qt::Checked) {
		ui->rmonitorCheckBox->setChecked(true);
		ui->schedFreqLineEdit->setEnabled(true);
	}
	else {
		ui->schedFreqLineEdit->setDisabled(true);
	}
}

void SystemSettingDialog::on_rmonitorCheckBox_stateChanged(int state) {
	if (state == Qt::Unchecked) {
		ui->schedulerCheckBox->setCheckState(Qt::Unchecked);
	}
}

void SystemSettingDialog::accept() {
	_settings->setValue("system/cpupernumanode", ui->numCpuPerNumaLineEdit->text().toInt());
	_settings->setValue("system/threadpercpu", ui->numHWThreadLineEdit->text().toInt());
	_settings->setValue("system/swthreadpercpu", ui->numSWThreadLineEdit->text().toInt());

	int totalCpus = _settings->value("system/numnumanodes").toInt()
	        * _settings->value("system/cpupernumanode").toInt()
	        * _settings->value("system/threadpercpu").toInt()
	        * _settings->value("system/swthreadpercpu").toInt();
	_settings->setValue("system/numcpus", totalCpus);


	// this is used in sagePixelReceiver
	_settings->setValue("system/sailaffinity", false);


	_settings->setValue("system/resourcemonitor", false);
	_settings->setValue("system/scheduler", false);

	// this is used in sagePixelReceiver
	_settings->setValue("system/scheduler_type", "SelfAdjusting");


	if ( ui->schedulerCheckBox->isChecked() ) {
		_settings->setValue("system/scheduler", true);
		_settings->setValue("system/resourcemonitor", true);
//		_settings->setValue("system/scheduler_type", ui->schedulerComboBox->currentText() );
		_settings->setValue("system/scheduler_freq", ui->schedFreqLineEdit->text().toInt());
	}
	else if (ui->rmonitorCheckBox->isChecked()) {
		_settings->setValue("system/scheduler", false);
		_settings->setValue("system/resourcemonitor", true);
	}
	if ( ! _settings->value("system/resourcemonitor").toBool() ) {
		_settings->setValue("system/scheduler", false);
	}
}

SystemSettingDialog::~SystemSettingDialog() {delete ui;}



/*
  Graphics
*/
GraphicsSettingDialog::GraphicsSettingDialog(QSettings *s, QWidget *parent)
	: QDialog(parent)
	, ui(new Ui::GraphicsSettingDialog)
	, _settings(s)
{
	ui->setupUi(this);

	if ( _settings->value("system/openglviewport", false).toBool() )
		ui->openglviewportCheckBox->setCheckState(Qt::Checked);
	else
		ui->openglviewportCheckBox->setCheckState(Qt::Unchecked);

	int idx = ui->viewportUpdateComboBox->findData(_settings->value("system/viewportupdatemode", "").toString());
	if (idx != -1) {
		ui->viewportUpdateComboBox->setCurrentIndex(idx);
	}
}

void GraphicsSettingDialog::accept() {
	/* graphics system */
//	settings->setValue("system/graphicssystem", ui->graphicssystemComboBox->currentText());
	if ( ui->openglviewportCheckBox->checkState() == Qt::Checked ) {
		_settings->setValue("system/openglviewport", true);
	}
	else {
		_settings->setValue("system/openglviewport", false);
	}
	_settings->setValue("system/viewportupdatemode", ui->viewportUpdateComboBox->currentText());
}

GraphicsSettingDialog::~GraphicsSettingDialog() {delete ui;}


/* GUI */
GuiSettingDialog::GuiSettingDialog(QSettings *s, QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::GuiSettingDialog)
    , _settings(s)
{
	ui->setupUi(this);

	ui->fontSizeLineEdit->setText(_settings->value("gui/fontpointsize", 20).toString());

	ui->frameBorderLeftLineEdit->setText(_settings->value("gui/framemarginleft", 3).toString());
	ui->frameBorderTopLineEdit->setText(_settings->value("gui/framemargintop", 3).toString());
	ui->frameBorderRightLineEdit->setText(_settings->value("gui/framemarginright", 3).toString());
	ui->frameBorderBottomLineEdit->setText(_settings->value("gui/framemarginbottom", 3).toString());
}
void GuiSettingDialog::accept() {
	_settings->setValue("gui/fontpointsize", ui->fontSizeLineEdit->text().toInt());
	/* window frame margins */
	_settings->setValue("gui/framemarginleft", ui->frameBorderLeftLineEdit->text().toInt());
	_settings->setValue("gui/framemargintop", ui->frameBorderTopLineEdit->text().toInt());
	_settings->setValue("gui/framemarginright", ui->frameBorderRightLineEdit->text().toInt());
	_settings->setValue("gui/framemarginbottom", ui->frameBorderBottomLineEdit->text().toInt());
}
GuiSettingDialog::~GuiSettingDialog() {delete ui;}




SettingStackedDialog::SettingStackedDialog(QSettings *s, QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::SettingStackedDialog)
    , _settings(s)
{
    ui->setupUi(this);
    
    setWindowTitle(tr("Config Dialog"));
	
	connect(ui->listWidget, SIGNAL(currentItemChanged(QListWidgetItem*,QListWidgetItem*)), this, SLOT(changeStackedWidget(QListWidgetItem*,QListWidgetItem*)));
	
	/*
      stacked widget for each setting page
      */
    ui->stackedWidget->addWidget(new GeneralSettingDialog(_settings));
	ui->stackedWidget->addWidget(new GraphicsSettingDialog(_settings));
	ui->stackedWidget->addWidget(new GuiSettingDialog(_settings));
	ui->stackedWidget->addWidget(new SystemSettingDialog(_settings));
    
    /*
      Fill listWidget
      */
	ui->listWidget->setMovement(QListView::Static);
	ui->listWidget->setViewMode(QListView::ListMode);
	
    QListWidgetItem *item = new QListWidgetItem("general");
	item->setText("General");
    item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
	ui->listWidget->addItem(item);

	item = new QListWidgetItem("graphics");
	item->setText("Graphics");
	item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
	ui->listWidget->addItem(item);

	item = new QListWidgetItem("gui");
	item->setText("GUI related");
	item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
	ui->listWidget->addItem(item);

	item = new QListWidgetItem("system");
	item->setText("System");
	item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
	ui->listWidget->addItem(item);
	
	/**
	  This must be called AFTER ui->stackedWidget->addWidget
	  */
	ui->listWidget->setCurrentRow(0); // will emit currentItemChanged which is connected to changeStackedWidget
}

SettingStackedDialog::~SettingStackedDialog()
{
	ui->listWidget->clear(); // will delete all the items
    delete ui;
}

void SettingStackedDialog::changeStackedWidget(QListWidgetItem *current, QListWidgetItem *previous) {
	QDialog *dialog = static_cast<QDialog *>(ui->stackedWidget->widget(ui->listWidget->row(previous)));
//	dialog->accept();
	
    if (!current) current = previous;
    ui->stackedWidget->setCurrentIndex(ui->listWidget->row(current));
}

void SettingStackedDialog::on_buttonBox_accepted()
{
	QDialog *dialog = 0;
	for (int i=0; i<ui->stackedWidget->count(); ++i) {
		dialog  = static_cast<QDialog *>(ui->stackedWidget->widget(i));
		dialog->accept();
	}

	/* network parameters used by fsManagerMsgThread
	  let's put these here for now
	  */
	_settings->setValue("network/recvwindow", 16777216);
	_settings->setValue("network/sendwindow", 65535);
	_settings->setValue("network/mtu", 1450);
}

void SettingStackedDialog::on_buttonBox_rejected()
{
	::exit(1);
}


