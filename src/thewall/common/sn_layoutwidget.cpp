#include "sn_layoutwidget.h"

#include "commonitem.h"
#include "../applications/base/basewidget.h"
#include "../applications/base/appinfo.h"
#include "../sagenextlauncher.h"


SN_LayoutWidget::SN_LayoutWidget(const QString &pos, SN_LayoutWidget *parentWidget, const QSettings *s, QGraphicsItem *parent)
    : QGraphicsWidget(parent)
    , _settings(s)
    , _parentLayoutWidget(parentWidget)
//    , _leftWidget(0)
//    , _rightWidget(0)
//    , _topWidget(0)
//    , _bottomWidget(0)
	, _firstChildLayout(0)
	, _secondChildLayout(0)
    , _bar(0)
    , _tileButton(0)
    , _hButton(0)
    , _vButton(0)
    , _xButton(0)
    , _buttonGrp(0)
    , _isTileOn(false)
    , _position(pos)
{
//	setFlag(QGraphicsItem::ItemIsSelectable, false);
	setFlag(QGraphicsItem::ItemIsMovable, false);
	setFlag(QGraphicsItem::ItemHasNoContents, true);// don't paint anything

	// pointer->setAppUnderPointer() will pass this item
	setAcceptedMouseButtons(0);

	setWindowFrameMargins(0,0,0,0);
	setContentsMargins(0,0,0,0);

	// these png files are 499x499
	_tileButton = new SN_PixmapButton(":/resources/tile_btn_over.jpg", _settings->value("gui/iconwidth").toDouble(), "", this);
	_hButton = new SN_PixmapButton(":/resources/horizontal_divider_btn_over.png", _settings->value("gui/iconwidth").toDouble(), "", this);
	_vButton = new SN_PixmapButton( ":/resources/vertical_divider_btn_over.png", _settings->value("gui/iconwidth").toDouble(), "", this);

	connect(_tileButton, SIGNAL(clicked()), this, SLOT(toggleTile()));
	// horizontal button will divide the widget vertically
	connect(_hButton, SIGNAL(clicked()), this, SLOT(createHBar()));
	// vertical button will divide the widget horizontally
	connect(_vButton, SIGNAL(clicked()), this, SLOT(createVBar()));

//	_buttonGrp = new QGraphicsItemGroup(this);
//	_buttonGrp->addToGroup(_vButton);
//	_buttonGrp->addToGroup(_hButton);

	if (parentWidget) {
		_xButton = new SN_PixmapButton(":/resources/close_over.png", _settings->value("gui/iconwidth").toDouble(), "", this);
		connect(_xButton, SIGNAL(clicked()), _parentLayoutWidget, SLOT(deleteChildPartitions()));
//		connect(_xButton, SIGNAL(clicked()), this, SLOT(deleteMyself()));

//		_buttonGrp->addToGroup(_xButton);
	}
	else {

	}
//	setSizePolicy(QSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed));
//	setMinimumSize(r.size());
}

SN_LayoutWidget::~SN_LayoutWidget() {

}

void SN_LayoutWidget::setRectangle(const QRectF &r) {
	resize(r.size());

	// my (0,0) is my boundingRect().center()
//	setPos(r.center());

	// my (0,0) is my boundingRect().topLeft()
	setPos(r.topLeft());
}

/**
  pos is in the current SN_LayoutWidget's coordinate
  */
