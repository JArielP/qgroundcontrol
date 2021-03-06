/****************************************************************************
 *
 *   (c) 2017 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#pragma once

#include "QGCLoggingCategory.h"

#include <QObject>
#include <QGeoCoordinate>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTimer>

Q_DECLARE_LOGGING_CATEGORY(TerrainQueryLog)

class TerrainAtCoordinateQuery;

/// Base class for offline/online terrain queries
class TerrainQueryInterface : public QObject
{
    Q_OBJECT

public:
    TerrainQueryInterface(QObject* parent) : QObject(parent) { }

    /// Request terrain heights for specified coodinates.
    /// Signals: coordinateHeights when data is available
    virtual void requestCoordinateHeights(const QList<QGeoCoordinate>& coordinates) = 0;

    /// Requests terrain heights along the path specified by the two coordinates.
    /// Signals: pathHeights
    ///     @param coordinates to query
    virtual void requestPathHeights(const QGeoCoordinate& fromCoord, const QGeoCoordinate& toCoord) = 0;

    /// Request terrain heights for the rectangular area specified.
    /// Signals: carpetHeights when data is available
    ///     @param swCoord South-West bound of rectangular area to query
    ///     @param neCoord North-East bound of rectangular area to query
    ///     @param statsOnly true: Return only stats, no carpet data
    virtual void requestCarpetHeights(const QGeoCoordinate& swCoord, const QGeoCoordinate& neCoord, bool statsOnly) = 0;

signals:
    void coordinateHeights(bool success, QList<double> heights);
    void pathHeights(bool success, double latStep, double lonStep, const QList<double>& heights);
    void carpetHeights(bool success, double minHeight, double maxHeight, const QList<QList<double>>& carpet);
};

/// AirMap online implementation of terrain queries
class TerrainAirMapQuery : public TerrainQueryInterface {
    Q_OBJECT

public:
    TerrainAirMapQuery(QObject* parent = NULL);

    // Overrides from TerrainQueryInterface
    void requestCoordinateHeights(const QList<QGeoCoordinate>& coordinates) final;
    void requestPathHeights(const QGeoCoordinate& fromCoord, const QGeoCoordinate& toCoord) final;
    void requestCarpetHeights(const QGeoCoordinate& swCoord, const QGeoCoordinate& neCoord, bool statsOnly) final;

private slots:
    void _requestFinished(void);

private:
    void _sendQuery             (const QString& path, const QUrlQuery& urlQuery);
    void _requestFailed         (void);
    void _parseCoordinateData   (const QJsonValue& coordinateJson);
    void _parsePathData         (const QJsonValue& pathJson);
    void _parseCarpetData       (const QJsonValue& carpetJson);

    enum QueryMode {
        QueryModeCoordinates,
        QueryModePath,
        QueryModeCarpet
    };

    QNetworkAccessManager   _networkManager;
    QueryMode               _queryMode;
    bool                    _carpetStatsOnly;
};

/// Used internally by TerrainAtCoordinateQuery to batch coordinate requests together
class TerrainAtCoordinateBatchManager : public QObject {
    Q_OBJECT

public:
    TerrainAtCoordinateBatchManager(void);

    void addQuery(TerrainAtCoordinateQuery* terrainAtCoordinateQuery, const QList<QGeoCoordinate>& coordinates);

private slots:
    void _sendNextBatch         (void);
    void _queryObjectDestroyed  (QObject* elevationProvider);
    void _coordinateHeights     (bool success, QList<double> heights);

private:
    typedef struct {
        TerrainAtCoordinateQuery*   terrainAtCoordinateQuery;
        QList<QGeoCoordinate>       coordinates;
    } QueuedRequestInfo_t;

    typedef struct {
        TerrainAtCoordinateQuery*   terrainAtCoordinateQuery;
        bool                        queryObjectDestroyed;
        int                         cCoord;
    } SentRequestInfo_t;


    enum class State {
        Idle,
        Downloading,
    };

    void _batchFailed(void);
    QString _stateToString(State state);

    QList<QueuedRequestInfo_t>  _requestQueue;
    QList<SentRequestInfo_t>    _sentRequests;
    State                       _state = State::Idle;
    const int                   _batchTimeout = 500;
    QTimer                      _batchTimer;
    TerrainAirMapQuery          _terrainQuery;
};

/// NOTE: TerrainAtCoordinateQuery is not thread safe. All instances/calls to ElevationProvider must be on main thread.
class TerrainAtCoordinateQuery : public QObject
{
    Q_OBJECT
public:
    TerrainAtCoordinateQuery(QObject* parent = NULL);

    /// Async terrain query for a list of lon,lat coordinates. When the query is done, the terrainData() signal
    /// is emitted.
    ///     @param coordinates to query
    void requestData(const QList<QGeoCoordinate>& coordinates);

    // Internal method
    void _signalTerrainData(bool success, QList<double>& heights);

signals:
    void terrainData(bool success, QList<double> heights);
};

class TerrainPathQuery : public QObject
{
    Q_OBJECT

public:
    TerrainPathQuery(QObject* parent = NULL);

    /// Async terrain query for terrain heights between two lat/lon coordinates. When the query is done, the terrainData() signal
    /// is emitted.
    ///     @param coordinates to query
    void requestData(const QGeoCoordinate& fromCoord, const QGeoCoordinate& toCoord);

    typedef struct {
        double          latStep;    ///< Amount of latitudinal distance between each returned height
        double          lonStep;    ///< Amount of longitudinal distance between each returned height
        QList<double>   heights;    ///< Terrain heights along path
    } PathHeightInfo_t;

signals:
    /// Signalled when terrain data comes back from server
    void terrainData(bool success, const PathHeightInfo_t& pathHeightInfo);

private slots:
    void _pathHeights(bool success, double latStep, double lonStep, const QList<double>& heights);

private:
    TerrainAirMapQuery _terrainQuery;
};

Q_DECLARE_METATYPE(TerrainPathQuery::PathHeightInfo_t)

class TerrainPolyPathQuery : public QObject
{
    Q_OBJECT

public:
    TerrainPolyPathQuery(QObject* parent = NULL);

    /// Async terrain query for terrain heights for the paths between each specified QGeoCoordinate.
    /// When the query is done, the terrainData() signal is emitted.
    ///     @param polyPath List of QGeoCoordinate
    void requestData(const QVariantList& polyPath);
    void requestData(const QList<QGeoCoordinate>& polyPath);

signals:
    /// Signalled when terrain data comes back from server
    void terrainData(bool success, const QList<TerrainPathQuery::PathHeightInfo_t>& rgPathHeightInfo);

private slots:
    void _terrainDataReceived(bool success, const TerrainPathQuery::PathHeightInfo_t& pathHeightInfo);

private:
    int                                         _curIndex;
    QList<QGeoCoordinate>                       _rgCoords;
    QList<TerrainPathQuery::PathHeightInfo_t>   _rgPathHeightInfo;
    TerrainPathQuery                            _pathQuery;
};


class TerrainCarpetQuery : public QObject
{
    Q_OBJECT

public:
    TerrainCarpetQuery(QObject* parent = NULL);

    /// Async terrain query for terrain information bounded by the specifed corners.
    /// When the query is done, the terrainData() signal is emitted.
    ///     @param swCoord South-West bound of rectangular area to query
    ///     @param neCoord North-East bound of rectangular area to query
    ///     @param statsOnly true: Return only stats, no carpet data
    void requestData(const QGeoCoordinate& swCoord, const QGeoCoordinate& neCoord, bool statsOnly);

signals:
    /// Signalled when terrain data comes back from server
    void terrainData(bool success, double minHeight, double maxHeight, const QList<QList<double>>& carpet);

private:
    TerrainAirMapQuery _terrainQuery;
};

