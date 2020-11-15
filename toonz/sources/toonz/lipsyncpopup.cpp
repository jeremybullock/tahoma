

#include "lipsyncpopup.h"

// Tnz6 includes
#include "tapp.h"
#include "iocommand.h"
#include "menubarcommandids.h"

// TnzQt includes
#include "toonzqt/menubarcommand.h"
#include "toonzqt/icongenerator.h"

// TnzLib includes
#include "toonz/toonzscene.h"
#include "toonz/txsheet.h"
#include "toonz/tscenehandle.h"
#include "toonz/txsheethandle.h"
#include "toonz/tcolumnhandle.h"
#include "toonz/tframehandle.h"
#include "toonz/txshsimplelevel.h"
#include "toonz/txshlevelhandle.h"
#include "toonz/txshcell.h"
#include "toonz/sceneproperties.h"
#include "tsound_io.h"
#include "toonzqt/gutil.h"
#include "toutputproperties.h"
#include "toonz/tproject.h"

// TnzCore includes
#include "filebrowsermodel.h"
#include "xsheetdragtool.h"
#include "historytypes.h"
#include "tsystem.h"
#include "tenv.h"

// Qt includes
#include <QHBoxLayout>
#include <QPushButton>
#include <QMainWindow>
#include <QApplication>
#include <QTextStream>
#include <QPainter>
#include <QSignalMapper>
#include <QComboBox>
#include <QProcess>
#include <QTextEdit>
#include <QIcon>
#include <QAudio>
#include <QGroupBox>
#include <QTimer>

//=============================================================================
/*! \class LipSyncPopup
                \brief The LipSyncPopup class provides a modal dialog to
   apply lip sync text data to a image column.

                Inherits \b Dialog.
*/
//-----------------------------------------------------------------------------

//============================================================
//	Lip Sync Undo
//============================================================

class LipSyncUndo final : public TUndo {
public:
  LipSyncUndo(int col, TXshSimpleLevel *sl, TXshLevelP cl,
              std::vector<TFrameId> activeFrameIds, QStringList textLines,
              int size, std::vector<TFrameId> previousFrameIds,
              std::vector<TXshLevelP> previousLevels, int startFrame);
  void undo() const override;
  void redo() const override;
  int getSize() const override { return sizeof(*this); }
  QString getHistoryString() override {
    return QObject::tr("Apply Lip Sync Data");
  }
  int getHistoryType() override { return HistoryType::Xsheet; }

private:
  int m_col;
  int m_startFrame;
  TXshSimpleLevel *m_sl;
  TXshLevelP m_cl;
  QStringList m_textLines;
  int m_lastFrame;
  std::vector<TFrameId> m_previousFrameIds;
  std::vector<TXshLevelP> m_previousLevels;
  std::vector<TFrameId> m_activeFrameIds;
};

LipSyncUndo::LipSyncUndo(int col, TXshSimpleLevel *sl, TXshLevelP cl,
                         std::vector<TFrameId> activeFrameIds,
                         QStringList textLines, int lastFrame,
                         std::vector<TFrameId> previousFrameIds,
                         std::vector<TXshLevelP> previousLevels, int startFrame)
    : m_col(col)
    , m_sl(sl)
    , m_cl(cl)
    , m_textLines(textLines)
    , m_lastFrame(lastFrame)
    , m_previousFrameIds(previousFrameIds)
    , m_previousLevels(previousLevels)
    , m_activeFrameIds(activeFrameIds)
    , m_startFrame(startFrame) {}

void LipSyncUndo::undo() const {
  int i        = 0;
  TXsheet *xsh = TApp::instance()->getCurrentScene()->getScene()->getXsheet();
  while (i < m_previousFrameIds.size()) {
    int currFrame  = i + m_startFrame;
    TXshCell cell  = xsh->getCell(currFrame, m_col);
    cell.m_frameId = m_previousFrameIds.at(i);
    cell.m_level   = m_previousLevels.at(i);
    xsh->setCell(currFrame, m_col, cell);
    i++;
  }
  TApp::instance()->getCurrentXsheet()->notifyXsheetChanged();
  TApp::instance()->getCurrentScene()->setDirtyFlag(true);
}

