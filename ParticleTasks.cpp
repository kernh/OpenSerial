// ParticleTasks.cpp:      Performs tasks primarily for single-particle acquisition
//
// Copyright (C) 2017 by  the Regents of the University of
// Colorado.  See Copyright.txt for full notice of copyright and limitations.
//
// Author: David Mastronarde
//
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "SerialEM.h"
#include "ParticleTasks.h"
#include "ComplexTasks.h"
#include "EMscope.h"
#include "FocusManager.h"
#include "ShiftManager.h"
#include "MacroProcessor.h"
#include "AutoTuning.h"
#include "ShiftManager.h"
#include "CameraController.h"
#include "EMmontageController.h"


CParticleTasks::CParticleTasks(void)
{
  SEMBuildTime(__DATE__, __TIME__);
  mWinApp = (CSerialEMApp *)AfxGetApp();
  mImBufs = mWinApp->GetImBufs();
  mMSCurIndex = -2;             // Index when it is not doing anything must be < -1
  mMSTestRun = 0;
  mWDFocusBuffer = NULL;
  mWaitingForDrift = 0;
  mWDDfltParams.measureType = WFD_USE_FOCUS;
  mWDDfltParams.driftRate = 1.;
  mWDDfltParams.useAngstroms = false;
  mWDDfltParams.interval = 10.;
  mWDDfltParams.maxWaitTime = 60.;
  mWDDfltParams.failureAction = 1;
  mWDDfltParams.setTrialParams = false;
  mWDDfltParams.exposure = 0.2f;
  mWDDfltParams.binning = 4;
}


CParticleTasks::~CParticleTasks(void)
{
}

// Set program pointers
void CParticleTasks::Initialize(void)
{
  mScope = mWinApp->mScope;
  mCamera = mWinApp->mCamera;
  mComplexTasks = mWinApp->mComplexTasks;
  mShiftManager = mWinApp->mShiftManager;
  mFocusManager = mWinApp->mFocusManager;
  mMSParams = mWinApp->mNavHelper->GetMultiShotParams();
}

/*
 * External call to start the operation with all the parameters
 */
