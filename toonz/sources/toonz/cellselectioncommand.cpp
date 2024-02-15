#include <memory>

#include "cellselection.h"
#include "cellkeyframeselection.h"
#include "keyframeselection.h"
#include "keyframedata.h"

// Tnz6 includes
#include "tapp.h"
#include "duplicatepopup.h"
#include "overwritepopup.h"
#include "selectionutils.h"
#include "columnselection.h"
#include "reframepopup.h"

// TnzQt includes
#include "toonzqt/tselectionhandle.h"
#include "toonzqt/gutil.h"
#include "historytypes.h"

// TnzLib includes
#include "toonz/txshcell.h"
#include "toonz/txshsimplelevel.h"
#include "toonz/levelset.h"
#include "toonz/tstageobject.h"
#include "toonz/toonzscene.h"
#include "toonz/txsheethandle.h"
#include "toonz/tscenehandle.h"
#include "toonz/tobjecthandle.h"
#include "toonz/stageobjectutil.h"
#include "toonz/hook.h"
#include "toonz/levelproperties.h"
#include "toonz/childstack.h"
#include "toonz/tframehandle.h"
#include "toonz/tcolumnhandle.h"
#include "toonz/tstageobjectcmd.h"

// TnzCore includes
#include "tsystem.h"
#include "tundo.h"
#include "tmsgcore.h"
#include "trandom.h"
#include "tpalette.h"

// Qt includes
#include <QLabel>
#include <QPushButton>
#include <QMainWindow>

// tcg includes
#include "tcg/tcg_macros.h"

// STD includes
#include <ctime>

//*********************************************************************************
//    Reverse Cells  command
//*********************************************************************************

namespace {

class ReverseUndo final : public TUndo {
  int m_r0, m_c0, m_r1, m_c1;

public:
  ReverseUndo(int r0, int c0, int r1, int c1)
      : m_r0(r0), m_c0(c0), m_r1(r1), m_c1(c1) {}

  void redo() const override;
  void undo() const override { redo(); }  // Reverse is idempotent :)

  int getSize() const override { return sizeof(*this); }

  QString getHistoryString() override { return QObject::tr("Reverse"); }
  int getHistoryType() override { return HistoryType::Xsheet; }
};

//-----------------------------------------------------------------------------

void ReverseUndo::redo() const {
  TCG_ASSERT(m_r1 >= m_r0 && m_c1 >= m_c0, return);

  TApp::instance()->getCurrentXsheet()->getXsheet()->reverseCells(m_r0, m_c0,
                                                                  m_r1, m_c1);

  TApp::instance()->getCurrentXsheet()->notifyXsheetChanged();
  TApp::instance()->getCurrentScene()->setDirtyFlag(true);
}

}  // namespace

//=============================================================================

void TCellSelection::reverseCells() {
  if (isEmpty() || areAllColSelectedLocked()) return;

  TUndo *undo =
      new ReverseUndo(m_range.m_r0, m_range.m_c0, m_range.m_r1, m_range.m_c1);
  TUndoManager::manager()->add(undo);

  undo->redo();
}

//*********************************************************************************
//    Swing Cells  command
//*********************************************************************************

namespace {

class SwingUndo final : public TUndo {
  int m_r0, m_c0, m_r1, m_c1;

public:
  SwingUndo(int r0, int c0, int r1, int c1)
      : m_r0(r0), m_c0(c0), m_r1(r1), m_c1(c1) {}

  void redo() const override;
  void undo() const override;

  int getSize() const override { return sizeof(*this); }

  QString getHistoryString() override { return QObject::tr("Swing"); }
  int getHistoryType() override { return HistoryType::Xsheet; }
};

//-----------------------------------------------------------------------------

void SwingUndo::redo() const {
  TApp::instance()->getCurrentXsheet()->getXsheet()->swingCells(m_r0, m_c0,
                                                                m_r1, m_c1);

  TApp::instance()->getCurrentXsheet()->notifyXsheetChanged();
  TApp::instance()->getCurrentScene()->setDirtyFlag(true);

  TCellSelection *cellSelection = dynamic_cast<TCellSelection *>(
      TApp::instance()->getCurrentSelection()->getSelection());
  if (cellSelection)
    cellSelection->selectCells(m_r0, m_c0, (m_r0 + ((m_r1 - m_r0) * 2)), m_c1);
}

//-----------------------------------------------------------------------------

void SwingUndo::undo() const {
  TCG_ASSERT(m_r1 >= m_r0 && m_c1 >= m_c0, return);

  for (int c = m_c0; c <= m_c1; ++c)
    TApp::instance()->getCurrentXsheet()->getXsheet()->removeCells(m_r1 + 1, c,
                                                                   m_r1 - m_r0);

  TApp::instance()->getCurrentXsheet()->notifyXsheetChanged();
  TApp::instance()->getCurrentScene()->setDirtyFlag(true);

  TCellSelection *cellSelection = dynamic_cast<TCellSelection *>(
      TApp::instance()->getCurrentSelection()->getSelection());
  if (cellSelection) cellSelection->selectCells(m_r0, m_c0, m_r1, m_c1);
}

}  // namespace

//=============================================================================

void TCellSelection::swingCells() {
  if (isEmpty() || areAllColSelectedLocked()) return;

  TUndo *undo =
      new SwingUndo(m_range.m_r0, m_range.m_c0, m_range.m_r1, m_range.m_c1);
  TUndoManager::manager()->add(undo);

  undo->redo();
}

//*********************************************************************************
//    Increment Cells  command
//*********************************************************************************

namespace {

class IncrementUndo final : public TUndo {
  int m_r0, m_c0, m_r1, m_c1;
  mutable std::vector<std::pair<TRect, TXshCell>> m_undoCells;

public:
  mutable bool m_ok;

public:
  IncrementUndo(int r0, int c0, int r1, int c1)
      : m_r0(r0), m_c0(c0), m_r1(r1), m_c1(c1), m_ok(true) {}

  void redo() const override;
  void undo() const override;

  int getSize() const override { return sizeof(*this); }

  QString getHistoryString() override { return QObject::tr("Autoexpose"); }
  int getHistoryType() override { return HistoryType::Xsheet; }
};

//-----------------------------------------------------------------------------

void IncrementUndo::redo() const {
  TCG_ASSERT(m_r1 >= m_r0 && m_c1 >= m_c0, return);

  m_undoCells.clear();
  m_ok = TApp::instance()->getCurrentXsheet()->getXsheet()->incrementCells(
      m_r0, m_c0, m_r1, m_c1, m_undoCells);

  TApp::instance()->getCurrentXsheet()->notifyXsheetChanged();
  TApp::instance()->getCurrentScene()->setDirtyFlag(true);
}

//-----------------------------------------------------------------------------

void IncrementUndo::undo() const {
  TCG_ASSERT(m_r1 >= m_r0 && m_c1 >= m_c0 && m_ok, return);

  TXsheet *xsh = TApp::instance()->getCurrentXsheet()->getXsheet();

  for (int i = m_undoCells.size() - 1; i >= 0; --i) {
    const TRect &r = m_undoCells[i].first;
    int size       = r.x1 - r.x0 + 1;

    if (m_undoCells[i].second.getFrameId().isNoFrame())
      xsh->removeCells(r.x0, r.y0, size);
    else {
      xsh->insertCells(r.x0, r.y0, size);
      for (int j = 0; j < size; ++j) {
        if (j > 0 && Preferences::instance()->isImplicitHoldEnabled())
          xsh->setCell(r.x0 + j, r.y0, TXshCell(0, TFrameId::EMPTY_FRAME));
        else
          xsh->setCell(r.x0 + j, r.y0, m_undoCells[i].second);
      }
    }
  }

  TApp::instance()->getCurrentXsheet()->notifyXsheetChanged();
  TApp::instance()->getCurrentScene()->setDirtyFlag(true);
}

}  // namespace

//=============================================================================

void TCellSelection::incrementCells() {
  if (isEmpty() || areAllColSelectedLocked()) return;

  TXsheet *xsh = TApp::instance()->getCurrentXsheet()->getXsheet();

  std::unique_ptr<IncrementUndo> undo(new IncrementUndo(
      m_range.m_r0, m_range.m_c0, m_range.m_r1, m_range.m_c1));

  if (undo->redo(), !undo->m_ok) {
    DVGui::error(
        QObject::tr("Invalid selection: each selected column must contain one "
                    "single level with increasing frame numbering."));
    return;
  }

  TUndoManager::manager()->add(undo.release());
}

