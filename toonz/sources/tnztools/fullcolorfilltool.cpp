#include "fullcolorfilltool.h"

#include "toonz/stage2.h"
#include "tools/cursors.h"
#include "toonz/txshlevelhandle.h"
#include "toonz/trasterimageutils.h"
#include "toonz/ttileset.h"
#include "toonz/ttilesaver.h"
#include "toonz/levelproperties.h"
#include "toonz/preferences.h"
#include "toonz/txsheethandle.h"
#include "toonz/tframehandle.h"
#include "toonz/tonionskinmaskhandle.h"
#include "toonz/tpalettehandle.h"

#include "tools/toolhandle.h"
#include "tools/toolutils.h"

#include "tenv.h"
#include "tpalette.h"
#include "tsystem.h"
#include "symmetrytool.h"

using namespace ToolUtils;

TEnv::IntVar FullColorMinFillDepth("InknpaintFullColorMinFillDepth", 4);
TEnv::IntVar FullColorMaxFillDepth("InknpaintFullColorMaxFillDepth", 12);
TEnv::IntVar FullColorFillReferenced("InknpaintFullColorFillReferenced", 0);

TEnv::StringVar FullColorRasterGapSetting("InknpaintFullColorRasterGapSetting",
                                          "Ignore Gaps");
extern TEnv::DoubleVar AutocloseDistance;

namespace {

//=============================================================================
// FullColorFillUndo
//-----------------------------------------------------------------------------

class FullColorFillUndo final : public TFullColorRasterUndo {
  FillParameters m_params;
  bool m_saveboxOnly;
  TXsheet *m_xsheet;
  int m_frameIndex;

public:
  FullColorFillUndo(TTileSetFullColor *tileSet, const FillParameters &params,
                    TXshSimpleLevel *sl, const TFrameId &fid, bool saveboxOnly,
                    TXsheet *xsheet = 0, int frameIndex = -1)
      : TFullColorRasterUndo(tileSet, sl, fid, false, false, 0)
      , m_params(params)
      , m_saveboxOnly(saveboxOnly)
      , m_xsheet(xsheet)
      , m_frameIndex(frameIndex) {}

  void redo() const override {
    TRasterImageP image = getImage();
    if (!image) return;
    TRaster32P r;
    if (m_saveboxOnly) {
      TRectD temp = image->getBBox();
      TRect ttemp = convert(temp);
      r           = image->getRaster()->extract(ttemp);
    } else
      r = image->getRaster();

    fullColorFill(r, m_params, 0, m_xsheet, m_frameIndex);

    TTool::Application *app = TTool::getApplication();
    if (app) {
      app->getCurrentXsheet()->notifyXsheetChanged();
      notifyImageChanged();
    }
  }

  int getSize() const override {
    return sizeof(*this) + TFullColorRasterUndo::getSize();
  }

  QString getToolName() override {
    return QString("Fill Tool : %1")
        .arg(QString::fromStdWString(m_params.m_fillType));
  }
  int getHistoryType() override { return HistoryType::FillTool; }
};

//=============================================================================
// doFill
//-----------------------------------------------------------------------------

void doFill(const TImageP &img, const TPointD &pos, FillParameters &params,
            bool isShiftFill, TXshSimpleLevel *sl, const TFrameId &fid,
            TXsheet *xsheet, int frameIndex, bool fillGaps = false,
            bool closeGaps = false, int closeStyleIndex = -1) {
  TTool::Application *app = TTool::getApplication();
  if (!app || !sl) return;

  if (TRasterImageP ri = TRasterImageP(img)) {
    TPoint offs(0, 0);
    TRaster32P ras = ri->getRaster();
    // only accept 32bpp images for now
    if (!ras.getPointer() || ras->isEmpty()) return;

    ras->lock();

    TTileSetFullColor *tileSet = new TTileSetFullColor(ras->getSize());
    TTileSaverFullColor tileSaver(ras, tileSet);
    TDimension imageSize = ras->getSize();
    TPointD p(imageSize.lx % 2 ? 0.0 : 0.5, imageSize.ly % 2 ? 0.0 : 0.5);

    /*-- params.m_p = convert(pos-p)�ł́A�}�C�i�X���W�ł��ꂪ������ --*/
    TPointD tmp_p = pos - p;
    params.m_p = TPoint((int)floor(tmp_p.x + 0.5), (int)floor(tmp_p.y + 0.5));

    params.m_p += ras->getCenter();
    params.m_p -= offs;
    params.m_shiftFill = isShiftFill;

    TRect rasRect(ras->getSize());
    if (!rasRect.contains(params.m_p)) {
      ras->unlock();
      return;
    }

    fullColorFill(ras, params, &tileSaver, xsheet, frameIndex, fillGaps,
                  closeGaps, closeStyleIndex, AutocloseDistance);

    if (tileSaver.getTileSet()->getTileCount() != 0) {
      static int count = 0;
      TSystem::outputDebug("RASTERFILL" + std::to_string(count++) + "\n");
      if (offs != TPoint())
        for (int i = 0; i < tileSet->getTileCount(); i++) {
          TTileSet::Tile *t = tileSet->editTile(i);
          t->m_rasterBounds = t->m_rasterBounds + offs;
        }
      TUndoManager::manager()->add(
          new FullColorFillUndo(tileSet, params, sl, fid,
                                Preferences::instance()->getFillOnlySavebox()));
    }

    sl->getProperties()->setDirtyFlag(true);

    ras->unlock();
  }

  TTool *t = app->getCurrentTool()->getTool();
  if (t) t->notifyImageChanged();
}
};

