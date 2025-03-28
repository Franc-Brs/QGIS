/***************************************************************************
     testqgsmaptoolellipse.cpp
     ------------------------
    Date                 : January 2018
    Copyright            : (C) 2018 by Paul Blottiere
    Email                : paul.blottiere@oslandia.com
 ***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "qgstest.h"

#include "qgisapp.h"
#include "qgsgeometry.h"
#include "qgsmapcanvas.h"
#include "qgssettingsregistrycore.h"
#include "qgsvectorlayer.h"
#include "qgsmaptooladdfeature.h"
#include "qgsgeometryutils.h"

#include "testqgsmaptoolutils.h"
#include "qgsmaptoolellipsecenterpoint.h"
#include "qgsmaptoolellipsecenter2points.h"
#include "qgsmaptoolellipseextent.h"
#include "qgsmaptoolellipsefoci.h"


class TestQgsMapToolEllipse : public QObject
{
    Q_OBJECT

  public:
    TestQgsMapToolEllipse();

  private slots:
    void initTestCase();
    void cleanupTestCase();

    void testEllipseFromCenterAndPoint();
    void testEllipseFromCenterAndPointWithDeletedVertex();
    void testEllipseFromCenterAnd2Points();
    void testEllipseFromCenterAnd2PointsWithDeletedVertex();
    void testEllipseFromExtent();
    void testEllipseFromExtentWithDeletedVertex();
    void testEllipseFromFoci();
    void testEllipseFromFociWithDeletedVertex();

  private:
    QgisApp *mQgisApp = nullptr;
    QgsMapToolCapture *mParentTool = nullptr;
    QgsMapCanvas *mCanvas = nullptr;
    QgsVectorLayer *mLayer = nullptr;
};

TestQgsMapToolEllipse::TestQgsMapToolEllipse() = default;


//runs before all tests
void TestQgsMapToolEllipse::initTestCase()
{
  QgsApplication::init();
  QgsApplication::initQgis();

  mQgisApp = new QgisApp();

  mCanvas = new QgsMapCanvas();
  mCanvas->setDestinationCrs( QgsCoordinateReferenceSystem( QStringLiteral( "EPSG:27700" ) ) );

  // make testing layers
  mLayer = new QgsVectorLayer( QStringLiteral( "LineStringZ?crs=EPSG:27700" ), QStringLiteral( "layer line Z" ), QStringLiteral( "memory" ) );
  QVERIFY( mLayer->isValid() );
  QgsProject::instance()->addMapLayers( QList<QgsMapLayer *>() << mLayer );

  // set layers in canvas
  mCanvas->setLayers( QList<QgsMapLayer *>() << mLayer );
  mCanvas->setCurrentLayer( mLayer );

  mParentTool = new QgsMapToolAddFeature( mCanvas, QgsMapToolCapture::CaptureLine );
}

void TestQgsMapToolEllipse::cleanupTestCase()
{
  QgsApplication::exitQgis();
}

void TestQgsMapToolEllipse::testEllipseFromCenterAndPoint()
{
  QgsSettingsRegistryCore::settingsDigitizingDefaultZValue.setValue( 333 );
  mLayer->startEditing();

  QgsMapToolEllipseCenterPoint mapTool( mParentTool, mCanvas );
  mCanvas->setMapTool( &mapTool );

  TestQgsMapToolAdvancedDigitizingUtils utils( &mapTool );
  utils.mouseClick( 0, 0, Qt::LeftButton );
  utils.mouseMove( 1, -1 );
  utils.mouseClick( 1, -1, Qt::RightButton );
  const QgsFeatureId newFid = utils.newFeatureId();

  QCOMPARE( mLayer->featureCount(), ( long )1 );
  const QgsFeature f = mLayer->getFeature( newFid );

  const QString wkt = f.geometry().asWkt().replace( "LineStringZ (", "" ).replace( ")", "" );
  const QgsPointSequence pts = QgsGeometryUtils::pointsFromWKT( wkt, true, false );

  for ( const QgsPoint &pt : pts )
  {
    QCOMPARE( pt.z(), ( double )333 );
  }

  mLayer->rollBack();
  QgsSettingsRegistryCore::settingsDigitizingDefaultZValue.setValue( 0 );
}
void TestQgsMapToolEllipse::testEllipseFromCenterAndPointWithDeletedVertex()
{
  QgsSettingsRegistryCore::settingsDigitizingDefaultZValue.setValue( 333 );
  mLayer->startEditing();

  QgsMapToolEllipseCenterPoint mapTool( mParentTool, mCanvas );
  mCanvas->setMapTool( &mapTool );

  TestQgsMapToolAdvancedDigitizingUtils utils( &mapTool );
  utils.mouseClick( 4, 1, Qt::LeftButton );
  utils.keyClick( Qt::Key_Backspace );
  utils.mouseClick( 0, 0, Qt::LeftButton );
  utils.mouseMove( 1, -1 );
  utils.mouseClick( 1, -1, Qt::RightButton );
  const QgsFeatureId newFid = utils.newFeatureId();

  QCOMPARE( mLayer->featureCount(), ( long )1 );
  const QgsFeature f = mLayer->getFeature( newFid );

  const QString wkt = f.geometry().asWkt().replace( "LineStringZ (", "" ).replace( ")", "" );
  const QgsPointSequence pts = QgsGeometryUtils::pointsFromWKT( wkt, true, false );

  for ( const QgsPoint &pt : pts )
  {
    QCOMPARE( pt.z(), ( double )333 );
  }

  mLayer->rollBack();
  QgsSettingsRegistryCore::settingsDigitizingDefaultZValue.setValue( 0 );
}


void TestQgsMapToolEllipse::testEllipseFromCenterAnd2Points()
{
  QgsSettingsRegistryCore::settingsDigitizingDefaultZValue.setValue( 111 );
  mLayer->startEditing();

  QgsMapToolEllipseCenter2Points mapTool( mParentTool, mCanvas );
  mCanvas->setMapTool( &mapTool );

  TestQgsMapToolAdvancedDigitizingUtils utils( &mapTool );
  utils.mouseClick( 0, 0, Qt::LeftButton );
  utils.mouseClick( 0, 1, Qt::LeftButton );
  utils.mouseMove( 0, -1 );
  utils.mouseClick( 0, -1, Qt::RightButton );
  const QgsFeatureId newFid = utils.newFeatureId();

  QCOMPARE( mLayer->featureCount(), ( long )1 );
  const QgsFeature f = mLayer->getFeature( newFid );

  const QString wkt = f.geometry().asWkt().replace( "LineStringZ (", "" ).replace( ")", "" );
  const QgsPointSequence pts = QgsGeometryUtils::pointsFromWKT( wkt, true, false );

  for ( const QgsPoint &pt : pts )
  {
    QCOMPARE( pt.z(), ( double )111 );
  }

  mLayer->rollBack();
  QgsSettingsRegistryCore::settingsDigitizingDefaultZValue.setValue( 0 );
}

void TestQgsMapToolEllipse::testEllipseFromCenterAnd2PointsWithDeletedVertex()
{
  QgsSettingsRegistryCore::settingsDigitizingDefaultZValue.setValue( 111 );
  mLayer->startEditing();

  QgsMapToolEllipseCenter2Points mapTool( mParentTool, mCanvas );
  mCanvas->setMapTool( &mapTool );

  TestQgsMapToolAdvancedDigitizingUtils utils( &mapTool );
  utils.mouseClick( 4, 1, Qt::LeftButton );
  utils.keyClick( Qt::Key_Backspace );
  utils.mouseClick( 0, 0, Qt::LeftButton );
  utils.mouseClick( 0, 1, Qt::LeftButton );
  utils.mouseMove( 0, -1 );
  utils.mouseClick( 0, -1, Qt::RightButton );
  const QgsFeatureId newFid = utils.newFeatureId();

  QCOMPARE( mLayer->featureCount(), ( long )1 );
  const QgsFeature f = mLayer->getFeature( newFid );

  const QString wkt = f.geometry().asWkt().replace( "LineStringZ (", "" ).replace( ")", "" );
  const QgsPointSequence pts = QgsGeometryUtils::pointsFromWKT( wkt, true, false );

  for ( const QgsPoint &pt : pts )
  {
    QCOMPARE( pt.z(), ( double )111 );
  }

  mLayer->rollBack();
  QgsSettingsRegistryCore::settingsDigitizingDefaultZValue.setValue( 0 );
}

void TestQgsMapToolEllipse::testEllipseFromExtent()
{
  QgsSettingsRegistryCore::settingsDigitizingDefaultZValue.setValue( 222 );
  mLayer->startEditing();

  QgsMapToolEllipseExtent mapTool( mParentTool, mCanvas );
  mCanvas->setMapTool( &mapTool );

  TestQgsMapToolAdvancedDigitizingUtils utils( &mapTool );
  utils.mouseClick( 0, 0, Qt::LeftButton );
  utils.mouseMove( 2, 2 );
  utils.mouseClick( 2, 2, Qt::RightButton );
  const QgsFeatureId newFid = utils.newFeatureId();

  QCOMPARE( mLayer->featureCount(), ( long )1 );
  const QgsFeature f = mLayer->getFeature( newFid );

  const QString wkt = f.geometry().asWkt().replace( "LineStringZ (", "" ).replace( ")", "" );
  const QgsPointSequence pts = QgsGeometryUtils::pointsFromWKT( wkt, true, false );

  for ( const QgsPoint &pt : pts )
  {
    QCOMPARE( pt.z(), ( double )222 );
  }

  mLayer->rollBack();
  QgsSettingsRegistryCore::settingsDigitizingDefaultZValue.setValue( 0 );
}

void TestQgsMapToolEllipse::testEllipseFromExtentWithDeletedVertex()
{
  QgsSettingsRegistryCore::settingsDigitizingDefaultZValue.setValue( 222 );
  mLayer->startEditing();

  QgsMapToolEllipseExtent mapTool( mParentTool, mCanvas );
  mCanvas->setMapTool( &mapTool );

  TestQgsMapToolAdvancedDigitizingUtils utils( &mapTool );
  utils.mouseClick( 4, 1, Qt::LeftButton );
  utils.keyClick( Qt::Key_Backspace );
  utils.mouseClick( 0, 0, Qt::LeftButton );
  utils.mouseMove( 2, 2 );
  utils.mouseClick( 2, 2, Qt::RightButton );
  const QgsFeatureId newFid = utils.newFeatureId();

  QCOMPARE( mLayer->featureCount(), ( long )1 );
  const QgsFeature f = mLayer->getFeature( newFid );

  const QString wkt = f.geometry().asWkt().replace( "LineStringZ (", "" ).replace( ")", "" );
  const QgsPointSequence pts = QgsGeometryUtils::pointsFromWKT( wkt, true, false );

  for ( const QgsPoint &pt : pts )
  {
    QCOMPARE( pt.z(), ( double )222 );
  }

  mLayer->rollBack();
  QgsSettingsRegistryCore::settingsDigitizingDefaultZValue.setValue( 0 );
}

void TestQgsMapToolEllipse::testEllipseFromFoci()
{
  QgsSettingsRegistryCore::settingsDigitizingDefaultZValue.setValue( 444 );
  mLayer->startEditing();

  QgsMapToolEllipseFoci mapTool( mParentTool, mCanvas );
  mCanvas->setMapTool( &mapTool );

  TestQgsMapToolAdvancedDigitizingUtils utils( &mapTool );
  utils.mouseClick( 0, 0, Qt::LeftButton );
  utils.mouseClick( 0, 2, Qt::LeftButton );
  utils.mouseMove( 0, -1 );
  utils.mouseClick( 0, -1, Qt::RightButton );
  const QgsFeatureId newFid = utils.newFeatureId();

  QCOMPARE( mLayer->featureCount(), ( long )1 );
  const QgsFeature f = mLayer->getFeature( newFid );

  const QString wkt = f.geometry().asWkt().replace( "LineStringZ (", "" ).replace( ")", "" );
  const QgsPointSequence pts = QgsGeometryUtils::pointsFromWKT( wkt, true, false );

  for ( const QgsPoint &pt : pts )
  {
    QCOMPARE( pt.z(), ( double )444 );
  }

  mLayer->rollBack();
  QgsSettingsRegistryCore::settingsDigitizingDefaultZValue.setValue( 0 );
}

void TestQgsMapToolEllipse::testEllipseFromFociWithDeletedVertex()
{
  QgsSettingsRegistryCore::settingsDigitizingDefaultZValue.setValue( 444 );
  mLayer->startEditing();

  QgsMapToolEllipseFoci mapTool( mParentTool, mCanvas );
  mCanvas->setMapTool( &mapTool );

  TestQgsMapToolAdvancedDigitizingUtils utils( &mapTool );
  utils.mouseClick( 4, 1, Qt::LeftButton );
  utils.keyClick( Qt::Key_Backspace );
  utils.mouseClick( 0, 0, Qt::LeftButton );
  utils.mouseClick( 0, 2, Qt::LeftButton );
  utils.mouseMove( 0, -1 );
  utils.mouseClick( 0, -1, Qt::RightButton );
  const QgsFeatureId newFid = utils.newFeatureId();

  QCOMPARE( mLayer->featureCount(), ( long )1 );
  const QgsFeature f = mLayer->getFeature( newFid );

  const QString wkt = f.geometry().asWkt().replace( "LineStringZ (", "" ).replace( ")", "" );
  const QgsPointSequence pts = QgsGeometryUtils::pointsFromWKT( wkt, true, false );

  for ( const QgsPoint &pt : pts )
  {
    QCOMPARE( pt.z(), ( double )444 );
  }

  mLayer->rollBack();
  QgsSettingsRegistryCore::settingsDigitizingDefaultZValue.setValue( 0 );
}

QGSTEST_MAIN( TestQgsMapToolEllipse )
#include "testqgsmaptoolellipse.moc"