void LipSyncUndo::redo() const {
  TXsheet *xsh = TApp::instance()->getCurrentScene()->getScene()->getXsheet();
  int i        = 0;
  int currentLine = 0;
  int size        = m_textLines.size();
  while (currentLine < m_textLines.size()) {
    int endAt;
    if (currentLine + 2 >= m_textLines.size()) {
      endAt = m_lastFrame;
    } else
      endAt = m_textLines.at(currentLine + 2).toInt() - 1;
    if (endAt <= 0) break;
    if (endAt <= i) {
      currentLine += 2;
      continue;
    }
    QString shape        = m_textLines.at(currentLine + 1).toLower();
    std::string strShape = shape.toStdString();
    TFrameId currentId   = TFrameId();
    if (shape == "ai") {
      currentId = m_activeFrameIds[0];
    } else if (shape == "e") {
      currentId = m_activeFrameIds[1];
    } else if (shape == "o") {
      currentId = m_activeFrameIds[2];
    } else if (shape == "u") {
      currentId = m_activeFrameIds[3];
    } else if (shape == "fv") {
      currentId = m_activeFrameIds[4];
    } else if (shape == "l") {
      currentId = m_activeFrameIds[5];
    } else if (shape == "mbp") {
      currentId = m_activeFrameIds[6];
    } else if (shape == "wq") {
      currentId = m_activeFrameIds[7];
    } else if (shape == "other" || shape == "etc") {
      currentId = m_activeFrameIds[8];
    } else if (shape == "rest") {
      currentId = m_activeFrameIds[9];
    }

    if (currentId.isEmptyFrame()) {
      currentLine += 2;
      continue;
    }

    while (i < endAt && i < m_lastFrame - m_startFrame) {
      int currFrame = i + m_startFrame;
      TXshCell cell = xsh->getCell(currFrame, m_col);
      if (m_sl)
        cell.m_level = m_sl;
      else
        cell.m_level = m_cl;
      cell.m_frameId = currentId;
      xsh->setCell(currFrame, m_col, cell);
      i++;
    }
    currentLine += 2;
  }
  TApp::instance()->getCurrentXsheet()->notifyXsheetChanged();
  TApp::instance()->getCurrentScene()->setDirtyFlag(true);
}