int CParticleTasks::StartMultiShot(int numPeripheral, int doCenter, float spokeRad, 
  float extraDelay, BOOL saveRec, int ifEarlyReturn, int earlyRetFrames, BOOL adjustBT,
  int inHoleOrMulti)
{
  float pixel;
  int nextShot, nextHole, testRun;
  double delISX, delISY, delBTX, delBTY;
  CameraParameters *camParam = mWinApp->GetActiveCamParam();
  ComaVsISCalib *comaVsIS = mWinApp->mAutoTuning->GetComaVsIScal();
  MontParam *montP = mWinApp->GetMontParam();
  bool multiInHole = (inHoleOrMulti & MULTI_IN_HOLE) != 0;
  bool multiHoles = (inHoleOrMulti & MULTI_HOLES) != 0;
  int testImage = inHoleOrMulti & MULTI_TEST_IMAGE;
  bool testComa = (inHoleOrMulti & MULTI_TEST_COMA) != 0;
  mMSNumPeripheral = multiInHole ? numPeripheral : 0;
  mMSDoCenter = multiInHole ? doCenter : 1;
  mMSExtraDelay = extraDelay;
  mMSIfEarlyReturn = ifEarlyReturn;
  mMSEarlyRetFrames = earlyRetFrames;
  mMSAdjustBeamTilt = adjustBT && comaVsIS->magInd > 0;

  // Check conditions, first for test runs
  if (testImage && testComa) {
    SEMMessageBox("You cannot test both multishot image location and coma correction in "
      "one run");
    return 1;
  }
  testRun = testImage;
  if (testImage) {
    if (mWinApp->Montaging() && ((mWinApp->LowDoseMode() && (montP->useViewInLowDose ||
      montP->useSearchInLowDose)) || montP->moveStage)) {
        SEMMessageBox("You cannot test multishot image location with a View or Search "
          "montage or a stage montage");
        return 1;
    }
  }
  if (testComa) {
    testRun =  MULTI_TEST_COMA;
    if (comaVsIS->magInd <= 0 && adjustBT) {
      SEMMessageBox("You cannot test multishot coma correction with\r\n""adjustment for"
        " image shift: there is no calibration of coma versus image shift");
      return 1;
    }
    mMSAdjustBeamTilt = true;
    mMSSaveRecord = false;
    mMSDoCenter = -1;
  }

  // Adjust some parameters for test runs
  if (testRun) {
    mMSIfEarlyReturn = 0;
    if (multiHoles) {
      mMSNumPeripheral = 0;
      multiInHole = false;
      mMSDoCenter = 1;
    } else if (testImage)
      mMSDoCenter = 0;
  }

  mMSSaveRecord = !(multiHoles && mWinApp->Montaging()) && !testComa && saveRec;

  // Then test other conditions
  if (mMSIfEarlyReturn && !camParam->K2Type) {
    SEMMessageBox("The current camera must be a K2/K3 to use early return for multiple "
       "shots");
    return 1;
  }
  if (multiInHole  && (numPeripheral < 2 || numPeripheral > 12)) {
      SEMMessageBox("To do multiple shots in a hole, the number of shots\n"
        "around the center must be between 2 and 12");
      return 1;
  }
  if (multiHoles && !((mMSParams->useCustomHoles && mMSParams->customHoleX.size() > 0) ||
    mMSParams->holeMagIndex > 0)) {
      SEMMessageBox("Hole positions have not been defined for doing multiple Records");
      return 1;
  }
  if (mMSIfEarlyReturn && earlyRetFrames <= 0 && mMSSaveRecord) {
    SEMMessageBox("You cannot save Record images from multiple\n"
      "shots when doing an early return with 0 frames");
    return 1;
  }
  if (mMSSaveRecord && !mWinApp->mStoreMRC) {
    SEMMessageBox("An image file must be open for saving\n"
      "before starting to acquire multiple Record shots");
    return 1;
  }
  if (mMSSaveRecord && mWinApp->Montaging()) {
    SEMMessageBox("Multiple Records are supposed to be saved but the current image file"
      " is a montage");
    return 1;
  }

  // Set this after all tests and parameter settings, it determines operation of
  // GetHolePositions which is called from SerialEMView
  mMSTestRun = testRun;

  // Go to low dose area before recording any values
  if (mWinApp->LowDoseMode())
    mScope->GotoLowDoseArea(RECORD_CONSET);
  mMagIndex = mScope->GetMagIndex();

  mMSHoleIndex = 0;
  mMSHoleISX.clear();
  mMSHoleISY.clear();
  if (inHoleOrMulti & MULTI_HOLES) {
     mMSNumHoles = GetHolePositions(mMSHoleISX, mMSHoleISY, mMagIndex, 
       mWinApp->GetCurrentCamera());
     mMSUseHoleDelay = true;
  } else {

    // No holes = 1
    mMSNumHoles = 1;
    mMSHoleISX.push_back(0.);
    mMSHoleISY.push_back(0.);
  }

  // Get the starting image shift and beam tilt, save BT as value to restore and value to
  // increment from when compensating
  mScope->GetImageShift(mBaseISX, mBaseISY);
  if (mMSAdjustBeamTilt) {
    mScope->GetBeamTilt(mBaseBeamTiltX, mBaseBeamTiltY);
    mCenterBeamTiltX = mBaseBeamTiltX;
    mCenterBeamTiltY = mBaseBeamTiltY;

    // If script hasn't already compensated for IS, get the IS to compensate and, compute
    // the beam tilt to apply, and set it as base for compensating position IS's
    if (!(mWinApp->mMacroProcessor->DoingMacro() && 
      mWinApp->mMacroProcessor->GetCompensatedBTforIS())) {
      mScope->GetLDCenteredShift(delISX, delISY);
      delBTX = comaVsIS->matrix.xpx * delISX + comaVsIS->matrix.xpy * delISY;
      delBTY = comaVsIS->matrix.ypx * delISX + comaVsIS->matrix.ypy * delISY;
      mCenterBeamTiltX = mBaseBeamTiltX + delBTX;
      mCenterBeamTiltY = mBaseBeamTiltY + delBTY;
      mScope->SetBeamTilt(mCenterBeamTiltX, mCenterBeamTiltY);
      SEMTrace('1', "For starting IS  %.3f %.3f  setting BT  %.3f  %.3f", 
        delISX, delISY, delBTX, delBTY);
    }
  }

  mActPostExposure = mWinApp->ActPostExposure() && !mMSTestRun;
  mLastISX = mBaseISX;
  mLastISY = mBaseISY;
  pixel = mShiftManager->GetPixelSize(mWinApp->GetCurrentCamera(), mMagIndex);
  mMSRadiusOnCam = spokeRad / pixel;
  mCamToIS = mShiftManager->CameraToIS(mMagIndex);

  // Current index runs from -1 to mMSNumPeripheral - 1 for center before
  //                          0 to mMSNumPeripheral - 1 for no center
  //                          0 to mMSNumPeripheral for center after
  mMSCurIndex = mMSDoCenter < 0 ? -1 : 0;
  mMSLastShotIndex = mMSNumPeripheral + (mMSDoCenter > 0 ? 0 : -1);
  mWinApp->UpdateBufferWindows();

  // Need to shift to the first position if doing holes or if center is not being done
  // first
  if (mMSUseHoleDelay || mMSDoCenter >= 0)
    SetUpMultiShotShift(mMSCurIndex, mMSHoleIndex, false);

  // Queue the next position if post-actions and there IS a next position
  if (mActPostExposure && GetNextShotAndHole(nextShot, nextHole))
    SetUpMultiShotShift(nextShot, nextHole, true);

  return StartOneShotOfMulti();
}