void SN_LayoutWidget::addItem(SN_BaseWidget *bw, const QPointF &pos /* = 30,30*/) {

//	qDebug() << "Layout : addItem() " << pos;
	/**
	  If _bar exist, that means this layoutWidget is just a container for child layoutwidgets.
	  So this layoutWidget can't have any baseWidget as a child.
	  */
	if (_bar) {

		// find out bw's effective center position
		QPointF bwCenter = QPointF(0.5 * bw->size().width() * bw->scale(), 0.5 * bw->size().height() * bw->scale());

		if ( _firstChildLayout->rect().contains( _firstChildLayout->mapFromItem(bw, bwCenter)) ) {
			_firstChildLayout->addItem(bw, _firstChildLayout->mapFromParent(pos));
		}
		else {
			_secondChildLayout->addItem(bw, _secondChildLayout->mapFromParent(pos));
		}
	}
	/**
	  BaseWidgets can be added to me
	  */
	else {
		/**
		  if the item already has a parent it is first removed from the previous parent
		  This implicitly adds this item to the scene of the parent.

		  QGraphicsObject::parentChanged() will be emitted
		  */
		bw->setParentItem(this);

		if ( _isTileOn ) {
			// I need to return as soon as possible
			QMetaObject::invokeMethod(this, "doTile", Qt::QueuedConnection);
		}
		else {
//		qDebug() << "SN_LayoutWidget::addItem() : bw->setPos() " << pos;
			bw->setPos(pos);
		}
	}


	/**********
	  Item is finally added to the scene and be shown
	  AFTER this function returns
	  *******/
}

/**
  Caller's child items will be reparented to the newParent
  */
void SN_LayoutWidget::reparentMyChildBasewidgets(SN_LayoutWidget *newParent) {
	if (_bar) {
		_firstChildLayout->reparentMyChildBasewidgets(newParent);
		_secondChildLayout->reparentMyChildBasewidgets(newParent);
	}
	else {
		foreach(QGraphicsItem *item, childItems()) {
			// exclude all the child but user application
//			if (item == _bar || item == _tileButton || item == _hButton || item == _vButton || item == _xButton || item==_firstChildLayout || item==_secondChildLayout) continue;
			if (item->type() < QGraphicsItem::UserType + 12) continue;

			//
			// this item's pos() which is in this layoutWidget's coordinate to newParent's coordinate
			//
			QPointF newPos = mapToItem(newParent, item->pos());

			// reparent
			item->setParentItem(newParent);

			item->setPos(newPos);
		}
	}
}


void SN_LayoutWidget::paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *) {
	QPen pen;
	pen.setColor(QColor(Qt::white));
	painter->setPen(pen);
	painter->drawRect(boundingRect());
}