LipSyncPopup::LipSyncPopup()
    : Dialog(TApp::instance()->getMainWindow(), true, true, "LipSyncPopup") {
  setWindowTitle(tr("Apply Lip Sync Data"));
  setFixedWidth(860);
  setFixedHeight(400);
  m_applyButton = new QPushButton(tr("Apply"), this);
  // m_applyButton->setEnabled(false);
  m_aiLabel    = new QLabel(tr("A I Drawing"));
  m_oLabel     = new QLabel(tr("O Drawing"));
  m_eLabel     = new QLabel(tr("E Drawing"));
  m_uLabel     = new QLabel(tr("U Drawing"));
  m_lLabel     = new QLabel(tr("L Drawing"));
  m_wqLabel    = new QLabel(tr("W Q Drawing"));
  m_mbpLabel   = new QLabel(tr("M B P Drawing"));
  m_fvLabel    = new QLabel(tr("F V Drawing"));
  m_restLabel  = new QLabel(tr("Rest Drawing"));
  m_otherLabel = new QLabel(tr("C D G K N R S Th Y Z"));
  m_startAt    = new DVGui::IntLineEdit(this, 0);
  m_restToEnd  = new QCheckBox(tr("Extend Rest Drawing to End Marker"), this);

  m_rhubarb = new QProcess(this);
  m_player  = new QMediaPlayer(this);
  m_progressDialog =
      new DVGui::ProgressDialog("Analyzing audio...", "", 1, 100, this);
  m_progressDialog->hide();

  QImage placeHolder(160, 90, QImage::Format_ARGB32);
  placeHolder.fill(Qt::white);
  m_soundLevels = new QComboBox(this);
  m_playButton  = new QPushButton(tr(""), this);
  m_playIcon    = createQIcon("play");
  m_stopIcon    = createQIcon("stop");
  m_playButton->setIcon(m_playIcon);
  m_generateDatButton =
      new QPushButton(tr("Generate Lip Sync Data File"), this);
  QGridLayout *rhubarbLayout = new QGridLayout(this);
  m_scriptEdit               = new QTextEdit(this);
  m_scriptEdit->setFixedHeight(80);
  QHBoxLayout *soundLayout = new QHBoxLayout(this);
  m_columnLabel            = new QLabel(tr("Audio Source: "), this);
  soundLayout->addWidget(m_columnLabel);
  soundLayout->addWidget(m_soundLevels);
  soundLayout->addWidget(m_playButton);
  soundLayout->addStretch();
  rhubarbLayout->addLayout(soundLayout, 0, 0, 1, 5);
  m_scriptLabel =
      new QLabel(tr("Audio Script (Optional, Improves accuracy): "), this);
  m_scriptLabel->setToolTip(
      tr("A script significantly increases the accuracy of the lip sync."));

  m_audioFile = new DVGui::FileField(this, QString(""));
  m_audioFile->setFileMode(QFileDialog::ExistingFile);
  QStringList audioFilters;
  audioFilters << "wav"
               << "aiff";
  m_audioFile->setFilters(QStringList(audioFilters));
  m_audioFile->setMinimumWidth(500);

  rhubarbLayout->addWidget(m_audioFile, 1, 0, 1, 5);
  rhubarbLayout->addWidget(m_scriptLabel, 2, 0, 1, 3);
  rhubarbLayout->addWidget(m_scriptEdit, 3, 0, 1, 5);
  rhubarbLayout->addWidget(m_generateDatButton, 4, 3, 1, 2);
  rhubarbLayout->setSpacing(4);
  rhubarbLayout->setMargin(10);

  m_rhubarbBox = new QGroupBox(tr("Generate from Audio File"), this);
  m_rhubarbBox->setLayout(rhubarbLayout);
  m_rhubarbBox->hide();

  QHBoxLayout *boxHolder = new QHBoxLayout(this);
  boxHolder->addWidget(m_rhubarbBox);
  boxHolder->setMargin(8);

  for (int i = 0; i < 10; i++) {
    m_pixmaps[i] = QPixmap::fromImage(placeHolder);
  }
  for (int i = 0; i < 10; i++) {
    m_imageLabels[i] = new QLabel();
    m_imageLabels[i]->setPixmap(m_pixmaps[i]);
    m_textLabels[i] = new QLabel("temp", this);
  }

  m_file = new DVGui::FileField(this, QString(""));
  m_file->setFileMode(QFileDialog::ExistingFile);
  QStringList filters;
  filters << "txt"
          << "dat";
  m_file->setFilters(QStringList(filters));
  m_file->setMinimumWidth(500);

  for (int i = 0; i < 20; i++) {
    if (!(i % 2)) {
      m_navButtons[i] = new QPushButton("<");
      m_navButtons[i]->setToolTip(tr("Previous Drawing"));
    } else {
      m_navButtons[i] = new QPushButton(">");
      m_navButtons[i]->setToolTip(tr("Next Drawing"));
    }
  }

  //--- layout
  m_topLayout->setMargin(0);
  m_topLayout->setSpacing(0);

  m_topLayout->addLayout(boxHolder);
  {
    QGridLayout *phonemeLay = new QGridLayout();
    phonemeLay->setMargin(10);
    phonemeLay->setVerticalSpacing(10);
    phonemeLay->setHorizontalSpacing(10);
    int i = 0;  // navButtons
    int j = 0;  // imageLabels
    int k = 0;  // textLabels
    phonemeLay->addWidget(m_aiLabel, 0, 0, 1, 2, Qt::AlignCenter);
    phonemeLay->addWidget(m_eLabel, 0, 2, 1, 2, Qt::AlignCenter);
    phonemeLay->addWidget(m_oLabel, 0, 4, 1, 2, Qt::AlignCenter);
    phonemeLay->addWidget(m_uLabel, 0, 6, 1, 2, Qt::AlignCenter);
    phonemeLay->addWidget(m_fvLabel, 0, 8, 1, 2, Qt::AlignCenter);

    phonemeLay->addWidget(m_imageLabels[j], 1, 0, 1, 2, Qt::AlignCenter);
    j++;
    phonemeLay->addWidget(m_imageLabels[j], 1, 2, 1, 2, Qt::AlignCenter);
    j++;
    phonemeLay->addWidget(m_imageLabels[j], 1, 4, 1, 2, Qt::AlignCenter);
    j++;
    phonemeLay->addWidget(m_imageLabels[j], 1, 6, 1, 2, Qt::AlignCenter);
    j++;
    phonemeLay->addWidget(m_imageLabels[j], 1, 8, 1, 2, Qt::AlignCenter);
    j++;

    phonemeLay->addWidget(m_textLabels[k], 2, 0, 1, 2, Qt::AlignCenter);
    k++;
    phonemeLay->addWidget(m_textLabels[k], 2, 2, 1, 2, Qt::AlignCenter);
    k++;
    phonemeLay->addWidget(m_textLabels[k], 2, 4, 1, 2, Qt::AlignCenter);
    k++;
    phonemeLay->addWidget(m_textLabels[k], 2, 6, 1, 2, Qt::AlignCenter);
    k++;
    phonemeLay->addWidget(m_textLabels[k], 2, 8, 1, 2, Qt::AlignCenter);
    k++;

    phonemeLay->addWidget(m_navButtons[i], 3, 0, Qt::AlignCenter);
    i++;
    phonemeLay->addWidget(m_navButtons[i], 3, 1, Qt::AlignCenter);
    i++;
    phonemeLay->addWidget(m_navButtons[i], 3, 2, Qt::AlignCenter);
    i++;
    phonemeLay->addWidget(m_navButtons[i], 3, 3, Qt::AlignCenter);
    i++;
    phonemeLay->addWidget(m_navButtons[i], 3, 4, Qt::AlignCenter);
    i++;
    phonemeLay->addWidget(m_navButtons[i], 3, 5, Qt::AlignCenter);
    i++;
    phonemeLay->addWidget(m_navButtons[i], 3, 6, Qt::AlignCenter);
    i++;
    phonemeLay->addWidget(m_navButtons[i], 3, 7, Qt::AlignCenter);
    i++;
    phonemeLay->addWidget(m_navButtons[i], 3, 8, Qt::AlignCenter);
    i++;
    phonemeLay->addWidget(m_navButtons[i], 3, 9, Qt::AlignCenter);
    i++;

    phonemeLay->addWidget(new QLabel("", this), 4, Qt::AlignCenter);

    phonemeLay->addWidget(m_lLabel, 5, 0, 1, 2, Qt::AlignCenter);
    phonemeLay->addWidget(m_mbpLabel, 5, 2, 1, 2, Qt::AlignCenter);
    phonemeLay->addWidget(m_wqLabel, 5, 4, 1, 2, Qt::AlignCenter);
    phonemeLay->addWidget(m_otherLabel, 5, 6, 1, 2, Qt::AlignCenter);
    phonemeLay->addWidget(m_restLabel, 5, 8, 1, 2, Qt::AlignCenter);

    phonemeLay->addWidget(m_imageLabels[j], 6, 0, 1, 2, Qt::AlignCenter);
    j++;
    phonemeLay->addWidget(m_imageLabels[j], 6, 2, 1, 2, Qt::AlignCenter);
    j++;
    phonemeLay->addWidget(m_imageLabels[j], 6, 4, 1, 2, Qt::AlignCenter);
    j++;
    phonemeLay->addWidget(m_imageLabels[j], 6, 6, 1, 2, Qt::AlignCenter);
    j++;
    phonemeLay->addWidget(m_imageLabels[j], 6, 8, 1, 2, Qt::AlignCenter);
    j++;

    phonemeLay->addWidget(m_textLabels[k], 7, 0, 1, 2, Qt::AlignCenter);
    k++;
    phonemeLay->addWidget(m_textLabels[k], 7, 2, 1, 2, Qt::AlignCenter);
    k++;
    phonemeLay->addWidget(m_textLabels[k], 7, 4, 1, 2, Qt::AlignCenter);
    k++;
    phonemeLay->addWidget(m_textLabels[k], 7, 6, 1, 2, Qt::AlignCenter);
    k++;
    phonemeLay->addWidget(m_textLabels[k], 7, 8, 1, 2, Qt::AlignCenter);
    k++;

    phonemeLay->addWidget(m_navButtons[i], 8, 0, Qt::AlignCenter);
    i++;
    phonemeLay->addWidget(m_navButtons[i], 8, 1, Qt::AlignCenter);
    i++;
    phonemeLay->addWidget(m_navButtons[i], 8, 2, Qt::AlignCenter);
    i++;
    phonemeLay->addWidget(m_navButtons[i], 8, 3, Qt::AlignCenter);
    i++;
    phonemeLay->addWidget(m_navButtons[i], 8, 4, Qt::AlignCenter);
    i++;
    phonemeLay->addWidget(m_navButtons[i], 8, 5, Qt::AlignCenter);
    i++;
    phonemeLay->addWidget(m_navButtons[i], 8, 6, Qt::AlignCenter);
    i++;
    phonemeLay->addWidget(m_navButtons[i], 8, 7, Qt::AlignCenter);
    i++;
    phonemeLay->addWidget(m_navButtons[i], 8, 8, Qt::AlignCenter);
    i++;
    phonemeLay->addWidget(m_navButtons[i], 8, 9, Qt::AlignCenter);
    i++;
    phonemeLay->addWidget(new QLabel("", this), 9, Qt::AlignCenter);
    phonemeLay->addWidget(new QLabel(tr("Insert at Frame: ")), 10, 0, 1, 1,
                          Qt::AlignRight);
    phonemeLay->addWidget(m_startAt, 10, 1, 1, 1, Qt::AlignLeft);
    phonemeLay->addWidget(m_restToEnd, 10, 2, 1, 6, Qt::AlignLeft);

    m_topLayout->addLayout(phonemeLay, 0);
  }

  m_buttonLayout->setMargin(0);
  m_buttonLayout->setSpacing(10);
  {
    QHBoxLayout *fileLay = new QHBoxLayout();
    QLabel *pathLabel    = new QLabel(tr("Lip Sync Data File: "), this);
    pathLabel->setStyleSheet("background: rgba(0, 0, 0, 0);");
    fileLay->addWidget(pathLabel, Qt::AlignLeft);
    fileLay->addWidget(m_file);
    m_buttonLayout->addLayout(fileLay);
    m_buttonLayout->addStretch();
    m_buttonLayout->addWidget(m_applyButton);
  }

  //---- signal-slot connections
  QSignalMapper *signalMapper = new QSignalMapper(this);
  bool ret                    = true;

  ret = ret && connect(signalMapper, SIGNAL(mapped(int)), this,
                       SLOT(imageNavClicked(int)));
  for (int i = 0; i < 20; i++) {
    signalMapper->setMapping(m_navButtons[i], i);
    ret = ret && connect(m_navButtons[i], SIGNAL(clicked()), signalMapper,
                         SLOT(map()));
  }

  ret = ret &&
        connect(m_applyButton, SIGNAL(clicked()), this, SLOT(onApplyButton()));
  ret = ret && connect(m_startAt, SIGNAL(editingFinished()), this,
                       SLOT(onStartValueChanged()));
  ret = ret && connect(m_playButton, &QPushButton::pressed, this,
                       &LipSyncPopup::playSound);
  ret = ret && connect(m_generateDatButton, &QPushButton::pressed, this,
                       &LipSyncPopup::generateDatFile);
  ret = ret && connect(m_soundLevels, SIGNAL(currentIndexChanged(int)), this,
                       SLOT(onLevelChanged(int)));
  ret = ret && connect(m_scriptEdit, &QTextEdit::textChanged,
                       [=]() { m_generateDatButton->setEnabled(true); });
  ret = ret && connect(m_audioFile, &DVGui::FileField::pathChanged,
                       [=]() { m_generateDatButton->setEnabled(true); });

  assert(ret);
}

