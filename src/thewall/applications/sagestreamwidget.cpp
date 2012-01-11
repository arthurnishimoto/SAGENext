#include "sagestreamwidget.h"
#include "sagepixelreceiver.h"

#include "../sage/fsmanagermsgthread.h"
#include "../sage/sagecommondefinitions.h"

#include "../common/commonitem.h"
#include "../common/imagedoublebuffer.h"

#include "base/appinfo.h"
#include "base/affinityinfo.h"
#include "base/perfmonitor.h"

#include "../system/resourcemonitor.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <netinet/in.h>
#include <netdb.h>

//#include <QProcess>

SN_SageStreamWidget::SN_SageStreamWidget(QString filename, const quint64 globalappid, const QSettings *s, QString senderIP, SN_ResourceMonitor *rm, QGraphicsItem *parent, Qt::WindowFlags wFlags)
    : SN_RailawareWidget(globalappid, s, parent, wFlags)
    , _settings(s)
    , _fsmMsgThread(0)
    , _sailAppProc(0) // QProcess *
    , _sageAppId(0)
    , _receiverThread(0) // QThread *
    , _textureid(-1) // will draw this texture
    , _usePbo(true)
    , _pboBufIdx(0)
//    , _imagePointer(0)
    , doubleBuffer(0) // application buffer
    //, _pixmap(0)
    , serversocket(0)
    , streamsocket(0) // streaming channel
//    , frameCounter(0)
//	, _bordersize(0)
    , _streamProtocol(0)
	, _readyForStreamer(false) // fsm thread polls on this


    , __firstFrame(true)
    , __bufferMapped(false)
//    , _recvThreadEnd(false)
    , _pbomutex(0)
    , _pbobufferready(0)
{
    // this is defined in BaseWidget
    setRMonitor(rm);

	_appInfo->setFileInfo(filename);
	_appInfo->setSrcAddr(senderIP);

	if ( ! QObject::connect(&_initReceiverWatcher, SIGNAL(finished()), this, SLOT(startReceivingThread())) ) {
		qCritical("SN_SageStreamWidget constructor : Failed to connect _initReceiverWatcher->finished() signal to this->startReceivingThread() slot");
	}

	_usePbo = s->value("graphics/openglpbo", false).toBool();

//	_bordersize = s->value("gui/framemargin",0).toInt();
	
//	setAttribute(Qt::WA_PaintOnScreen);


//	connect(&_memcpyThreadWatcher, SIGNAL(finished()), this, SLOT(unmapGLBuffer()));


	if (_usePbo) {
		_pbomutex = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
		if ( pthread_mutex_init(_pbomutex, 0) != 0 ) {
			perror("pthread_mutex_init");
		}
		if ( pthread_mutex_unlock(_pbomutex) != 0 ) {
			perror("pthread_mutex_unlock");
		}

		_pbobufferready = (pthread_cond_t *)malloc(sizeof(pthread_cond_t));
		if ( pthread_cond_init(_pbobufferready, 0) != 0 ) {
			perror("pthread_cond_init");
		}
	}
}



SN_SageStreamWidget::~SN_SageStreamWidget()
{
//    qDebug() << _globalAppId << "begin destructor" << QTime::currentTime().toString("hh:mm:ss.zzz");

    if (_affInfo)  {
        if ( !  _affInfo->disconnect() ) {
            qDebug() << "affInfo disconnect() failed";
        }
    }

    if (_rMonitor) {
        //_affInfo->disconnect();
        _rMonitor->removeSchedulableWidget(this); // remove this from ResourceMonitor::widgetMultiMap
        _rMonitor->removeApp(this); // will emit appRemoved(int) which is connected to Scheduler::loadBalance()
        //qDebug() << "affInfo removed from resourceMonitor";
    }

    disconnect(this);


    /**
      1. close fsm message channel
    **/
    _fsmMsgThread->sendSailShutdownMsg();
    _fsmMsgThread->wait();


    if (_receiverThread) {
        disconnect(_receiverThread, SIGNAL(frameReceived()), this, SLOT(scheduleUpdate()));

        /**
          2. pixel receiving thread must exit from run()
          **/
        _receiverThread->endReceiver();
    }

    /**
      3. The fsm thread can be deleted.
      **/
    delete _fsmMsgThread;



    /**
      4. Before calling receiverThread->wait(), let's release locks first so that receiverThread can exit from run() safely
      **/
    if (doubleBuffer) {
        doubleBuffer->releaseBackBuffer();
        doubleBuffer->releaseLocks();
    }



	if (_receiverThread) {
		if (_pbomutex) {
			//
			// without below two statements, _receiverThread->wait() will block forever
			//
			_receiverThread->flip(0); // very important !
			pthread_cond_signal(_pbobufferready);
		}
    /**
      5. make sure receiverThread finished safely
      **/
		_receiverThread->wait();

    /**
      6. Then schedule deletion
      **/
		_receiverThread->deleteLater();
	}

    /**
      7. Now the double buffer can be deleted
      **/
    if (doubleBuffer) delete doubleBuffer;
    doubleBuffer = 0;


	if (_useOpenGL && glIsTexture(_textureid)) {
		glDeleteTextures(1, &_textureid);
	}

	if (_usePbo) {
//		_recvThreadEnd = true;
//		_recvThreadFuture.cancel();
//		_recvThreadWatcher.cancel();

		glDeleteBuffersARB(2, _pboIds);
		glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, 0);

		if (_pbobufferready) {
			__bufferMapped = true;
			pthread_cond_signal(_pbobufferready);
		}
		if (_pbomutex) {
			pthread_mutex_unlock(_pbomutex);
	//		pthread_mutex_destroy(_mutex);
		}

//		_recvThreadFuture.waitForFinished();
//		qDebug() << "~SageStreamWidget finished\n\n";
		free(_pbobufferready);
		free(_pbomutex);
	}

	// this causes other sagestreamwidget gets killed
	// don't know why
//	if (_sailAppProc) {
//		_sailAppProc->kill();
//	}