//*********************************************************************************
//    Random Cells  command
//*********************************************************************************

namespace {

class RandomUndo final : public TUndo {
  int m_r0, m_c0, m_r1, m_c1;

  std::vector<int> m_shuffle;  //!< Shuffled indices
  std::vector<int> m_elffuhs;  //!< Inverse shuffle indices

public:
  RandomUndo(int r0, int c0, int r1, int c1);

  void shuffleCells(int row, int col, const std::vector<int> &data) const;

  void redo() const override;
  void undo() const override;

  int getSize() const override {
    return sizeof(*this) + 2 * sizeof(int) * m_shuffle.size();
  }

  QString getHistoryString() override { return QObject::tr("Random"); }
  int getHistoryType() override { return HistoryType::Xsheet; }
};

//-----------------------------------------------------------------------------

RandomUndo::RandomUndo(int r0, int c0, int r1, int c1)
    : m_r0(r0), m_c0(c0), m_r1(r1), m_c1(c1) {
  TCG_ASSERT(m_r1 >= m_r0 && m_c1 >= m_c0, return);

  int r, rowCount = r1 - r0 + 1;
  std::vector<std::pair<unsigned int, int>> rndTable(rowCount);

  TRandom rnd(std::time(0));  // Standard seeding
  for (r = 0; r < rowCount; ++r) rndTable[r] = std::make_pair(rnd.getUInt(), r);

  std::sort(rndTable.begin(), rndTable.end());  // Random sort shuffle

  m_shuffle.resize(rowCount);
  m_elffuhs.resize(rowCount);
  for (r = 0; r < rowCount; ++r) {
    m_shuffle[r]                  = rndTable[r].second;
    m_elffuhs[rndTable[r].second] = r;
  }
}

//-----------------------------------------------------------------------------

void RandomUndo::shuffleCells(int row, int col,
                              const std::vector<int> &data) const {
  int rowCount = data.size();
  assert(rowCount > 0);

  TXsheet *xsh = TApp::instance()->getCurrentXsheet()->getXsheet();

  std::vector<TXshCell> bCells(rowCount), aCells(rowCount);
  xsh->getCells(row, col, rowCount, &bCells[0]);

  for (int i = 0; i < rowCount; ++i) aCells[data[i]] = bCells[i];

  xsh->setCells(row, col, rowCount, &aCells[0]);
}

//-----------------------------------------------------------------------------

void RandomUndo::undo() const {
  for (int c = m_c0; c <= m_c1; ++c) shuffleCells(m_r0, c, m_elffuhs);

  TApp::instance()->getCurrentXsheet()->notifyXsheetChanged();
  TApp::instance()->getCurrentScene()->setDirtyFlag(true);
}

//-----------------------------------------------------------------------------

void RandomUndo::redo() const {
  for (int c = m_c0; c <= m_c1; ++c) shuffleCells(m_r0, c, m_shuffle);

  TApp::instance()->getCurrentXsheet()->notifyXsheetChanged();
  TApp::instance()->getCurrentScene()->setDirtyFlag(true);
}

}  // namespace

//=============================================================================

void TCellSelection::randomCells() {
  if (isEmpty() || areAllColSelectedLocked()) return;

  TUndo *undo =
      new RandomUndo(m_range.m_r0, m_range.m_c0, m_range.m_r1, m_range.m_c1);
  TUndoManager::manager()->add(undo);

  undo->redo();
}

//*********************************************************************************
//    Step Cells  command
//*********************************************************************************

namespace {

class StepUndo final : public TUndo {
  int m_r0, m_c0, m_r1, m_c1;
  int m_rowsCount, m_colsCount;

  int m_step;
  int m_newRows;

  std::unique_ptr<TXshCell[]> m_cells;

public:
  StepUndo(int r0, int c0, int r1, int c1, int step);

  void redo() const override;
  void undo() const override;

  int getSize() const override { return sizeof(*this); }

  QString getHistoryString() override {
    return QObject::tr("Step %1").arg(QString::number(m_step));
  }
  int getHistoryType() override { return HistoryType::Xsheet; }
};

//-----------------------------------------------------------------------------

StepUndo::StepUndo(int r0, int c0, int r1, int c1, int step)
    : m_r0(r0)
    , m_c0(c0)
    , m_r1(r1)
    , m_c1(c1)
    , m_rowsCount(r1 - r0 + 1)
    , m_colsCount(c1 - c0 + 1)
    , m_step(step)
    , m_newRows(m_rowsCount * (step - 1))
    , m_cells(new TXshCell[m_rowsCount * m_colsCount]) {
  assert(m_rowsCount > 0 && m_colsCount > 0 && step > 0);
  assert(m_cells);

  TXsheetP xsh = TApp::instance()->getCurrentXsheet()->getXsheet();
  int k        = 0;
  for (int r = r0; r <= r1; ++r)
    for (int c = c0; c <= c1; ++c) {
      const TXshCell &cell = xsh->getCell(r, c, false);
      m_cells[k++] = cell;
    }
}

//-----------------------------------------------------------------------------

void StepUndo::redo() const {
  TCG_ASSERT(m_rowsCount > 0 && m_colsCount > 0, return);

  TApp::instance()->getCurrentXsheet()->getXsheet()->stepCells(m_r0, m_c0, m_r1,
                                                               m_c1, m_step);

  TApp::instance()->getCurrentXsheet()->notifyXsheetChanged();
  TApp::instance()->getCurrentScene()->setDirtyFlag(true);

  TCellSelection *cellSelection = dynamic_cast<TCellSelection *>(
      TApp::instance()->getCurrentSelection()->getSelection());
  if (cellSelection)
    cellSelection->selectCells(m_r0, m_c0, (m_r0 + (m_rowsCount * m_step) - 1),
                               m_c1);
}

//-----------------------------------------------------------------------------

void StepUndo::undo() const {
  TCG_ASSERT(m_rowsCount > 0 && m_colsCount > 0 && m_cells, return);

  TApp *app    = TApp::instance();
  TXsheet *xsh = app->getCurrentXsheet()->getXsheet();

  for (int c = m_c0; c <= m_c1; ++c) xsh->removeCells(m_r1 + 1, c, m_newRows);

  int k = 0;
  for (int r = m_r0; r <= m_r1; ++r)
    for (int c = m_c0; c <= m_c1; ++c) {
      if (m_cells[k].isEmpty())
        xsh->clearCells(r, c);
      else
        xsh->setCell(r, c, m_cells[k]);
      k++;
    }
  app->getCurrentXsheet()->notifyXsheetChanged();
  app->getCurrentScene()->setDirtyFlag(true);

  TCellSelection *cellSelection = dynamic_cast<TCellSelection *>(
      TApp::instance()->getCurrentSelection()->getSelection());
  if (cellSelection)
    cellSelection->selectCells(m_r0, m_c0, (m_r0 + (m_rowsCount - 1)), m_c1);
}

}  // namespace

//=============================================================================

void TCellSelection::stepCells(int step) {
  if (isEmpty() || areAllColSelectedLocked()) return;

  TUndo *undo = new StepUndo(m_range.m_r0, m_range.m_c0, m_range.m_r1,
                             m_range.m_c1, step);
  TUndoManager::manager()->add(undo);

  undo->redo();
}

//*********************************************************************************
//    Each Cells  command
//*********************************************************************************

namespace {

class EachUndo final : public TUndo {
  int m_r0, m_c0, m_r1, m_c1;
  int m_rowsCount, m_colsCount;

  int m_each;
  int m_newRows;

  std::unique_ptr<TXshCell[]> m_cells;

public:
  EachUndo(int r0, int c0, int r1, int c1, int each);

  void redo() const override;
  void undo() const override;

  int getSize() const override { return sizeof(*this); }