/*
 * Do the next operations after a shot is done
 */
void CParticleTasks::MultiShotNextTask(int param)
{
  int nextShot, nextHole;
  if (mMSCurIndex < -1)
    return;

  // Save Record
  if (mMSSaveRecord && mWinApp->mBufferManager->SaveImageBuffer(mWinApp->mStoreMRC)) {
    StopMultiShot();
    return;
  }

  // Set indices for next shot if there is one, otherwise quit
  if (GetNextShotAndHole(nextShot, nextHole)) {
    mMSCurIndex = nextShot;
    mMSHoleIndex = nextHole;
  } else {
    StopMultiShot();
    return;
  }

  // Do or queue image shift as needed and take shot
  // For post-action queuing, get the hole and shot index of next position
  if (mActPostExposure) {
    if (GetNextShotAndHole(nextShot, nextHole))
      SetUpMultiShotShift(nextShot, nextHole, true);
  } else {
    SetUpMultiShotShift(mMSCurIndex, mMSHoleIndex, false);
  }
  StartOneShotOfMulti(); 
}

int CParticleTasks::MultiShotBusy(void)
{
  return (DoingMultiShot() && (mWinApp->mCamera->CameraBusy() || 
    mWinApp->mMontageController->DoingMontage() || 
    mWinApp->mAutoTuning->GetDoingCtfBased())) ? 1 : 0;
}

// Cleanup call on error/timeout
void CParticleTasks::MultiShotCleanup(int error)
{
  if (error == IDLE_TIMEOUT_ERROR)
    SEMMessageBox(_T("Time out getting an image with multi-shot routine"));
  StopMultiShot();
  mWinApp->ErrorOccurred(error);
}

