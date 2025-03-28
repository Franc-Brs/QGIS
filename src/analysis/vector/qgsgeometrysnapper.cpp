/***************************************************************************
 *  qgsgeometrysnapper.cpp                                                 *
 *  -------------------                                                    *
 *  copyright            : (C) 2014 by Sandro Mani / Sourcepole AG         *
 *  email                : smani@sourcepole.ch                             *
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "qgsfeatureiterator.h"
#include "qgsgeometry.h"
#include "qgsvectorlayer.h"
#include "qgsgeometrysnapper.h"
#include "qgsvectordataprovider.h"
#include "qgsgeometryutils.h"
#include "qgsmapsettings.h"
#include "qgssurface.h"
#include "qgsmultisurface.h"
#include "qgscurve.h"

#include <QtConcurrentMap>
#include <geos_c.h>

///@cond PRIVATE

QgsSnapIndex::PointSnapItem::PointSnapItem( const QgsSnapIndex::CoordIdx *_idx, bool isEndPoint )
  : SnapItem( isEndPoint ? QgsSnapIndex::SnapEndPoint : QgsSnapIndex::SnapPoint )
  , idx( _idx )
{}

QgsPoint QgsSnapIndex::PointSnapItem::getSnapPoint( const QgsPoint &/*p*/ ) const
{
  return idx->point();
}

QgsSnapIndex::SegmentSnapItem::SegmentSnapItem( const QgsSnapIndex::CoordIdx *_idxFrom, const QgsSnapIndex::CoordIdx *_idxTo )
  : SnapItem( QgsSnapIndex::SnapSegment )
  , idxFrom( _idxFrom )
  , idxTo( _idxTo )
{}

QgsPoint QgsSnapIndex::SegmentSnapItem::getSnapPoint( const QgsPoint &p ) const
{
  return QgsGeometryUtils::projectPointOnSegment( p, idxFrom->point(), idxTo->point() );
}

bool QgsSnapIndex::SegmentSnapItem::getIntersection( const QgsPoint &p1, const QgsPoint &p2, QgsPoint &inter ) const
{
  const QgsPoint &q1 = idxFrom->point(), & q2 = idxTo->point();
  QgsVector v( p2.x() - p1.x(), p2.y() - p1.y() );
  QgsVector w( q2.x() - q1.x(), q2.y() - q1.y() );
  const double vl = v.length();
  const double wl = w.length();

  if ( qgsDoubleNear( vl, 0, 0.000000000001 ) || qgsDoubleNear( wl, 0, 0.000000000001 ) )
  {
    return false;
  }
  v = v / vl;
  w = w / wl;

  const double d = v.y() * w.x() - v.x() * w.y();

  if ( d == 0 )
    return false;

  const double dx = q1.x() - p1.x();
  const double dy = q1.y() - p1.y();
  const double k = ( dy * w.x() - dx * w.y() ) / d;

  inter = QgsPoint( p1.x() + v.x() * k, p1.y() + v.y() * k );

  const double lambdav = QgsVector( inter.x() - p1.x(), inter.y() - p1.y() ) *  v;
  if ( lambdav < 0. + 1E-8 || lambdav > vl - 1E-8 )
    return false;

  const double lambdaw = QgsVector( inter.x() - q1.x(), inter.y() - q1.y() ) * w;
  return !( lambdaw < 0. + 1E-8 || lambdaw >= wl - 1E-8 );
}

bool QgsSnapIndex::SegmentSnapItem::getProjection( const QgsPoint &p, QgsPoint &pProj )
{
  const QgsPoint &s1 = idxFrom->point();
  const QgsPoint &s2 = idxTo->point();
  const double nx = s2.y() - s1.y();
  const double ny = -( s2.x() - s1.x() );
  const double t = ( p.x() * ny - p.y() * nx - s1.x() * ny + s1.y() * nx ) / ( ( s2.x() - s1.x() ) * ny - ( s2.y() - s1.y() ) * nx );
  if ( t < 0. || t > 1. )
  {
    return false;
  }
  pProj = QgsPoint( s1.x() + ( s2.x() - s1.x() ) * t, s1.y() + ( s2.y() - s1.y() ) * t );
  return true;
}