//    qDebug() << _globalAppId << "end destructor" << QTime::currentTime().toString("hh:mm:ss.zzz");
    qDebug("%s::%s() ",metaObject()->className(),  __FUNCTION__);
}

void SN_SageStreamWidget::setFsmMsgThread(fsManagerMsgThread *thread) {
	_fsmMsgThread = thread;
	_fsmMsgThread->start();
}


void SN_SageStreamWidget::paint(QPainter *painter, const QStyleOptionGraphicsItem *o, QWidget *w) {
	if (_perfMon) {
		_perfMon->getDrawTimer().start();
	}

//	painter->setCompositionMode(QPainter::CompositionMode_Source);
	
	if (_useOpenGL && painter->paintEngine()->type() == QPaintEngine::OpenGL2
	//|| painter->paintEngine()->type() == QPaintEngine::OpenGL
	)
	{
		//
		// 0 draw latency because it's drawn from the cache
		// but higher latency in scheduleUpdate()
		//
		//	QGLWidget *viewportWidget = (QGLWidget *)w;
		//	viewportWidget->drawTexture(QPointF(0,0), _textureid);

		/*
		  this takes lots of time when doing DMA write to GPU memory using QGLBuffer due to the context switching
		  */
		painter->beginNativePainting();

		glEnable(GL_TEXTURE_2D);
		glBindTexture(GL_TEXTURE_2D, _textureid);

		glBegin(GL_QUADS);
		//
		// below is same with QGLContext::InvertedYBindOption
		//
		glTexCoord2f(0.0, 1.0); glVertex2f(0, size().height());
		glTexCoord2f(1.0, 1.0); glVertex2f(size().width(), size().height());
		glTexCoord2f(1.0, 0.0); glVertex2f(size().width(), 0);
		glTexCoord2f(0.0, 0.0); glVertex2f(0, 0);

		//
		// below is normal (In OpenGL, 0,0 is bottom-left, In Qt, 0,0 is top-left)
		//
		//glTexCoord2f(0.0, 1.0); glVertex2f(0, 0);
		//glTexCoord2f(1.0, 1.0); glVertex2f(_imagePointer->width(), 0);
		//glTexCoord2f(1.0, 0.0); glVertex2f(_imagePointer->width(), _imagePointer->height());
		//glTexCoord2f(0.0, 0.0); glVertex2f(0, _imagePointer->height());

		glEnd();

		glBindTexture(GL_TEXTURE_2D, 0);
		glDisable(GL_TEXTURE_2D);

		painter->endNativePainting();
	}
	else {
		/***
		  On Linux/X11 platforms which support it, Qt will use glX/EGL texture-from-pixmap extension.
	This means that if your QPixmap has a real X11 pixmap backend, we simply bind that X11 pixmap as a texture and avoid copying it.
	You will be using the X11 pixmap backend if the pixmap was created with QPixmap::fromX11Pixmap() or you’re using the “native” graphics system.
	Not only does this avoid overhead but it also allows you to write a composition manager or even a widget which shows previews of all your windows.


	QPixmap, unlike QImage, may be hardware dependent.
	On X11, Mac and Symbian, a QPixmap is stored on the server side while a QImage is stored on the client side
	(on Windows,these two classes have an equivalent internal representation,
	i.e. both QImage and QPixmap are stored on the client side and don't use any GDI resources).

	So, drawing pixmap is much faster but QImage has to be converted to QPixmap for every frame which involves converting plus copy to X Server.
		  ***/

		if (!_pixmapForDrawing.isNull()) {
			painter->drawPixmap(0, 0, _pixmapForDrawing);
		}
	}

	SN_BaseWidget::paint(painter,o,w);

	if (_perfMon)
		_perfMon->updateDrawLatency(); // drawTimer.elapsed() will be called.
}



void SN_SageStreamWidget::scheduleReceive() {
//	qDebug() << "widget wakeOne";
//	if(wc) wc->wakeOne();
	_receiverThread->receivePixel();
}


/**
  This slot connected to the signal SagePixelReceiver::frameReceived() so that this can be called after receiving each image frame
  */