//-----------------------------------------------------------------------------

void LipSyncPopup::showEvent(QShowEvent *) {
  // reset
  m_activeFrameIds.clear();
  m_levelFrameIds.clear();
  m_sl = NULL;
  m_cl = NULL;
  m_startAt->setValue(1);
  TApp *app    = TApp::instance();
  TXsheet *xsh = app->getCurrentScene()->getScene()->getXsheet();
  m_col        = TTool::getApplication()->getCurrentColumn()->getColumnIndex();
  int row      = app->getCurrentFrame()->getFrame();
  m_isEditingLevel = app->getCurrentFrame()->isEditingLevel();
  m_startAt->setValue(row + 1);
  m_startAt->clearFocus();
  TXshLevelHandle *level = app->getCurrentLevel();
  m_sl                   = level->getSimpleLevel();
  if (!m_sl) {
    TXshCell cell = xsh->getCell(row, m_col);
    if (!cell.isEmpty()) {
      m_cl         = cell.m_level->getChildLevel();
      m_childLevel = cell.m_level;
    }
  }
  if (m_cl)
    DVGui::warning(
        tr("Thumbnails are not available for sub-Xsheets.\nPlease use the "
           "frame numbers for reference."));
  if (!m_sl && !m_cl) {
    DVGui::warning(tr("Unable to apply lip sync data to this column type"));
    return;
  }
  level->getLevel()->getFids(m_levelFrameIds);
  if (m_levelFrameIds.size() > 0) {
    int i = 0;
    // load frame ids from the level
    while (i < m_levelFrameIds.size() && i < 10) {
      m_activeFrameIds.push_back(m_levelFrameIds.at(i));
      i++;
    }
    // fill unused frameIds
    while (i < 10) {
      m_activeFrameIds.push_back(m_levelFrameIds.at(0));
      i++;
    }
  }
  refreshSoundLevels();
  onLevelChanged(-1);
  m_generateDatButton->setEnabled(true);
  findRhubarb();
}

