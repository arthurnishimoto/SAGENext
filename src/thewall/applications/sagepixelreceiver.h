#ifndef SAGEPIXELRECEIVER_H
#define SAGEPIXELRECEIVER_H

#include <QThread>
//#include "common/imagedoublebuffer.h"

class DoubleBuffer;
class SN_RailawareWidget;
class SN_SageStreamWidget;
class QImage;
//class QPixmap;
//class QSemaphore;
//class QMutex;
class QAbstractSocket;
class PerfMonitor;
class AppInfo;
class AffinityInfo;
class QSettings;


#include <QMutex>
#include <QWaitCondition>


/**
  * receives pixel from sail
  * its parent is SageWidget : QWidget
  */
class SN_SagePixelReceiver : public QThread {
	Q_OBJECT

public:
	SN_SagePixelReceiver(int protocol, int sockfd, /*GLuint tid, GLuint *pboids*/ /*QGLWidget *_sw*/ DoubleBuffer *idb, AppInfo *ap, PerfMonitor *pm, AffinityInfo *ai, /*RailawareWidget *rw, QMutex *m, QWaitCondition *wwcc,*/ const QSettings *s, QObject *parent = 0);
//	SagePixelReceiver(int protocol, int sockfd, QImage *img,  AppInfo *ap, PerfMonitor *pm, AffinityInfo *ai, /*RailawareWidget *rw,*/ QMutex *m, QWaitCondition *wwcc, const QSettings *s, QObject *parent = 0);
	~SN_SagePixelReceiver();

	void run();

private:
	const QSettings *s;

	/*!
	  * this breaks while(1) loop in run()
	  * and can be set by different thread (SageStreamWidget)
	  */
	volatile bool _end;

	/*!
	  * streaming channel b/w sender (SAIL) and me.
	  * this is created at the SageStreamWidget
	  */
	int _tcpsocket;

	/*!
	  UDP streaming is not implemented yet
	  */
	QAbstractSocket *_udpsocket;

	/*!
	  texture handle for the image frame
	  */
//	GLuint _textureid;

//	GLuint *_pboIds;

	/*!
	  For the OpenGL context to which my _glbuffers will bind
	  */
//	QGLWidget *_myGlWidget;

	/*!
	  SageStreamWidget has to pass the pointer to the viewport widget (which is QGLWidget for the OpenGL Viewport)
	  _myGLWidget is sharing with _shareWidget
	  to share texture objects
	  */
//	QGLWidget *_shareWidget;

	DoubleBuffer *doubleBuffer;

	enum sageNwProtocol {SAGE_TCP, SAGE_UDP};

	int receiveUdpPortNumber();

	AppInfo *appInfo;
	PerfMonitor *perf;
	AffinityInfo *affInfo;

	QMutex _mutex;
	QWaitCondition _waitCond;


	/*!
	  Double OpenGL buffers. This buffers are created in the server side.
	  So writing to these buffers is DMA to GPU memory
	  */
//	QGLBuffer **_glbuffers;
//	int initQGLBuffers(int bytecount);

	bool _useGLBuffer;

public:
	void receivePixel();
	void endReceiver();

signals:
	/*!
	  * This signal is emitted after a frame has received, and is connected to SageStreamWidget::schedule_update()
	  */
	void frameReceived();

};

#endif // SAGEPIXELRECEIVER_H
