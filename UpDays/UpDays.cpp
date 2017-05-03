#include "stdafx.h"
#include "plugin.h"
#include "stdio.h"

#include "MemLeaker.h"
#include <string>
#include <sstream>
#include <fstream>
#include <io.h>
//#include <vector>


/* ��������ڲ���һֱ���ǵĹ�Ʊ���������ַ���
   ����һ���ڵ㷨
   ��ȡUpDays.txt���ڶ������ǽڵ��գ�ÿ�������ڵĽڵ�֮������Ƿ��Ȳ������û�ָ���ķ���
   �����������ַ�
   ��ָ��ʱ�����Ϊ���Σ�ÿ�������ڵľ��ֵ�֮������Ƿ��Ȳ������û�ָ���ķ���
*/


BOOL APIENTRY DllMain( HANDLE hModule, 
                       DWORD  ul_reason_for_call, 
                       LPVOID lpReserved
					 )
{
    switch (ul_reason_for_call)
	{
		case DLL_PROCESS_ATTACH:
		case DLL_THREAD_ATTACH:
		case DLL_THREAD_DETACH:
		case DLL_PROCESS_DETACH:
			break;
    }
    return TRUE;
}

PDATAIOFUNC	 g_pFuncCallBack;

//��ȡ�ص�����
void RegisterDataInterface(PDATAIOFUNC pfn)
{
	g_pFuncCallBack = pfn;
}

//ע������Ϣ
void GetCopyRightInfo(LPPLUGIN info)
{
	//��д������Ϣ
	strcpy_s(info->Name, "һֱ����");
	strcpy_s(info->Dy, "US");
	strcpy_s(info->Author, "john");
	strcpy_s(info->Period, "����");
	strcpy_s(info->Descript, "һֱ����");
	strcpy_s(info->OtherInfo, "һֱ����");
	//��д������Ϣ
	info->ParamNum = 2;
	
	strcpy_s(info->ParamInfo[0].acParaName, "�ٷֱ�");
	info->ParamInfo[0].nMin=0;
	info->ParamInfo[0].nMax=100;
	info->ParamInfo[0].nDefault=5;

	strcpy_s(info->ParamInfo[1].acParaName, "�ȷ�");
	info->ParamInfo[1].nMin=1;
	info->ParamInfo[1].nMax=10;
	info->ParamInfo[1].nDefault=3;
}

////////////////////////////////////////////////////////////////////////////////
//�Զ���ʵ��ϸ�ں���(�ɸ���ѡ����Ҫ���)
#define DATENODE_LENGTH 256

const	BYTE	g_nAvoidMask[]={0xF8,0xF8,0xF8,0xF8};	// ��Ч���ݱ�־(ϵͳ����)

char* g_nFatherCode[] = { "999999", "399001", "399005", "399006" };

int g_FatherUpPercent[] = {-1, -1, -1, -1};

NTime g_DateNode[DATENODE_LENGTH];

const int cIgnoreStocksMaxCount = 4096;
char g_IgnoreStocks[cIgnoreStocksMaxCount][7];

bool g_bInitial = false;

const char g_UserDir[] = {".\\UserData\\"};

const char g_IgnoreKeyword[] = {"IGS_UpDays*.EBK"};


typedef enum _eFatherCode
{
	EShangHaiZZ,
	EShenZhenCZ,
	EZhongXBZZ,
	EChuangYBZZ,
	EFatherCodeMax
} EFatherCode;


BOOL fEqual(double a, double b)
{
	const double fJudge = 0.01;
	double fValue = 0.0;

	if (a > b)
		fValue = a - b;
	else 
		fValue = b - a;

	if (fValue > fJudge)
		return FALSE;

	return TRUE;
}


int dateComp(NTime& nLeft, NTime& nRight)
{
	if (nLeft.year < nRight.year)
		return -1;
	else if (nLeft.year > nRight.year)
		return 1;

	if (nLeft.month < nRight.month)
		return -1;
	else if (nLeft.month > nRight.month)
		return 1;

	if (nLeft.day < nRight.day)
		return -1;
	else if (nLeft.day > nRight.day)
		return 1;

	return 0;
}


#define DATE_LEFT_EARLY(L, R) (-1 == dateComp(L, R))

#define DATE_LEFT_LATER(L, R) (1 == dateComp(L, R))

#define DATE_EQUAL(L, R) (0 == dateComp(L, R))