void SN_LayoutWidget::resizeEvent(QGraphicsSceneResizeEvent *e) {
	// adjust button position
	setButtonPos();

	//
	// this signal will make parentWidget to call adjustBar()
	//
	emit resized();

	QSizeF deltaSize = e->newSize() - e->oldSize();

	//
	// upon resizing, resize my child layout widgets as well
	// Since I have the bar, I dont have any base widget as a child
	//
	if (_bar) {

		/*******
		  ****
		 *****  Below is when (0, 0) is center

		QRectF br = boundingRect();
		QRectF first, second;

		if (_bar->orientation() == Qt::Horizontal) {
			//
			// bar is horizontal so my children is at TOP and BOTTOM
			//
			first = QRectF(br.topLeft(), QSizeF(br.width(), _bar->line().y1() - br.top()));
			second = QRectF(br.left(), _bar->line().y1(), br.width(), br.bottom() - _bar->line().y1());
		}
		else {
			first = QRectF(br.topLeft(), QSizeF(_bar->line().x1() - br.left(), br.height()));
			second = QRectF(_bar->line().x1(), br.top(), br.right() - _bar->line().x1(), br.height());
		}
		_firstChildLayout->setRectangle(first);
		_secondChildLayout->setRectangle(second);
		******
		*****/

		if (_bar->orientation() == Qt::Horizontal) {
			//
			// bar is horizontal so my children is at TOP and BOTTOM
			//
			_firstChildLayout->setPos(0, 0);

			if (deltaSize.height() == 0) {
				//
				// I'm resized horizontally.  -> only width changes for my childs
				//
				_firstChildLayout->resize(   e->newSize().width() ,    _firstChildLayout->size().height());
				_secondChildLayout->resize(e->newSize().width() , _secondChildLayout->size().height());

				_secondChildLayout->setPos(0, _secondChildLayout->geometry().y());
			}
			else if (deltaSize.width() == 0) {
				//
				// resized vertically, top and bottom child widgets will share height delta
				//
				_firstChildLayout->resize(      _firstChildLayout->size().width() ,    _firstChildLayout->size().height() + deltaSize.height() / 2.0);
				_secondChildLayout->resize(_secondChildLayout->size().width() , _secondChildLayout->size().height() + deltaSize.height() / 2.0);

				_secondChildLayout->setPos(0, _secondChildLayout->geometry().y() + deltaSize.height()/2.0);
			}
		}
		else {
			//
			// the bar is vertical so my children is at LEFT and RIGHT
			//
			_firstChildLayout->setPos(0,0);

			if (deltaSize.height() == 0) {
				// I'm resized horizontally. -> no height changes in my children
				_firstChildLayout->resize(_firstChildLayout->size().width() + deltaSize.width()/2.0,  _firstChildLayout->size().height());
				_secondChildLayout->resize(_secondChildLayout->size().width() + deltaSize.width()/2.0, _secondChildLayout->size().height());

				_secondChildLayout->setPos(_secondChildLayout->geometry().x() + deltaSize.width()/2.0 , 0);
			}
			else if (deltaSize.width() == 0) {
				// I'm resized vertically -> no width changes in my children
				_firstChildLayout->resize( _firstChildLayout->size().width(),  e->newSize().height());
				_secondChildLayout->resize(_secondChildLayout->size().width(), e->newSize().height());

				_secondChildLayout->setPos(_secondChildLayout->geometry().x(), 0);
			}
		}
	}

	//
	// no _bar, so this layoutWidget contains child basewidgets
	//
	else {
		if (_isTileOn) {
			doTile();
		}
		else {
			// If growing, do nothing
			// If shrinking, move child BaseWidgets accordingly

			if (deltaSize.width() < 0) {
				// I'm left or right layoutWidget

				if (pos().x() == 0) {
					// I'm left. parent layoutwidget's bar moved to the left
					adjustChildPos(0); // move them to the left
				}
				else {
					// I'm right
					adjustChildPos(0); // to the right
				}

			}
			else if (deltaSize.height() < 0) {
				// I'm top or bottom layoutWidget

				if (pos().y() == 0) {
					// I'm top. Parent layoutWidget's bar moved to the top
					adjustChildPos(1);
				}
				else {
					// I'm bottom
					adjustChildPos(1);
				}
			}
		}
	}
}

void SN_LayoutWidget::createChildPartitions(Qt::Orientation dividerOrientation) {
	QRectF first;
	QRectF second;

	QRectF br = boundingRect();
	if (dividerOrientation == Qt::Horizontal) {
		//
		// bar is horizontal, partition is Top and Bottom (same X pos)
		//

		/******* (0,0) is center
		first = QRectF(br.topLeft(), QSizeF(br.width(), br.height()/2));
		second = QRectF(br.left(), 0, br.width(), br.height()/2);
		********/

		first = QRectF( 0, 0,             br.width(), br.height()/2);
		second = QRectF(0, br.height()/2, br.width(), br.height()/2);
	}
	else {
		//
		// bar is vertical, partition is Left and Right (same Y pos)
		//

		/********* (0,0) is center
		first = QRectF(br.topLeft(), QSizeF(br.width()/2, br.height()));
		second = QRectF(0, br.top(), br.width()/2, br.height());
		*********/

		first = QRectF(  0,           0, br.width()/2, br.height());
		second = QRectF(br.width()/2, 0, br.width()/2, br.height());

	}

	createChildPartitions(dividerOrientation, first, second);
}