// Stop call: restore image shift 
void CParticleTasks::StopMultiShot(void)
{
  if (mMSCurIndex < -1)
    return;
  if (mCamera->Acquiring()) {
    mCamera->SetImageShiftToRestore(mBaseISX, mBaseISY);
    if (mMSAdjustBeamTilt)
      mCamera->SetBeamTiltToRestore(mBaseBeamTiltX, mBaseBeamTiltY);
  } else {
    mScope->SetImageShift(mBaseISX, mBaseISY);
    if (mMSAdjustBeamTilt)
      mScope->SetBeamTilt(mBaseBeamTiltX, mBaseBeamTiltY);
  }

  // Montage does a second restore after it is no longer "Doing", so just set this
  mWinApp->mMontageController->SetBaseISXY(mBaseISX, mBaseISY);
  mMSCurIndex = -2;
  mMSTestRun = 0;
  mWinApp->UpdateBufferWindows();
  mWinApp->SetStatusText(MEDIUM_PANE, "");
}

/*
 * Compute the image shift to the given position and queue it or do it
 */
void CParticleTasks::SetUpMultiShotShift(int shotIndex, int holeIndex, BOOL queueIt)
{
  double ISX, ISY, delBTX, delBTY, delISX = 0, delISY = 0, angle, cosAng, sinAng;
  float delay, BTbacklash, rotation;
  int BTdelay;
  ComaVsISCalib *comaVsIS = mWinApp->mAutoTuning->GetComaVsIScal();

  // Compute IS from center for a peripheral shot
  rotation = (float)mShiftManager->GetImageRotation(mWinApp->GetCurrentCamera(), 
    mMagIndex);
  if (shotIndex < mMSNumPeripheral && shotIndex >= 0) {
    angle = DTOR * (shotIndex * 360. / mMSNumPeripheral + rotation);
    cosAng = cos(angle);
    sinAng = sin(angle);
    delISX = mMSRadiusOnCam * (mCamToIS.xpx * cosAng + mCamToIS.xpy * sinAng);
    delISY = mMSRadiusOnCam * (mCamToIS.ypx * cosAng + mCamToIS.ypy * sinAng);
  }

  // Get full IS and delta IS and default delay for move from last position
  // Multiply by hole delay factor if doing hole; add extra delay
  delISX += mMSHoleISX[holeIndex];
  delISY += mMSHoleISY[holeIndex];
  ISX = mBaseISX + delISX;
  ISY = mBaseISY + delISY;
  if (GetDebugOutput('1') || mMSTestRun) 
   PrintfToLog("For hole %d shot %d  %s  delIS  %.3f %.3f", holeIndex, shotIndex,
    queueIt ? "Queuing" : "Setting", delISX, delISY);
  delay = mShiftManager->ComputeISDelay(ISX - mLastISX, ISY - mLastISY);
  if (mMSUseHoleDelay)
    delay *= mMSParams->holeDelayFactor;
  if (mMSExtraDelay > 0.)
    delay += mMSExtraDelay;
  mMSUseHoleDelay = false;

  if (mMSAdjustBeamTilt) {
    BTbacklash = mWinApp->mAutoTuning->GetBeamTiltBacklash();
    BTdelay = mWinApp->mAutoTuning->GetBacklashDelay();
    delBTX = comaVsIS->matrix.xpx * delISX + comaVsIS->matrix.xpy * delISY;
    delBTY = comaVsIS->matrix.ypx * delISX + comaVsIS->matrix.ypy * delISY;
    SEMTrace('1', "%s incremental IS  %.3f %.3f   BT  %.3f  %.3f  backlash %f  delay %d", 
      queueIt ? "Queuing" : "Setting", delISX, delISY, delBTX, delBTY, BTbacklash, 
      BTdelay);
  }

  // Queue it or do it
  if (queueIt) {
    mCamera->QueueImageShift(ISX, ISY, B3DNINT(1000. * delay));
    if (mMSAdjustBeamTilt)
      mCamera->QueueBeamTilt(mCenterBeamTiltX + delBTX, mCenterBeamTiltY + delBTY, 
      BTdelay);
  } else {
    mScope->SetImageShift(ISX, ISY);
    mShiftManager->SetISTimeOut(delay);
    if (mMSAdjustBeamTilt) {
      if (BTdelay > 0) {
        mScope->SetBeamTilt(mCenterBeamTiltX + delBTX + BTbacklash, 
          mCenterBeamTiltY + delBTY + BTbacklash);
        Sleep(BTdelay);
      }
      mScope->SetBeamTilt(mCenterBeamTiltX + delBTX, mCenterBeamTiltY + delBTY);
    }
  }
  mLastISX = ISX;
  mLastISY = ISY;
}