///////////////////////////////////////////////////////////////////////////////

class Raytracer
{
    // Raytrace on an integer, unit-width 2D grid with floating point coordinates
    // See http://playtechs.blogspot.ch/2007/03/raytracing-on-grid.html
  public:
    Raytracer( float x0, float y0, float x1, float y1 )
      : m_dx( std::fabs( x1 - x0 ) )
      , m_dy( std::fabs( y1 - y0 ) )
      , m_x( std::floor( x0 ) )
      , m_y( std::floor( y0 ) )
      , m_n( 1 )
    {
      if ( m_dx == 0. )
      {
        m_xInc = 0.;
        m_error = std::numeric_limits<float>::infinity();
      }
      else if ( x1 > x0 )
      {
        m_xInc = 1;
        m_n += int( std::floor( x1 ) ) - m_x;
        m_error = ( std::floor( x0 ) + 1 - x0 ) * m_dy;
      }
      else
      {
        m_xInc = -1;
        m_n += m_x - int( std::floor( x1 ) );
        m_error = ( x0 - std::floor( x0 ) ) * m_dy;
      }
      if ( m_dy == 0. )
      {
        m_yInc = 0.;
        m_error = -std::numeric_limits<float>::infinity();
      }
      else if ( y1 > y0 )
      {
        m_yInc = 1;
        m_n += int( std::floor( y1 ) ) - m_y;
        m_error -= ( std::floor( y0 ) + 1 - y0 ) * m_dx;
      }
      else
      {
        m_yInc = -1;
        m_n += m_y - int( std::floor( y1 ) );
        m_error -= ( y0 - std::floor( y0 ) ) * m_dx;
      }
    }
    int curCol() const { return m_x; }
    int curRow() const { return m_y; }
    void next()
    {
      if ( m_error > 0 )
      {
        m_y += m_yInc;
        m_error -= m_dx;
      }
      else if ( m_error < 0 )
      {
        m_x += m_xInc;
        m_error += m_dy;
      }
      else
      {
        m_x += m_xInc;
        m_y += m_yInc;
        m_error += m_dx;
        m_error -= m_dy;
        --m_n;
      }
      --m_n;
    }

    bool isValid() const { return m_n > 0; }

  private:
    float m_dx, m_dy;
    int m_x, m_y;
    int m_xInc, m_yInc;
    float m_error;
    int m_n;
};

///////////////////////////////////////////////////////////////////////////////

QgsSnapIndex::QgsSnapIndex( const QgsPoint &origin, double cellSize )
  : mOrigin( origin )
  , mCellSize( cellSize )
{
  mSTRTree = GEOSSTRtree_create_r( QgsGeos::getGEOSHandler(), ( size_t )10 );
}

QgsSnapIndex::~QgsSnapIndex()
{
  qDeleteAll( mCoordIdxs );
  qDeleteAll( mSnapItems );

  GEOSSTRtree_destroy_r( QgsGeos::getGEOSHandler(), mSTRTree );
}

void QgsSnapIndex::addPoint( const CoordIdx *idx, bool isEndPoint )
{
  const QgsPoint p = idx->point();
  const int col = std::floor( ( p.x() - mOrigin.x() ) / mCellSize );
  const int row = std::floor( ( p.y() - mOrigin.y() ) / mCellSize );

  GEOSContextHandle_t geosctxt = QgsGeos::getGEOSHandler();
#if GEOS_VERSION_MAJOR>3 || GEOS_VERSION_MINOR>=8
  geos::unique_ptr point( GEOSGeom_createPointFromXY_r( geosctxt, row, col ) );
#else
  GEOSCoordSequence *seq = GEOSCoordSeq_create_r( geosctxt, 1, 2 );
  GEOSCoordSeq_setX_r( geosctxt, seq, 0, row );
  GEOSCoordSeq_setY_r( geosctxt, seq, 0, col );
  geos::unique_ptr point( GEOSGeom_createPoint_r( geosctxt, seq ) );
#endif

  PointSnapItem *item = new PointSnapItem( idx, isEndPoint );
  GEOSSTRtree_insert_r( geosctxt, mSTRTree, point.get(), item );
#if GEOS_VERSION_MAJOR>3 || GEOS_VERSION_MINOR<9
  mSTRTreeItems.emplace_back( std::move( point ) );
#endif
  mSnapItems << item;
}

