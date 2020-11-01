#pragma once

#ifndef GRAPHWIDGET_H
#define GRAPHWIDGET_H

#include "toonz/tstageobjectspline.h"
#include "toonzqt/intfield.h"

#include <QObject>
#include <QWidget>
#include <QMouseEvent>
#include <QImage>

class QFrame;
class TThickPoint;

//=============================================================================
// GraphArea
//-----------------------------------------------------------------------------

class GraphWidget : public QWidget {
  Q_OBJECT

  QList<QPointF> m_points;
  int m_currentControlPointIndex;

  Qt::MouseButton m_mouseButton;

  int m_curveHeight;

  int m_LeftRightMargin;
  int m_TopMargin;
  int m_BottomMargin;

  bool m_isLinear;
  bool m_lockExtremePoints = true;
  bool m_constrainToBounds = true;

  QPointF m_preMousePos;

  bool m_isEnlarged;

  double m_maxXValue;
  double m_maxYValue;

public:
  explicit GraphWidget(QWidget* parent = nullptr);

  void setMaxXValue(int x) { m_maxXValue = x; }
  void setMaxYValue(int y) { m_maxYValue = y; }

  void setPoints(QList<TPointD> points);
  void clearPoints() { m_points.clear(); }
  QList<TPointD> getPoints();

  int getCurrentControlPointIndex() { return m_currentControlPointIndex; };
  void setCurrentControlPointIndex(int index) {
    m_currentControlPointIndex = index;
  };

  bool eventFilter(QObject* object, QEvent* event) override;

  void setFirstLastXPosition(std::pair<double, double> values, bool isDragging);

  void setLinear(bool isLinear);
  void moveCurrentControlPoint(QPointF delta);

  void setEnlarged(bool isEnlarged);

protected:
  QPointF viewToStrokePoint(const QPointF& p);
  int getClosestPointIndex(const QPointF& pos, double& minDistance2) const;

  bool isCentralControlPoint(const int index) const { return index % 3 == 0; }
  bool isLeftControlPoint(const int index) const { return index % 3 == 2; }
  bool isRightControlPoint(const int index) const { return index % 3 == 1; }

  void setPoint(int index, const QPointF point);
  void movePoint(int index, const QPointF delta);
  QPointF checkPoint(const QPointF p);

  QPointF getVisibleHandlePos(int index) const;

  void moveCentralControlPoint(int index, QPointF delta);

  void addControlPoint(double percent);

  void removeControlPoint(int index);
  void removeCurrentControlPoint();

  void selectNextControlPoint();
  void selectPreviousControlPoint();

  QPainterPath getPainterPath();

  void paintEvent(QPaintEvent*) override;
  void mouseMoveEvent(QMouseEvent*) override;
  void mousePressEvent(QMouseEvent*) override;
  void mouseReleaseEvent(QMouseEvent*) override;
  void keyPressEvent(QKeyEvent* e) override;

  void focusInEvent(QFocusEvent* fe) override;
  void focusOutEvent(QFocusEvent* fe) override;

signals:
  void focusOut();
  void controlPointChanged(bool isDragging);
  void controlPointAdded(int index);
  void controlPointRemoved(int index);

  void firstLastXPostionChanged(double, double);
  void updateCurrentPosition(int, QPointF);
};

#endif  // GRAPHWIDGET_H