  QString getHistoryString() override {
    return QObject::tr("Each %1").arg(QString::number(m_each));
  }
  int getHistoryType() override { return HistoryType::Xsheet; }
};

//-----------------------------------------------------------------------------

EachUndo::EachUndo(int r0, int c0, int r1, int c1, int each)
    : m_r0(r0)
    , m_c0(c0)
    , m_r1(r1)
    , m_c1(c1)
    , m_rowsCount(r1 - r0 + 1)
    , m_colsCount(c1 - c0 + 1)
    , m_each(each)
    , m_newRows((m_rowsCount % each) ? m_rowsCount / each + 1
                                     : m_rowsCount / each)
    , m_cells(new TXshCell[m_rowsCount * m_colsCount]) {
  assert(m_rowsCount > 0 && m_colsCount > 0 && each > 0);
  assert(m_cells);

  int k        = 0;
  TXsheetP xsh = TApp::instance()->getCurrentXsheet()->getXsheet();
  for (int r = r0; r <= r1; ++r)
    for (int c = c0; c <= c1; ++c) {
      const TXshCell &cell = xsh->getCell(r, c, false);
      m_cells[k++] = cell;
    }
}

//-----------------------------------------------------------------------------

void EachUndo::redo() const {
  TCG_ASSERT(m_rowsCount > 0 && m_colsCount > 0, return);

  TApp::instance()->getCurrentXsheet()->getXsheet()->eachCells(m_r0, m_c0, m_r1,
                                                               m_c1, m_each);

  TApp::instance()->getCurrentXsheet()->notifyXsheetChanged();
  TApp::instance()->getCurrentScene()->setDirtyFlag(true);

  TCellSelection *cellSelection = dynamic_cast<TCellSelection *>(
      TApp::instance()->getCurrentSelection()->getSelection());
  if (cellSelection) {
    int newR = m_r0 + (m_r1 - m_r0 + m_each) / m_each - 1;
    cellSelection->selectCells(m_r0, m_c0, newR, m_c1);
  }
}

//-----------------------------------------------------------------------------

void EachUndo::undo() const {
  TCG_ASSERT(m_rowsCount > 0 && m_colsCount > 0 && m_cells, return);

  TApp *app    = TApp::instance();
  TXsheet *xsh = app->getCurrentXsheet()->getXsheet();

  for (int c = m_c0; c <= m_c1; ++c)
    xsh->insertCells(m_r0 + m_newRows, c, m_rowsCount - m_newRows);

  int k = 0;
  for (int r = m_r0; r <= m_r1; ++r)
    for (int c = m_c0; c <= m_c1; ++c) {
      if (m_cells[k].isEmpty())
        xsh->clearCells(r, c);
      else
        xsh->setCell(r, c, m_cells[k]);
      k++;
    }

  app->getCurrentXsheet()->notifyXsheetChanged();
  app->getCurrentScene()->setDirtyFlag(true);

  TCellSelection *cellSelection = dynamic_cast<TCellSelection *>(
      TApp::instance()->getCurrentSelection()->getSelection());
  if (cellSelection)
    cellSelection->selectCells(m_r0, m_c0, (m_r0 + (m_rowsCount - 1)), m_c1);
}

}  // namespace

//=============================================================================

void TCellSelection::eachCells(int each) {
  if (isEmpty() || areAllColSelectedLocked()) return;

  // Do nothing if they select less than Each #
  if ((m_range.m_r1 - m_range.m_r0 + 1) < each) return;

  TUndo *undo = new EachUndo(m_range.m_r0, m_range.m_c0, m_range.m_r1,
                             m_range.m_c1, each);
  TUndoManager::manager()->add(undo);

  undo->redo();
}

//*********************************************************************************
//    Reframe command : 強制的にNコマ打ちにする
//*********************************************************************************

namespace {

class ReframeUndo final : public TUndo {
  int m_r0, m_r1;
  int m_c0, m_c1;
  int m_step;

  std::vector<int> m_orgRows;
  std::vector<int> m_newRows;
  int m_maximumRows;

  int m_withBlank;
  std::unique_ptr<TXshCell[]> m_cells;
  std::vector<int> m_columnIndeces;

public:
  ReframeUndo(int r0, int r1, std::vector<int> columnIndeces, int step,
              int withBlank = -1);
  ~ReframeUndo();
  void undo() const override;
  void redo() const override;
  void onAdd() override;  // run initial reframing
  int maximumRows() { return m_maximumRows; }
  bool isValid() { return !m_columnIndeces.empty(); }
  void repeat() const;

  int getSize() const override { return sizeof(*this); }

  QString getHistoryString() override {
    if (m_withBlank == -1)
      return QObject::tr("Reframe to %1's").arg(QString::number(m_step));
    else
      return QObject::tr("Reframe to %1's with %2 blanks")
          .arg(QString::number(m_step))
          .arg(QString::number(m_withBlank));
  }
  int getHistoryType() override { return HistoryType::Xsheet; }
};

//-----------------------------------------------------------------------------

ReframeUndo::ReframeUndo(int r0, int r1, std::vector<int> columnIndeces,
                         int step, int withBlank)
    : m_r0(r0)
    , m_r1(r1)
    , m_step(step)
    , m_columnIndeces(columnIndeces)
    , m_withBlank(withBlank) {
  TXsheet *xsh = TApp::instance()->getCurrentXsheet()->getXsheet();

  // check if columns are cell column and not empty
  auto colItr = m_columnIndeces.begin();
  while (colItr != m_columnIndeces.end()) {
    if (xsh->isColumnEmpty(*colItr) ||
        !xsh->getColumn(*colItr)->getCellColumn())
      colItr = m_columnIndeces.erase(colItr);
    else
      colItr++;
  }
  if (m_columnIndeces.empty()) return;

  int orgCellAmount = 0;
  // if the bottom cells are followed by hold cells, include them as target rows
  // for each column
  for (auto colIndex : m_columnIndeces) {
    int rTo                = m_r1;
    TXshCellColumn *column = xsh->getColumn(colIndex)->getCellColumn();
    int colLen0, colLen1;
    column->getRange(colLen0, colLen1);
    TXshCell tmpCell = column->getCell(m_r1);
    // For the purposes counting cells at the end, treat stop frames as empty
    // cells
    if (tmpCell.getFrameId().isStopFrame())
      tmpCell = TXshCell(0, TFrameId::EMPTY_FRAME);
    while (rTo < colLen1) {
      TXshCell nextCell = column->getCell(rTo + 1);
      if (nextCell.getFrameId().isStopFrame())
        nextCell = TXshCell(0, TFrameId::EMPTY_FRAME);
      if (nextCell != tmpCell) break;
      rTo++;
    }
    int nr = rTo - m_r0 + 1;
    m_orgRows.push_back(nr);
    orgCellAmount += nr + 1;
  }
  m_cells.reset(new TXshCell[orgCellAmount]);
  assert(m_cells);
  int k        = 0;
  m_c0         = std::numeric_limits<int>::max();
  m_c1         = -1;
  for (int c = 0; c < (int)m_columnIndeces.size(); c++) {
    for (int r = r0; r < r0 + m_orgRows[c] + 1; r++) {
      const TXshCell &cell = xsh->getCell(r, m_columnIndeces[c], false);
      m_cells[k++]         = cell;
    }
    m_c0 = std::min(m_c0, m_columnIndeces[c]);
    m_c1 = std::max(m_c1, m_columnIndeces[c]);
  }

  m_newRows.clear();
}

//-----------------------------------------------------------------------------

ReframeUndo::~ReframeUndo() {}

//-----------------------------------------------------------------------------

void ReframeUndo::undo() const {
  TApp *app    = TApp::instance();
  TXsheet *xsh = app->getCurrentXsheet()->getXsheet();
  int rowCount = m_r1 - m_r0;
  if (rowCount < 0 || m_columnIndeces.empty()) return;

  for (int c = 0; c < m_columnIndeces.size(); c++) {
    /*-- コマンド後に縮んだカラムはその分引き伸ばす --*/
    if (m_newRows[c] < m_orgRows[c])
      xsh->insertCells(m_r0 + m_newRows[c], m_columnIndeces[c],
                       m_orgRows[c] - m_newRows[c]);
    /*-- コマンド後に延びたカラムはその分縮める --*/
    else if (m_newRows[c] > m_orgRows[c])
      xsh->removeCells(m_r0 + m_orgRows[c], m_columnIndeces[c],
                       m_newRows[c] - m_orgRows[c]);
  }

  if (m_cells) {
    int k = 0;
    for (int c = 0; c < m_columnIndeces.size(); c++)
      for (int r = m_r0; r < m_r0 + m_orgRows[c] + 1; r++) {
        if (m_cells[k].isEmpty())
          xsh->clearCells(r, m_columnIndeces[c]);
        else
          xsh->setCell(r, m_columnIndeces[c], m_cells[k]);
        k++;
      }
  }
  app->getCurrentXsheet()->notifyXsheetChanged();

  TCellSelection *cellSelection = dynamic_cast<TCellSelection *>(
      TApp::instance()->getCurrentSelection()->getSelection());
  if (cellSelection)
    cellSelection->selectCells(m_r0, m_c0, (m_r0 + rowCount), m_c1);
}

//-----------------------------------------------------------------------------

void ReframeUndo::redo() const {
  if (m_r1 - m_r0 < 0 || m_columnIndeces.empty()) return;

  TApp *app = TApp::instance();

  int rows = m_r1 - m_r0;
  for (int c = 0; c < m_columnIndeces.size(); c++) {
    rows = app->getCurrentXsheet()->getXsheet()->reframeCells(
        m_r0, m_r0 + m_orgRows[c] - 1, m_columnIndeces[c], m_step, m_withBlank);
  }

  app->getCurrentXsheet()->notifyXsheetChanged();

  TCellSelection *cellSelection = dynamic_cast<TCellSelection *>(
      TApp::instance()->getCurrentSelection()->getSelection());
  if (cellSelection)
    cellSelection->selectCells(m_r0, m_c0, (m_r0 + rows - 1), m_c1);
}
//-----------------------------------------------------------------------------

void ReframeUndo::onAdd() {
  if (m_r1 - m_r0 < 0 || m_columnIndeces.empty()) return;

  TApp *app = TApp::instance();

  m_maximumRows = 0;

  for (int c = 0; c < m_columnIndeces.size(); c++) {
    int nrows = app->getCurrentXsheet()->getXsheet()->reframeCells(
        m_r0, m_r0 + m_orgRows[c] - 1, m_columnIndeces[c], m_step, m_withBlank);
    m_newRows.push_back(nrows);
    m_maximumRows = std::max(m_maximumRows, nrows);
  }

  app->getCurrentScene()->setDirtyFlag(true);
  app->getCurrentXsheet()->notifyXsheetChanged();
}

//-----------------------------------------------------------------------------

void ReframeUndo::repeat() const {}

//-----------------------------------------------------------------------------
// return true if
// 1) cells in the range contain at least one blank cell, OR
// 2) for each column in the col-range, all cells in the row-range are identical
bool hasBlankOrAllIdentical(const TXsheet *xsh,
                            const TCellSelection::Range &range) {
  bool isIdentical = true;
  for (int c = range.m_c0; c <= range.m_c1; c++) {
    if (xsh->isColumnEmpty(c)) return true;
    int colRange0, colRange1;
    TXshCellColumn *column = xsh->getColumn(c)->getCellColumn();
    if (!column) return true;
    column->getRange(colRange0, colRange1);
    if (range.m_r0 > colRange1) return true;

    TXshCell cell = column->getCell(range.m_r0);
    int rTo       = std::min(range.m_r1, colRange1);
    for (int r = range.m_r0; r <= rTo; r++) {
      if (column->isCellEmpty(r)) return true;
      TXshCell tempCell = column->getCell(r);
      if (tempCell.getFrameId().isStopFrame()) return true;
      if (isIdentical && cell != tempCell) isIdentical = false;
    }
  }
  return isIdentical;
}

//-----------------------------------------------------------------------------
// return true if
// 1) columns in the vector contain at least one blank cell, OR
// 2) for each column, all cells are identical
bool hasBlankOrAllIdentical(const TXsheet *xsh,
                            const std::vector<int> &colIndices) {
  bool isIdentical = true;
  for (auto c : colIndices) {
    if (xsh->isColumnEmpty(c)) return true;
    int colRange0, colRange1;
    TXshCellColumn *column = xsh->getColumn(c)->getCellColumn();
    if (!column) return true;
    column->getRange(colRange0, colRange1);
    // if the columns starts with blank cell, then return true
    if (colRange0 != 0) return true;
    TXshCell cell = column->getCell(colRange0);
    for (int r = colRange0 + 1; r <= colRange1; r++) {
      if (column->isCellEmpty(r)) return true;
      TXshCell tempCell = column->getCell(r);
      if (tempCell.getFrameId().isStopFrame()) return true;
      if (isIdentical && cell != tempCell) isIdentical = false;
    }
  }
  return isIdentical;
}

}  // namespace