void QgsSnapIndex::addSegment( const CoordIdx *idxFrom, const CoordIdx *idxTo )
{
  const QgsPoint pFrom = idxFrom->point();
  const QgsPoint pTo = idxTo->point();
  // Raytrace along the grid, get touched cells
  const float x0 = ( pFrom.x() - mOrigin.x() ) / mCellSize;
  const float y0 = ( pFrom.y() - mOrigin.y() ) / mCellSize;
  const float x1 = ( pTo.x() - mOrigin.x() ) / mCellSize;
  const float y1 = ( pTo.y() - mOrigin.y() ) / mCellSize;

  Raytracer rt( x0, y0, x1, y1 );
  GEOSContextHandle_t geosctxt = QgsGeos::getGEOSHandler();
  for ( ; rt.isValid(); rt.next() )
  {
#if GEOS_VERSION_MAJOR>3 || GEOS_VERSION_MINOR>=8
    geos::unique_ptr point( GEOSGeom_createPointFromXY_r( geosctxt, rt.curRow(), rt.curCol() ) );
#else
    GEOSCoordSequence *seq = GEOSCoordSeq_create_r( geosctxt, 1, 2 );
    GEOSCoordSeq_setX_r( geosctxt, seq, 0, rt.curRow() );
    GEOSCoordSeq_setY_r( geosctxt, seq, 0, rt.curCol() );
    geos::unique_ptr point( GEOSGeom_createPoint_r( geosctxt, seq ) );
#endif

    SegmentSnapItem *item = new SegmentSnapItem( idxFrom, idxTo );
    GEOSSTRtree_insert_r( geosctxt, mSTRTree, point.get(), item );
#if GEOS_VERSION_MAJOR>3 || GEOS_VERSION_MINOR<9
    mSTRTreeItems.push_back( std::move( point ) );
#endif
    mSnapItems << item;
  }
}

void QgsSnapIndex::addGeometry( const QgsAbstractGeometry *geom )
{
  for ( int iPart = 0, nParts = geom->partCount(); iPart < nParts; ++iPart )
  {
    for ( int iRing = 0, nRings = geom->ringCount( iPart ); iRing < nRings; ++iRing )
    {
      int nVerts = geom->vertexCount( iPart, iRing );

      if ( qgsgeometry_cast< const QgsSurface * >( geom ) )
        nVerts--;
      else if ( const QgsCurve *curve = qgsgeometry_cast< const QgsCurve * >( geom ) )
      {
        if ( curve->isClosed() )
          nVerts--;
      }

      for ( int iVert = 0; iVert < nVerts; ++iVert )
      {
        CoordIdx *idx = new CoordIdx( geom, QgsVertexId( iPart, iRing, iVert ) );
        CoordIdx *idx1 = new CoordIdx( geom, QgsVertexId( iPart, iRing, iVert + 1 ) );
        mCoordIdxs.append( idx );
        mCoordIdxs.append( idx1 );
        addPoint( idx, iVert == 0 || iVert == nVerts - 1 );
        if ( iVert < nVerts - 1 )
          addSegment( idx, idx1 );
      }
    }
  }
}

struct _GEOSQueryCallbackData
{
  QList< QgsSnapIndex::SnapItem * > *list;
};

void _GEOSQueryCallback( void *item, void *userdata )
{
  reinterpret_cast<_GEOSQueryCallbackData *>( userdata )->list->append( static_cast<QgsSnapIndex::SnapItem *>( item ) );
}