void SN_SageStreamWidget::scheduleUpdate() {

    if ( !doubleBuffer || !_receiverThread || _receiverThread->isFinished() || !isVisible() )
        return;

	//qDebug() << _globalAppId << "scheduleUpdate" << QTime::currentTime().toString("hh:mm:ss.zzz");

	const QImage *constImageRef = static_cast<QImage *>(doubleBuffer->getBackBuffer());

	if ( !constImageRef || constImageRef->isNull()) {
		qCritical("SageStreamWidget::%s() : globalAppId %llu, sageAppId %llu : imgPtr is null. Failed to retrieve back buffer from double buffer", __FUNCTION__, globalAppId(), _sageAppId);
		return;
	}

	_perfMon->getConvTimer().start();


	// use PBO
	/**********
	if ( _useOpenGL && _usePbo ) {
		_pboBufIdx = (_pboBufIdx + 1) % 2;
		int nextbufidx = (_pboBufIdx + 1) % 2;

		glBindTexture(GL_TEXTURE_2D, _textureid);
		glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, _pboIds[_pboBufIdx]);
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, _appInfo->nativeSize().width(), _appInfo->nativeSize().height(), _pixelFormat, GL_UNSIGNED_BYTE, 0);


		glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, _pboIds[nextbufidx]);
		glBufferDataARB(GL_PIXEL_UNPACK_BUFFER_ARB, _appInfo->frameSizeInByte(), 0, GL_STREAM_DRAW_ARB);
//		GLubyte *ptr = (GLubyte *)glMapBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, GL_WRITE_ONLY_ARB);
		void *ptr = glMapBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, GL_WRITE_ONLY_ARB);
		if (ptr) {
//			Q_ASSERT(constImageRef->byteCount() == _appInfo->frameSizeInByte());
			::memcpy(ptr, constImageRef->bits(), _appInfo->frameSizeInByte());
			glUnmapBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB);

//			_memcpyThreadFuture = QtConcurrent::run(this, &SN_SageStreamWidget::__doMemCpyThread, ptr, constImageRef->bits(), constImageRef->byteCount());
//			_memcpyThreadWatcher.setFuture(_memcpyThreadFuture);
		}
		glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, 0);
	}
	********/
	if (_useOpenGL) {

		// QGLContext::InvertedYBindOption Because In OpenGL 0,0 is bottom left, In Qt 0,0 is top left
		//
		// Below is awfully slow. Probably because QImage::scaled() inside qgl.cpp to convert image size near power of two
		// _textureid = glContext->bindTexture(constImageRef->convertToFormat(QImage::Format_RGB32), GL_TEXTURE_2D, QGLContext::InvertedYBindOption);

		//glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		//glEnable(GL_TEXTURE_2D);
		glBindTexture(GL_TEXTURE_2D, _textureid);

		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

		//
		// Note that it's QImage::Format_RGB888 we're getting from SAGE app
		//
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, constImageRef->width(), constImageRef->height(), _pixelFormat, GL_UNSIGNED_BYTE, constImageRef->bits());
		//GLenum error = glGetError();
		//if(error != GL_NO_ERROR) {
		//		qCritical("texture upload failed. error code 0x%x\n", error);
		//}

		glBindTexture(GL_TEXTURE_2D, 0);
	}
	else {
		/*
		  // drawing RGB32 is the fastest. But drawing QImage is slower than drawing QPixmap
		_imageForDrawing = constImageRef->convertToFormat(QImage::Format_RGB32);
		if (_imageForDrawing.isNull()) {
			qCritical("SN_SageStreamWidget::%s() : Image conversion failed", __FUNCTION__);
			return;
		}
		*/

		//
	    // Drawing QPixmap is the fastest. But in X11, this means image is converted and copied to X Server -> slow !
	    // For static images this is ok but for animations this results bad frame rate
	    //
	   /*
      _pixmap = QPixmap::fromImage(*constImageRef);
      if (_pixmap.isNull()) {
	   qCritical("SageStreamWidget::scheduleUpdate() : QPixmap::fromImage() error");
	   return;
      }
      */

		//
		// Below doesn't work under X11 backend. Use raster backend
		//
		_pixmapForDrawing.convertFromImage(*constImageRef, Qt::ColorOnly | Qt::ThresholdDither);


   //	There's small conversion delay but drawing is faster
   //	_imageForDrawing = constImageRef->convertToFormat(QImage::Format_RGB32); // faster drawing !!
   //	_imageForDrawing = constImageRef->convertToFormat(QImage::Format_ARGB32_Premultiplied); // faster drawing !!

	   // Don't create QImage like this. This slot is called frequently
   //	_imageForDrawing = QImage(rawptr, doubleBuffer->imageWidth(), doubleBuffer->imageHeight(), doubleBuffer->imageFormat()).convertToFormat(QImage::Format_RGB32);

   //	_imageForDrawing = constImageRef->convertToFormat(QImage::Format_RGB32);
   //	if (_imageForDrawing.isNull()) {
   //		qCritical("SN_SageStreamWidget::%s() : Image conversion failed", __FUNCTION__);
   //		return;
   //	}
	}

	_perfMon->updateConvDelay();

	setScheduled(false); // reset scheduling flag for SMART scheduler

	//qDebug() << QTime::currentTime().toString("mm:ss.zzz") << " widget : " << frameCounter << " has converted";

	/*
	  Maybe I should schedule update() and releaseBackBuffer in the scheduler
	 */
	doubleBuffer->releaseBackBuffer();


	// Schedules a redraw. This is not an immediate paint. This actually is postEvent()
	// QGraphicsView will process the event
	update(); // post paint event to myself
	//qApp->sendPostedEvents(this, QEvent::MetaCall);
	//qApp->flush();
	//qApp->processEvents();

	//this->scene()->views().front()->update( mapRectToScene(boundingRect()).toRect() );
}



/**
  This is invoked after the waitForPixelStreamingConnection thread finishes
  */
void SN_SageStreamWidget::startReceivingThread() {
	Q_ASSERT(streamsocket > 0);

	if (_useOpenGL) {
		glGenTextures(1, &_textureid);
		glBindTexture(GL_TEXTURE_2D, _textureid);
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, size().width(), size().height(), 0, _pixelFormat, GL_UNSIGNED_BYTE, (void *)0);
		glBindTexture(GL_TEXTURE_2D, 0);

		if ( _usePbo ) {
			qDebug() << "SN_SageStreamWidget : OpenGL pbuffer extension is present. Using PBO";
			glGenBuffersARB(2, _pboIds);

			glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, _pboIds[0]);
			glBufferDataARB(GL_PIXEL_UNPACK_BUFFER_ARB, _appInfo->frameSizeInByte(), 0, GL_STREAM_DRAW_ARB);

			glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, _pboIds[1]);
			glBufferDataARB(GL_PIXEL_UNPACK_BUFFER_ARB, _appInfo->frameSizeInByte(), 0, GL_STREAM_DRAW_ARB);

			glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, 0);


			////////////// receiving thread /////////////////
/*
			connect(&_recvThreadWatcher, SIGNAL(started()), this, SLOT(schedulePboUpdate()));
			connect(&_recvThreadWatcher, SIGNAL(finished()), this, SLOT(close()));
			connect(_fsmMsgThread, SIGNAL(finished()), &_recvThreadWatcher, SLOT(cancel()));

			if ( ! connect(this, SIGNAL(frameReady()), this, SLOT(schedulePboUpdate())) ) {
				qDebug() << "\n\nFailed to connect frameReady() <-> schedulePboUpdate()\n";
			}
			else {
				_recvThreadFuture = QtConcurrent::run(this, &SN_SageStreamWidget::__recvThread);
				_recvThreadWatcher.setFuture(_recvThreadFuture);
			}


			return;
			*/
		}
	}


	_receiverThread = new SN_SagePixelReceiver(_streamProtocol, streamsocket, doubleBuffer, _usePbo, _pbobufarray, _pbomutex, _pbobufferready, _appInfo, _perfMon, _affInfo, _settings);
    Q_ASSERT(_receiverThread);

    QObject::connect(_receiverThread, SIGNAL(finished()), this, SLOT(close())); // WA_Delete_on_close is defined

    // don't do below.