// Given current hole and shot index, this will give the next one
bool CParticleTasks::GetNextShotAndHole(int &nextShot, int &nextHole)
{
  nextShot = mMSCurIndex + 1;
  nextHole = mMSHoleIndex;
  if (nextShot > mMSLastShotIndex) {
    nextShot = mMSDoCenter < 0 ? -1 : 0;
    nextHole++;
  }
  return (nextHole < mMSNumHoles);
}

/*
 * Start an acquisition: Set up early return, start shot, set the status pane
 */
int CParticleTasks::StartOneShotOfMulti(void)
{
  CString str;
  int nextShot, nextHole;
  int numInHole = mMSNumPeripheral + (mMSDoCenter ? 1 : 0);
  bool earlyRet = (mMSIfEarlyReturn == 1 && !GetNextShotAndHole(nextShot, nextHole)) ||
    mMSIfEarlyReturn > 1;
  if (earlyRet && mCamera->SetNextAsyncSumFrames(mMSEarlyRetFrames < 0 ? 65535 : 
    mMSEarlyRetFrames, false)) {
    StopMultiShot();
    return 1;
  }
  if (mMSTestRun & MULTI_TEST_COMA) {
    mWinApp->mAutoTuning->CtfBasedAstigmatismComa(1, 0, 1, 1);
  } else if (mMSTestRun && mWinApp->Montaging()) {
    mWinApp->mMontageController->StartMontage(MONT_NOT_TRIAL, false);
  } else {
    mCamera->InitiateCapture(RECORD_CONSET);
  }
  mWinApp->AddIdleTask(TASK_MULTI_SHOT, 0, 0);
  str.Format("DOING MULTI-SHOT %d OF %d", mMSHoleIndex * numInHole + 
    mMSCurIndex + (mMSDoCenter < 0 ? 2 : 1), numInHole * mMSNumHoles);
  mWinApp->SetStatusText(MEDIUM_PANE, str);
  return 0;
}

/*
 * Get relative image shifts of the holes into the vectors, transferred to the given
 * mag and camera
 */