//-----------------------------------------------------------------------------

void LipSyncPopup::refreshSoundLevels() {
  int currentIndex = 0;
  if (m_soundLevels->count() > 1) {
    currentIndex = m_soundLevels->currentIndex();
  }
  TXsheet *xsh = TApp::instance()->getCurrentXsheet()->getXsheet();
  m_soundLevels->clear();
  int colCount = xsh->getColumnCount();
  for (int i = 0; i < colCount; i++) {
    TXshColumn *col = xsh->getColumn(i);
    if (col->getSoundColumn()) {
      m_soundLevels->addItem(tr("Column ") + QString::number(i + 1));
    }
  }
  m_soundLevels->addItem(tr("Choose File"));
  if (currentIndex < m_soundLevels->count())
    m_soundLevels->setCurrentIndex(currentIndex);
}

//-----------------------------------------------------------------------------
void LipSyncPopup::generateDatFile() {
  m_audioPath  = "";
  m_startFrame = -1;
  m_deleteFile = false;
  if (m_soundLevels->currentIndex() < m_soundLevels->count() - 1) {
    saveAudio();
    m_deleteFile = true;
  } else {
    TFilePath tempPath(m_audioFile->getPath());
    ToonzScene *scene = TApp::instance()->getCurrentScene()->getScene();
    tempPath          = scene->decodeFilePath(tempPath);
    m_audioPath       = tempPath.getQString();
    ;
  }
  if (m_audioPath == "" ||
      !TSystem::doesExistFileOrLevel(TFilePath(m_audioPath))) {
    DVGui::warning(tr("Please choose an audio file and try again."));
    return;
  }
  runRhubarb();
}