//		connect(receiverThread, SIGNAL(finished()), receiverThread, SLOT(deleteLater()));

//		if (!scheduler) {
            // This is queued connection because receiverThread reside outside of the main thread

	if (_usePbo) {
		if ( ! QObject::connect(_receiverThread, SIGNAL(frameReceived()), this, SLOT(schedulePboUpdate())) ) {
			qCritical("%s::%s() : Failed to connect frameReceived() signal and schedulePboUpdate() slot", metaObject()->className(), __FUNCTION__);
			return;
        }
		QObject::connect(_receiverThread, SIGNAL(started()), this, SLOT(schedulePboUpdate()));
	}
	else {
		if ( ! QObject::connect(_receiverThread, SIGNAL(frameReceived()), this, SLOT(scheduleUpdate())) ) {
			qCritical("%s::%s() : Failed to connect frameReceived() signal and scheduleUpdate() slot", metaObject()->className(), __FUNCTION__);
			return;
		}
	}
//		}
    _receiverThread->start();

}



void SN_SageStreamWidget::schedulePboUpdate() {

	Q_ASSERT(_pbomutex);
	Q_ASSERT(_pbobufferready);
	Q_ASSERT(_appInfo);

	_perfMon->getConvTimer().start();

	//
	// flip array index
	//
	_pboBufIdx = (_pboBufIdx + 1) % 2;
	int nextbufidx = (_pboBufIdx + 1) % 2;

	GLenum error = glGetError();

	//
	// unmap previous buffer
	//
	if (!__firstFrame) {
//		qDebug() << "unmap" << nextbufidx;
		glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, _pboIds[nextbufidx]);
		if ( ! glUnmapBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB) ) {
			qDebug() << "schedulePboUpdate() : glUnmapBufferARB() failed";
		}
	}
	else {
		__firstFrame = false;
	}

	//
	// map buffer
	//
//	qDebug() << "map" << _pboBufIdx;
	glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, _pboIds[_pboBufIdx]);
	error = glGetError();
	if(error != GL_NO_ERROR) qCritical("glBindBufferARB() error code 0x%x\n", error);

	glBufferDataARB(GL_PIXEL_UNPACK_BUFFER_ARB, _appInfo->frameSizeInByte(), 0, GL_STREAM_DRAW_ARB);
	error = glGetError();
	if(error != GL_NO_ERROR) qCritical("glBufferDataARB() error code 0x%x\n", error);

	void *ptr = glMapBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, GL_WRITE_ONLY_ARB);
	error = glGetError();
	if(error != GL_NO_ERROR) qCritical("glMapBufferARB() error code 0x%x\n", error);

	if (ptr) {
		_pbobufarray[_pboBufIdx] = ptr;

		// signal thread
		pthread_mutex_lock(_pbomutex);

		__bufferMapped = true;
		_receiverThread->flip(_pboBufIdx);

		pthread_cond_signal(_pbobufferready);
	//	qDebug() << QDateTime::currentMSecsSinceEpoch() << "signaled";
		pthread_mutex_unlock(_pbomutex);
	}
	else {
		qCritical() << "glMapBUffer failed()";
	}

	//
	// update texture with the pbo buffer
	//
//	qDebug() << "update texture" << nextbufidx;
	glBindTexture(GL_TEXTURE_2D, _textureid);
	glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, _pboIds[nextbufidx]);
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, _appInfo->nativeSize().width(), _appInfo->nativeSize().height(), _pixelFormat, GL_UNSIGNED_BYTE, 0);

	//
	// schedule paintEvent
	//
	update();

	//
	// reset GL state
	//
	glBindTexture(GL_TEXTURE_2D, 0);
	glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, 0);

	_perfMon->updateConvDelay();
}






/*!
  This will run in a separate thread
  */
/***
void SN_SageStreamWidget::__recvThread() {
	struct timeval lats, late;
	struct rusage ru_start, ru_end;
	if(_perfMon) {
		_perfMon->getRecvTimer().start(); //QTime::start()

#if defined(Q_OS_LINUX)
		getrusage(RUSAGE_THREAD, &ru_start); // that of calling thread. Linux specific
#elif defined(Q_OS_MAC)
		getrusage(RUSAGE_SELF, &ru_start);
#endif
	}

	ssize_t totalread = 0;

	while(!_recvThreadEnd) {
		// wait for glMapBufferARB
		pthread_mutex_lock(_pbomutex);
//		qDebug() << QDateTime::currentMSecsSinceEpoch() << "will wait";
		while(!__bufferMapped)
			pthread_cond_wait(_pbobufferready, _pbomutex);


		gettimeofday(&lats, 0);

		// receive frame (DMA write to GPU memory)
		// pointer in the _bufarray must be valid at this point
		totalread = __recvFrame(streamsocket, _appInfo->frameSizeInByte(), _pbobufarray[_pboBufIdx]);
		if (totalread <= 0 ) break;

		gettimeofday(&late, 0);


		// reset flag
		__bufferMapped = false;

		// schedule screen update
		emit frameReady();


		if (_perfMon) {
#if defined(Q_OS_LINUX)
			getrusage(RUSAGE_THREAD, &ru_end);
#elif defined(Q_OS_MAC)
			getrusage(RUSAGE_SELF, &ru_end);
#endif
			qreal networkrecvdelay = ((double)late.tv_sec + (double)late.tv_usec * 0.000001) - ((double)lats.tv_sec + (double)lats.tv_usec * 0.000001);

			// calculate
			_perfMon->updateObservedRecvLatency(totalread, networkrecvdelay, ru_start, ru_end);
			ru_start = ru_end;
		}

		pthread_mutex_unlock(_pbomutex);
	}
	qDebug() << "SN_SageStreamWidget::__recvThread finished\n";
}

ssize_t SN_SageStreamWidget::__recvFrame(int socket, int byteCount, void *ptr) {
	Q_ASSERT(socket);

	ssize_t totalread = 0;
	ssize_t read = 0;

//	gettimeofday(&lats, 0);

	GLubyte *bufptr = (GLubyte *)ptr;

	// PIXEL RECEIVING
	while (totalread < byteCount ) {
		// If remaining byte is smaller than user buffer length (which is groupSize)
		if ( byteCount-totalread < _appInfo->networkUserBufferLength() ) {
			read = recv(socket, bufptr, byteCount-totalread , MSG_WAITALL);
		}
		// otherwise, always read groupSize bytes
		else {
			read = recv(socket, bufptr, _appInfo->networkUserBufferLength(), MSG_WAITALL);
		}
		if ( read == -1 ) {
			qDebug("SagePixver::run() : error while reading.");
			break;
		}
		else if ( read == 0 ) {
			qDebug("SagePiver::run() : sender disconnected");
			break;
		}
		// advance pointer
		bufptr += read;
		totalread += read;
	}
	if ( totalread < byteCount ) {
		qDebug() << "totalread < bytecount";
	}

	return totalread;
}
***/