QgsPoint QgsSnapIndex::getClosestSnapToPoint( const QgsPoint &p, const QgsPoint &q )
{
  GEOSContextHandle_t geosctxt = QgsGeos::getGEOSHandler();

  // Look for intersections on segment from the target point to the point opposite to the point reference point
  // p2 = p1 + 2 * (q - p1)
  const QgsPoint p2( 2 * q.x() - p.x(), 2 * q.y() - p.y() );

  // Raytrace along the grid, get touched cells
  const float x0 = ( p.x() - mOrigin.x() ) / mCellSize;
  const float y0 = ( p.y() - mOrigin.y() ) / mCellSize;
  const float x1 = ( p2.x() - mOrigin.x() ) / mCellSize;
  const float y1 = ( p2.y() - mOrigin.y() ) / mCellSize;

  Raytracer rt( x0, y0, x1, y1 );
  double dMin = std::numeric_limits<double>::max();
  QgsPoint pMin = p;
  for ( ; rt.isValid(); rt.next() )
  {
#if GEOS_VERSION_MAJOR>3 || GEOS_VERSION_MINOR>=8
    geos::unique_ptr searchPoint( GEOSGeom_createPointFromXY_r( geosctxt, rt.curRow(), rt.curCol() ) );
#else
    GEOSCoordSequence *seq = GEOSCoordSeq_create_r( geosctxt, 1, 2 );
    GEOSCoordSeq_setX_r( geosctxt, seq, 0, rt.curRow() );
    GEOSCoordSeq_setY_r( geosctxt, seq, 0, rt.curCol() );
    geos::unique_ptr searchPoint( GEOSGeom_createPoint_r( geosctxt, seq ) );
#endif
    QList<SnapItem *> items;
    struct _GEOSQueryCallbackData callbackData;
    callbackData.list = &items;
    GEOSSTRtree_query_r( geosctxt, mSTRTree, searchPoint.get(), _GEOSQueryCallback, &callbackData );
    for ( const SnapItem *item : items )
    {
      if ( item->type == SnapSegment )
      {
        QgsPoint inter;
        if ( static_cast<const SegmentSnapItem *>( item )->getIntersection( p, p2, inter ) )
        {
          const double dist = QgsGeometryUtils::sqrDistance2D( q, inter );
          if ( dist < dMin )
          {
            dMin = dist;
            pMin = inter;
          }
        }
      }
    }
  }

  return pMin;
}

QgsSnapIndex::SnapItem *QgsSnapIndex::getSnapItem( const QgsPoint &pos, double tol, QgsSnapIndex::PointSnapItem **pSnapPoint, QgsSnapIndex::SegmentSnapItem **pSnapSegment, bool endPointOnly ) const
{
  const int colStart = std::floor( ( pos.x() - tol - mOrigin.x() ) / mCellSize );
  const int rowStart = std::floor( ( pos.y() - tol - mOrigin.y() ) / mCellSize );
  const int colEnd = std::floor( ( pos.x() + tol - mOrigin.x() ) / mCellSize );
  const int rowEnd = std::floor( ( pos.y() + tol - mOrigin.y() ) / mCellSize );

  GEOSContextHandle_t geosctxt = QgsGeos::getGEOSHandler();

  GEOSCoordSequence *coord = GEOSCoordSeq_create_r( geosctxt, 2, 2 );
#if GEOS_VERSION_MAJOR>3 || GEOS_VERSION_MINOR>=8
  GEOSCoordSeq_setXY_r( geosctxt, coord, 0, rowStart, colStart );
  GEOSCoordSeq_setXY_r( geosctxt, coord, 1, rowEnd, colEnd );
#else
  GEOSCoordSeq_setX_r( geosctxt, coord, 0, rowStart );
  GEOSCoordSeq_setY_r( geosctxt, coord, 0, colStart );
  GEOSCoordSeq_setX_r( geosctxt, coord, 1, rowEnd );
  GEOSCoordSeq_setY_r( geosctxt, coord, 1, colEnd );
#endif

  geos::unique_ptr searchDiagonal( GEOSGeom_createLineString_r( geosctxt, coord ) );

  QList<SnapItem *> items;
  struct _GEOSQueryCallbackData callbackData;
  callbackData.list = &items;
  GEOSSTRtree_query_r( geosctxt, mSTRTree, searchDiagonal.get(), _GEOSQueryCallback, &callbackData );

  double minDistSegment = std::numeric_limits<double>::max();
  double minDistPoint = std::numeric_limits<double>::max();
  QgsSnapIndex::SegmentSnapItem *snapSegment = nullptr;
  QgsSnapIndex::PointSnapItem *snapPoint = nullptr;

  const auto constItems = items;
  for ( QgsSnapIndex::SnapItem *item : constItems )
  {
    if ( ( ! endPointOnly && item->type == SnapPoint ) || item->type == SnapEndPoint )
    {
      const double dist = QgsGeometryUtils::sqrDistance2D( item->getSnapPoint( pos ), pos );
      if ( dist < minDistPoint )
      {
        minDistPoint = dist;
        snapPoint = static_cast<PointSnapItem *>( item );
      }
    }
    else if ( item->type == SnapSegment && !endPointOnly )
    {
      QgsPoint pProj;
      if ( !static_cast<SegmentSnapItem *>( item )->getProjection( pos, pProj ) )
      {
        continue;
      }
      const double dist = QgsGeometryUtils::sqrDistance2D( pProj, pos );
      if ( dist < minDistSegment )
      {
        minDistSegment = dist;
        snapSegment = static_cast<SegmentSnapItem *>( item );
      }
    }
  }
  snapPoint = minDistPoint < tol * tol ? snapPoint : nullptr;
  snapSegment = minDistSegment < tol * tol ? snapSegment : nullptr;
  if ( pSnapPoint ) *pSnapPoint = snapPoint;
  if ( pSnapSegment ) *pSnapSegment = snapSegment;
  return minDistPoint < minDistSegment ? static_cast<QgsSnapIndex::SnapItem *>( snapPoint ) : static_cast<QgsSnapIndex::SnapItem *>( snapSegment );
}