//-----------------------------------------------------------------------------

void LipSyncPopup::playSound() {
  int count = m_soundLevels->count();
  if (count - 1 != m_soundLevels->currentIndex()) {
    int level       = m_soundLevels->currentText().split(" ")[1].toInt() - 1;
    TXsheet *xsh    = TApp::instance()->getCurrentXsheet()->getXsheet();
    TXshColumn *col = xsh->getColumn(level);
    TXshSoundColumn *sc = col->getSoundColumn();
    if (sc) {
      if (sc->isPlaying()) {
        sc->stop();
        m_playButton->setIcon(m_playIcon);
      } else {
        int r0, r1;
        xsh->getCellRange(level, r0, r1);
        double duration = sc->getOverallSoundTrack(r0, r1)->getDuration() * 1000;
        sc->play(r0);
        m_playButton->setIcon(m_stopIcon);
        QTimer::singleShot(duration, [=]() {
            sc->stop();
            m_playButton->setIcon(m_playIcon);
        });
      }
    }
  } else {
    if (m_player->state() == QMediaPlayer::StoppedState) {
      TFilePath tempPath(m_audioFile->getPath());
      ToonzScene* scene = TApp::instance()->getCurrentScene()->getScene();
      tempPath = scene->decodeFilePath(tempPath);
      m_player->setMedia(QUrl::fromLocalFile(tempPath.getQString()));
      m_player->setVolume(50);
      m_player->setNotifyInterval(20);
      connect(m_player, SIGNAL(positionChanged(qint64)), this,
              SLOT(updatePlaybackDuration(qint64)));
      connect(m_player, SIGNAL(stateChanged(QMediaPlayer::State)), this,
              SLOT(onMediaStateChanged(QMediaPlayer::State)));
      m_playButton->setIcon(m_stopIcon);

      // m_stoppedAtEnd = false;
      m_player->play();
    } else {
      m_player->stop();
      m_playButton->setIcon(m_playIcon);
    }
  }
}

//-----------------------------------------------------------------------------

void LipSyncPopup::onMediaStateChanged(QMediaPlayer::State state) {
  // stopping can happen through the stop button or the file ending
  if (state == QMediaPlayer::StoppedState) {
    m_playButton->setIcon(m_playIcon);
  }
}

//-----------------------------------------------------------------------------
void LipSyncPopup::saveAudio() {
  QString cacheRoot = ToonzFolder::getCacheRootFolder().getQString();
  if (!TSystem::doesExistFileOrLevel(TFilePath(cacheRoot + "/rhubarb"))) {
    TSystem::mkDir(TFilePath(cacheRoot + "/rhubarb"));
  }
  TFilePath audioPath     = TFilePath(cacheRoot + "/rhubarb/temp.wav");
  std::string tempSString = audioPath.getQString().toStdString();

  int level           = m_soundLevels->currentText().split(" ")[1].toInt() - 1;
  TXsheet *xsh        = TApp::instance()->getCurrentXsheet()->getXsheet();
  TXshColumn *col     = xsh->getColumn(level);
  TXshSoundColumn *sc = col->getSoundColumn();
  if (sc) {
    int r0, r1;
    xsh->getCellRange(level, r0, r1);
    TSoundTrackP st = sc->getOverallSoundTrack(r0);
    TSoundTrackWriter::save(audioPath, st);
    m_audioPath  = audioPath.getQString();
    m_startFrame = r0 + 1;
  }
}

//-----------------------------------------------------------------------------

QString LipSyncPopup::findRhubarb() {
  QString path = QDir::currentPath() + "/rhubarb/rhubarb";
  bool found   = false;
#if defined(_WIN32)
  path = path + ".exe";
#endif

#ifdef MACOSX
  path = QDir::currentPath() + "/" +
         QString::fromStdString(TEnv::getApplicationFileName()) +
         ".app/rhubarb/rhubarb";
  if (TSystem::doesExistFileOrLevel(TFilePath(path))) {
    found = true;
  }

#endif

  std::string sPath = path.toStdString();
  if (!found && TSystem::doesExistFileOrLevel(TFilePath(path))) {
    found = true;
  }

  if (found) {
    m_rhubarbBox->show();
    int index = m_soundLevels->currentIndex();
    int count = m_soundLevels->count();
    if (index == count - 1) {
      m_audioFile->show();
    } else {
      m_audioFile->hide();
    }
    return path;
  } else {
    m_rhubarbBox->hide();
    return QString("");
  }
}