void SN_LayoutWidget::createChildPartitions(Qt::Orientation dividerOrientation, const QRectF &first, const QRectF &second) {
	if (_isTileOn) {
		// disable tile first
		toggleTile();
	}

	// create PartitionBar child item
	_bar = new SN_WallPartitionBar(dividerOrientation, this, this);

	_firstChildLayout = new SN_LayoutWidget("first", this, _settings, this);
	_secondChildLayout = new SN_LayoutWidget("second", this, _settings, this);

	_firstChildLayout->setSiblingLayout(_secondChildLayout);
	_secondChildLayout->setSiblingLayout(_firstChildLayout);

	//
	// if child widget is resized, then adjust my bar pos and length
	//
	connect(_firstChildLayout, SIGNAL(resized()), this, SLOT(adjustBar()));


	// will invoke resize()
	_firstChildLayout->setRectangle(first);
	_secondChildLayout->setRectangle(second);
//	qDebug() << "createchild result" << rect() << firstlayoutchild->geometry() << secondlayoutchild->geometry();


	//
	// reparent child items to appropriate widget. I shouldn't have any child baseWidgets at this point
	//
	foreach(QGraphicsItem *item, childItems()) {
//		if (item == _bar || item == _tileButton || item == _hButton || item == _vButton || item == _xButton ||  item==_firstChildLayout || item==_secondChildLayout ) {
////			qDebug() << "createChildlayout skipping myself, buttons and bar";
//			continue;
//		}
		if (item->type() < QGraphicsItem::UserType + 12) continue;

		SN_BaseWidget *bw = static_cast<SN_BaseWidget *>(item);

		QPointF newPos;

		// find out bw's effective center position
//		QPointF bwCenter = QPointF(0.5 * bw->size().width() * bw->scale(), 0.5 * bw->size().height() * bw->scale());

		if ( _firstChildLayout->rect().contains(_firstChildLayout->mapFromItem(bw, bw->boundingRect().center())) ) {
			newPos = mapToItem(_firstChildLayout, item->pos());
			item->setParentItem(_firstChildLayout);
		}
		else {
			newPos = mapToItem(_secondChildLayout, item->pos());
			item->setParentItem(_secondChildLayout);
		}
		item->setPos(newPos);
	}


	//
	// hides my buttons. This widget will just hold child basewidgets
	//
	_tileButton->hide();
	_vButton->hide();
	_hButton->hide();
	if (_xButton) _xButton->hide();
}


/**
  This is called when the _firstChildLayoutWidget is being resized
  */
void SN_LayoutWidget::adjustBar() {
	Q_ASSERT(_bar);

	/*******
	  ***** (0,0) is center
	if ( _bar->orientation() == Qt::Horizontal ) {
		// my children is top and bottom widgets
		qreal newY = boundingRect().top() + _firstChildLayout->size().height();
		QLineF line(boundingRect().left(), newY, boundingRect().right(), newY);
		_bar->setLine(line);
	}
	else {
		// my children is left and right widgets
		qreal newX = boundingRect().left() + _firstChildLayout->size().width();
		QLineF line(newX, boundingRect().top(), newX, boundingRect().bottom());
		_bar->setLine(line);
	}
	********
	*****/

	if ( _bar->orientation() == Qt::Horizontal ) {
		// my children is top and bottom widgets
		QPointF bottomLeft = _firstChildLayout->geometry().bottomLeft();
		QPointF bottomRight = _firstChildLayout->geometry().bottomRight();

		_bar->setLine(bottomLeft.x(), bottomLeft.y(), bottomRight.x(), bottomRight.y());
	}
	else {
		// my children is left and right widgets
		QPointF topRight = _firstChildLayout->geometry().topRight();
		QPointF bottomRight = _firstChildLayout->geometry().bottomRight();

		_bar->setLine(topRight.x(), topRight.y(), bottomRight.x(), bottomRight.y());
	}
}

/**
  find out child basewidgets located on top of the parent layoutwidget's _bar
  */
void SN_LayoutWidget::adjustChildPos(int direction) {

	foreach(QGraphicsItem *item, childItems()) {
		if (item->type() < QGraphicsItem::UserType + 12) continue;
		SN_BaseWidget *bw = static_cast<SN_BaseWidget *>(item);

		Q_ASSERT(_parentLayoutWidget);
		SN_WallPartitionBar *parentBar =  _parentLayoutWidget->bar();

		if (bw->collidesWithItem(parentBar, Qt::IntersectsItemBoundingRect)) {
			switch(direction) {
			case 0 : { // left or right
				bw->moveBy(parentBar->pos().x() - bw->pos().x() , 0);
 				break;
			}
			case 1 : { // up or down
				bw->moveBy(0, parentBar->pos().y() - bw->pos().y());
				break;
			}
			}
		}
	}
}

