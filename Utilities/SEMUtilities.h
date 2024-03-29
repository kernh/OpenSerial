#ifndef SEMUTILITIES_H
#define SEMUTILITIES_H
struct FrameAliParams;
#include <string>

#define RELEASE_RETURN_ON_ERR(a, b) if (a) { AdocReleaseMutex() ; return b;}

struct MontParam;
struct CameraParameters;
class KStoreMRC;
const char *SEMCCDErrorMessage(int code);
int MontageConSetNum(MontParam *param, bool trueSet, int lowDose = -1);
int MontageLDAreaIndex(MontParam *param);
void SEMUtilInitialize();
bool UtilOKtoAllocate(int numBytes);
int UtilThreadBusy(CWinThread **threadpp, DWORD *exitPtr = NULL);
void UtilThreadCleanup(CWinThread **threadpp);
KStoreMRC *UtilOpenOldMRCFile(CString filename);
int UtilOpenFileReadImage(CString filename, CString descrip);
int UtilSaveSingleImage(CString filename, CString descrip, bool useMdoc);
void UtilRemoveFile(CString filename);
int UtilRenameFile(CString fromName, CString toName, const char *message = NULL);
void UtilSplitExtension(CString filename, CString &root, CString &ext);
void UtilSplitPath(CString fullPath, CString &directory, CString &filename);
int UtilRelativePath(std::string fromDir, std::string toDir, std::string &relPath);
int UtilStandardizePath(std::string &dir);
int UtilRelativePath(CString fromDir, CString toDir, CString &relPath);
int UtilStandardizePath(CString &dir);
void UtilAppendWithSeparator(CString &filename, CString toAdd, const char* sep);
int CreateFrameDirIfNeeded(CString &directory, CString *errStr, char debug);
void UtilBalancedGroupLimits(int numTotal, int numGroups, int groupInd, int &start, 
                         int &end);
int FindIndexForMagValue(int magval, int camera = -1, int magInd = -1);
int DLL_IM_EX MagOrEFTEMmag(BOOL GIFmode, int index, BOOL STEMcam = false);
int MagForCamera(CameraParameters *camParam, int index);
int MagForCamera(int camera, int index);
int BinDivisorI(CameraParameters *camParam);
float BinDivisorF(CameraParameters *camParam);
bool CamHasDoubledBinnings(CameraParameters *camParam);
bool UtilInvertedMagRangeLimits(BOOL EFTEM, int &lowInd, int &highInd);
bool UtilMagInInvertedRange(int magInd, BOOL EFTEM);
void ConstrainWindowPlacement(int *left, int *top, int *right, int *bottom);
void ConstrainWindowPlacement(WINDOWPLACEMENT *place);
float UtilEvaluateGpuCapability(int nx, int ny, int dataSize, bool gainNorm, bool defects,
  int sumBinning,  FrameAliParams &faParam, int numAllVsAll, int numAliFrames, 
  int refineIter, int groupSize, int numFilt, int doSpline, double gpuMemory, 
  double maxMemory, int &gpuFlags, int &deferGpuSum, bool &gettingFRC);
int UtilGetFrameAlignBinning(FrameAliParams & param, int frameSizeX, int frameSizeY);
int AdocGetMutexSetCurrent(int index);
void AdocReleaseMutex();
BOOL AdocAcquireMutex();
int CamLDParamIndex(int camera = -1);
int CamLDParamIndex(CameraParameters *camParam);
bool NewSpinnerValue(NMHDR *pNMHDR, LRESULT *pResult, int oldVal, int lowerLim,
  int upperLim, int &newVal);
bool NewSpinnerValue(NMHDR *pNMHDR, LRESULT *pResult, int lowerLim, int upperLim,
  int &newVal);
void SetDropDownHeight(CComboBox* pMyComboBox, int itemsToShow);
void UtilModifyMenuItem(int subMenuNum, UINT itemID, const char * newText);;
bool UtilCamRadiosNeedSmallFont(CButton *radio);
double UtilGoodAngle(double angle);
int UtilFindValidFrameAliParams(CameraParameters *camParam, int readMode, bool takeK3Binned, 
  int whereAlign, int curIndex, int &newIndex, CString *message);
int UtilWriteTextFile(CString fileName, CString text);
#endif
