#include "applications/base/sn_railawarewidget.h"
#include "applications/base/sn_affinityinfo.h"
#include "applications/base/sn_affinitycontroldialog.h"
#include "applications/base/sn_appinfo.h"
#include "applications/base/sn_perfmonitor.h"

#include "system/sn_resourcemonitor.h"
#include "system/sn_scheduler.h"

#include <QtGui>

SN_RailawareWidget::SN_RailawareWidget()
    : affCtrlDialog(0)
    , _affCtrlAction(0)
    , _widgetClosed(false)
    , _scheduled(false)
{
//	setWidgetType(SN_BaseWidget::Widget_RealTime);
	setCacheMode(QGraphicsItem::NoCache);
}

SN_RailawareWidget::SN_RailawareWidget(quint64 globalappid, const QSettings *s, SN_ResourceMonitor *rmonitor, QGraphicsItem *parent, Qt::WindowFlags wflags)
    : SN_BaseWidget(globalappid, s, parent, wflags)
    , affCtrlDialog(0)
    , _affCtrlAction(0)
    , _widgetClosed(false)
    , _scheduled(false)
{
	failToSchedule = 0;

	/* railaware widget is streaming widget, so turn off cache */
	setCacheMode(QGraphicsItem::NoCache);
	//	setCacheMode(QGraphicsItem::ItemCoordinateCache);
	//	setCacheMode(QGraphicsItem::DeviceCoordinateCache);
	//	setBoundingRegionGranularity(0.25);


	//	qDebug() << "affInfo" << affInfo;
	//	qDebug() << "gaid" << globalappid;
	//	qDebug() << "rail on?" << s->value("system/rail").toBool();

	_rMonitor = rmonitor;

	if (_rMonitor) {
		_rMonitor->addSchedulableWidget(this);

        //
        // for now, don't create railaware stuff (6/5/2012)
        //
//		createAffInstances();

		/*
		if ( ! QObject::connect(_affInfo, SIGNAL(cpuOfMineChanged(SN_RailawareWidget *,int,int)), _rMonitor, SLOT(updateAffInfo(SN_RailawareWidget *,int,int))) ) {
			qDebug() << "SN_RailawareWidget() : connection _affInfo::cpuOfMineChanged() -> _rMonitor::updateAffInfo() failed";
		}
		*/
	}
}

SN_RailawareWidget::~SN_RailawareWidget()
{
	if (_rMonitor) _rMonitor->removeSchedulableWidget(this);

	if (_affInfo) {
		//
		// todo :  explain why
		//
		_affInfo->disconnect();
		delete _affInfo;
	}
	if (affCtrlDialog) delete affCtrlDialog;

	qDebug("%s::%s()", metaObject()->className(), __FUNCTION__);
}

void SN_RailawareWidget::createAffInstances()
{
	if (!_affInfo)
		_affInfo = new SN_AffinityInfo(this);

	if (!_affCtrlAction) {
		_affCtrlAction = new QAction("Affinity Control", this);
		_affCtrlAction->setEnabled(false);
		_contextMenu->addAction(_affCtrlAction);
		QObject::connect(_affCtrlAction, SIGNAL(triggered()), this, SLOT(showAffCtrlDialog()));
	}

	//	Q_ASSERT(_affInfo);
	//	if ( rMonitor ) {
	//		if ( connect(_affInfo, SIGNAL(cpuOfMineChanged(RailawareWidget *,int,int)), rMonitor, SLOT(updateAffInfo(RailawareWidget *,int,int))) ) {
	//		}
	//		else {
	//			qCritical("RailawareWidget::%s() : connecting AffinityInfo::affInfoChanged() to ResourceMonitor::updateAffInfo() failed", __FUNCTION__);
	//		}
	//	}
}

//int SN_RailawareWidget::setQuality(qreal newQuality) {

//	//qDebug() << _globalAppId << "railwarewidget::setQuality" << newQuality;

//	if ( newQuality > 1.0 ) {
//		_quality = 1.0;
//	}
//	else if ( newQuality <= 0.0 ) {
//		_quality = 0.1;
//	}
//	else {
//		_quality = newQuality;
//	}

//	return -1;
//}

//qreal SN_RailawareWidget::observedQuality() {
//	if (_perfMon) {
//		//qDebug() << _perfMon->getCurrRecvFps() << _perfMon->getExpetctedFps() << _perfMon->getCurrRecvFps() / _perfMon->getExpetctedFps();
//		return _perfMon->getCurrRecvFps() / _perfMon->getExpetctedFps(); // frame rate for now
//	}
//	else return -1;
//}

//qreal SN_RailawareWidget::observedQualityAdjusted() {
//	//
//	// ratio of the current framerate to the ADJUSTED(demanded) framerate
//	//
//	return _perfMon->getCurrRecvFps() / _perfMon->getAdjustedFps();
//}



void SN_RailawareWidget::contextMenuEvent(QGraphicsSceneContextMenuEvent *event)
{
	if ( _affInfo && _affCtrlAction ) {
		_affCtrlAction->setEnabled(true);
	}
	//	BaseWidget::contextMenuEvent(event);
	scene()->clearSelection();
	setSelected(true);

	//	_contextMenu->exec(event->screenPos());
	_contextMenu->popup(event->screenPos());
}


void SN_RailawareWidget::showAffCtrlDialog() {
	if ( affCtrlDialog ) {
		affCtrlDialog->updateInfo();
		affCtrlDialog->show();
		return;
	}

	Q_ASSERT(_affInfo);
	Q_ASSERT(_settings);
	Q_ASSERT(globalAppId() > 0);
	/* will modify mask through affInfo pointer and sets the flag */
	affCtrlDialog = new SN_AffinityControlDialog(_globalAppId, _affInfo, _settings);
	affCtrlDialog->show();
}