//=============================================================================

void TCellSelection::reframeCells(int count) {
  if (isEmpty() || areAllColSelectedLocked()) return;

  std::vector<int> colIndeces;
  for (int c = m_range.m_c0; c <= m_range.m_c1; c++) colIndeces.push_back(c);

  ReframeUndo *undo =
      new ReframeUndo(m_range.m_r0, m_range.m_r1, colIndeces, count);

  if (!undo->isValid()) {
    delete undo;
    return;
  }

  // reframing executed on adding undo
  TUndoManager::manager()->add(undo);

  m_range.m_r1 = m_range.m_r0 + undo->maximumRows() - 1;
}

void TColumnSelection::reframeCells(int count) {
  if (isEmpty()) return;

  int rowCount =
      TApp::instance()->getCurrentXsheet()->getXsheet()->getFrameCount();
  std::vector<int> colIndeces;
  std::set<int>::const_iterator it;
  for (it = m_indices.begin(); it != m_indices.end(); it++)
    colIndeces.push_back(*it);

  ReframeUndo *undo = new ReframeUndo(0, rowCount - 1, colIndeces, count);

  if (!undo->isValid()) {
    delete undo;
    return;
  }

  // reframing executed on adding undo
  TUndoManager::manager()->add(undo);
}

//=============================================================================

void TCellSelection::reframeWithEmptyInbetweens() {
  if (isEmpty() || areAllColSelectedLocked()) return;

  std::vector<int> colIndeces;
  for (int c = m_range.m_c0; c <= m_range.m_c1; c++) colIndeces.push_back(c);

  // destruction of m_reframePopup will be done along with the main window
  if (!m_reframePopup) m_reframePopup = new ReframePopup();

  // check if the reframe popup should show the "insert blank" field.
  // The field will be hidden when;
  // 1) selected cells contain blank cell, OR
  // 2) for each column, all selected cells are identical
  bool showInsertBlankField = !hasBlankOrAllIdentical(
      TApp::instance()->getCurrentXsheet()->getXsheet(), m_range);

  m_reframePopup->showInsertBlankField(showInsertBlankField);

  int ret = m_reframePopup->exec();
  if (ret == QDialog::Rejected) return;

  int step, blank;
  m_reframePopup->getValues(step, blank);

  ReframeUndo *undo =
      new ReframeUndo(m_range.m_r0, m_range.m_r1, colIndeces, step, blank);

  if (!undo->isValid()) {
    delete undo;
    return;
  }

  // reframing executed on adding undo
  TUndoManager::manager()->add(undo);

  // select reframed range
  selectCells(m_range.m_r0, m_range.m_c0,
              m_range.m_r0 + undo->maximumRows() - 1, m_range.m_c1);
}

void TColumnSelection::reframeWithEmptyInbetweens() {
  if (isEmpty()) return;

  int rowCount =
      TApp::instance()->getCurrentXsheet()->getXsheet()->getFrameCount();
  std::vector<int> colIndeces;
  std::set<int>::const_iterator it;
  for (it = m_indices.begin(); it != m_indices.end(); it++)
    colIndeces.push_back(*it);

  if (!m_reframePopup) m_reframePopup = new ReframePopup();

  // check if the reframe popup should show the "insert blank" field.
  // The field will be hidden when;
  // 1) selected columns contain blank cell, OR
  // 2) for each column, all contained cells are identical
  bool showInsertBlankField = !hasBlankOrAllIdentical(
      TApp::instance()->getCurrentXsheet()->getXsheet(), colIndeces);

  m_reframePopup->showInsertBlankField(showInsertBlankField);

  int ret = m_reframePopup->exec();
  if (ret == QDialog::Rejected) return;

  int step, blank;
  m_reframePopup->getValues(step, blank);

  ReframeUndo *undo = new ReframeUndo(0, rowCount - 1, colIndeces, step, blank);

  if (!undo->isValid()) {
    delete undo;
    return;
  }

  // reframing executed on adding undo
  TUndoManager::manager()->add(undo);
}

//=============================================================================

void TColumnSelection::renumberColumns() {
  if (isEmpty()) return;

  int rowCount =
    TApp::instance()->getCurrentXsheet()->getXsheet()->getFrameCount();
  std::vector<int> colIndeces;
  std::set<int>::const_iterator it;

  TUndoManager *undoManager = TUndoManager::manager();
  undoManager->beginBlock();

  for (it = m_indices.begin(); it != m_indices.end(); it++) {
    TCellSelection selection;
    selection.selectCells(0, *it, rowCount, *it);
    selection.dRenumberCells();
  }

  undoManager->endBlock();

  TApp::instance()->getCurrentScene()->setDirtyFlag(true);
  TApp::instance()->getCurrentXsheet()->notifyXsheetChanged();
}

//*********************************************************************************
//    Reset Step Cells  command
//*********************************************************************************