int CParticleTasks::GetHolePositions(FloatVec &delISX, FloatVec &delISY, int magInd,
  int camera)
{
  int numHoles = 0, ind, ix, iy, direction[2], startInd[2], endInd[2], fromMag, jump[2];
  double xCenISX, yCenISX, xCenISY, yCenISY, transISX, transISY;
  std::vector<double> fromISX, fromISY;
  delISX.clear();
  delISY.clear();
  if (mMSParams->useCustomHoles && mMSParams->customHoleX.size() > 0) {

    // Custom holes are easy, the list is relative to the center position
    numHoles = (int)mMSParams->customHoleX.size();
    for (ind = 0; ind < numHoles; ind++) {
      fromISX.push_back(mMSParams->customHoleX[ind]);
      fromISY.push_back(mMSParams->customHoleY[ind]);
    }
    fromMag = mMSParams->customMagIndex;
  } else if (mMSParams->holeMagIndex > 0) {

    // The hole pattern requires computing position relative to center of pattern
    numHoles = mMSParams->numHoles[0] * mMSParams->numHoles[1];
    if (mMSTestRun)
      numHoles = B3DMIN(2, mMSParams->numHoles[0]) * B3DMIN(2, mMSParams->numHoles[1]);
    xCenISX = mMSParams->holeISXspacing[0] * 0.5 * (mMSParams->numHoles[0] - 1);
    xCenISY = mMSParams->holeISYspacing[0] * 0.5 * (mMSParams->numHoles[0] - 1);
    yCenISX = mMSParams->holeISXspacing[1] * 0.5 * (mMSParams->numHoles[1] - 1);
    yCenISY = mMSParams->holeISYspacing[1] * 0.5 * (mMSParams->numHoles[1] - 1);
    fromMag = mMSParams->holeMagIndex;

    // Set up to do arbitrary directions in each axis
    direction[0] = direction[1] = 1;
    startInd[0] = startInd[1] = 0;
    endInd[0] = mMSParams->numHoles[0] - 1;
    endInd[1] = mMSParams->numHoles[1] - 1;
    jump[0] = jump[1] = 1;
    if (mMSTestRun) {
      jump[0] = B3DMAX(2, mMSParams->numHoles[0]) - 1;
      jump[1] = B3DMAX(2, mMSParams->numHoles[1]) - 1;
    }
    for (iy = startInd[1]; (endInd[1] - iy) * direction[1] >= 0; 
      iy += direction[1] * jump[1]) {
      for (ix = startInd[0]; (endInd[0] - ix) * direction[0] >= 0 ; 
        ix += direction[0] * jump[0]) {
        fromISX.push_back((ix * mMSParams->holeISXspacing[0] - xCenISX) +
          (iy * mMSParams->holeISXspacing[1] - yCenISX));
        fromISY.push_back((ix * mMSParams->holeISYspacing[0] - xCenISY) +
          (iy * mMSParams->holeISYspacing[1] - yCenISY));
      }

      // For now, zigzag pattern
      ix = startInd[0];
      startInd[0] = endInd[0];
      endInd[0] = ix;
      direction[0] = -direction[0];
    }
  }

  // Transfer the image shifts and add to return vectors
  for (ind = 0; ind < numHoles; ind++) {
    mWinApp->mShiftManager->TransferGeneralIS(fromMag, fromISX[ind], fromISY[ind], magInd,
      transISX, transISY, camera);
    delISX.push_back((float)transISX);
    delISY.push_back((float)transISY);
  }
  return numHoles;
}

// Returns true and the number of the current hole and peripheral position numbered from
// 1, and 0 for the center position; or returns false if not doing multishot
bool CParticleTasks::CurrentHoleAndPosition(int &curHole, int &curPos)
{
  curHole = curPos = -1;
  if (mMSCurIndex < -1)
    return false;
  curHole = mMSHoleIndex + 1;
  if (mMSNumPeripheral)
    curPos = (mMSCurIndex + 1) % (mMSNumPeripheral + 1);
  else
    curPos = 0;
  return true;
}

///////////////////////////////////////////////////////////////////////
// WAITING FOR DRIFT

// The routine to statr it with given parameters
int CParticleTasks::WaitForDrift(DriftWaitParams &param)
{
  ControlSet *conSets = mWinApp->GetConSets();
  CameraParameters *camParam = mWinApp->GetActiveCamParam();
  int binInd, realBin, shotType = FOCUS_CONSET;

  mWDParm = param;

  // Set trial parameters
  if (param.measureType == WFD_USE_TRIAL) {
    shotType = TRACK_CONSET;
    conSets[TRACK_CONSET] = conSets[TRIAL_CONSET];
    if (param.setTrialParams && param.exposure > 0.)
      conSets[TRACK_CONSET].exposure = param.exposure;
    if (param.setTrialParams && param.binning > 0) {
      conSets[TRACK_CONSET].binning = param.binning;
      mCamera->FindNearestBinning(camParam, &conSets[TRACK_CONSET], binInd, realBin);
    }
    conSets[TRACK_CONSET].mode = SINGLE_FRAME;
  }

  // Initialize
  mWDSavedTripleMode = mFocusManager->GetTripleMode();
  mWDInitialStartTime = mWDLastStartTime = GetTickCount();
  mWDSleeping = false;
  mWaitingForDrift = 1;
  mWDLastDriftRate = -1.;
  mWDLastFailed = 5;
  mWDUpdateStatus = false;
  if (param.measureType == WFD_BETWEEN_AUTOFOC)
    mWDFocusBuffer = new EMimageBuffer;
  mWinApp->UpdateBufferWindows();
  mWinApp->SetStatusText(MEDIUM_PANE, "WAITING for Drift");

  // Take image or autofocus
  if (param.measureType == WFD_USE_TRIAL || param.measureType == WFD_USE_FOCUS) {
    mCamera->SetRequiredRoll(1);
    mCamera->InitiateCapture(shotType);
  } else {
    mFocusManager->SetTripleMode(true);
    mFocusManager->AutoFocusStart(1);
  }
  mWinApp->AddIdleTask(TASK_WAIT_FOR_DRIFT, 0, 0);
  return 0;
}