//-----------------------------------------------------------------------------

void LipSyncPopup::runRhubarb() {
  QString path = findRhubarb();

  QString cacheRoot = ToonzFolder::getCacheRootFolder().getQString();
  if (!TSystem::doesExistFileOrLevel(TFilePath(cacheRoot + "/rhubarb"))) {
    TSystem::mkDir(TFilePath(cacheRoot + "/rhubarb"));
  }
  m_datPath                 = TFilePath(cacheRoot + "/rhubarb/temp.dat");
  QString datPath           = m_datPath.getQString();
  std::string tempDatString = datPath.toStdString();

  QStringList args;
  args << "-o" << datPath << "-f"
       << "dat"
       << "--datUsePrestonBlair";
  if (m_scriptEdit->toPlainText() != "") {
    QString script = m_scriptEdit->toPlainText();
    const QString qPath("testQTextStreamEncoding.txt");
    QString scriptPath =
        TFilePath(cacheRoot + "/rhubarb/script.txt").getQString();

    QFile qFile(scriptPath);
    if (qFile.open(QIODevice::WriteOnly)) {
      QTextStream out(&qFile);
      out << script;
      qFile.close();
      args << "-d" << scriptPath;
    }
  }

  int frameRate = std::rint(TApp::instance()
                                ->getCurrentScene()
                                ->getScene()
                                ->getProperties()
                                ->getOutputProperties()
                                ->getFrameRate());
  args << "--datFrameRate" << QString::number(frameRate) << "--machineReadable";

  args << m_audioPath;
  m_progressDialog->show();
  connect(m_rhubarb, &QProcess::readyReadStandardError, this,
          &LipSyncPopup::onOutputReady);
  connect(m_rhubarb,
          QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
          &LipSyncPopup::onProcessFinished);
  m_rhubarb->start(path, args);
}

//-----------------------------------------------------------------------------

void LipSyncPopup::onProcessFinished() {
  // rhubarb->waitForFinished();
  m_progressDialog->hide();
  QString results = m_rhubarb->readAllStandardError();
  results += m_rhubarb->readAllStandardOutput();
  m_rhubarb->close();
  std::string strResults = results.toStdString();
  m_file->setPath(m_datPath.getQString());
  m_startAt->setValue(std::max(1, m_startFrame));

  if (m_deleteFile && TSystem::doesExistFileOrLevel(TFilePath(m_audioPath)))
    TSystem::deleteFile(TFilePath(m_audioPath));
  m_deleteFile = false;
  m_generateDatButton->setEnabled(false);
}

//-----------------------------------------------------------------------------

void LipSyncPopup::onOutputReady() {
  QString output    = m_rhubarb->readAllStandardError().simplified();
  int index         = output.lastIndexOf("%");
  QString newString = output.mid(index - 2, 2);
  m_progressDialog->setValue(newString.toInt());
  qDebug() << "output: " << output;
}

//-----------------------------------------------------------------------------

void LipSyncPopup::onLevelChanged(int index) {
  index     = m_soundLevels->currentIndex();
  int count = m_soundLevels->count();
  if (index == count - 1) {
    m_audioFile->show();
  } else {
    m_audioFile->hide();
  }
  m_generateDatButton->setEnabled(true);
}

//-----------------------------------------------------------------------------