namespace {

class ResetStepUndo final : public TUndo {
  int m_r0, m_c0, m_r1, m_c1;
  int m_rowsCount, m_colsCount;

  std::unique_ptr<TXshCell[]> m_cells;
  QMap<int, int> m_insertedCells;  //!< Count of inserted cells, by column

public:
  ResetStepUndo(int r0, int c0, int r1, int c1);

  void redo() const override;
  void undo() const override;

  int getSize() const override { return sizeof(*this); }
};

//-----------------------------------------------------------------------------

ResetStepUndo::ResetStepUndo(int r0, int c0, int r1, int c1)
    : m_r0(r0)
    , m_c0(c0)
    , m_r1(r1)
    , m_c1(c1)
    , m_rowsCount(m_r1 - m_r0 + 1)
    , m_colsCount(m_c1 - m_c0 + 1)
    , m_cells(new TXshCell[m_rowsCount * m_colsCount]) {
  assert(m_rowsCount > 0 && m_colsCount > 0);
  assert(m_cells);

  TApp *app = TApp::instance();

  int k = 0;
  for (int c = c0; c <= c1; ++c) {
    TXshCell prevCell;
    m_insertedCells[c] = 0;

    TXsheetP xsh = app->getCurrentXsheet()->getXsheet();
    for (int r = r0; r <= r1; ++r) {
      const TXshCell &cell = xsh->getCell(r, c);
      m_cells[k++] = xsh->getCell(r, c, false);

      if (prevCell != cell) {
        prevCell = cell;
        m_insertedCells[c]++;
      }
    }
  }
}

//-----------------------------------------------------------------------------

void ResetStepUndo::redo() const {
  TCG_ASSERT(m_rowsCount > 0 && m_colsCount > 0, return);

  TApp::instance()->getCurrentXsheet()->getXsheet()->resetStepCells(m_r0, m_c0,
                                                                    m_r1, m_c1);

  TApp::instance()->getCurrentXsheet()->notifyXsheetChanged();
  TApp::instance()->getCurrentScene()->setDirtyFlag(true);

  TCellSelection *cellSelection = dynamic_cast<TCellSelection *>(
      TApp::instance()->getCurrentSelection()->getSelection());
  if (cellSelection) {
    int newR = 1;
    for (int c = m_c0; c <= m_c1; ++c)
      newR = std::max(newR, m_insertedCells[c]);
    cellSelection->selectCells(m_r0, m_c0, (m_r0 + newR - 1), m_c1);
  }
}

//-----------------------------------------------------------------------------

void ResetStepUndo::undo() const {
  TApp *app    = TApp::instance();
  TXsheet *xsh = app->getCurrentXsheet()->getXsheet();

  int k = 0;
  for (int c = m_c0; c <= m_c1; ++c) {
    xsh->removeCells(m_r0, c, m_insertedCells[c]);

    xsh->insertCells(m_r0, c, m_rowsCount);
    for (int r = m_r0; r <= m_r1; ++r) xsh->setCell(r, c, m_cells[k++]);
  }

  app->getCurrentXsheet()->notifyXsheetChanged();
  TApp::instance()->getCurrentScene()->setDirtyFlag(true);

  TCellSelection *cellSelection = dynamic_cast<TCellSelection *>(
      TApp::instance()->getCurrentSelection()->getSelection());
  if (cellSelection)
    cellSelection->selectCells(m_r0, m_c0, (m_r0 + (m_rowsCount - 1)), m_c1);
}

}  // namespace

//=============================================================================

void TCellSelection::resetStepCells() {
  if (isEmpty() || areAllColSelectedLocked()) return;

  TUndo *undo =
      new ResetStepUndo(m_range.m_r0, m_range.m_c0, m_range.m_r1, m_range.m_c1);
  TUndoManager::manager()->add(undo);

  undo->redo();
}

//*********************************************************************************
//    Increase Step Cells  command
//*********************************************************************************

namespace {

class IncreaseStepUndo final : public TUndo {
  int m_r0, m_c0, m_r1, m_c1;
  int m_rowsCount, m_colsCount;

  std::unique_ptr<TXshCell[]> m_cells;
  QMap<int, int> m_insertedCells;

public:
  mutable int m_newR1;  //!< r1 updated by TXsheet::increaseStepCells()

public:
  IncreaseStepUndo(int r0, int c0, int r1, int c1);

  void redo() const override;
  void undo() const override;

  int getSize() const override { return sizeof(*this); }
};

//-----------------------------------------------------------------------------

IncreaseStepUndo::IncreaseStepUndo(int r0, int c0, int r1, int c1)
    : m_r0(r0)
    , m_c0(c0)
    , m_r1(r1)
    , m_c1(c1)
    , m_rowsCount(m_r1 - m_r0 + 1)
    , m_colsCount(m_c1 - m_c0 + 1)
    , m_cells(new TXshCell[m_rowsCount * m_colsCount])
    , m_newR1(m_r1) {
  assert(m_cells);

  int k = 0;
  for (int c = c0; c <= c1; ++c) {
    TXshCell prevCell;
    m_insertedCells[c] = 0;

    TXsheetP xsh = TApp::instance()->getCurrentXsheet()->getXsheet();
    for (int r = r0; r <= r1; ++r) {
      const TXshCell &cell = xsh->getCell(r, c);
      m_cells[k++] = xsh->getCell(r, c, false);

      if (prevCell != cell) {
        prevCell = cell;
        m_insertedCells[c]++;
      }
    }
  }
}

//-----------------------------------------------------------------------------

void IncreaseStepUndo::redo() const {
  m_newR1 = m_r1;
  TApp::instance()->getCurrentXsheet()->getXsheet()->increaseStepCells(
      m_r0, m_c0, m_newR1, m_c1);

  TApp::instance()->getCurrentXsheet()->notifyXsheetChanged();
  TApp::instance()->getCurrentScene()->setDirtyFlag(true);

  TCellSelection *cellSelection = dynamic_cast<TCellSelection *>(
      TApp::instance()->getCurrentSelection()->getSelection());
  if (cellSelection) cellSelection->selectCells(m_r0, m_c0, m_newR1, m_c1);
}

//-----------------------------------------------------------------------------

void IncreaseStepUndo::undo() const {
  TApp *app    = TApp::instance();
  TXsheet *xsh = app->getCurrentXsheet()->getXsheet();

  int k = 0;
  for (int c = m_c0; c <= m_c1; ++c) {
    xsh->removeCells(m_r0, c, m_rowsCount + m_insertedCells[c]);

    xsh->insertCells(m_r0, c, m_rowsCount);
    for (int r = m_r0; r <= m_r1; ++r) xsh->setCell(r, c, m_cells[k++]);
  }

  app->getCurrentXsheet()->notifyXsheetChanged();
  TApp::instance()->getCurrentScene()->setDirtyFlag(true);

  TCellSelection *cellSelection = dynamic_cast<TCellSelection *>(
      TApp::instance()->getCurrentSelection()->getSelection());
  if (cellSelection)
    cellSelection->selectCells(m_r0, m_c0, (m_r0 + (m_rowsCount - 1)), m_c1);
}

}  // namespace

//=============================================================================

void TCellSelection::increaseStepCells() {
  if (isEmpty()) {
    int row = TTool::getApplication()->getCurrentFrame()->getFrame();
    int col = TTool::getApplication()->getCurrentColumn()->getColumnIndex();
    TXsheet *xsh = TApp::instance()->getCurrentXsheet()->getXsheet();
    m_range.m_r0 = row;
    m_range.m_r1 = row;
    m_range.m_c0 = col;
    m_range.m_c1 = col;
    TXshCell cell;
    cell = xsh->getCell(row, col);
    if (cell.isEmpty()) return;
  }
  if (areAllColSelectedLocked()) return;

  IncreaseStepUndo *undo = new IncreaseStepUndo(m_range.m_r0, m_range.m_c0,
                                                m_range.m_r1, m_range.m_c1);
  TUndoManager::manager()->add(undo);

  undo->redo();
}

//*********************************************************************************
//    Decrease Step Cells  command
//*********************************************************************************

namespace {

class DecreaseStepUndo final : public TUndo {
  int m_r0, m_c0, m_r1, m_c1;
  int m_rowsCount, m_colsCount;
  bool m_singleRow = false;

  std::unique_ptr<TXshCell[]> m_cells;
  QMap<int, int> m_removedCells;

public:
  mutable int m_newR1;  //!< r1 updated by TXsheet::decreaseStepCells()

public:
  DecreaseStepUndo(int r0, int c0, int r1, int c1);