/**
  THis slot is invoked by fsManagerMsgThread
  */
void SN_SageStreamWidget::doInitReceiver(quint64 sageappid, const QString &appname, const QRect &initrect, int protocol, int port) {
//	qDebug() << "\nRunning waitForPixelStreamConnection";
	_sageAppId = sageappid;
	_initReceiverFuture = QtConcurrent::run(this, &SN_SageStreamWidget::waitForPixelStreamerConnection, protocol, port, appname);
	_initReceiverWatcher.setFuture(_initReceiverFuture);
}


/**
  Receive SAGE message from the sender

  This slot will run in a separate thread.
  */
int SN_SageStreamWidget::waitForPixelStreamerConnection(int protocol, int port, const QString &appname) {
	_streamProtocol = protocol;

	/* accept connection from sageStreamer */
    serversocket = ::socket(AF_INET, SOCK_STREAM, 0);
    if ( serversocket == -1 ) {
            qCritical("SageStreamWidget::%s() : couldn't create socket", __FUNCTION__);
            return -1;
    }

    // setsockopt
    int optval = 1;
    if ( setsockopt(serversocket, SOL_SOCKET, SO_REUSEADDR, &optval, (socklen_t)sizeof(optval)) != 0 ) {
            qWarning("SageStreamWidget::%s() : setsockopt SO_REUSEADDR failed",  __FUNCTION__);
    }

    // bind to port
    struct sockaddr_in localAddr, clientAddr;
    memset(&localAddr, 0, sizeof(localAddr));
    localAddr.sin_family = AF_INET;
    localAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    localAddr.sin_port = htons(protocol + port);

    // bind
    if( bind(serversocket, (struct sockaddr *)&localAddr, sizeof(struct sockaddr_in)) != 0) {
            qCritical("SageStreamWidget::%s() : bind error",  __FUNCTION__);
            return -1;
    }

    // put in listen mode
    listen(serversocket, 15);

    // accept
    /** accept will BLOCK **/
//	qDebug() << "SN_SageStreamWidget::waitForPixelStreamerConn() : sageappid" << _sageAppId << "Before accept(). TCP port" << protocol+port << QTime::currentTime().toString("hh:mm:ss.zzz");

    memset(&clientAddr, 0, sizeof(clientAddr));
    int addrLen = sizeof(struct sockaddr_in);

	_readyForStreamer = true;

    if ((streamsocket = accept(serversocket, (struct sockaddr *)&clientAddr, (socklen_t*)&addrLen)) == -1) {
            qCritical("SageStreamWidget::%s() : accept error", __FUNCTION__);
            perror("accept");
            return -1;
    }

//	struct hostent *he = gethostbyaddr( (void *)&clientAddr, addrLen, AF_INET);
//	Q_ASSERT(he);
//	qDebug("SageStreamWidget::%s() : %s", __FUNCTION__, he->h_name);

    // read regMsg 1024Byte
    /*char regMsg[REG_MSG_SIZE];
              sprintf(regMsg, "%d %d %d %d %d %d %d %d %d %d %d",
                              config.streamType, // HARD_SYNC
                              config.frameRate,
                              winID,
                              config.groupSize,
                              blockSize,
                              config.nodeNum,
                              (int)config.pixFmt,
                              config.blockX,
                              config.blockY,
                              config.totalWidth,
                              config.totalHeight);


                               [103 60 1 131072 12416 1 5 64 64 400 400]
              */


    QByteArray regMsg(OldSage::REG_MSG_SIZE, '\0');
    int read = recv(streamsocket, (void *)regMsg.data(), regMsg.size(), MSG_WAITALL);
    if ( read == -1 ) {
            qCritical("SageStreamWidget::%s() : error while reading regMsg. %s",__FUNCTION__, "");
            return -1;
    }
    else if ( read == 0 ) {
            qCritical("SageStreamWidget::%s() : sender disconnected, while reading 1KB regMsg",__FUNCTION__);
            return -1;
    }

    QString regMsgStr(regMsg);
    QStringList regMsgStrList = regMsgStr.split(" ", QString::SkipEmptyParts);
//    qDebug("SageStreamWidget::%s() : recved regMsg, port %d, sageStreamer::connectToRcv() [%s]",  __FUNCTION__, protocol+port, regMsg.constData());
    int framerate = regMsgStrList.at(1).toInt();
    int groupsize = regMsgStrList.at(3).toInt(); // this is going to be the network user buffer size

    _appInfo->setNetworkUserBufferLength(groupsize);

    int pixfmt = regMsgStrList.at(6).toInt();
    int resX = regMsgStrList.at(9).toInt();
    int resY = regMsgStrList.at(10).toInt();
    Q_ASSERT(resX > 0 && resY > 0);


	//	int fmargin = _settings->value("gui/framemargin",0).toInt();
	//    resize(resX + fmargin*2, resY + fmargin*2); // BaseWidget::ResizeEvent will call setTransforOriginPoint
	resize(resX, resY);
	_appInfo->setFrameSize(resX, resY, getPixelSize((sagePixFmt)pixfmt) * 8);


	qDebug() << "SN_SageStreamWidget : streamer connected. groupSize" << _appInfo->networkUserBufferLength() << "Byte. Framerate" << framerate << "fps";
    _perfMon->setExpectedFps( (qreal)framerate );
    _perfMon->setAdjustedFps( (qreal)framerate );



    /* create double buffer */
	if (!_usePbo) {
		if ( createImageBuffer(resX, resY, (sagePixFmt)pixfmt) != 0 ) {
			qCritical("%s::%s() : imagedoublebuffer is not valid", metaObject()->className(), __FUNCTION__);
			::shutdown(streamsocket, SHUT_RDWR);
			QMetaObject::invokeMethod(_fsmMsgThread, "sendSailShutdownMsg", Qt::QueuedConnection);
			deleteLater();
			return -1;
		}
	}
	

    if(_affInfo)
		_affInfo->setWidgetID(_sageAppId);


//		qDebug("SageStreamWidget::%s() : sageappid %llu, groupsize %d, frameSize(SAIL) %d, frameSize(QImage) %d, expectedFps %.2f", __FUNCTION__, sageAppId, _appInfo->getNetworkUserBufferLength(), imageSize, _appInfo->getFrameBytecount(), _perfMon->getExpetctedFps());

    _appInfo->setExecutableName( appname );
    if ( appname == "imageviewer" ) {
		_appInfo->setMediaType(SAGENext::MEDIA_TYPE_IMAGE);
    }
    else {
		// this is done in the Launcher
//                _appInfo->setMediaType(MEDIA_TYPE_VIDEO);
    }

//	qDebug() << "waitForStreamerConnection returning";
	return streamsocket;
}