void SN_LayoutWidget::deleteChildPartitions() {
	Q_ASSERT(_bar);

	//
	// reparent all basewidgets of my child layouts to me
	//
	_firstChildLayout->reparentMyChildBasewidgets(this);
	_secondChildLayout->reparentMyChildBasewidgets(this);
	delete _firstChildLayout;
	delete _secondChildLayout;

	delete _bar;
	_bar = 0; // do I need this?

	//
	// provide interactivity after everything is done
	//
	_tileButton->show();
	_vButton->show();
	_hButton->show();
	if (_xButton) _xButton->show();
}

/**
  This layoutWidget doesn't have any child layoutWidget
  */
void SN_LayoutWidget::deleteMyself() {
	SN_LayoutWidget *sibling = siblingLayout();
	Q_ASSERT(sibling);

	sibling->setPos(0,0);
	sibling->resize(_parentLayoutWidget->size());


	// move my child (basewidgets) to my sibling
	reparentMyChildBasewidgets(sibling);


	if (sibling->bar()) {
		// if sibling has child LayoutWidgets
		_parentLayoutWidget->setFirstChildLayout(sibling->firstChildLayout());
		_parentLayoutWidget->setSecondChildLayout(sibling->secondChildLayout());

		sibling->firstChildLayout()->setParentLayoutWidget(_parentLayoutWidget);
		sibling->secondChildLayout()->setParentLayoutWidget(_parentLayoutWidget);
	}
	else {
		sibling->reparentMyChildBasewidgets(_parentLayoutWidget);
	}


	// I'll be deleted
	deleteLater();

	// my sibling will be deleted
	sibling->deleteLater();
}

void SN_LayoutWidget::doTile() {
	if (_bar) return;

	int itemcount = 0;

//	qreal layoutRatio = size().width() / size().height();

	qreal sumWHratio = 0.0;
	foreach(QGraphicsItem *item, childItems()) {
		if (item->type() < QGraphicsItem::UserType + 12) continue;
		SN_BaseWidget *bw = static_cast<SN_BaseWidget *>(item);
		itemcount++;

		if (bw->size().isNull() || bw->size().isEmpty())
			continue;

		sumWHratio += (bw->size().width() / bw->size().height());

		qDebug() << bw->size() << sumWHratio;
	}
	qreal avgWHratio = sumWHratio / itemcount;

	int numItemH = sqrt( itemcount * avgWHratio );
	if (numItemH < 1) numItemH = 1;
	int numItemV = itemcount - numItemH;


	/***
	setLayout(0);
	QGraphicsGridLayout *gridlayout = new QGraphicsGridLayout;
//	SN_GridLayout *grid = new SN_GridLayout;

	int row = 0, col = 0;
	foreach(QGraphicsItem *item, childItems()) {
		if (item->type() < QGraphicsItem::UserType + 12) continue;

		SN_BaseWidget * bw = static_cast<SN_BaseWidget *>(item);

		if (col == numItemH) {
			col = 0; // reset col index
			row++;
		}

		gridlayout->addItem(bw, row, col, Qt::AlignCenter);

		col++;
	}
	setLayout(gridlayout);
	***/


	int itemSpacing = 32; // pixel

	qreal widthPerItem = size().width() - itemSpacing;
	if ( numItemH > 1) {
		widthPerItem = (size().width() / numItemH) - itemSpacing;
	}
	qreal heightPerItem = size().height() - itemSpacing;
	if (numItemV > 1) {
		heightPerItem = (size().height() / numItemV) - itemSpacing;
	}

	qDebug() << numItemH << numItemV;
	qDebug() << widthPerItem << heightPerItem;

	int row = 0;
	int col = 0;
	foreach(QGraphicsItem *item, childItems()) {
		if (item->type() < QGraphicsItem::UserType + 12) continue;

		SN_BaseWidget * bw = static_cast<SN_BaseWidget *>(item);

		if (col == numItemH) {
			col = 0; // reset col index
			row++;
		}

		bw->setPos(col * (widthPerItem + itemSpacing) , row * (heightPerItem + itemSpacing));

		if ( bw->isWindow()) {
			bw->resize( widthPerItem, heightPerItem );
		}
		else {
			qreal scalewidth = 1.0, scaleheight = 1.0;
				// the the max effective width of this bw is widthPerItem
			scalewidth = widthPerItem / bw->size().width();
			scaleheight = heightPerItem / bw->size().height();

			bw->setScale(qMin(scalewidth, scaleheight));
		}

		col++;
	}
}