NTime dateInterval(NTime nLeft, NTime nRight)
{
	NTime nInterval;
	memset(&nInterval, 0, sizeof(NTime));
	
	unsigned int iLeft = 0;
	unsigned int iRight = 0;
	unsigned int iInterval = 0;

	const unsigned int cDayofyear = 365;
	const unsigned int cDayofmonth = 30;

	iLeft = nLeft.year*cDayofyear + nLeft.month*cDayofmonth + nLeft.day;
	iRight = nRight.year*cDayofyear + nRight.month*cDayofmonth + nRight.day;

	iInterval = (iLeft > iRight) ? iLeft - iRight : iRight - iLeft;

	nInterval.year = iInterval / cDayofyear;
	iInterval = iInterval % cDayofyear;
	nInterval.month = iInterval / cDayofmonth;
	iInterval = iInterval % cDayofmonth;
	nInterval.day = iInterval;

	return nInterval;
}


/* ���˺���
   ����ֵ����S��*��ͷ�Ĺ�Ʊ ���� ���в���һ�꣬����FALSE�����򷵻�TRUE
*/
BOOL filterStock(char * Code, short nSetCode, BYTE nTQ)
{
	if (NULL == Code)
		return FALSE;

	{
		//�û�ָ���Ĺ�Ʊ��Ҫ����
		for (int iRow = 0; iRow < cIgnoreStocksMaxCount; iRow++)
		{
			if (0 == strlen(g_IgnoreStocks[iRow]))
				break;

			if (0 == strcmp(g_IgnoreStocks[iRow], Code))
			{
				OutputDebugStringA("User abandon ");
				return FALSE;
			}
		}
	}

	const unsigned short cMinYears = 2;	
	const short cInfoNum = 2;
	short iInfoNum = cInfoNum;

	{
		STOCKINFO stockInfoArray[cInfoNum];
		memset(stockInfoArray, 0, cInfoNum*sizeof(STOCKINFO));

		LPSTOCKINFO pStockInfo = stockInfoArray;

		NTime nTime;
		memset(&nTime, 0, sizeof(nTime));
		//��ȡ��Ʊ�����Լ�����ʱ��
		long readnum = g_pFuncCallBack(Code, nSetCode, STKINFO_DAT, pStockInfo, iInfoNum, nTime, nTime, nTQ, 0);
		if (readnum <= 0)
		{
			OutputDebugStringA("g_pFuncCallBack get start date error.");
			pStockInfo = NULL;
			return FALSE;
		}
		if ('S' == pStockInfo->Name[0] || '*' == pStockInfo->Name[0])
		{
			OutputDebugStringA("Begin with S or *");
			pStockInfo = NULL;
			return FALSE;
		}

		NTime startDate, todayDate, dInterval;
		memset(&startDate, 0, sizeof(NTime));
		memset(&todayDate, 0, sizeof(NTime));
		memset(&dInterval, 0, sizeof(NTime));

		long lStartDate = pStockInfo->J_start;
		startDate.year = (short)(lStartDate / 10000);
		lStartDate = lStartDate % 10000;
		startDate.month = (unsigned char)(lStartDate / 100);
		lStartDate = lStartDate % 100;
		startDate.day = (unsigned char)lStartDate;

		//��ȡ��������
		SYSTEMTIME tdTime;
		memset(&tdTime, 0, sizeof(SYSTEMTIME));
		GetLocalTime(&tdTime);

		todayDate.year = tdTime.wYear;
		todayDate.month = (unsigned char)tdTime.wMonth;
		todayDate.day = (unsigned char)tdTime.wDay;

		dInterval = dateInterval(startDate, todayDate);

		//̫����Ĺɷ���FALSE
		if (dInterval.year < cMinYears)
		{
			OutputDebugStringA("It's too young.");
			pStockInfo = NULL;
			return FALSE;
		}

		pStockInfo = NULL;
	}

	return TRUE;
}