/// @endcond


//
// QgsGeometrySnapper
//

QgsGeometrySnapper::QgsGeometrySnapper( QgsFeatureSource *referenceSource )
  : mReferenceSource( referenceSource )
{
  // Build spatial index
  mIndex = QgsSpatialIndex( *mReferenceSource );
}

QgsFeatureList QgsGeometrySnapper::snapFeatures( const QgsFeatureList &features, double snapTolerance, SnapMode mode )
{
  QgsFeatureList list = features;
  QtConcurrent::blockingMap( list, ProcessFeatureWrapper( this, snapTolerance, mode ) );
  return list;
}

void QgsGeometrySnapper::processFeature( QgsFeature &feature, double snapTolerance, SnapMode mode )
{
  if ( !feature.geometry().isNull() )
    feature.setGeometry( snapGeometry( feature.geometry(), snapTolerance, mode ) );
  emit featureSnapped();
}

QgsGeometry QgsGeometrySnapper::snapGeometry( const QgsGeometry &geometry, double snapTolerance, SnapMode mode ) const
{
  // Get potential reference features and construct snap index
  QList<QgsGeometry> refGeometries;
  mIndexMutex.lock();
  QgsRectangle searchBounds = geometry.boundingBox();
  searchBounds.grow( snapTolerance );
  const QgsFeatureIds refFeatureIds = qgis::listToSet( mIndex.intersects( searchBounds ) );
  mIndexMutex.unlock();

  if ( refFeatureIds.isEmpty() )
    return QgsGeometry( geometry );

  refGeometries.reserve( refFeatureIds.size() );
  QgsFeatureIds missingFeatureIds;
  const QgsFeatureIds cachedIds = qgis::listToSet( mCachedReferenceGeometries.keys() );
  for ( const QgsFeatureId id : refFeatureIds )
  {
    if ( cachedIds.contains( id ) )
    {
      refGeometries.append( mCachedReferenceGeometries[id] );
    }
    else
    {
      missingFeatureIds << id;
    }
  }

  if ( missingFeatureIds.size() > 0 )
  {

    mReferenceLayerMutex.lock();
    const QgsFeatureRequest refFeatureRequest = QgsFeatureRequest().setFilterFids( missingFeatureIds ).setNoAttributes();
    QgsFeatureIterator refFeatureIt = mReferenceSource->getFeatures( refFeatureRequest );
    QgsFeature refFeature;
    while ( refFeatureIt.nextFeature( refFeature ) )
    {
      refGeometries.append( refFeature.geometry() );
    }
    mReferenceLayerMutex.unlock();
  }

  return snapGeometry( geometry, snapTolerance, refGeometries, mode );
}