void SN_LayoutWidget::toggleTile() {
	if (_isTileOn) {
		_isTileOn = false;
		// do nothing
		setLayout(0);
	}
	else {
		_isTileOn = true;
		doTile();
	}
}

void SN_LayoutWidget::setButtonPos() {
//	qDebug() << "attachButton" << boundingRect() << geometry();

	/******* (0,0) center
	_tileButton->setPos(boundingRect().topRight().x() - _tileButton->size().width() - 10, 0);
	*****/

	_tileButton->setPos(size().width() - _tileButton->size().width() - 10, size().height()/2);

	_vButton->setPos(_tileButton->geometry().x(), _tileButton->geometry().bottom() + 5);
	_hButton->setPos(_vButton->geometry().x(),  _vButton->geometry().bottom() + 5);
	if (_xButton) {
		_xButton->setPos(_hButton->geometry().x(),  _hButton->geometry().bottom() + 5);
	}
}

void SN_LayoutWidget::saveSession(QDataStream &out) {

	if (_bar) {
		out << QString("LAYOUT");
		if (_bar->orientation() == Qt::Horizontal)
			out << 0;
		else
			out << 1;

		out << _firstChildLayout->pos() << _firstChildLayout->size() << _secondChildLayout->pos() << _secondChildLayout->size();
		_firstChildLayout->saveSession(out);
		_secondChildLayout->saveSession(out);
	}
	else {
		foreach(QGraphicsItem *item, childItems()) {
			if (item == _bar || item == _tileButton || item == _hButton || item == _vButton || item == _xButton || item == _firstChildLayout || item == _secondChildLayout) {
				continue;
			}

			// only consider user application
			if (item->type() < QGraphicsItem::UserType + 12 ) continue;

			SN_BaseWidget *bw = static_cast<SN_BaseWidget *>(item);
			if (!bw) continue;

			AppInfo *ai = bw->appInfo();
			out << QString("ITEM");
			//
			// item's pos() is saved. not the scenePos()
			//
			out << (int)ai->mediaType() << bw->scenePos() << bw->size() << bw->scale();

			// video, image, pdf, plugin, web have filename
			if (ai->fileInfo().exists()) {
				out << ai->mediaFilename();
				qDebug() << "SN_LayoutWidget::saveSession() : " << (int)ai->mediaType() << bw->scenePos() << bw->size() << bw->scale() << ai->mediaFilename();
			}
			else if (!ai->webUrl().isEmpty()) {
				out << ai->webUrl().toString();
				qDebug() << "SN_LayoutWidget::saveSession() : " << (int)ai->mediaType() << bw->scenePos() << bw->size() << bw->scale() << ai->webUrl().toString();
			}
			// vnc doesn't have filename
			else {
				out << ai->srcAddr() << ai->vncUsername() << ai->vncPassword();
				qDebug() << "SN_LayoutWidget::saveSession() : " << (int)ai->mediaType() << bw->scenePos() << bw->size() << bw->scale() << ai->srcAddr() << ai->vncUsername() << ai->vncPassword();
			}
		}
		out << QString("RETURN");
	}
}

