#include "applications/base/sn_appinfo.h"

SN_AppInfo::SN_AppInfo(quint64 gaid)
    : _GID(gaid)
	, _fileinfo(QFileInfo())

	, _webUrl(QUrl())
	, _nativeSize(QSize())
	, _srcaddr(QString())
	, _bitPerPixel(0)
	, _frameSize(0)
	, _recentScale(1)
	, _drawingThreadCpu(-1)
	, _networkUserBufferLength(65535) /* 64 kB */
{
}

SN_AppInfo::SN_AppInfo(quint64 gaid, int width, int height, int bpp)
	: _GID(gaid)
	, _fileinfo(QFileInfo())
	, _webUrl(QUrl())
	, _nativeSize(QSize(width, height))
	, _srcaddr(QString())
	, _bitPerPixel(bpp)
	, _frameSize(width * height * bpp)
	, _recentScale(1)
	, _drawingThreadCpu(-1)
	, _networkUserBufferLength(65535) /* 64 kB */
{
}

SN_AppInfo::SN_AppInfo(quint64 gaid, int width, int height, int bpp, QString file, QString srcip)
	: _GID(gaid)
    , _fileinfo(QFileInfo(file))
	, _webUrl(QUrl()),
		_nativeSize(QSize(width, height)),
		_srcaddr(srcip),
		_bitPerPixel(bpp),
		_frameSize(width * height * bpp),
		_recentScale(1),
		_drawingThreadCpu(-1),
		_networkUserBufferLength(65535) /* 64 kB */
{
}


void SN_AppInfo::setFrameSize(int width, int height, int bpp) {
//	orgWidth = width;
//	orgHeight = height;
	_nativeSize.rwidth() = width;
	_nativeSize.rheight() = height;
//	_nativeSize = QSize(width, height);
	_bitPerPixel = bpp;

	_frameSize = width * height * bpp / 8; // Byte
}