void LipSyncPopup::onApplyButton() {
  m_valid = false;
  m_textLines.clear();
  QString path = m_file->getPath();
  if (path.length() == 0) {
    DVGui::warning(tr("Please choose a lip sync data file to continue."));
    return;
  }
  TFilePath tempPath(path);
  ToonzScene *scene = TApp::instance()->getCurrentScene()->getScene();
  tempPath          = scene->decodeFilePath(tempPath);

  if (!TSystem::doesExistFileOrLevel(tempPath)) {
    DVGui::warning(
        tr("Cannot find the file specified. \nPlease choose a valid lip sync "
           "data file to continue."));
    return;
  }
  QFile file(tempPath.getQString());
  if (!file.open(QIODevice::ReadOnly)) {
    DVGui::warning(tr("Unable to open the file: \n") + file.errorString());
    return;
  }

  QTextStream in(&file);

  while (!in.atEnd()) {
    QString line        = in.readLine();
    QStringList entries = line.split(" ");
    if (entries.size() != 2) continue;
    bool ok;
    // make sure the first entry is a number
    int checkInt = entries.at(0).toInt(&ok);
    if (!ok) continue;
    // make sure the second entry isn't a number;
    checkInt = entries.at(1).toInt(&ok);
    if (ok) continue;
    m_textLines << entries;
  }
  if (m_textLines.size() <= 1) {
    DVGui::warning(
        tr("Invalid data file.\n Please choose a valid lip sync data file to "
           "continue."));
    m_valid = false;
    return;
  } else {
    m_valid = true;
  }

  file.close();

  if (!m_valid || (!m_sl && !m_cl)) {
    hide();
    return;
  }

  int i          = 0;
  int startFrame = m_startAt->getValue() - 1;
  TXsheet *xsh   = TApp::instance()->getCurrentScene()->getScene()->getXsheet();

  int lastFrame = m_textLines.at(m_textLines.size() - 2).toInt() + startFrame;

  if (m_restToEnd->isChecked()) {
    int r0, r1, step;
    TApp::instance()->getCurrentXsheet()->getXsheet()->getCellRange(m_col, r0,
                                                                    r1);
    if (lastFrame < r1 + 1) lastFrame = r1 + 1;
  }
  std::vector<TFrameId> previousFrameIds;
  std::vector<TXshLevelP> previousLevels;
  for (int previousFrame = startFrame; previousFrame < lastFrame;
       previousFrame++) {
    TXshCell cell = xsh->getCell(previousFrame, m_col);
    previousFrameIds.push_back(cell.m_frameId);
    previousLevels.push_back(cell.m_level);
  }

  LipSyncUndo *undo =
      new LipSyncUndo(m_col, m_sl, m_childLevel, m_activeFrameIds, m_textLines,
                      lastFrame, previousFrameIds, previousLevels, startFrame);
  TUndoManager::manager()->add(undo);
  undo->redo();
  hide();
}

//-----------------------------------------------------------------------------

void LipSyncPopup::imageNavClicked(int id) {
  if (!m_sl && !m_cl) return;
  int direction           = id % 2 ? 1 : -1;
  int frameNumber         = id / 2;
  TFrameId currentFrameId = m_activeFrameIds[frameNumber];
  std::vector<TFrameId>::iterator it;
  it =
      std::find(m_levelFrameIds.begin(), m_levelFrameIds.end(), currentFrameId);
  int frameIndex = std::distance(m_levelFrameIds.begin(), it);
  int newIndex;
  if (frameIndex == m_levelFrameIds.size() - 1 && direction == 1)
    newIndex = 0;
  else if (frameIndex == 0 && direction == -1)
    newIndex = m_levelFrameIds.size() - 1;
  else
    newIndex                    = frameIndex + direction;
  m_activeFrameIds[frameNumber] = m_levelFrameIds.at(newIndex);
  TXshCell newCell =
      TApp::instance()->getCurrentScene()->getScene()->getXsheet()->getCell(
          30, m_col);
  newCell.m_frameId = m_levelFrameIds.at(newIndex);
  newCell.m_level   = m_sl;
}

//-----------------------------------------------------------------------------

void LipSyncPopup::paintEvent(QPaintEvent *) {
  if (m_sl || m_cl) {
    int i = 0;
    while (i < 10) {
      QPixmap pm;
      if (m_sl)
        pm = IconGenerator::instance()->getSizedIcon(
            m_sl, m_activeFrameIds[i], "_lips", TDimension(160, 90));

      if (m_cl) {
        TFrameId currentFrameId = m_activeFrameIds[i];
        std::vector<TFrameId>::iterator it;
        it = std::find(m_levelFrameIds.begin(), m_levelFrameIds.end(),
                       currentFrameId);
        int frameIndex = std::distance(m_levelFrameIds.begin(), it);
        QImage placeHolder(160, 90, QImage::Format_ARGB32);
        placeHolder.fill(Qt::gray);
        QPainter p(&placeHolder);
        p.setPen(Qt::black);
        QRect r = placeHolder.rect();
        p.drawText(r, tr("SubXSheet Frame ") + QString::number(frameIndex + 1),
                   QTextOption(Qt::AlignCenter));
        pm = QPixmap::fromImage(placeHolder);
      }
      if (!pm.isNull()) {
        m_pixmaps[i] = pm;
        m_imageLabels[i]->setPixmap(m_pixmaps[i]);
        m_textLabels[i]->setText(
            tr("Drawing: ") + QString::number(m_activeFrameIds[i].getNumber()));
      }
      i++;
    }
  } else {
    QImage placeHolder(160, 90, QImage::Format_ARGB32);
    placeHolder.fill(Qt::gray);
    for (int i = 0; i < 10; i++) {
      m_pixmaps[i] = QPixmap::fromImage(placeHolder);
      m_imageLabels[i]->setPixmap(m_pixmaps[i]);
    }
  }
}

//-----------------------------------------------------------------------------

void LipSyncPopup::onStartValueChanged() {
  int value = m_startAt->getValue();
  if (value < 1) m_startAt->setValue(1);
}

OpenPopupCommandHandler<LipSyncPopup> openLipSyncPopup(MI_LipSyncPopup);