//=============================================================================
// FullColorFillTool
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------

FullColorFillTool::FullColorFillTool()
    : TTool("T_Fill")
    , m_fillDepth("Fill Depth", 0, 15, 4, 12)
    , m_referenced("Refer Visible", false)
    , m_closeStyleIndex("Style Index:", L"current")  // W_ToolOptions_InkIndex
    , m_rasterGapDistance("Distance:", 1, 100, 10)
    , m_closeRasterGaps("Gaps:")
    , m_filledOnPress(false) {
  bind(TTool::RasterImage);
  m_prop.bind(m_fillDepth);
  m_prop.bind(m_closeRasterGaps);
  m_prop.bind(m_rasterGapDistance);
  m_prop.bind(m_closeStyleIndex);
  m_prop.bind(m_referenced);

  m_closeRasterGaps.setId("CloseGaps");
  m_rasterGapDistance.setId("RasterGapDistance");
  m_closeStyleIndex.setId("RasterGapInkIndex");

  m_closeRasterGaps.addValue(IGNOREGAPS);
  m_closeRasterGaps.addValue(FILLGAPS);
  m_closeRasterGaps.addValue(CLOSEANDFILLGAPS);
}

void FullColorFillTool::updateTranslation() {
  m_fillDepth.setQStringName(tr("Fill Depth"));
  m_referenced.setQStringName(tr("Refer Visible"));
  m_rasterGapDistance.setQStringName(tr("Distance:"));
  m_closeStyleIndex.setQStringName(tr("Style Index:"));
  m_closeRasterGaps.setQStringName(tr("Gaps:"));
  m_closeRasterGaps.setItemUIName(IGNOREGAPS, tr("Ignore Gaps"));
  m_closeRasterGaps.setItemUIName(FILLGAPS, tr("Fill Gaps"));
  m_closeRasterGaps.setItemUIName(CLOSEANDFILLGAPS, tr("Close and Fill"));
}

FillParameters FullColorFillTool::getFillParameters() const {
  FillParameters params;
  int styleId           = TTool::getApplication()->getCurrentLevelStyleIndex();
  params.m_styleId      = styleId;
  params.m_minFillDepth = (int)m_fillDepth.getValue().first;
  params.m_maxFillDepth = (int)m_fillDepth.getValue().second;
  params.m_referenced   = m_referenced.getValue();

  if (m_level) params.m_palette = m_level->getPalette();
  return params;
}

void FullColorFillTool::leftButtonDown(const TPointD &pos,
                                       const TMouseEvent &e) {
  m_clickPoint  = pos;
  TApplication *app = TTool::getApplication();
  TXshLevel *xl = app->getCurrentLevel()->getLevel();
  m_level       = xl ? xl->getSimpleLevel() : 0;
  FillParameters params = getFillParameters();

  int closeStyleIndex = m_closeStyleIndex.getStyleIndex();
  if (closeStyleIndex == -1) {
    closeStyleIndex = app->getCurrentPalette()->getStyleIndex();
  }

  m_filledOnPress = true;

  int frameIndex = app->getCurrentFrame()->getFrameIndex();

  TXsheetHandle *xsh = app->getCurrentXsheet();
  TXsheet *xsheet =
    params.m_referenced && !app->getCurrentFrame()->isEditingLevel() && xsh
    ? xsh->getXsheet()
    : 0;

  applyFill(getImage(true), pos, params, e.isShiftPressed(),
            m_level.getPointer(), getCurrentFid(), xsheet, frameIndex,
            m_closeRasterGaps.getIndex() > 0, m_closeRasterGaps.getIndex() > 1,
            closeStyleIndex);
  invalidate();
}

