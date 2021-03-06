#ifndef SN_CHECKER_H
#define SN_CHECKER_H

#include "applications/base/sn_railawarewidget.h"
//#include "system/sn_resourcemonitor.h"

#include <QtCore>
#include <QtOpenGL>

#include <sys/resource.h>


class WorkerThread : public QThread {
	Q_OBJECT
public:
	WorkerThread(QObject *parent=0);
	WorkerThread(SN_AppInfo *ai, SN_PerfMonitor *pm, qreal frate, QObject *parent=0) : QThread(parent), _appInfo(ai), _perfMon(pm), _framerate(frate) {}

	inline void setBufPtr(unsigned char *bufptr) {_bufptr = bufptr;}
	inline void setAppInfo(SN_AppInfo *ai) {_appInfo = ai;}
	inline void setPerfMon(SN_PerfMonitor *pm) {_perfMon = pm;}
	inline void setFramerate(qreal f) {_framerate = f;}

	inline void setNumpixel(int i) {_numpixel = i;}
	inline void setByteperpixel(int b) {_byteperpixel = b;}

	inline void setThreadEnd() {_threadEnd = true;}

	inline void setPboMutex(pthread_mutex_t *m) {_pbomutex = m;}
	inline void setPboCondition(pthread_cond_t *c) {_pbocond = c;}

	inline void setBufferReadyForWriting(bool b) {_bufferReadyForWriting = b;}

protected:
	void run();

private:
	/*!
	  The address of the data buffer to which I'm going write
	  */
	unsigned char * _bufptr;

	SN_AppInfo *_appInfo;

	SN_PerfMonitor *_perfMon;

	struct timeval lats;
	struct timeval late;

	struct rusage ru_start;
	struct rusage ru_end;

	qreal _framerate;

	int _numpixel;

	int _byteperpixel;

	unsigned char _red;
	unsigned char _green;
	unsigned char _blue;

	bool _threadEnd;


	/*!
	  To be used with PBO widget
	  */
	pthread_mutex_t *_pbomutex;
	pthread_cond_t *_pbocond;
	bool _bufferReadyForWriting;

	/*!
	  changes pixel value
	  */
	void updateData();
signals:
	/*!
	  signal GUI thread after writing is done
	  */
	void dataReady();

public slots:
	/*!
	  worker function
	  */
	void writeData();
};



/*!
  SN_Checker abstract class
  */
class SN_Checker : public SN_RailawareWidget {
	Q_OBJECT
public:
	SN_Checker(const QSize &imagesize, qreal framerate, const quint64 appid, const QSettings *s, SN_ResourceMonitor *rm, QGraphicsItem *parent=0, Qt::WindowFlags wFlags = 0);
	virtual ~SN_Checker();

protected:
	QSize _imgsize;

	qreal _framerate;

	WorkerThread *_workerThread;

public slots:
	virtual void scheduleUpdate() = 0;

private slots:
	virtual void doInit() = 0;
};




/*!
  OpenGL
  */
class SN_CheckerGL : public SN_Checker {
	Q_OBJECT
public:
	SN_CheckerGL(GLenum pixfmt, const QSize &imagesize, qreal framerate, const quint64 appid, const QSettings *s, SN_ResourceMonitor *rm, QGraphicsItem *parent=0, Qt::WindowFlags wFlags=0);
	~SN_CheckerGL();

	void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget);

private:
	/*!
	  pixel container
	  */
	QImage *_image;

	GLuint _textureid;

	GLenum _pixelFormat;

protected:
	void doInit();

public slots:
	void scheduleUpdate();
};




/*!
  OpenGL PBO
  */
class SN_CheckerGLPBO : public SN_Checker {
	Q_OBJECT
public:
	SN_CheckerGLPBO(GLenum pixfmt, const QSize &imagesize, qreal framerate, const quint64 appid, const QSettings *s, SN_ResourceMonitor *rm, QGraphicsItem *parent=0, Qt::WindowFlags wFlags=0);
	~SN_CheckerGLPBO();

	void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget);

private:
	GLuint _textureid;

	GLenum _pixelFormat;

	/*!
	  PBO buffer id
	  */
	GLuint _pboIds[2];

    QGLBuffer *_pbobuf[2];

	/*!
	  The mapped double buffer for mapped pbo
	  */
	void * _pbobufarray[2];

	bool __firstFrame;

	/*!
	  which buffer index is writable ?
	  1 - _pboBufIdx is used to display on the screen.
	  */
	int _pboBufIdx;

	/*!
	  Is one of the buffer idx is ready to write?
	  */
	bool __bufferMapped;


	/*!
	  For this example, mutex isn't needed because pixel writing function (Worker::writeData()) is called explicitly after the correct pbo buffer is mapped in scheduleUpdate()

	  If Worker::writeData() has forever loop (while(1)) then mutex/condition variable is needed for the producer/consumer synchronization
	  */
	pthread_mutex_t *_pbomutex;
	pthread_cond_t *_pbobufferready;
	bool _initPboMutex();

protected:
	void doInit();

public slots:
	void scheduleUpdate();
};




#endif // SN_CHECKER_H