  void redo() const override;
  void undo() const override;

  int getSize() const override { return sizeof(*this); }
};

//-----------------------------------------------------------------------------

DecreaseStepUndo::DecreaseStepUndo(int r0, int c0, int r1, int c1)
    : m_r0(r0)
    , m_c0(c0)
    , m_r1(r1)
    , m_c1(c1)
    , m_colsCount(m_c1 - m_c0 + 1) {
  assert(m_cells);

  if (r0 == r1) {
    m_singleRow = true;
    m_r1++;
  }

  m_newR1     = m_r1;
  m_rowsCount = (m_r1 - m_r0 + 1);
  m_cells =
      std::unique_ptr<TXshCell[]>(new TXshCell[m_rowsCount * m_colsCount]);

  int k        = 0;
  TXsheetP xsh = TApp::instance()->getCurrentXsheet()->getXsheet();
  for (int c = c0; c <= c1; ++c) {
    TXshCell prevCell = xsh->getCell(r0, c);
    m_removedCells[c] = 0;

    bool removed = false;
    m_cells[k++] = xsh->getCell(r0, c, false);

    for (int r = m_r0 + 1; r <= m_r1; ++r) {
      const TXshCell &cell = xsh->getCell(r, c);
      m_cells[k++] = xsh->getCell(r, c, false);

      if (prevCell == cell && !cell.isEmpty()) {
        if (!removed) {
          removed = true;
          m_removedCells[c]++;
        }
      } else {
        removed  = false;
        prevCell = cell;
      }
    }
  }
}

//-----------------------------------------------------------------------------

void DecreaseStepUndo::redo() const {
  m_newR1 = m_r1;
  TApp::instance()->getCurrentXsheet()->getXsheet()->decreaseStepCells(
      m_r0, m_c0, m_newR1, m_c1);

  TApp::instance()->getCurrentXsheet()->notifyXsheetChanged();
  TApp::instance()->getCurrentScene()->setDirtyFlag(true);

  TCellSelection *cellSelection = dynamic_cast<TCellSelection *>(
      TApp::instance()->getCurrentSelection()->getSelection());
  if (m_singleRow) m_newR1 = m_r0;
  if (cellSelection) cellSelection->selectCells(m_r0, m_c0, m_newR1, m_c1);
}

//-----------------------------------------------------------------------------

void DecreaseStepUndo::undo() const {
  TApp *app    = TApp::instance();
  TXsheet *xsh = app->getCurrentXsheet()->getXsheet();

  int k = 0;
  for (int c = m_c0; c <= m_c1; ++c) {
    xsh->removeCells(m_r0, c, m_rowsCount - m_removedCells[c]);

    xsh->insertCells(m_r0, c, m_rowsCount);
    for (int r = m_r0; r <= m_r1; ++r) xsh->setCell(r, c, m_cells[k++]);
  }

  app->getCurrentXsheet()->notifyXsheetChanged();
  app->getCurrentScene()->setDirtyFlag(true);

  TCellSelection *cellSelection = dynamic_cast<TCellSelection *>(
      TApp::instance()->getCurrentSelection()->getSelection());
  if (cellSelection) {
    int newR1 = m_singleRow ? m_r0 : (m_r0 + (m_rowsCount - 1));
    cellSelection->selectCells(m_r0, m_c0, newR1, m_c1);
  }
}

}  // namespace

//=============================================================================

void TCellSelection::decreaseStepCells() {
  if (isEmpty()) {
    int row = TTool::getApplication()->getCurrentFrame()->getFrame();
    int col = TTool::getApplication()->getCurrentColumn()->getColumnIndex();
    int r1  = row;
    TXsheet *xsh = TApp::instance()->getCurrentXsheet()->getXsheet();
    TXshCell cell;
    TXshCell nextCell;
    bool sameCells = true;
    cell           = xsh->getCell(row, col);
    if (cell.isEmpty()) return;

    for (int i = 1; sameCells; i++) {
      nextCell = xsh->getCell(row + i, col);
      if (nextCell.m_frameId == cell.m_frameId &&
          nextCell.m_level == cell.m_level &&
          !xsh->isImplicitCell(row + i, col)) {
        r1 = row + i;
      } else
        sameCells = false;
    }
    m_range.m_r0 = row;
    m_range.m_r1 = r1;
    m_range.m_c0 = col;
    m_range.m_c1 = col;
  }
  DecreaseStepUndo *undo = new DecreaseStepUndo(m_range.m_r0, m_range.m_c0,
                                                m_range.m_r1, m_range.m_c1);
  TUndoManager::manager()->add(undo);

  undo->redo();
}

//*********************************************************************************
//    Rollup Cells  command
//*********************************************************************************

namespace {

class RollupUndo : public TUndo {
  int m_r0, m_c0, m_r1, m_c1;

public:
  RollupUndo(int r0, int c0, int r1, int c1)
      : m_r0(r0), m_c0(c0), m_r1(r1), m_c1(c1) {}

  void redo() const override {
    TApp *app    = TApp::instance();
    TXsheet *xsh = app->getCurrentXsheet()->getXsheet();

    xsh->rollupCells(m_r0, m_c0, m_r1, m_c1);

    app->getCurrentXsheet()->notifyXsheetChanged();
    app->getCurrentScene()->setDirtyFlag(true);
  }

  void undo() const override {
    TApp *app    = TApp::instance();
    TXsheet *xsh = app->getCurrentXsheet()->getXsheet();

    xsh->rolldownCells(m_r0, m_c0, m_r1, m_c1);

    app->getCurrentXsheet()->notifyXsheetChanged();
    app->getCurrentScene()->setDirtyFlag(true);
  }

  int getSize() const override { return sizeof(*this); }

  QString getHistoryString() override { return QObject::tr("Roll Up"); }
  int getHistoryType() override { return HistoryType::Xsheet; }
};

}  // namespace

//=============================================================================

void TCellSelection::rollupCells() {
  TUndo *undo =
      new RollupUndo(m_range.m_r0, m_range.m_c0, m_range.m_r1, m_range.m_c1);
  TUndoManager::manager()->add(undo);

  undo->redo();
}

//*********************************************************************************
//    Rolldown Cells  command
//*********************************************************************************

namespace {

class RolldownUndo final : public RollupUndo {
public:
  RolldownUndo(int r0, int c0, int r1, int c1) : RollupUndo(r0, c0, r1, c1) {}

  void redo() const override { RollupUndo::undo(); }
  void undo() const override { RollupUndo::redo(); }

  QString getHistoryString() override { return QObject::tr("Roll Down"); }
};

}  // namespace

//=============================================================================

void TCellSelection::rolldownCells() {
  TUndo *undo =
      new RolldownUndo(m_range.m_r0, m_range.m_c0, m_range.m_r1, m_range.m_c1);
  TUndoManager::manager()->add(undo);

  undo->redo();
}

//*********************************************************************************
//    Set Keyframes  command
//*********************************************************************************

void TCellSelection::setKeyframes() {
  if (isEmpty()) return;

  // Preliminary data-fetching
  TApp *app = TApp::instance();

  TXsheetHandle *xshHandle = app->getCurrentXsheet();
  TXsheet *xsh             = xshHandle->getXsheet();

  int row = m_range.m_r0, col = m_range.m_c0;

  if (xsh->getColumn(col) && xsh->getColumn(col)->getFolderColumn()) return;

  const TXshCell &cell = xsh->getCell(row, col);
  if (cell.getSoundLevel() || cell.getSoundTextLevel()) return;

  const TStageObjectId &id =
      col >= 0 ? TStageObjectId::ColumnId(col)
               : TStageObjectId::CameraId(xsh->getCameraColumnIndex());

  TStageObject *obj = xsh->getStageObject(id);
  if (!obj) return;

  // Command body
  if (obj->isFullKeyframe(row)) {
    const TStageObject::Keyframe &key = obj->getKeyframe(row);

    UndoRemoveKeyFrame *undo = new UndoRemoveKeyFrame(id, row, key, xshHandle);
    undo->setObjectHandle(app->getCurrentObject());

    TUndoManager::manager()->add(undo);
    undo->redo();
  } else {
    UndoSetKeyFrame *undo = new UndoSetKeyFrame(id, row, xshHandle);
    undo->setObjectHandle(app->getCurrentObject());

    TUndoManager::manager()->add(undo);
    undo->redo();
  }

  TApp::instance()->getCurrentScene()->setDirtyFlag(
      true);  // Should be moved inside the undos!
}

//*********************************************************************************
//    Clone Level  command
//*********************************************************************************