int SN_SageStreamWidget::createImageBuffer(int resX, int resY, sagePixFmt pixfmt) {
    int bytePerPixel = getPixelSize(pixfmt);
//    int memwidth = resX * bytePerPixel; //Byte (single row of frame)

    qDebug("%s::%s() : recved regMsg. size %d x %d, pixfmt %d, pixelSize %d Byte, bytecount %d Byte", metaObject()->className(), __FUNCTION__, resX, resY, pixfmt, bytePerPixel, resX * bytePerPixel * resY);

	if (!_usePbo)
		if (!doubleBuffer)
			doubleBuffer = new ImageDoubleBuffer;

    /*
         Do not draw ARGB32 images into the raster engine.
         ARGB32_premultiplied and RGB32 are the best ! (they are pixel wise compatible)
         http://labs.qt.nokia.com/2009/12/18/qt-graphics-and-performance-the-raster-engine/
      */
    switch(pixfmt) {
    case PIXFMT_888 : { // GL_RGB
		_pixelFormat = GL_RGB;
        if (doubleBuffer) doubleBuffer->initBuffer(resX, resY, QImage::Format_RGB888);
        //image = new QImage(resX, resY, QImage::Format_RGB32); // x0ffRRGGBB
        break;
    }
    case PIXFMT_888_INV : { // GL_BGR
		_pixelFormat = GL_BGR;
        if (doubleBuffer) doubleBuffer->initBuffer(resX, resY, QImage::Format_RGB888);
        if (doubleBuffer) doubleBuffer->rgbSwapped();
        break;
    }
    case PIXFMT_8888 : { // GL_RGBA
		_pixelFormat = GL_RGBA;
        if (doubleBuffer) doubleBuffer->initBuffer(resX, resY, QImage::Format_RGB32);
        break;
    }
    case PIXFMT_8888_INV : { // GL_BGRA
		_pixelFormat = GL_BGRA;
        if (doubleBuffer) doubleBuffer->initBuffer(resX, resY, QImage::Format_RGB32);
        if (doubleBuffer) doubleBuffer->rgbSwapped();
        break;
    }
    case PIXFMT_555 : { // GL_RGB, GL_UNSIGNED_SHORT_5_5_5_1
        if (doubleBuffer) doubleBuffer->initBuffer(resX, resY, QImage::Format_RGB555);
        break;
    }
    default: {
		_pixelFormat = GL_RGB;
        if (doubleBuffer) doubleBuffer->initBuffer(resX, resY, QImage::Format_RGB888);
        break;
    }
    }

	return 0;
}

int SN_SageStreamWidget::getPixelSize(sagePixFmt type)
{
	int bytesPerPixel = 0;
	switch(type) {
	case PIXFMT_555:
	case PIXFMT_555_INV:
	case PIXFMT_565:
	case PIXFMT_565_INV:
	case PIXFMT_YUV: {
		bytesPerPixel = 2;
		break;
	}
	case PIXFMT_888: {
		_pixelFormat = GL_RGB;
		bytesPerPixel = 3;
		break;
	}
	case PIXFMT_888_INV: {
		_pixelFormat = GL_BGR;
		bytesPerPixel = 3;
		break;
	}

	case PIXFMT_8888: {
		_pixelFormat = GL_RGBA;
		bytesPerPixel = 4;
		break;
	}
	case PIXFMT_8888_INV: {
		_pixelFormat = GL_BGRA;
		bytesPerPixel = 4;
		break;
	}

	case PIXFMT_DXT: {
		bytesPerPixel = 8;
		break;
	}

	default: {
		_pixelFormat = GL_RGB;
		bytesPerPixel = 3;
		break;
	}
	}
	return bytesPerPixel;
}