// Next task when waiting for drift
void CParticleTasks::WaitForDriftNextTask(int param)
{
  CameraParameters *camParam = mWinApp->GetActiveCamParam();
  CString str;
  double driftX, driftY, delta, pixel;
  float shiftX, shiftY;
  double elapsed = 0.001 * SEMTickInterval(mWDInitialStartTime);
  if (!mWaitingForDrift)
    return;
  if (mWDSleeping) {

    // If sleeping, now start the next measurement
    if (mWDParm.measureType == WFD_USE_TRIAL || mWDParm.measureType == WFD_USE_FOCUS)
      mCamera->InitiateCapture(mWDParm.measureType == WFD_USE_TRIAL ? 
        TRACK_CONSET : FOCUS_CONSET);
    else
      mFocusManager->AutoFocusStart(1);
    mWDLastStartTime = GetTickCount();
    mWDSleeping = false;
    mWDUpdateStatus = mWDLastDriftRate > 0;
  } else {

    // Otherwise first check for focus failure
    if ((mWDParm.measureType == WFD_WITHIN_AUTOFOC || 
      mWDParm.measureType == WFD_BETWEEN_AUTOFOC) &&
      (mFocusManager->GetLastFailed() || mFocusManager->GetLastAborted())) {
      DriftWaitFailed(3, "autofocus failed");
      return;
    }

    // If this is not the first round, get the drift
    if (mWaitingForDrift > 1) {
      if (mWDParm.measureType == WFD_WITHIN_AUTOFOC) {

        // From autofocus itself
        if (mFocusManager->GetLastDrift(driftX, driftY)) {
          DriftWaitFailed(4, "no drift value available "
            "from last autofocus ");
          return;
        }
      } else {

        // If between autofocus, copy last buffer A to B
        if (mWDParm.measureType == WFD_BETWEEN_AUTOFOC)
          mWinApp->mBufferManager->CopyImBuf(mWDFocusBuffer, &mImBufs[1]);

        // Autoalign to B, apply image shift if option set
        if (mShiftManager->AutoAlign(1, 1, mWDParm.changeIS)) {
          DriftWaitFailed(2, "autoalignment failed");
          return;
        }

        // Get the image shift then clear it, and convert to nm/sec
        pixel = mShiftManager->GetPixelSize(mImBufs);
        delta = mImBufs[0].mTimeStamp - mImBufs[2].mTimeStamp;
        if (!pixel || delta <= 0.) {
          DriftWaitFailed(4, "images are missing needed information");
          return;
        }
        mImBufs->mImage->getShifts(shiftX, shiftY);
        mImBufs->mImage->setShifts(0., 0.);
        pixel *= 1000. / delta;
        driftX = shiftX * pixel;
        driftY = shiftY * pixel;
      }

      // Drift rate: has it passed the criterion?
      mWDLastDriftRate = (float)sqrt(driftX * driftX + driftY * driftY);
      if (mWDLastDriftRate <= mWDParm.driftRate) {
        mWinApp->AppendToLog(str);
        str.Format("Drift reached %s after %.1f sec, going on",
          (LPCTSTR)FormatDrift(mWDLastDriftRate), elapsed);
        mWDLastFailed = 0;
        StopWaitForDrift();
        mWinApp->AppendToLog(str, LOG_SWALLOW_IF_CLOSED);
        return;
      }

      // Or is the time up?
      if (elapsed + mWDParm.interval / 3. > mWDParm.maxWaitTime) {
        str.Format("drift is still %s after %.0f sec", 
          (LPCTSTR)FormatDrift(mWDLastDriftRate), elapsed);
        DriftWaitFailed(1, str);
        return;
      }
    }

    // If not, copy the buffer if using autofocus images, then start sleeping
    if (mWDParm.measureType == WFD_BETWEEN_AUTOFOC)
      mWinApp->mBufferManager->CopyImBuf(&mImBufs[0], mWDFocusBuffer);
    mWDSleeping = true;
    mWaitingForDrift++;
    str = "";
    if (mWDLastDriftRate > 0) {
      str = FormatDrift(mWDLastDriftRate);
      if (mWinApp->mComplexTasks->GetVerbose() && 
        mWDParm.measureType != WFD_WITHIN_AUTOFOC)
        PrintfToLog("Elapsed time %.1f sec: drift %s", elapsed, (LPCTSTR)str);
    }
    mWinApp->SetStatusText(MEDIUM_PANE, "WAITING for Drift" + str);
  }
  mWinApp->AddIdleTask(TASK_WAIT_FOR_DRIFT, 0, 0);
}