/* ���ڵ��ж��������� */
BOOL isUpDays(char * Code, short nSetCode, short DataType, BYTE nTQ, int iRat)
{
	BOOL bUpDays = FALSE;

	//������ֹʱ��
	int iNumNodes = 0;
	for (int i = 0; i < DATENODE_LENGTH; i++)
	{
		if (0 == g_DateNode[i].year)
		{
			iNumNodes = i;
			break;
		}
	}
	if (0 == iNumNodes)
		return FALSE;

	//�������ݸ���
	long datanum = g_pFuncCallBack(Code, nSetCode, DataType, NULL, -1, g_DateNode[0], g_DateNode[iNumNodes-1], nTQ, 0);
	if ( 1 > datanum )
	{
		std::ostringstream ss;
		ss << "Failed: " << Code << " datanum = " << datanum << std::endl;
		OutputDebugStringA(ss.str().c_str());
		return FALSE;
	}

	LPHISDAT pHisDat = new HISDAT[datanum];

	long readnum = g_pFuncCallBack(Code, nSetCode, DataType, pHisDat, (short)datanum, g_DateNode[0], g_DateNode[iNumNodes-1], nTQ, 0);
	if ( 1 > readnum || readnum > datanum )
	{
		std::ostringstream ss;
		ss << "Failed: " << Code << "readnum = " << readnum << " datanum = " << datanum << std::endl;
		OutputDebugStringA(ss.str().c_str());
		delete[] pHisDat;
		pHisDat = NULL;
		return FALSE;
	}
	
	float fPreClose = 0;
	int iNode = 0;
	LPHISDAT pMoveHisDat = pHisDat;
	for (int i = 0; i < readnum; i++, pMoveHisDat++)
	{
		if (!DATE_LEFT_EARLY(pMoveHisDat->Time, g_DateNode[iNode]))
		{
			iNode++;
			if (pMoveHisDat->Close <= fPreClose)
			{
				//�۸��µ�ֱ���˳�
				break;
			}
			if (!fEqual(0, fPreClose))
			{
				int curRat = (int)((pMoveHisDat->Close - fPreClose) * 100 / fPreClose);
				if (curRat < iRat)
				{
					std::ostringstream ss;
					ss << "Ratio failed: " << Code << " Date " << (UINT)pMoveHisDat->Time.month << "-" << (UINT)pMoveHisDat->Time.day << " " << curRat << "%" << std::endl;
					OutputDebugStringA(ss.str().c_str());
					break;
				}
			}
			
			if (iNumNodes == iNode)
			{
				bUpDays = TRUE;
				break;
			}
			fPreClose = pMoveHisDat->Close;
		}
	}

	delete[] pHisDat;
	pHisDat = NULL;

	return bUpDays;
}


/* ����ʱ��ξ����ж��������� */
BOOL isUpDays(char * Code, short nSetCode, short DataType, BYTE nTQ, int iRat, int iSections, NTime time1, NTime time2)
{
	BOOL bUpDays = FALSE;

	//�������ݸ���
	long datanum = g_pFuncCallBack(Code, nSetCode, DataType, NULL, -1, time1, time2, nTQ, 0);
	if ( 1 > datanum )
	{
		std::ostringstream ss;
		ss << "Failed: " << Code << " datanum = " << datanum << std::endl;
		OutputDebugStringA(ss.str().c_str());
		return FALSE;
	}

	LPHISDAT pHisDat = new HISDAT[datanum];

	long readnum = g_pFuncCallBack(Code, nSetCode, DataType, pHisDat, (short)datanum, time1, time2, nTQ, 0);
	if ( 1 > readnum || readnum > datanum )
	{
		std::ostringstream ss;
		ss << "Failed: " << Code << "readnum = " << readnum << " datanum = " << datanum << std::endl;
		OutputDebugStringA(ss.str().c_str());
		delete[] pHisDat;
		pHisDat = NULL;
		return FALSE;
	}


	int steps = (readnum / iSections) + 1;
	int counter = 0;
	float fPreClose, fNextClose;

	LPHISDAT pWorkDat = pHisDat;
	LPHISDAT pEndDat = pHisDat + readnum - 1;

	bUpDays = TRUE;
	while (pWorkDat < pEndDat)
	{
		counter++;		

		fPreClose = pWorkDat->Close;

		pWorkDat += steps;

		if (counter == iSections || pWorkDat > pEndDat)
		{
			pWorkDat = pEndDat;
		}

		fNextClose = pWorkDat->Close;

		int curRat = (int)((fNextClose - fPreClose) * 100 / fPreClose);

		if (curRat < iRat)
		{
			bUpDays = FALSE;
			break;
		}
	}

	delete[] pHisDat;
	pHisDat = NULL;

	return bUpDays;
}