/*
int SageStreamWidget::initialize(quint64 sageappid, QString appname, QRect initrect, int protocol, int port) {

        _sageAppId = sageappid;

        // accept connection from sageStreamer
        serversocket = ::socket(AF_INET, SOCK_STREAM, 0);
        if ( serversocket == -1 ) {
                qCritical("SageStreamWidget::%s() : couldn't create socket", __FUNCTION__);
                return -1;
        }

        // setsockopt
        int optval = 1;
        if ( setsockopt(serversocket, SOL_SOCKET, SO_REUSEADDR, &optval, (socklen_t)sizeof(optval)) != 0 ) {
                qWarning("SageStreamWidget::%s() : setsockopt SO_REUSEADDR failed",  __FUNCTION__);
        }

        // bind to port
        struct sockaddr_in localAddr, clientAddr;
        memset(&localAddr, 0, sizeof(localAddr));
        localAddr.sin_family = AF_INET;
        localAddr.sin_addr.s_addr = htonl(INADDR_ANY);
        localAddr.sin_port = htons(protocol + port);

        // bind
        if( bind(serversocket, (struct sockaddr *)&localAddr, sizeof(struct sockaddr_in)) != 0) {
                qCritical("SageStreamWidget::%s() : bind error",  __FUNCTION__);
                return -1;
        }

        // put in listen mode
        listen(serversocket, 15);

        // accept
//	qDebug("SageStreamWidget::%s() : Blocking waiting for sender to connect to TCP port %d", __FUNCTION__,protocol+port);
        memset(&clientAddr, 0, sizeof(clientAddr));
        int addrLen = sizeof(struct sockaddr_in);
        if ((streamsocket = accept(serversocket, (struct sockaddr *)&clientAddr, (socklen_t*)&addrLen)) == -1) {
                qCritical("SageStreamWidget::%s() : accept error", __FUNCTION__);
                perror("accept");
                return -1;
        }



        QByteArray regMsg(OldSage::REG_MSG_SIZE, '\0');
        int read = recv(streamsocket, (void *)regMsg.data(), regMsg.size(), MSG_WAITALL);
        if ( read == -1 ) {
                qCritical("SageStreamWidget::%s() : error while reading regMsg. %s",__FUNCTION__, "");
                return -1;
        }
        else if ( read == 0 ) {
                qCritical("SageStreamWidget::%s() : sender disconnected, while reading 1KB regMsg",__FUNCTION__);
                return -1;
        }

        QString regMsgStr(regMsg);
        QStringList regMsgStrList = regMsgStr.split(" ", QString::SkipEmptyParts);
        qDebug("SageStreamWidget::%s() : recved regMsg from sageStreamer::connectToRcv() [%s]",  __FUNCTION__, regMsg.constData());
        int framerate = regMsgStrList.at(1).toInt();
        int groupsize = regMsgStrList.at(3).toInt(); // this is going to be the network user buffer size
        _appInfo->setNetworkUserBufferLength(groupsize);
        int pixfmt = regMsgStrList.at(6).toInt();
        int resX = regMsgStrList.at(9).toInt();
        int resY = regMsgStrList.at(10).toInt();
        Q_ASSERT(resX > 0 && resY > 0);

//	qDebug() << "sd;fkljasdf;lkasjdf;    " << framerate << "\n";
        _perfMon->setExpectedFps( (qreal)framerate );
        _perfMon->setAdjustedFps( (qreal)framerate );

        resize(resX, resY); // BaseWidget::ResizeEvent will call setTransforOriginPoint


        // create double buffer
        if ( createImageBuffer(resX, resY, (sagePixFmt)pixfmt) != 0 ) {
                qCritical("%s::%s() : imagedoublebuffer is not valid", metaObject()->className(), __FUNCTION__);
                ::shutdown(streamsocket, SHUT_RDWR);
                QMetaObject::invokeMethod(_fsmMsgThread, "sendSailShutdownMsg", Qt::QueuedConnection);
                deleteLater();
                return -1;
        }



        if(_affInfo)
                _affInfo->setWidgetID(_sageAppId);

        Q_ASSERT(_appInfo);
        _appInfo->setFrameSize(image->width(), image->height(), image->depth()); // == _image->byteCount()

//		qDebug("SageStreamWidget::%s() : sageappid %llu, groupsize %d, frameSize(SAIL) %d, frameSize(QImage) %d, expectedFps %.2f", __FUNCTION__, sageAppId, _appInfo->getNetworkUserBufferLength(), imageSize, _appInfo->getFrameBytecount(), _perfMon->getExpetctedFps());

        _appInfo->setExecutableName( appname );
        if ( appname == "imageviewer" ) {
                _appInfo->setMediaType(MEDIA_TYPE_IMAGE);
        }
        else {
			// this is done in the Launcher
//                _appInfo->setMediaType(MEDIA_TYPE_VIDEO);
        }



        // starting receiving thread

        // image->bits() will do deep copy (detach)
        receiverThread = new SagePixelReceiver(protocol, streamsocket, doubleBuffer, _appInfo, _perfMon, _affInfo,  settings);
//		qDebug("SageStreamWidget::%s() : SagePixelReceiver thread has begun",  __FUNCTION__);

        Q_ASSERT(receiverThread);

        connect(receiverThread, SIGNAL(finished()), this, SLOT(close())); // WA_Delete_on_close is defined

        // don't do below.
//		connect(receiverThread, SIGNAL(finished()), receiverThread, SLOT(deleteLater()));


//		if (!scheduler) {
                // This is queued connection because receiverThread reside outside of the main thread
                if ( ! connect(receiverThread, SIGNAL(frameReceived()), this, SLOT(scheduleUpdate())) ) {
                        qCritical("%s::%s() : Failed to connect frameReceived() signal and scheduleUpdate() slot", metaObject()->className(), __FUNCTION__);
                        return -1;
                }
                else {
//				qDebug("%s::%s() : frameReceived() -> scheduleUpdate() are connected", metaObject()->className(), __FUNCTION__);
                }
//		}
        receiverThread->start();




		//
		// I shouldnt do this here
		// because Launcher will want to set pos when opening a session
		//
//        setPos(initrect.x(), initrect.y());

        return 0;
}
*/


//void SageStreamWidget::pixelRecvThread() {

//	/*
//	 * Initially store current affinity settings of this thread using NUMA API
//	 */
//	if (_affInfo)
//		_affInfo->figureOutCurrentAffinity();


//	/*
//	QThread *thread = this->thread();
//	// The operating system will schedule the thread according to the priority parameter. The effect of the priority parameter is dependent on the operating system's scheduling policy. In particular, the priority will be ignored on systems that do not support thread priorities (such as on Linux, see http://linux.die.net/man/2/sched_setscheduler for more details).
//	qDebug("SagePixelReceiver::%s() : priority %d", __FUNCTION__, thread->priority());