namespace {

class CloneLevelUndo final : public TUndo {
  typedef std::map<TXshSimpleLevel *, TXshLevelP> InsertedLevelsMap;
  typedef std::set<int> InsertedColumnsSet;

  struct ExistsFunc;
  class LevelNamePopup;

private:
  TCellSelection::Range m_range;

  mutable InsertedLevelsMap m_insertedLevels;
  mutable InsertedColumnsSet m_insertedColumns;
  mutable bool m_clonedLevels;

public:
  mutable bool m_ok;

public:
  CloneLevelUndo(const TCellSelection::Range &range)
      : m_range(range), m_clonedLevels(false), m_ok(false) {}

  void redo() const override;
  void undo() const override;

  int getSize() const override {
    return sizeof *this + (sizeof(TXshLevelP) + sizeof(TXshSimpleLevel *)) *
                              m_insertedLevels.size();
  }

  QString getHistoryString() override {
    if (m_insertedLevels.empty()) return QString();
    QString str;
    if (m_insertedLevels.size() == 1) {
      str = QObject::tr("Clone  Level : %1 > %2")
                .arg(QString::fromStdWString(
                    m_insertedLevels.begin()->first->getName()))
                .arg(QString::fromStdWString(
                    m_insertedLevels.begin()->second->getName()));
    } else {
      str = QObject::tr("Clone  Levels : ");
      std::map<TXshSimpleLevel *, TXshLevelP>::const_iterator it =
          m_insertedLevels.begin();
      for (; it != m_insertedLevels.end(); ++it) {
        str += QString("%1 > %2, ")
                   .arg(QString::fromStdWString(it->first->getName()))
                   .arg(QString::fromStdWString(it->second->getName()));
      }
    }
    return str;
  }
  int getHistoryType() override { return HistoryType::Xsheet; }

private:
  TXshSimpleLevel *cloneLevel(const TXshSimpleLevel *srcSl,
                              const TFilePath &dstPath,
                              const std::set<TFrameId> &frames) const;

  bool chooseLevelName(TFilePath &fp) const;
  bool chooseOverwrite(OverwriteDialog *dialog, TFilePath &dstPath,
                       TXshSimpleLevel *&dstSl) const;

  void cloneLevels() const;
  void insertLevels() const;
  void insertCells() const;
};

//-----------------------------------------------------------------------------

struct CloneLevelUndo::ExistsFunc final : public OverwriteDialog::ExistsFunc {
  ToonzScene *m_scene;

public:
  ExistsFunc(ToonzScene *scene) : m_scene(scene) {}

  QString conflictString(const TFilePath &fp) const override {
    return OverwriteDialog::tr(
               "Level \"%1\" already exists.\n\nWhat do you want to do?")
        .arg(QString::fromStdWString(fp.withoutParentDir().getWideString()));
  }

  bool operator()(const TFilePath &fp) const override {
    return TSystem::doesExistFileOrLevel(fp) ||
           m_scene->getLevelSet()->getLevel(*m_scene, fp);
  }
};

//-----------------------------------------------------------------------------

class CloneLevelUndo::LevelNamePopup final : public DVGui::Dialog {
  DVGui::LineEdit *m_name;
  QPushButton *m_ok, *m_cancel;

public:
  LevelNamePopup(const std::wstring &defaultLevelName)
      : DVGui::Dialog(TApp::instance()->getMainWindow(), true, true,
                      "Clone Level") {
    setWindowTitle(
        QObject::tr("Clone Level", "CloneLevelUndo::LevelNamePopup"));

    beginHLayout();

    QLabel *label = new QLabel(
        QObject::tr("Level Name:", "CloneLevelUndo::LevelNamePopup"));
    addWidget(label);

    m_name = new DVGui::LineEdit;
    addWidget(m_name);

    m_name->setText(QString::fromStdWString(defaultLevelName));

    endHLayout();

    m_ok     = new QPushButton(QObject::tr("Ok"));
    m_cancel = new QPushButton(QObject::tr("Cancel"));
    addButtonBarWidget(m_ok, m_cancel);

    m_ok->setDefault(true);

    connect(m_ok, SIGNAL(clicked()), this, SLOT(accept()));
    connect(m_cancel, SIGNAL(clicked()), this, SLOT(reject()));
  }