QgsGeometry QgsGeometrySnapper::snapGeometry( const QgsGeometry &geometry, double snapTolerance, const QList<QgsGeometry> &referenceGeometries, QgsGeometrySnapper::SnapMode mode )
{
  if ( QgsWkbTypes::geometryType( geometry.wkbType() ) == QgsWkbTypes::PolygonGeometry &&
       ( mode == EndPointPreferClosest || mode == EndPointPreferNodes || mode == EndPointToEndPoint ) )
    return geometry;

  const QgsPoint center = qgsgeometry_cast< const QgsPoint * >( geometry.constGet() ) ? *static_cast< const QgsPoint * >( geometry.constGet() ) :
                          QgsPoint( geometry.constGet()->boundingBox().center() );

  QgsSnapIndex refSnapIndex( center, 10 * snapTolerance );
  for ( const QgsGeometry &geom : referenceGeometries )
  {
    refSnapIndex.addGeometry( geom.constGet() );
  }

  // Snap geometries
  QgsAbstractGeometry *subjGeom = geometry.constGet()->clone();
  QList < QList< QList<PointFlag> > > subjPointFlags;

  // Pass 1: snap vertices of subject geometry to reference vertices
  for ( int iPart = 0, nParts = subjGeom->partCount(); iPart < nParts; ++iPart )
  {
    subjPointFlags.append( QList< QList<PointFlag> >() );

    for ( int iRing = 0, nRings = subjGeom->ringCount( iPart ); iRing < nRings; ++iRing )
    {
      subjPointFlags[iPart].append( QList<PointFlag>() );

      for ( int iVert = 0, nVerts = polyLineSize( subjGeom, iPart, iRing ); iVert < nVerts; ++iVert )
      {
        if ( ( mode == EndPointPreferClosest || mode == EndPointPreferNodes || mode == EndPointToEndPoint ) &&
             QgsWkbTypes::geometryType( subjGeom->wkbType() ) == QgsWkbTypes::LineGeometry && ( iVert > 0 && iVert < nVerts - 1 ) )
        {
          //endpoint mode and not at an endpoint, skip
          subjPointFlags[iPart][iRing].append( Unsnapped );
          continue;
        }

        QgsSnapIndex::PointSnapItem *snapPoint = nullptr;
        QgsSnapIndex::SegmentSnapItem *snapSegment = nullptr;
        const QgsVertexId vidx( iPart, iRing, iVert );
        const QgsPoint p = subjGeom->vertexAt( vidx );
        if ( !refSnapIndex.getSnapItem( p, snapTolerance, &snapPoint, &snapSegment, mode == EndPointToEndPoint ) )
        {
          subjPointFlags[iPart][iRing].append( Unsnapped );
        }
        else
        {
          switch ( mode )
          {
            case PreferNodes:
            case PreferNodesNoExtraVertices:
            case EndPointPreferNodes:
            case EndPointToEndPoint:
            {
              // Prefer snapping to point
              if ( snapPoint )
              {
                subjGeom->moveVertex( vidx, snapPoint->getSnapPoint( p ) );
                subjPointFlags[iPart][iRing].append( SnappedToRefNode );
              }
              else if ( snapSegment )
              {
                subjGeom->moveVertex( vidx, snapSegment->getSnapPoint( p ) );
                subjPointFlags[iPart][iRing].append( SnappedToRefSegment );
              }
              break;
            }

            case PreferClosest:
            case PreferClosestNoExtraVertices:
            case EndPointPreferClosest:
            {
              QgsPoint nodeSnap, segmentSnap;
              double distanceNode = std::numeric_limits<double>::max();
              double distanceSegment = std::numeric_limits<double>::max();
              if ( snapPoint )
              {
                nodeSnap = snapPoint->getSnapPoint( p );
                distanceNode = nodeSnap.distanceSquared( p );
              }
              if ( snapSegment )
              {
                segmentSnap = snapSegment->getSnapPoint( p );
                distanceSegment = segmentSnap.distanceSquared( p );
              }
              if ( snapPoint && distanceNode < distanceSegment )
              {
                subjGeom->moveVertex( vidx, nodeSnap );
                subjPointFlags[iPart][iRing].append( SnappedToRefNode );
              }
              else if ( snapSegment )
              {
                subjGeom->moveVertex( vidx, segmentSnap );
                subjPointFlags[iPart][iRing].append( SnappedToRefSegment );
              }
              break;
            }
          }
        }
      }
    }
  }

  //nothing more to do for points
  if ( qgsgeometry_cast< const QgsPoint * >( subjGeom ) )
    return QgsGeometry( subjGeom );
  //or for end point snapping
  if ( mode == PreferClosestNoExtraVertices || mode == PreferNodesNoExtraVertices || mode == EndPointPreferClosest || mode == EndPointPreferNodes || mode == EndPointToEndPoint )
  {
    QgsGeometry result( subjGeom );
    result.removeDuplicateNodes();
    return result;
  }

  // SnapIndex for subject feature
  std::unique_ptr< QgsSnapIndex > subjSnapIndex( new QgsSnapIndex( center, 10 * snapTolerance ) );
  subjSnapIndex->addGeometry( subjGeom );

  std::unique_ptr< QgsAbstractGeometry > origSubjGeom( subjGeom->clone() );
  std::unique_ptr< QgsSnapIndex > origSubjSnapIndex( new QgsSnapIndex( center, 10 * snapTolerance ) );
  origSubjSnapIndex->addGeometry( origSubjGeom.get() );

  // Pass 2: add missing vertices to subject geometry
  for ( const QgsGeometry &refGeom : referenceGeometries )
  {
    for ( int iPart = 0, nParts = refGeom.constGet()->partCount(); iPart < nParts; ++iPart )
    {
      for ( int iRing = 0, nRings = refGeom.constGet()->ringCount( iPart ); iRing < nRings; ++iRing )
      {
        for ( int iVert = 0, nVerts = polyLineSize( refGeom.constGet(), iPart, iRing ); iVert < nVerts; ++iVert )
        {

          QgsSnapIndex::PointSnapItem *snapPoint = nullptr;
          QgsSnapIndex::SegmentSnapItem *snapSegment = nullptr;
          const QgsPoint point = refGeom.constGet()->vertexAt( QgsVertexId( iPart, iRing, iVert ) );
          if ( subjSnapIndex->getSnapItem( point, snapTolerance, &snapPoint, &snapSegment ) )
          {
            // Snap to segment, unless a subject point was already snapped to the reference point
            if ( snapPoint && QgsGeometryUtils::sqrDistance2D( snapPoint->getSnapPoint( point ), point ) < 1E-16 )
            {
              continue;
            }
            else if ( snapSegment )
            {
              // Look if there is a closer reference segment, if so, ignore this point
              const QgsPoint pProj = snapSegment->getSnapPoint( point );
              const QgsPoint closest = refSnapIndex.getClosestSnapToPoint( point, pProj );
              if ( QgsGeometryUtils::sqrDistance2D( pProj, point ) > QgsGeometryUtils::sqrDistance2D( pProj, closest ) )
              {
                continue;
              }

              // If we are too far away from the original geometry, do nothing
              if ( !origSubjSnapIndex->getSnapItem( point, snapTolerance ) )
              {
                continue;
              }

              const QgsSnapIndex::CoordIdx *idx = snapSegment->idxFrom;
              subjGeom->insertVertex( QgsVertexId( idx->vidx.part, idx->vidx.ring, idx->vidx.vertex + 1 ), point );
              subjPointFlags[idx->vidx.part][idx->vidx.ring].insert( idx->vidx.vertex + 1, SnappedToRefNode );
              subjSnapIndex.reset( new QgsSnapIndex( center, 10 * snapTolerance ) );
              subjSnapIndex->addGeometry( subjGeom );
            }
          }
        }
      }
    }
  }
  subjSnapIndex.reset();
  origSubjSnapIndex.reset();
  origSubjGeom.reset();

  // Pass 3: remove superfluous vertices: all vertices which are snapped to a segment and not preceded or succeeded by an unsnapped vertex
  for ( int iPart = 0, nParts = subjGeom->partCount(); iPart < nParts; ++iPart )
  {
    for ( int iRing = 0, nRings = subjGeom->ringCount( iPart ); iRing < nRings; ++iRing )
    {
      const bool ringIsClosed = subjGeom->vertexAt( QgsVertexId( iPart, iRing, 0 ) ) == subjGeom->vertexAt( QgsVertexId( iPart, iRing, subjGeom->vertexCount( iPart, iRing ) - 1 ) );
      for ( int iVert = 0, nVerts = polyLineSize( subjGeom, iPart, iRing ); iVert < nVerts; ++iVert )
      {
        const int iPrev = ( iVert - 1 + nVerts ) % nVerts;
        const int iNext = ( iVert + 1 ) % nVerts;
        const QgsPoint pMid = subjGeom->vertexAt( QgsVertexId( iPart, iRing, iVert ) );
        const QgsPoint pPrev = subjGeom->vertexAt( QgsVertexId( iPart, iRing, iPrev ) );
        const QgsPoint pNext = subjGeom->vertexAt( QgsVertexId( iPart, iRing, iNext ) );

        if ( subjPointFlags[iPart][iRing][iVert] == SnappedToRefSegment &&
             subjPointFlags[iPart][iRing][iPrev] != Unsnapped &&
             subjPointFlags[iPart][iRing][iNext] != Unsnapped &&
             QgsGeometryUtils::sqrDistance2D( QgsGeometryUtils::projectPointOnSegment( pMid, pPrev, pNext ), pMid ) < 1E-12 )
        {
          if ( ( ringIsClosed && nVerts > 3 ) || ( !ringIsClosed && nVerts > 2 ) )
          {
            subjGeom->deleteVertex( QgsVertexId( iPart, iRing, iVert ) );
            subjPointFlags[iPart][iRing].removeAt( iVert );
            iVert -= 1;
            nVerts -= 1;
          }
          else
          {
            // Don't delete vertices if this would result in a degenerate geometry
            break;
          }
        }
      }
    }
  }

  QgsGeometry result( subjGeom );
  result.removeDuplicateNodes();
  return result;
}