//	pthread_setschedparam();
//	*/

//	struct rusage ru_start, ru_end;


//	int byteCount = _appInfo->getFrameSize();
////	int byteCount = pBuffer->width() * pBuffer->height() * 3;
////	unsigned char *buffer = (unsigned char *)malloc(sizeof(unsigned char) * byteCount);
////	memset(buffer, 127, byteCount);


//	Q_ASSERT(doubleBuffer);
//	unsigned char *bufptr = static_cast<QImage *>(doubleBuffer->getFrontBuffer())->bits();


////	QMutex mutex;

//	while( ! threadEnd() ) {
//		if ( _affInfo ) {
//			if ( _affInfo->isChanged() ) {
//				// apply new affinity;
//				//			qDebug("SagePixelReceiver::%s() : applying new affinity parameters", __FUNCTION__);
//				_affInfo->applyNewParameters(); // use NUMA lib

//				// update info in _affInfo
//				// this function must be called in this thread
//				_affInfo->figureOutCurrentAffinity(); // use NUMA lib
//			}
//			else {
//#if defined(Q_OS_LINUX)
//				/* this is called too many times */
//				// if cpu has changed, AffinityInfo::cpuOfMineChanged() will be emitted
//				// which is connected to ResourceMonitor::update_affInfo()
//				_affInfo->setCpuOfMine( sched_getcpu() );
//#endif
//			}
//		}


//		if(_perfMon) {
//			_perfMon->getRecvTimer().start(); //QTime::start()

//	#if defined(Q_OS_LINUX)
//			getrusage(RUSAGE_THREAD, &ru_start); // that of calling thread. Linux specific
//	#elif defined(Q_OS_MAC)
//			getrusage(RUSAGE_SELF, &ru_start);
//	#endif
//		}

//		/**
//		  this must happen after _affInfo->applyNewParameters()
//		  **/
//		if (scheduler) {
//			mutex.lock();
//			waitCond.wait(&mutex);
//		}


//		ssize_t totalread = 0;
//		ssize_t read = 0;

////		gettimeofday(&s, 0);
////		ToPixmap->acquire();
////		gettimeofday(&e, 0);
////		qreal el = ((double)e.tv_sec + (double)e.tv_usec * 0.000001) - ((double)s.tv_sec+(double)s.tv_usec*0.000001);
////		qDebug() << "acquire: " << el * 1000.0 << " msec";

////		mutex->lock();
////		unsigned char *bufptr = (imageArray[*arrayIndex])->bits();

////		if ( !image ||  !(image->bits()) ) {
////			qDebug() << "QImage is null";
////			break;
////		}
////		unsigned char *bufptr = image->bits(); // will detach().. : deep copy


////		int byteCount = pBuffer->width() * pBuffer->height() * 3;
////		pBuffer->detach();

////		qDebug() << (QTime::currentTime()).toString("mm:ss.zzz") << " recevier start receiving " << frameCounter + 1;

//		// PRODUCER
//		while (totalread < byteCount ) {
//			// If remaining byte is smaller than user buffer length (which is groupSize)
//			if ( byteCount-totalread < _appInfo->getNetworkUserBufferLength() ) {
//				read = recv(socket, bufptr, byteCount-totalread , MSG_WAITALL);
//			}
//			// otherwise, always read groupSize bytes
//			else {
//				read = recv(socket, bufptr, _appInfo->getNetworkUserBufferLength(), MSG_WAITALL);
//			}
//			if ( read == -1 ) {
//				qCritical("SagePixelReceiver::%s() : error while reading.", __FUNCTION__);
//				break;
//			}
//			else if ( read == 0 ) {
//				qDebug("SagePixelReceiver::%s() : sender disconnected", __FUNCTION__);
//				break;
//			}

//			// advance pointer
//			bufptr += read;
//			totalread += read;
//		}
//		if ( totalread < byteCount ) break;
//		read = totalread;

////		++frameCounter;
////		qDebug() << (QTime::currentTime()).toString("mm:ss.zzz") << " recevier : " << frameCounter << " received";

//		// _ts_nextframe, _deadline_missed are also updated in this function
////		qDebug() << _perfMon->set_ts_currframe() * 1000.0 << " ms";
//		_perfMon->set_ts_currframe();


//		if (_perfMon) {
//#if defined(Q_OS_LINUX)
//			getrusage(RUSAGE_THREAD, &ru_end);
//#elif defined(Q_OS_MAC)
//			getrusage(RUSAGE_SELF, &ru_end);
//#endif
//			// calculate
//			_perfMon->updateRecvLatency(read, ru_start, ru_end); // QTimer::restart()
////			ru_start = ru_end;

////			qDebug() << "SageStreamWidget" << _perfMon->getRecvFpsVariance();
//		}


//		if ( doubleBuffer ) {

//			// will wait until consumer (SageStreamWidget) consumes the data
//			doubleBuffer->swapBuffer();
////			qDebug("%s() : swapBuffer returned", __FUNCTION__);

////			emit this->frameReceived(); // Queued Connection. Will trigger SageStreamWidget::updateWidget()
////			qDebug("%s() : signal emitted", __FUNCTION__);

//			if ( ! QMetaObject::invokeMethod(this, "updateWidget", Qt::QueuedConnection) ) {
//				qCritical("%s::%s() : invoke updateWidget() failed", metaObject()->className(), __FUNCTION__);
//			}
////			static_cast<SageStreamWidget *>(this)->updateWidget();


//			// getFrontBuffer() will return immediately. There's no mutex waiting in this function
//			bufptr = static_cast<QImage *>(doubleBuffer->getFrontBuffer())->bits(); // bits() will detach
////			qDebug("%s() : grabbed front buffer", __FUNCTION__);
//		}
//		else {
//			break;
//		}


//		if(scheduler) {
//			mutex.unlock();
//		}
//	}


//	/* pixel receiving thread exit */
//	qDebug("SageStreamWidget::%s() : thread exit", __FUNCTION__);
//}