void SN_LayoutWidget::loadSession(QDataStream &in, SN_Launcher *launcher) {
	QString header;
	in >> header;

	if (header == "LAYOUT") {
		int orien;
		in >> orien;

		QPointF pos1, pos2;
		QSizeF size1, size2;
		in >> pos1 >> size1 >> pos2 >> size2;

		if (orien==0)
			createChildPartitions(Qt::Horizontal, QRectF(pos1, size1), QRectF(pos2, size2));
		else
			createChildPartitions(Qt::Vertical, QRectF(pos1, size1), QRectF(pos2, size2));

		_firstChildLayout->loadSession(in, launcher);
		_secondChildLayout->loadSession(in, launcher);
	}
	else if (header == "ITEM") {
		int mtype;
		QPointF scenepos;
		QSizeF size;
		qreal scale;


		in >> mtype >> scenepos >> size >> scale;
		//qDebug() << "\tentry : " << mtype << scenepos << size << scale;

		QString file;
		QString user;
		QString pass;
		QString srcaddr;

		SN_BaseWidget *bw = 0;

		if (mtype == SAGENext::MEDIA_TYPE_VNC) {
			in >> srcaddr >> user >> pass;
			bw = launcher->launch(user, pass, 0, srcaddr, 10, scenepos);
		}
		else {
			in >> file;
			bw = launcher->launch(mtype, file, scenepos);
			if (mtype == SAGENext::MEDIA_TYPE_LOCAL_VIDEO || mtype == SAGENext::MEDIA_TYPE_VIDEO) {
//				::usleep(100 * 1000);
			}
		}


		if (!bw) {
			qDebug() << "Error : can't launch this entry from the session file" << mtype << file << srcaddr << user << pass << scenepos << size << scale;
		}
		else {
			//
			// at this point, the bw has added to the _rootLayoutWidget already
			//

			/******

			  in SN_BaseWidget::resizeEvent(), item's transformation origin is set to its center.
			  Because of this, if an image, whose pos is an edge of the scene, is scaled down (from its CENTER), the image can end up positioning out of scene's visible area.
			  So, I need to apply scale not from its center but from its topleft.
			  //bw->setScale(scale);
			  *****/
			//QTransform scaleTrans(scale, 0, 0, scale, 0, 0); // m31(dx) and m32(dy) which specify horizontal and vertical translation are set to 0
			//bw->setTransform(scaleTrans, true); // false -> this transformation matrix won't be combined with the current matrix. It will replace the current matrix.



			/**
			  below will work ok if item's transformOrigin is its top left
			  **/
			bw->setScale(scale);

			bw->resize(size);

			bw->setParentItem(this);
			bw->setPos(mapFromScene(scenepos));
		}

		loadSession(in, launcher);
	}
	else if (header == "RETURN") {
		qDebug("%s::%s() : RETURN", metaObject()->className(), __FUNCTION__);
	}
	else {
		qDebug("%s::%s() : Unknown entry", metaObject()->className(), __FUNCTION__);
	}
}

//QRectF SN_LayoutWidget::boundingRect() const {
//	return QRectF( -1 * size().width()/2 , -1 * size().height()/2 , size().width(), size().height());
//}




SN_GridLayout::SN_GridLayout(QGraphicsLayoutItem *parent)
    : QGraphicsGridLayout(parent)
{

}

void SN_GridLayout::setGeometry(const QRectF &rect) {

}




SN_WallPartitionBar::SN_WallPartitionBar(Qt::Orientation ori, SN_LayoutWidget *owner, QGraphicsItem *parent)
    : QGraphicsLineItem(parent)
    , _ownerNode(owner)
    , _orientation(ori)
{
//	setFlag(QGraphicsItem::ItemSendsScenePositionChanges, true);
//	setFlag(QGraphicsItem::ItemSendsGeometryChanges, true);
	setFlag(QGraphicsItem::ItemIsSelectable, false);
	setFlag(QGraphicsItem::ItemIsMovable, true);

	setAcceptedMouseButtons(Qt::LeftButton | Qt::NoButton);

	QPen pen;
	pen.setWidth(12);
	pen.setStyle(Qt::DashLine);
	pen.setColor(QColor(Qt::lightGray));
	setPen(pen);
}