  QString getName() const { return m_name->text(); }
};

//-----------------------------------------------------------------------------

TXshSimpleLevel *CloneLevelUndo::cloneLevel(
    const TXshSimpleLevel *srcSl, const TFilePath &dstPath,
    const std::set<TFrameId> &frames) const {
  ToonzScene *scene = TApp::instance()->getCurrentScene()->getScene();

  int levelType = srcSl->getType();
  assert(levelType > 0);

  const std::wstring &dstName = dstPath.getWideName();

  TXshSimpleLevel *dstSl =
      scene->createNewLevel(levelType, dstName)->getSimpleLevel();

  assert(dstSl);
  dstSl->setPath(scene->codeFilePath(dstPath));
  dstSl->setName(dstName);
  dstSl->clonePropertiesFrom(srcSl);
  *dstSl->getHookSet() = *srcSl->getHookSet();

  if (levelType == TZP_XSHLEVEL || levelType == PLI_XSHLEVEL) {
    TPalette *palette = srcSl->getPalette();
    assert(palette);

    dstSl->setPalette(palette->clone());
    dstSl->getPalette()->setDirtyFlag(true);
  }

  // The level clone shell was created. Now, clone the associated frames found
  // in the selection
  std::set<TFrameId>::const_iterator ft, fEnd(frames.end());
  for (ft = frames.begin(); ft != fEnd; ++ft) {
    const TFrameId &fid = *ft;

    TImageP img = srcSl->getFullsampledFrame(*ft, ImageManager::dontPutInCache);
    if (!img) continue;

    dstSl->setFrame(*ft, img->cloneImage());
  }

  dstSl->setDirtyFlag(true);

  return dstSl;
}

//-----------------------------------------------------------------------------

bool CloneLevelUndo::chooseLevelName(TFilePath &fp) const {
  std::unique_ptr<LevelNamePopup> levelNamePopup(
      new LevelNamePopup(fp.getWideName()));
  if (levelNamePopup->exec() == QDialog::Accepted) {
    const QString &levelName = levelNamePopup->getName();

    if (isValidFileName_message(levelName) &&
        !isReservedFileName_message(levelName)) {
      fp = fp.withName(levelName.toStdWString());
      return true;
    }
  }

  return false;
}

//-----------------------------------------------------------------------------

bool CloneLevelUndo::chooseOverwrite(OverwriteDialog *dialog,
                                     TFilePath &dstPath,
                                     TXshSimpleLevel *&dstSl) const {
  ToonzScene *scene = TApp::instance()->getCurrentScene()->getScene();
  ExistsFunc exists(scene);

  OverwriteDialog::Resolution acceptedRes = OverwriteDialog::ALL_RESOLUTIONS;

  TXshLevel *xl = scene->getLevelSet()->getLevel(*scene, dstPath);
  if (xl)
    acceptedRes =
        OverwriteDialog::Resolution(acceptedRes & ~OverwriteDialog::OVERWRITE);

  // Apply user's decision
  switch (dialog->execute(dstPath, exists, acceptedRes,
                          OverwriteDialog::APPLY_TO_ALL_FLAG)) {
  default:
    return false;

  case OverwriteDialog::KEEP_OLD:
    // Load the level at the preferred clone path
    if (!xl) xl = scene->loadLevel(dstPath);  // Hard load - from disk

    assert(xl);
    dstSl = xl->getSimpleLevel();
    break;

  case OverwriteDialog::OVERWRITE:
    assert(!xl);
    break;

  case OverwriteDialog::RENAME:
    break;
  }

  return true;
}

//-----------------------------------------------------------------------------

void CloneLevelUndo::cloneLevels() const {
  TApp *app         = TApp::instance();
  ToonzScene *scene = app->getCurrentScene()->getScene();
  TXsheet *xsh      = app->getCurrentXsheet()->getXsheet();

  // Retrieve the simple levels and associated frames in the specified range
  typedef std::set<TFrameId> FramesSet;
  typedef std::map<TXshSimpleLevel *, FramesSet> LevelsMap;

  LevelsMap levels;
  getSelectedFrames(*xsh, m_range.m_r0, m_range.m_c0, m_range.m_r1,
                    m_range.m_c1, levels);

  if (!levels.empty()) {
    bool askCloneName = (levels.size() == 1);

    // Now, try to clone every found level in the associated range
    std::unique_ptr<OverwriteDialog> dialog;
    ExistsFunc exists(scene);

    LevelsMap::iterator lt, lEnd(levels.end());
    for (lt = levels.begin(); lt != lEnd; ++lt) {
      assert(lt->first && !lt->second.empty());

      TXshSimpleLevel *srcSl = lt->first;
      if (srcSl->getPath().isUneditable()) continue;

      const TFilePath &srcPath = srcSl->getPath();

      // Build the destination level data
      TXshSimpleLevel *dstSl = 0;
      TFilePath dstPath      = scene->decodeFilePath(
          srcPath.withName(srcPath.getWideName() + L"_clone"));

      // Ask user to suggest an appropriate level name
      if (askCloneName && !chooseLevelName(dstPath)) continue;

      // Get a unique level path
      if (exists(dstPath)) {
        // Ask user for action
        if (!dialog.get()) dialog.reset(new OverwriteDialog);

        if (!chooseOverwrite(dialog.get(), dstPath, dstSl)) continue;
      }

      // If the destination level was not retained from existing data, it must
      // be created and cloned
      if (!dstSl) dstSl = cloneLevel(srcSl, dstPath, lt->second);

      assert(dstSl);
      m_insertedLevels[srcSl] = dstSl;
    }
  }
}

//-----------------------------------------------------------------------------

void CloneLevelUndo::insertLevels() const {
  ToonzScene *scene = TApp::instance()->getCurrentScene()->getScene();

  InsertedLevelsMap::iterator lt, lEnd = m_insertedLevels.end();
  for (lt = m_insertedLevels.begin(); lt != lEnd; ++lt)
    scene->getLevelSet()->insertLevel(lt->second.getPointer());
}

//-----------------------------------------------------------------------------

void CloneLevelUndo::insertCells() const {
  TXsheet *xsh = TApp::instance()->getCurrentXsheet()->getXsheet();

  m_insertedColumns.clear();

  // If necessary, insert blank columns AFTER the columns range.
  // Remember the indices of inserted columns, too.
  for (int c = 1; c <= m_range.getColCount(); ++c) {
    int colIndex = m_range.m_c1 + c;
    if (xsh->isColumnEmpty(
            colIndex))  // If there already is a hole, no need to insert -
      continue;         // we'll just use it.

    xsh->insertColumn(colIndex);
    m_insertedColumns.insert(colIndex);
  }

  bool useImplicitHold = Preferences::instance()->isImplicitHoldEnabled();

  // Now, re-traverse the selected range, and add corresponding cells
  // in the destination range
  for (int c = m_range.m_c0; c <= m_range.m_c1; ++c) {
    TXshLevelP lastLevel = 0;
    for (int r = m_range.m_r0; r <= m_range.m_r1; ++r) {
      TXshCell srcCell = xsh->getCell(r, c, false);
      if (srcCell.isEmpty() && useImplicitHold &&
          xsh->isColumnEmpty(c + m_range.getColCount()))
        srcCell = xsh->getCell(r, c, true);
      if (TXshSimpleLevel *srcSl = srcCell.getSimpleLevel()) {
        std::map<TXshSimpleLevel *, TXshLevelP>::iterator lt =
            m_insertedLevels.find(srcSl);
        if (lt != m_insertedLevels.end()) {
          lastLevel = lt->second;
          TXshCell dstCell(lt->second, srcCell.getFrameId());
          xsh->setCell(r, c + m_range.getColCount(), dstCell);
        }
      }
    }
    if (useImplicitHold &&
        !xsh->getCell(m_range.m_r1, c + m_range.getColCount()).isEmpty())
      xsh->setCell(m_range.m_r1 + 1, c + m_range.getColCount(),
                   TXshCell(lastLevel, TFrameId::STOP_FRAME));
  }
}

//-----------------------------------------------------------------------------

void CloneLevelUndo::redo() const {
  if (m_clonedLevels)
    insertLevels();
  else {
    m_clonedLevels = true;
    cloneLevels();
  }

  if (m_insertedLevels.empty()) return;

  // Command succeeded, let's deal with the xsheet
  m_ok = true;
  insertCells();

  // Finally, emit notifications
  TApp *app = TApp::instance();

  app->getCurrentXsheet()->notifyXsheetChanged();
  app->getCurrentScene()->setDirtyFlag(true);
  app->getCurrentScene()->notifyCastChange();
}

//-----------------------------------------------------------------------------

void CloneLevelUndo::undo() const {
  assert(!m_insertedLevels.empty());

  TApp *app         = TApp::instance();
  ToonzScene *scene = app->getCurrentScene()->getScene();

  TXsheet *xsh    = scene->getXsheet();
  TXsheet *topXsh = scene->getChildStack()->getTopXsheet();

  // Erase inserted columns from the xsheet
  for (int i = m_range.getColCount(); i > 0; --i) {
    int index                        = m_range.m_c1 + i;
    std::set<int>::const_iterator it = m_insertedColumns.find(index);
    xsh->removeColumn(index);
    if (it == m_insertedColumns.end()) xsh->insertColumn(index);
  }

  // Attempt removal of inserted columns from the cast
  // NOTE: Cloned levels who were KEEP_OLD'd may have already been present in
  // the cast

  std::map<TXshSimpleLevel *, TXshLevelP>::const_iterator lt,
      lEnd = m_insertedLevels.end();
  for (lt = m_insertedLevels.begin(); lt != lEnd; ++lt) {
    if (!topXsh->isLevelUsed(lt->second.getPointer()))
      scene->getLevelSet()->removeLevel(lt->second.getPointer());
  }

  app->getCurrentXsheet()->notifyXsheetChanged();
  app->getCurrentScene()->setDirtyFlag(true);
  app->getCurrentScene()->notifyCastChange();
}

}  // namespace

//-----------------------------------------------------------------------------

void TCellSelection::cloneLevel() {
  TUndoManager::manager()->beginBlock();

  std::unique_ptr<CloneLevelUndo> undo(new CloneLevelUndo(m_range));

  if (undo->redo(), undo->m_ok) {
    TUndoManager::manager()->add(undo.release());

    // Clone level always adds new columns after the column selection.
    TApplication *app = TApp::instance();
    for (int col = m_range.m_c0; col <= m_range.m_c1; col++) {
      int newCol              = col + m_range.getColCount();
      TStageObjectId columnId = TStageObjectId::ColumnId(newCol);
      TXsheet *xsh            = app->getCurrentXsheet()->getXsheet();
      TXshCell cell           = xsh->getCell(m_range.m_r0, newCol);
      if (cell.isEmpty()) continue;
      TXshSimpleLevel *level  = cell.getSimpleLevel();
      std::string columnName =
          QString::fromStdWString(level->getName()).toStdString();
      TStageObjectCmd::rename(columnId, columnName, app->getCurrentXsheet());
    }
  }

  TUndoManager::manager()->endBlock();
}

//=============================================================================

void TCellSelection::shiftKeyframes(int direction) {
  if (isEmpty() || areAllColSelectedLocked()) return;

  int shift = m_range.getRowCount() * direction;
  if (!shift) return;

  TXsheetHandle *xsheet = TApp::instance()->getCurrentXsheet();
  TXsheet *xsh          = xsheet->getXsheet();
  TCellKeyframeSelection *cellKeyframeSelection = new TCellKeyframeSelection(
      new TCellSelection(), new TKeyframeSelection());

  cellKeyframeSelection->setXsheetHandle(xsheet);

  TUndoManager::manager()->beginBlock();
  for (int col = m_range.m_c0; col <= m_range.m_c1; col++) {
    TXshColumn *column = xsh->getColumn(col);
    if (!column || column->isLocked()) continue;

    TStageObjectId colId =
        col < 0 ? TStageObjectId::ColumnId(xsh->getCameraColumnIndex())
                : TStageObjectId::ColumnId(col);
    TStageObject *colObj = xsh->getStageObject(colId);
    TStageObject::KeyframeMap keyframes;
    colObj->getKeyframes(keyframes);
    if (!keyframes.size()) continue;
    int row = m_range.m_r0;
    for (TStageObject::KeyframeMap::iterator it = keyframes.begin();
         it != keyframes.end(); it++) {
      if (it->first < m_range.m_r0) continue;
      row = it->first;
      cellKeyframeSelection->selectCellsKeyframes(row, col,
                                                  xsh->getFrameCount(), col);
      cellKeyframeSelection->getKeyframeSelection()->shiftKeyframes(
          row, row + shift, col, col);
      break;
    }
  }
  TUndoManager::manager()->endBlock();

  delete cellKeyframeSelection;
}