void FullColorFillTool::leftButtonDrag(const TPointD &pos,
                                       const TMouseEvent &e) {
  // On a tap durning normal fills, the fill happens on the initial press and
  // may delay the release event.  Movement may occur inbetween and where it
  // registers the movement may cause accidental drag fills.  If this is the 1st
  // movement after an initial press, ignore it by changing click point
  if (m_filledOnPress) {
    m_filledOnPress = false;
    m_clickPoint    = pos;
  }

  FillParameters params = getFillParameters();
  if (m_clickPoint == pos) return;
  if (!m_level || !params.m_palette) return;
  TImageP img = getImage(true);
  TPixel32 fillColor =
      params.m_palette->getStyle(params.m_styleId)->getMainColor();
  if (TRasterImageP ri = img) {
    TRaster32P ras = ri->getRaster();
    if (!ras) return;
    TPointD center = ras->getCenterD();
    TPoint ipos    = convert(pos + center);
    if (!ras->getBounds().contains(ipos)) return;
    TPixel32 pix = ras->pixels(ipos.y)[ipos.x];
    if (pix == fillColor) {
      invalidate();
      return;
    }
  } else
    return;

  TApplication *app = TTool::getApplication();
  int frameIndex    = app->getCurrentFrame()->getFrameIndex();

  TXsheetHandle *xsh = app->getCurrentXsheet();
  TXsheet *xsheet =
      params.m_referenced && !app->getCurrentFrame()->isEditingLevel() && xsh
          ? xsh->getXsheet()
          : 0;

  int closeStyleIndex = m_closeStyleIndex.getStyleIndex();
  if (closeStyleIndex == -1) {
    closeStyleIndex = app->getCurrentPalette()->getStyleIndex();
  }

  applyFill(img, pos, params, e.isShiftPressed(), m_level.getPointer(),
            getCurrentFid(), xsheet, frameIndex,
            m_closeRasterGaps.getIndex() > 0, m_closeRasterGaps.getIndex() > 1,
            closeStyleIndex);

  invalidate();
}

void FullColorFillTool::leftButtonUp(const TPointD &pos, const TMouseEvent &e) {
  m_filledOnPress = false;
}

bool FullColorFillTool::onPropertyChanged(std::string propertyName) {
  // Fill Depth
  if (propertyName == m_fillDepth.getName()) {
    FullColorMinFillDepth = (int)m_fillDepth.getValue().first;
    FullColorMaxFillDepth = (int)m_fillDepth.getValue().second;
  } else if (propertyName == m_rasterGapDistance.getName()) {
    AutocloseDistance       = m_rasterGapDistance.getValue();
    TTool::Application *app = TTool::getApplication();
    // This is a hack to get the viewer to update with the distance.
    app->getCurrentOnionSkin()->notifyOnionSkinMaskChanged();
  }

  else if (propertyName == m_closeStyleIndex.getName()) {
  }

  else if (propertyName == m_closeRasterGaps.getName()) {
    FullColorRasterGapSetting = ::to_string(m_closeRasterGaps.getValue());
  }

  return true;
}

void FullColorFillTool::onActivate() {
  static bool firstTime = true;
  if (firstTime) {
    m_fillDepth.setValue(TDoublePairProperty::Value(FullColorMinFillDepth,
                                                    FullColorMaxFillDepth));
    firstTime = false;
  }

  m_closeRasterGaps.setValue(
      ::to_wstring(FullColorRasterGapSetting.getValue()));
  m_rasterGapDistance.setValue(AutocloseDistance);
}

int FullColorFillTool::getCursorId() const {
  int ret = ToolCursor::FillCursor;
  if (ToonzCheck::instance()->getChecks() & ToonzCheck::eBlackBg)
    ret = ret | ToolCursor::Ex_Negate;
  return ret;
}

void FullColorFillTool::applyFill(const TImageP &img, const TPointD &pos,
                                  FillParameters &params, bool isShiftFill,
                                  TXshSimpleLevel *sl, const TFrameId &fid,
                                  TXsheet *xsheet, int frameIndex, bool fillGap,
                                  bool closeGap, int closeStyleIndex) {
  TRasterImageP ri = TRasterImageP(img);

  SymmetryTool *symmetryTool = dynamic_cast<SymmetryTool *>(
      TTool::getTool("T_Symmetry", TTool::RasterImage));
  if (ri && symmetryTool && symmetryTool->isGuideEnabled()) {
    TUndoManager::manager()->beginBlock();
  }

  doFill(img, pos, params, isShiftFill, sl, fid, xsheet, frameIndex, fillGap,
         closeGap, closeStyleIndex);

  if (ri && symmetryTool && symmetryTool->isGuideEnabled()) {
    TPointD dpiScale  = getViewer()->getDpiScale();
    TRasterP ras      = ri->getRaster();
    TPointD rasCenter = ras ? ras->getCenterD() : TPointD(0, 0);
    TPointD fillPt    = pos + rasCenter;
    std::vector<TPointD> symmPts =
        symmetryTool->getSymmetryPoints(fillPt, rasCenter, dpiScale);

    for (int i = 0; i < symmPts.size(); i++) {
      if (symmPts[i] == fillPt) continue;
      doFill(img, symmPts[i] - rasCenter, params, isShiftFill, sl, fid, xsheet,
             frameIndex, fillGap, closeGap, closeStyleIndex);
    }

    TUndoManager::manager()->endBlock();
  }
}

FullColorFillTool FullColorRasterFillTool;