int CParticleTasks::WaitForDriftBusy(void)
{
  CString str;
  if (mWaitingForDrift && mWDSleeping) {
    return (SEMTickInterval(mWDLastStartTime) < 1000. * mWDParm.interval) ? 1 : 0;
  }
  if (mWaitingForDrift && mWDUpdateStatus && SEMTickInterval(mWDLastStartTime) > 1500) {
    str = FormatDrift(mWDLastDriftRate);
    mWinApp->SetStatusText(MEDIUM_PANE, "WAITING for Drift" + str);
    mWDUpdateStatus = false;
  }
  return (mWaitingForDrift && (mWinApp->mCamera->CameraBusy() ||
    ((mWDParm.measureType == WFD_BETWEEN_AUTOFOC || 
      mWDParm.measureType == WFD_WITHIN_AUTOFOC) && mFocusManager->DoingFocus())));
}

// Cleanup call on error/timeout
void CParticleTasks::WaitForDriftCleanup(int error)
{
  if (error == IDLE_TIMEOUT_ERROR)
    SEMMessageBox(_T("Time out getting an image with wait for drift routine"));
  StopWaitForDrift();
  mWinApp->ErrorOccurred(error);
}

// Stop call 
void CParticleTasks::StopWaitForDrift(void)
{
  if (!mWaitingForDrift)
    return;
  mWaitingForDrift = 0;
  mWinApp->UpdateBufferWindows();
  mWinApp->SetStatusText(MEDIUM_PANE, "");
  mCamera->SetRequiredRoll(0);
  mFocusManager->SetTripleMode(mWDSavedTripleMode);
  delete mWDFocusBuffer;
  mWDFocusBuffer = NULL;
}

// Call to handle failure
void CParticleTasks::DriftWaitFailed(int type, CString reason)
{
  mWDLastFailed = type;
  reason = "Stopping the wait for drift: " + reason;
  StopWaitForDrift();
  if (mWDParm.failureAction)
    SEMMessageBox(reason, MB_EXCLAME, true);  // TODO: consult policy
  else
    mWinApp->AppendToLog(reason + "; going on");
}

// Produce properly formatted string
CString CParticleTasks::FormatDrift(float drift)
{
  CString str;
  if (mWDParm.useAngstroms)
    str.Format(" %.1f A/sec", 10. * drift);
  else
    str.Format(" %.2f nm/sec", drift);
  return str;
}