void restoreIgnoreStocks()
{
	long lFind = 0;
	struct _finddata_t fInfo;
	std::string szFind = g_UserDir;
	szFind.append(g_IgnoreKeyword);

	memset(g_IgnoreStocks, 0, sizeof(g_IgnoreStocks));

	lFind = _findfirst(szFind.c_str(), &fInfo);
	if (-1 == lFind)
		return;

	int iRow = 0;
	do{
		std::string szFilePath = g_UserDir;
		szFilePath.append(fInfo.name);
		
		std::ifstream ifs;
		ifs.open(szFilePath);
		if (!ifs.is_open())
			continue;

		while(!ifs.eof())
		{
			char szLine[64] = {0};
			ifs.getline(szLine, 63);
			std::string dataLine(szLine+1);
			if (6 <= dataLine.length())
			{
				if (iRow >= cIgnoreStocksMaxCount)
					break;
				strcpy_s(g_IgnoreStocks[iRow++], 7, dataLine.c_str());
			}
		}
		ifs.close();
		
	} while (0 == _findnext(lFind, &fInfo));
	_findclose(lFind);

	return;
}


void initDateNode()
{
	memset(g_DateNode, 0, DATENODE_LENGTH*sizeof(NTime));

	std::string szFilePath = g_UserDir;
	szFilePath.append("UpDays.txt");

	std::ifstream ifs;
	ifs.open(szFilePath);
	if (!ifs.is_open())
		return;

	int iNodePos = 0;
	while(!ifs.eof())
	{
		char szLine[128] = {0};
		ifs.getline(szLine, 127);
		std::string dataLine(szLine);

		std::size_t fPos = dataLine.find('-');
		std::size_t sPos = dataLine.rfind('-');

		if (fPos == std::string::npos || sPos == std::string::npos || fPos == sPos)
			continue;
		
		std::string szYear = dataLine.substr(0, fPos);
		std::string szMonth = dataLine.substr(fPos + 1, sPos - fPos - 1);
		std::string szDay = dataLine.substr(sPos + 1);

		NTime ntNode;
		memset(&ntNode, 0, sizeof(ntNode));

		sscanf_s(szYear.c_str(), "%d", &ntNode.year);
		sscanf_s(szMonth.c_str(), "%d", &ntNode.month);
		sscanf_s(szDay.c_str(), "%d", &ntNode.day);

		g_DateNode[iNodePos++] = ntNode;
		if (DATENODE_LENGTH == iNodePos)
			break;
	}
	ifs.close();

	return;
}


bool init()
{
	if (g_bInitial)
		return false;

	restoreIgnoreStocks();

	initDateNode();

	g_bInitial = true;

	return true;
}




BOOL InputInfoThenCalc1(char * Code,short nSetCode,int Value[4],short DataType,short nDataNum,BYTE nTQ,unsigned long unused) //��������ݼ���
{
	BOOL nRet = FALSE;

	if ((Value[0] < 0 || Value[0] > 100)
		|| NULL == Code )
	{
		OutputDebugStringA("Parameters Error!\n");
		goto endCalc2;
	}		

	if (!g_bInitial)
		init();

	int iSonRate = 0;

	/* ���������ɺ�ͣ�ƹ� */
	if (FALSE == filterStock(Code, nSetCode, nTQ))
	{
		OutputDebugStringA("===== filter stock : ");
		OutputDebugStringA(Code);
		OutputDebugStringA(" =========\n");
		goto endCalc2;
	}

	/* ������������ռ�ٷֱ� */
	if (isUpDays(Code, nSetCode, DataType, nTQ, Value[0]))
		nRet = TRUE;
	
endCalc2:
	MEMLEAK_OUTPUT();

	return nRet;
}

BOOL InputInfoThenCalc2(char * Code,short nSetCode,int Value[4],short DataType,NTime time1,NTime time2,BYTE nTQ,unsigned long unused)  //ѡȡ����
{
	BOOL nRet = FALSE;

	if ((Value[0] < 0 || Value[0] > 100)
		|| (Value[1] < 1 || Value[1] > 10)
		|| NULL == Code )
	{
		OutputDebugStringA("Parameters Error!\n");
		goto endCalc2;
	}		

	if (!g_bInitial)
		init();

	int iSonRate = 0;

	/* ���������ɺ�ͣ�ƹ� */
	if (FALSE == filterStock(Code, nSetCode, nTQ))
	{
		OutputDebugStringA("===== filter stock : ");
		OutputDebugStringA(Code);
		OutputDebugStringA(" =========\n");
		goto endCalc2;
	}

	/* �ж��������� */
	if (isUpDays(Code, nSetCode, DataType, nTQ, Value[0], Value[1], time1, time2))
		nRet = TRUE;
	
endCalc2:
	MEMLEAK_OUTPUT();

	return nRet;
}