int QgsGeometrySnapper::polyLineSize( const QgsAbstractGeometry *geom, int iPart, int iRing )
{
  const int nVerts = geom->vertexCount( iPart, iRing );

  if ( qgsgeometry_cast< const QgsSurface * >( geom ) || qgsgeometry_cast< const QgsMultiSurface * >( geom ) )
  {
    const QgsPoint front = geom->vertexAt( QgsVertexId( iPart, iRing, 0 ) );
    const QgsPoint back = geom->vertexAt( QgsVertexId( iPart, iRing, nVerts - 1 ) );
    if ( front == back )
      return nVerts - 1;
  }

  return nVerts;
}





//
// QgsInternalGeometrySnapper
//

QgsInternalGeometrySnapper::QgsInternalGeometrySnapper( double snapTolerance, QgsGeometrySnapper::SnapMode mode )
  : mSnapTolerance( snapTolerance )
  , mMode( mode )
{}

QgsGeometry QgsInternalGeometrySnapper::snapFeature( const QgsFeature &feature )
{
  if ( !feature.hasGeometry() )
    return QgsGeometry();

  QgsFeature feat = feature;
  QgsGeometry geometry = feat.geometry();
  if ( !mFirstFeature )
  {
    // snap against processed geometries
    // Get potential reference features and construct snap index
    QgsRectangle searchBounds = geometry.boundingBox();
    searchBounds.grow( mSnapTolerance );
    const QgsFeatureIds refFeatureIds = qgis::listToSet( mProcessedIndex.intersects( searchBounds ) );
    if ( !refFeatureIds.isEmpty() )
    {
      QList< QgsGeometry > refGeometries;
      const auto constRefFeatureIds = refFeatureIds;
      for ( const QgsFeatureId id : constRefFeatureIds )
      {
        refGeometries << mProcessedGeometries.value( id );
      }

      geometry = QgsGeometrySnapper::snapGeometry( geometry, mSnapTolerance, refGeometries, mMode );
    }
  }
  mProcessedGeometries.insert( feat.id(), geometry );
  mProcessedIndex.addFeature( feat );
  mFirstFeature = false;
  return geometry;
}
