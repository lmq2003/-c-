﻿////////////////////////////////////////////////////////////////////////////
//  
//  本程序的文件名：OS实验参考程序2008.CPP
//
////////////////////////////////////////////////////////////////////////////
//
// 程序中假定：
//   FAT[K]存储FAT表，本程序中K设为5000，其中FAT[0]存放空盘块数；
//   Disk[K][R]用于存储对应磁盘块的内容(char型)，每个盘块最多存R(=64)个字符；
//   文件目录项FCB中，文件属性fattrib=1表示“只读”，=2表示“隐藏”，=4表示“系统”；
//   文件属性fattrib=16表示“子目录”而非文件，各属性可以互相组合。
//   用户打开文件表UOF中，状态state=0表示空登记栏，=1表示“建立”，=2表示“打开”状态；
//   UOF中“文件属性”即为被打开的文件的属性，对于“只读”文件，打开后只能读，不能写。
//
//   本系统对输入的命令，除文件名和目录名区分大小写外，其余部分字母都不区分大小写。
//
////////////////////////////////////////////////////////////////////////////
//
// 本模拟文件系统，包括如下操作命令：
// dir [<目录名>]——显示路径名指定的目录内容；
// cd [<目录名>]——指定当前目录。路径中“..”表示父目录；
// md <目录名>——创建子目录；
// rd <目录名>——删除子目录；
// create <文件名>[ <文件属性>]——创建文件；
// open <文件名>——打开文件；
// write <文件名> [<位置/app>[ insert]]——写文件；
// read <文件名> [<位置m> [<字节数n>]]——读文件；
// close <文件名>——关闭文件；
// ren <原文件名> <新文件名>——文件更名；
// copy <源文件名> [<目标文件名>]——复制文件；
// closeall——关闭当前用户的所有打开的文件
// del <文件名>——删除指定的文件
// type <文件名>——显示指定文件的内容；
// undel [<目录名>]——恢复指定目录中被删除的文件
// help——显示各命令的使用格式。
// attrib <文件名> [±<属性>]——显示[修改]文件/目录属性。
// rewind <文件名>——读、写指针移到文件开头(第一个字节处)
// fseek <文件名> <位置n>——将读、写指针都移到指定位置n处。
// block <文件名>——显示文件或目录占用的盘块号。
// uof——显示用户的UOF(文件打开表)。
// prompt——提示符中显示/不显示当前目录的切换。
// fat——显示模拟磁盘的空闲块数(FAT表中0的个数)。
// check——检查核对FAT表对应的空闲块数。
// exit——结束本程序的运行。
//
////////////////////////////////////////////////////////////////////////////
#define _CRT_SECURE_NO_WARNINGS

#include <iostream>	//cout,cin
#include <iomanip>	//setw(),setiosflags()
#include <stdlib.h>		//exit(),atoi()
#include <string>		//strcpy(),_stricmp()
#include <fstream>	//文件操作用

using namespace std;
//定义若干符号常量

#define S 32			//假设最多同时打开32个文件 
#define K 5000			//假设磁盘共有5000个盘块
#define SIZE 64			//假设磁盘的大小是64字节
#define CK 8			//命令分解后的段数
#define INPUT_LEN 128	//输入缓冲区长度
#define COMMAND_LEN 11	//命令字符串长度
#define FILENAME_LEN 11	//文件名长度
#define PATH_LEN INPUT_LEN-COMMAND_LEN
#define DM 40			//恢复被删除文件表的表项数
#define FCB_COUNT 4		//后添加   FCB_COUNT= (BLOCK_SIZE / FCB_SIZE)
#define ROOT_BLOCK 1  // 假设根目录的首块号是 1

struct FCB			//定义文件目录项FCB的结构(共16个字节)
{
	char FileName[FILENAME_LEN];//文件名(1～10字节)
	char Fattrib;				//文件属性
	short int Addr;				//文件首块号
	short int Fsize;			//文件长度
};


//用于管理系统中用户打开的文件，每个用户打开的文件都会在该表中有一个对应的条目
struct UOF			//定义用户打开文件表的结构
{
	char fname[PATH_LEN];		//文件全路径名
	char attr;					//文件属性，1=只可读；0=可读写
	short int faddr;			//文件的首块号
	short int fsize;			//文件大小(字节数)
	FCB* fp;					//该文件的目录项指针
	short int state;			//状态：0=空表项；1=新建；2=打开
	short int readp;			//读指针，指向某个要读的字符位置，0=空文件，每次读取文件时，读指针会更新，指向下一个可以读取的位置
	short int writep;			//写读指针，指向某个要写读的字符位置
};

struct CurPath		//定义存储当前目录的数据结构
{
	short int fblock;			//当前目录的首块号
	char cpath[PATH_LEN];		//当前目录绝对路径字符串(全路径名)
};

struct UnDel		//恢复被删除文件表的数据结构
{
	char gpath[PATH_LEN];		//被删除文件的全路径名(不含文件名)	
	char ufname[FILENAME_LEN];	//被删除文件名
	short ufaddr;				//被删除文件名的首块号		
	short fb;					//存储被删除文件块号的第一个块号(链表头指针)	串起来
	//首块号也存于fb所指的盘块中
};

//关于恢复被删除文件问题，还可以采用类似于Windows的回收站的方法。例如可以在根目录中
//建立一个特殊的文件夹recycled (其属性为：只读、隐藏、系统)，其FCB记录结构中的成员
//Fsize，不用来存储文件长度，而用来存储一个盘块号，该盘块中存储文件长度和文件的全路
//径名(不含文件名)，这里的“全路径名”就是文件的原位置，还原文件时该信息是不可或缺的。
//dir等命令处理recycled文件夹时，与普通文件夹略有不同(因其文件长度等信息要从Fsize号
//盘块中取出，不能直接获得)。rd命令应修改成不能删除文件夹recycled，copy,move,replace
//等命令也改成不能对文件夹recycled操作。

//当用del命令删除文件时,将该文件的有关信息保存到特殊的文件夹recycled中，亦即将文件“搬”
//到回收站，文件占用的磁盘空间并不释放；恢复时工作相反。清空回收站时才释放磁盘空间。
//此方案比前述UnDel结构的方案耗费更多的磁盘空间(删除的文件仍占用磁盘空间)

int FAT[K];						//FAT表,盘块数为K
//文件系统中，每个文件的数据可能会分散存储在不同的磁盘块上。
// FAT 表通过链表的形式管理文件数据块的顺序。例如，如果文件的第一个数据块在盘块 2，
// FAT[2] 可能指向盘块 3，FAT[3] 可能指向盘块 5，直到文件的最后一个数据块，
// FAT 表中该条目为 -1（表示文件结束）。


//char (*Disk)[SIZE]=new char [K][SIZE];//定义磁盘空间，每个盘块容量为SIZE个字节
char Disk[K][SIZE];
//Disk[K][SIZE] 是一个二维字符数组，表示磁盘的存储空间，其中 K 是盘块的数量，
// SIZE 是每个盘块的大小（单位字节）。

UOF uof[S];						//用户打开文件表UOF,最多同时打开S个文件 


char comd[CK][PATH_LEN];		//分析命令时使用
//comd[CK][PATH_LEN] 是一个二维字符数组，用于存储分析命令时使用的命令字符串。
//ck 是一个常量，表示最多能够解析的命令数量，PATH_LEN 是命令路径的最大长度。

char temppath[PATH_LEN];		//临时路径(全路径)
//在一些文件操作（如文件复制、移动等）中，需要临时存储路径信息。这个数组通常用于，存储当前操作的文件路径或目标路径

//声明对象实例化
CurPath curpath;


UnDel udtab[DM];				//定义删除文件恢复表，退出系统时该表可存于文件UdTab.dat中


short Udelp = 0;					//udtab表的第一个空表项的下标，系统初始化时它为0。
//当Udelp=DM时，表示表已满，需清除最早的表项(后续表项依次前移)
short ffbp = 1;
//0号盘快中存储如下内容：
//	short ffbp;		//从该位置开始查找空闲盘快(类似循环首次适应分配)
//	short Udelp;	//udtab表的第一个空表项的下标

int dspath = 1;		//dspath=1,提示符中显示当前目录
//当 dspath = 1 时，命令行提示符将显示当前目录。
// 通常，当用户在文件系统中执行文件操作时，提示符会显示当前所在的目录路径，帮助用户确认操作的目录位置。

//函数原型说明
int CreateComd(int);			//create命令处理函数
int OpenComd(int);				//open命令处理函数
int ReadComd(int);				//read命令处理函数
int WriteComd(int);				//write命令处理函数
int CloseComd(int);				//close命令处理函数
void CloseallComd(int);			//closeaal命令处理函数, 关闭当前用户所有打开的文件
int DelComd(int);				//del命令处理函数
int UndelComd(int);				//undel命令处理函数，恢复被删除文件
int CopyComd(int);				//copy命令处理函数
int DirComd(int);				//dir命令处理函数，显示指定的文件目录——频繁使用
int CdComd(int);				//cd命令处理函数
int MdComd(int);				//md命令处理函数
int RdComd(int);				//rd命令处理函数
int TypeComd(int);				//type命令处理函数
int RenComd(int);				//ren命令处理函数
int AttribComd(int);			//attrib命令处理函数
void UofComd(void);				//uof命令处理函数
void HelpComd(void);			//help命令处理函数
int FindPath(char*, char, int, FCB*&);	//找指定目录(的首块号)
int FindFCB(char*, int, char, FCB*&);		//找指定的文件或目录
int FindBlankFCB(short s, FCB*& fcbp1);	//寻找首块号为s的目录中的空目录项
int RewindComd(int);			//rewind命令处理函数, 读、写指针移到文件开头(第一个字节处)
int FseekComd(int);				//fseek命令处理函数, 读、写指针移到文件第n个字节处
int blockf(int);				//block命令处理函数(显示文件占用的盘块号)
int delall(int);				//delall命令处理函数, 删除指定目录中的所有文件
void save_FAT(void);
void save_Disk(void);
int getblock(void);				//获得一个盘块
void FatComd(void);
void CheckComd(void);
int Check_UOF(char*);
void ExitComd(void);
bool IsName(char*);				//判断名字是否符合规则
void PromptComd(void);			//prompt命令，提示符是否显示当前目录的切换
void UdTabComd(void);			//udtab命令，显示udtab表内容
void releaseblock(short s);		//释放s开始的盘块链
int buffer_to_file(FCB* fcbp, char* Buffer);	//Buffer写入文件
int file_to_buffer(FCB* fcbp, char* Buffer);	//文件内容读到Buffer,返回文件长度
int ParseCommand(char*);		//将输入的命令行分解成命令和参数等
void ExecComd(int);				//执行命令
void ShowOprateComd();			//打印目前操作文件信息（自加）
int FcComd(int);
int ReplaceComd(int);
int BatchComd(int );
void ExecuteBufferCommands(char* buffer, int length);// 删除文件并回收空间的辅助函数
void ProcessAndExecuteCommand(char* command);// 复制文件内容到目标文件的辅助函数
//#define INIT	//决定初始化还是从磁盘读入

void main()
{

	char cmd[INPUT_LEN];			//命令行缓冲区
	int i, k;
	// 进入系统时，当前目录是根目录
	//初始化工作路径
	curpath.fblock = 1;				//当前目录(根目录)的首块号
	strcpy(curpath.cpath, "/");		//根目录的路径字符串
	
	//磁盘空间管理
#ifdef INIT

	int j;
	FCB* fcbp;
	// *********** 初始化FAT和Disk ************
	for (i = 0; i < K; i++)	//开始时所有盘块空闲
		FAT[i] = 0;		//空闲盘块标记
	FAT[0] = K - 1;			//FAT[0]中保存空闲盘块数 因为FAT[0]用来存储空闲盘数
	for (i = 1; i < 30; i++)	//构造根目录盘块链
	{
		FAT[i] = i + 1;		//初始化根目录的FAT表
		FAT[0]--;		//空盘块数减1
	}
	FAT[i] = -1;			//根目录尾标记
	FAT[0]--;			//空盘块数减1
	for (i++; i < 40; i++)
	{
		FAT[i] = -1;		//各子目录尾标记
		FAT[0]--;
	}
	// *********** 初始化磁盘空间Disk ************
	fcbp = (FCB*)Disk[1];		//将指针 fcbp 指向磁盘的第二个盘块
	j = 40 * SIZE / sizeof(FCB);		//计算可存储的目录项数
	for (i = 1; i <= j; i++)
	{
		fcbp->FileName[0] = '\0';	//初始目录树各目录中初始化为空目录项
		fcbp++;
	}
	//以下建立初始目录树中各个子目录
	fcbp = (FCB*)Disk[1];			//指向Disk[1]即第二块磁盘
	strcpy(fcbp->FileName, "bin");	//子目录bin
	fcbp->Fattrib = 16;				//表示是子目录
	fcbp->Addr = 31;					//该子目录的首盘块号是31（起始盘块号）
	fcbp->Fsize = 0;					//约定子目录的长度为0
	fcbp++;							//指向下一个目录项

	strcpy(fcbp->FileName, "usr");	//子目录usr
	fcbp->Fattrib = 16;				//表示是子目录
	fcbp->Addr = 32;					//该子目录的首盘块号是32
	fcbp->Fsize = 0;					//约定子目录的长度为0
	fcbp++;

	strcpy(fcbp->FileName, "auto");	//文件unix的目录项
	fcbp->Fattrib = 0;				//表示是普通文件
	fcbp->Addr = 0;					//该子目录的首盘块号是0，表示是空文件
	fcbp->Fsize = 0;					//该文件的长度为0
	fcbp++;

	strcpy(fcbp->FileName, "dev");	//子目录etc
	fcbp->Fattrib = 16;				//表示是子目录
	fcbp->Addr = 33;					//该子目录的首盘块号是33
	fcbp->Fsize = 0;					//约定子目录的长度为0
	fcbp = (FCB*)Disk[31];

	strcpy(fcbp->FileName, "..");	//bin的父目录对应的目录项
	fcbp->Fattrib = 16;				//表示是目录而不是文件
	fcbp->Addr = 1;					//父目录(此处是根目录)的首盘块号是1
	fcbp->Fsize = 0;					//约定子目录的长度为0
	fcbp = (FCB*)Disk[32];

	strcpy(fcbp->FileName, "..");	//usr的父目录对应的目录项
	fcbp->Fattrib = 16;				//表示是目录而不是文件
	fcbp->Addr = 1;					//父目录(此处是根目录)的首盘块号是1
	fcbp->Fsize = 0;					//约定子目录的长度为0
	fcbp++;

	strcpy(fcbp->FileName, "user");	//子目录lib
	fcbp->Fattrib = 16;				//表示是子目录
	fcbp->Addr = 34;					//该子目录的首盘块号是34
	fcbp->Fsize = 0;					//约定子目录的长度为0
	fcbp++;

	strcpy(fcbp->FileName, "lib");	//子目录user
	fcbp->Fattrib = 16;				//表示是子目录
	fcbp->Addr = 35;					//该子目录的首盘块号是35
	fcbp->Fsize = 0;					//约定子目录的长度为0
	fcbp++;

	strcpy(fcbp->FileName, "bin");	//子目录bin
	fcbp->Fattrib = 16;				//表示是子目录
	fcbp->Addr = 36;					//该子目录的首盘块号是36
	fcbp->Fsize = 0;					//约定子目录的长度为0
	fcbp = (FCB*)Disk[33];

	strcpy(fcbp->FileName, "..");	//etc的父目录对应的目录项
	fcbp->Fattrib = 16;				//表示是目录而不是文件
	fcbp->Addr = 1;					//父目录(此处是根目录)的首盘块号是1
	fcbp->Fsize = 0;					//约定子目录的长度为0
	fcbp = (FCB*)Disk[34];

	strcpy(fcbp->FileName, "..");	//lib的父目录对应的目录项
	fcbp->Fattrib = 16;				//表示是目录而不是文件
	fcbp->Addr = 32;					//父目录(此处是usr目录)的首盘块号是32
	fcbp->Fsize = 0;					//约定子目录的长度为0
	fcbp++;

	strcpy(fcbp->FileName, "lin");	//子目录liu
	fcbp->Fattrib = 16;				//表示是子目录
	fcbp->Addr = 37;					//该子目录的首盘块号是37
	fcbp->Fsize = 0;					//约定子目录的长度为0
	fcbp++;

	strcpy(fcbp->FileName, "sun");	//子目录sun
	fcbp->Fattrib = 16;				//表示是子目录
	fcbp->Addr = 38;					//该子目录的首盘块号是38
	fcbp->Fsize = 0;					//约定子目录的长度为0
	fcbp++;

	strcpy(fcbp->FileName, "ma");	//子目录fti
	fcbp->Fattrib = 16;				//表示是子目录
	fcbp->Addr = 39;					//该子目录的首盘块号是39
	fcbp->Fsize = 0;					//约定子目录的长度为0
	fcbp = (FCB*)Disk[35];

	strcpy(fcbp->FileName, "..");	//user的父目录对应的目录项
	fcbp->Fattrib = 16;				//表示是目录而不是文件
	fcbp->Addr = 32;					//父目录(此处是usr目录)的首盘块号是32
	fcbp->Fsize = 0;					//约定子目录的长度为0
	fcbp = (FCB*)Disk[36];

	strcpy(fcbp->FileName, "..");	//usr/bin的父目录对应的目录项
	fcbp->Fattrib = 16;				//表示是目录而不是文件
	fcbp->Addr = 32;					//父目录(此处是usr目录)的首盘块号是32
	fcbp->Fsize = 0;					//约定子目录的长度为0
	fcbp = (FCB*)Disk[37];

	strcpy(fcbp->FileName, "..");	//usr/lib/liu的父目录对应的目录项
	fcbp->Fattrib = 16;				//表示是目录而不是文件
	fcbp->Addr = 34;					//父目录(此处是usr/lib目录)的首盘块号是34
	fcbp->Fsize = 0;					//约定子目录的长度为0
	fcbp = (FCB*)Disk[38];

	strcpy(fcbp->FileName, "..");	//usr/lib/sun的父目录对应的目录项
	fcbp->Fattrib = 16;				//表示是目录而不是文件
	fcbp->Addr = 34;					//父目录(此处是usr/lib目录)的首盘块号是34
	fcbp->Fsize = 0;					//约定子目录的长度为0
	fcbp = (FCB*)Disk[39];

	strcpy(fcbp->FileName, "..");	//usr/lib/fti的父目录对应的目录项
	fcbp->Fattrib = 16;				//表示是目录而不是文件
	fcbp->Addr = 34;					//父目录(此处是usr/lib目录)的首盘块号是34
	fcbp->Fsize = 0;					//约定子目录的长度为0

	// *********** 初始化UnDel表 ************
	Udelp = 0;

	ffbp = 1;			//从FAT表开头查找空闲盘快

#else
//////////////////////////////////////////////////////////////////////////////////////
// ///////////////////////////////////////////////////////////////////////////////////
	// 读入文件分配表FAT
	char yn;
	ifstream ffi("E:\\操作系统作业\\mine\\FAT2008.txt", ios::in);//打开文件‪E:\操作系统作业\mine\FAT2008.txt
	if (!ffi)
	{
		cout << "Can't open E:\\操作系统作业\\mine\\FAT2008.txt!\n";
		cin >> yn;
		exit(0);
	}
	for (i = 0; i < K; i++)		//从文件‪E:\操作系统作业\mine\FAT2008.txt读入文件分配表FAT
		if (ffi)
			ffi >> FAT[i];
		else
			break;
	ffi.close();

	//读入磁盘块Disk[ ]信息
	ffi.open("E:\\操作系统作业\\mine\\Disk2008.dat", ios::binary | ios::in);
	if (!ffi)
	{
		cout << "Can't open E:\\操作系统作业\\mine\\Disk2008.dat!\n";
		cin >> yn;
		exit(0);
	}
	for (i = 0; i < K; i++)		//从文件E:\操作系统作业\mine\Disk2008.dat读入盘块内容
		if (ffi)
			ffi.read((char*)&Disk[i], SIZE);
		else
			break;
	ffi.close();

	//读入恢复删除文件表UdTab.dat信息
	ffi.open("E:\\操作系统作业\\mine\\UdTab2008.dat", ios::binary | ios::in);
	if (!ffi)
	{
		cout << "Can't open E:\\操作系统作业\\mine\\UdTab2008.dat!\n";
		cin >> yn;
		exit(0);
	}
	for (i = 0; i < DM; i++)		//从文件E:\操作系统作业\mine\Disk2008.dat读入盘块内容
		if (ffi)
			ffi.read((char*)&udtab[i], sizeof(udtab[0]));
		else
			break;
	ffi.close();

	short* pp = (short*)Disk[0];
	ffbp = pp[0];
	Udelp = pp[1];

////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////

#endif


	for (i = 0; i < S; i++)		//初始化UOF。state：0＝空表项；1＝新建；2＝打开
		uof[i].state = 0;		//初始化为空表项

	cout << "\n现在你可以输入各种操作命令.\n"
		<< "Help —— 简易帮助信息.\n"
		<< "exit —— 退出本程序.\n";
	while (1)	//循环，等待用户输入命令，直到输入“exit”结束循环，程序结束
	{			//输入命令，分析并执行命令

		while (1)
		{
			cout << "\nC:";					//显示提示符(本系统总假定是C盘)
			if (dspath)
				cout << curpath.cpath;
			cout << ">";
			cin.getline(cmd, INPUT_LEN);		//输入命令
			if (strlen(cmd) > 0)
				break;
		}
		k = ParseCommand(cmd);		//分解命令及其参数
		//comd[0]中是命令，comd[1],comd[2]...是参数		
		ExecComd(k);				//执行命令
	}
}

/////////////////////////////////////////////////////////////////

void ExecComd(int k)		//执行命令
{
	int cid;				//命令标识

	//操作命令表 
	char CmdTab[][COMMAND_LEN] = { "create","open","write","read","close",
		"del","dir","cd","md","rd","ren","copy","type","help","attrib",
		"uof","closeall","block","rewind","fseek","fat","check","exit",
		"undel","Prompt","udtab","showopt","fc","replace","move","batch"};
	int M = sizeof(CmdTab) / COMMAND_LEN;	//统计命令个数
	for (cid = 0; cid < M; cid++)			//在命令表中检索命令
		if (_stricmp(CmdTab[cid], comd[0]) == 0)//命令不区分大小写
			break;
	//以下命令函数中，命令参数通过全局变量comd[][]传递，故未出现在函数参数表中
	switch (cid)
	{
	case 0:CreateComd(k);		//create命令，建立文件
		break;
	case 1:OpenComd(k);		//open命令，打开文件 
		break;
	case 2:WriteComd(k);		//write命令，k为命令中的参数个数(命令本身除外) 
		break;
	case 3:ReadComd(k);		//read命令，读文件 
		break;
	case 4:CloseComd(k);		//close命令，关闭文件 
		break;
	case 5:DelComd(k);			//del命令，删除文件
		break;
	case 6:DirComd(k);			//dir命令 
		break;
	case 7:CdComd(k);			//cd命令 
		break;
	case 8:MdComd(k);			//md命令 
		break;
	case 9:RdComd(k);			//rd命令 
		break;
	case 10:RenComd(k);		//ren命令，文件更名 
		break;
	case 11:CopyComd(k);		//copy命令，复制文件
		break;
	case 12:TypeComd(k);		//type命令，显示文件内容(块号) 
		break;
	case 13:HelpComd();		//help命令，帮助信息 
		break;
	case 14:AttribComd(k);		//attrib命令，修改文件属性 
		break;
	case 15:UofComd();			//uof命令，显示用户的UOF(文件打开表) 
		break;
	case 16:CloseallComd(1);	//closeall命令，关闭所有文件
		break;
	case 17:blockf(k);			//block命令，显示文件的盘块号
		break;
	case 18:RewindComd(k);		//rewind命令，将读指针移到文件开头 
		break;
	case 19:FseekComd(k);		//fseek命令：将读、写指针都移到指定记录号 
		break;
	case 20:FatComd();			//fat命令
		break;
	case 21:CheckComd();		//check命令
		break;
	case 22:ExitComd();		//exit命令
		break;
	case 23:UndelComd(k);		//undel命令
		break;
	case 24:PromptComd();		//prompt命令
		break;
	case 25:UdTabComd();		//udtab命令
		break;
	case 26:ShowOprateComd();		//showopt命令
		break;
	case 27:FcComd(k);			//fc命令
		break;
	case 28:ReplaceComd(k);
		break;					//replace命令

	/*
	case 29:MoveComd(k);
		 break;					//move 命令

	*/
	case 30:BatchComd(k);
		 break;					//batch 命令

	
	default:cout << "\n命令错:" << comd[0] << endl;
	}
}

//////////////////////////////////////////////////////////////////////////////////////

void HelpComd()				//help命令，帮助信息(显示各命令格式)
{
	cout << "\n* * * * * * * * * 本系统主要的文件操作命令简述如下 * * * * * * * * * *\n\n";
	cout << left;
	cout << setw(40) << "create <文件名> [ <文件属性>]" << "——创建新文件, 文件属性是r、h或s。\n";
	cout << setw(40) << "open <文件名>" << "——打开文件，操作类型可为r、h或(与)s。\n";
	cout << setw(40) << "write <文件名> [<位置/app> [insert]]" << "——在指定位置写文件(有插入功能)。\n";
	cout << setw(40) << "read <文件名> [<位置m> [<字节数n>]]" << "——读文件，从第m字节处读n个字节。\n";
	cout << setw(40) << "close <文件名>" << "——关闭文件。\n";
	cout << setw(40) << "del <文件名>" << "——撤消(删除)文件。\n";
	cout << setw(40) << "dir [<路径名>] [|<属性>]" << "——显示当前目录。\n";
	cout << setw(40) << "cd [<路径名>]" << "——改变当前目录。\n";
	cout << setw(40) << "md <路径名> [<属性>]" << "——创建指定目录。\n";
	cout << setw(40) << "rd [<路径名>]" << "——删除指定目录。\n";
	cout << setw(40) << "ren <旧文件名> <新文件名>" << "——文件更名。\n";
	cout << setw(40) << "attrib <文件名> [±<属性>]" << "——修改文件属性(r、h、s)。\n";
	cout << setw(40) << "copy <源文件名> [<目标文件名>]" << "——复制文件。\n";
	cout << setw(40) << "type <文件名>" << "——显示文件内容。\n";
	cout << setw(40) << "rewind <文件名>" << "——将读、写指针移到文件第一个字符处。\n";
	cout << setw(40) << "fseek <文件名> <位置>" << "——将读、写指针都移到指定位置。\n";
	cout << setw(40) << "block <文件名>" << "——显示文件占用的盘块号。\n";
	cout << setw(40) << "closeall" << "——关闭当前打开的所有文件。\n";
	cout << setw(40) << "uof" << "——显示UOF(用户打开文件表)。\n";
	cout << setw(40) << "undel [<路径名>]" << "——恢复指定目录中被删除的文件。\n";
	cout << setw(40) << "exit" << "——退出本程序。\n";
	cout << setw(40) << "prompt" << "——提示符是否显示当前目录(切换)。\n";
	cout << setw(40) << "fat" << "——显示FAT表中空闲盘块数(0的个数)。\n";
	cout << setw(40) << "check" << "——核对后显示FAT表中空闲盘块数。\n";
	cout << setw(40) << "showopt" << "——显示当前正在操作文件。\n";
	cout << setw(40) << "fc <文件名1> <文件名2>" << "——逐个字节比较指定的两个文件。\n";
	cout << setw(40) << "replace <文件名> <文件名>" << "——以“文件名”指定的文件，取代“目录名”指定目录中的同名文件。\n";
	cout << setw(40) << "batch <batchfile>" << "——从磁盘文件batchfile逐行读入命令行执行直到batchfile中所有命令行都执行完毕为止。\n";
}


/////////////////////////////////////////////////////////////////


int GetAttrib(char* str, char& attrib)				//解析和设置文件属性参数
{
	int i, len;
	char ar = '\01', ah = '\02', as = '\04';
	if (str[0] != '|')
	{
		cout << "\n命令中属性参数错误。\n";
		return -1;
	}
	len = strlen(str);
	_strlwr(str);		//转换成小写字母
	for (i = 1; i < len; i++)
	{
		switch (str[i])
		{
		case 'r': attrib = attrib | ar;		//此部分可执行按位与操作
			break;
		case 'h': attrib = attrib | ah;
			break;
		case 's': attrib = attrib | as;
			break;
		default: cout << "\n命令中属性参数错误。\n";
			return -1;
		}
	}
	return 1;
}

/////////////////////////////////////////////////////////////////

int DirComd(int k)	//dir命令，显示指定目录的内容（文件名或目录名等）
{
	// 命令形式：dir[ <目录名>[ <属性>]]
	// 命令功能：显示"目录名"指定的目录中文件名和第一级子目录名。若指
	// 定目录不存在，则给出错误信息。如果命令中没有指定目录名，则显示
	// 当前目录下的相应内容。若命令中无"属性"参数，则显示指定目录中"非
	// 隐藏"属性的全部文件名和第一级子目录名；若命令中有"属性"参数，则
	// 仅显示指定属性的文件名和目录名。h、r或s或两者都有，则显示隐藏属
	// 性或只读属性或既是隐藏又是只读属性的文件。属性参数的形式是"|<属
	// 性符号>"，其中属性符号有r、h和s三种（不区分大小写），分别表示"只
	// 读"、"隐藏"和"系统"三种属性,它们可以组合使用且次序不限。例如"|rh"
	// 和"|hr"都表示要求显示同时具有"只读"和"隐藏"属性的文件和目录名。显
	// 示文件名时，显示该文件长度；显示目录名时，同时显示"<DIR>"的字样。

	// 举例：
	//		dir /usr |h	 
	// 上述命令显示根目录下usr子目录中全部"隐藏"属性的文件名和子目录名
	//		dir ..		 
	// 上述命令显示当前目录的父目录中全部"非隐藏"属性的文件和子目录名(包
	// 括"只读"属性的也显示，但一般不显示"系统"属性的，因为"系统"属性的对
	// 象一般也是"隐藏"属性的)。
	//
	// 学生可考虑将此函数修改成命令中的路径的最后允许是文件名的情况。
	// 另外还可以考虑含通配符的问题。

	short i, s;
	short filecount, dircount, fsizecount;	//文件数、目录数、文件长度累计
	char ch, attrib = '\0', attr, cc;
	FCB* fcbp, * p;

	filecount = dircount = fsizecount = 0;
	if (k > 1)	//命令中多于1个参数，错误(较复杂的处理应当允许有多个参数)
	{
		cout << "\n命令错误：参数太多。\n";
		return -1;
	}
	if (k < 1)	//命令无参数，显示当前目录
	{
		strcpy(temppath, curpath.cpath);
		s = curpath.fblock;	//当前目录的首块号保存于s
	}

	else if (k == 1)		//命令有1个参数(k=1)
	{
		if (comd[1][0] == '|')
		{
			i = GetAttrib(comd[1], attrib);
			if (i < 0) return i;
			strcpy(temppath, curpath.cpath);
			s = curpath.fblock;	//当前目录的首块号保存于s
		}
		else
		{
			s = FindPath(comd[1], '\020', 1, fcbp);	//找指定目录(的首块号)
			if (s < 1)
			{
				cout << "\n输入的路径错误！" << endl;
				return -1;
			}
		}
	}
	else		//命令有2个参数(k=2)
	{
		s = FindPath(comd[1], '\020', 1, fcbp);	//找指定目录(的首块号)
		if (s < 1)
		{
			cout << "\n输入的路径错误！" << endl;
			return -1;
		}
		i = GetAttrib(comd[2], attrib);
		if (i < 0) return i;
	}
	cout << "\nThe Directory of C:" << temppath << endl << endl;
	while (s > 0)
	{
		p = (FCB*)Disk[s];	//p指向该目录的第一个盘块
		for (i = 0; i < 4; i++, p++)
		{
			ch = p->FileName[0];	//取文件(目录)名的第一个字符
			if (ch == (char)0xe5)		//空目录项
				continue;
			if (ch == '\0')		//已至目录尾部
				break;
			attr = p->Fattrib & '\07';	//不考虑文件还是目录,只考虑属性
			if (attrib == 0)			//命令中没有指定属性
			{
				if (attr & '\02')		//不显示“隐藏”属性文件
					continue;
			}
			else
			{
				cc = attr & attrib;
				if (attrib != cc)		//只显示指定属性的文件
					continue;
			}
			cout << setiosflags(ios::left) << setw(20) << p->FileName;
			if (p->Fattrib >= '\20')	//是子目录
			{
				cout << "<DIR>\n";
				dircount++;
			}
			else
			{
				cout << resetiosflags(ios::left);
				cout << setiosflags(ios::right) << setw(10) << p->Fsize << endl;
				filecount++;
				fsizecount += p->Fsize;
			}
		}
		if (ch == '\0') break;
		s = FAT[s];		//指向该目录的下一个盘块
	}
	cout << resetiosflags(ios::left) << endl;
	cout << setiosflags(ios::right) << setw(6) << filecount << " file(s)";
	cout << setw(8) << fsizecount << " bytes" << endl;
	cout << setw(6) << dircount << " dir(s) " << setw(8) << SIZE * FAT[0];
	cout << " free" << endl;
	return 1;
}

/////////////////////////////////////////////////////////////////

int CdComd(int k)
{
	// 当前目录（工作目录）转移到指定目录下。指定目录不存在时，给出错误信息。
	// 若命令中无目录名，则显示当前目录路径。

	short i, s;
	char attrib = (char)16;
	FCB* fcbp;
	if (k > 1)	//命令中多于1个参数，错误
	{
		cout << "\n命令错误：参数太多。\n";
		return -1;
	}
	if (k < 1)	//命令无参数，显示当前目录
	{
		cout << "\nThe Current Directory is C:" << curpath.cpath << endl;
		return 1;
	}
	else		//命令有一个参数，将指定目录作为当前目录
	{
		i = strlen(comd[1]);
		if (i > 1 && comd[1][i - 1] == '/')	//路径以"/"结尾，错误
		{
			cout << "\n路径名错误！\n";
			return -1;
		}
		s = FindPath(comd[1], attrib, 1, fcbp);	//找指定目录(的首块号)
		if (s < 1)
		{
			cout << "\n路径名错误！" << endl;
			return -1;
		}
		curpath.fblock = s;
		strcpy(curpath.cpath, temppath);
		if (!dspath)
			cout << "\n当前目录变为 C:" << curpath.cpath << endl;
		return 1;
	}
}

/////////////////////////////////////////////////////////////////

int M_NewDir(char* Name, FCB* p, short fs, char attrib)	//在p位置创建一新子目录
{
	//成功返回新子目录的首块号

	short i, b, kk;
	FCB* q;
	kk = SIZE / sizeof(FCB);
	b = getblock();		//新目录须分配一磁盘块用于存储目录项“..”
	if (b < 0)
		return b;
	strcpy(p->FileName, Name);	//目录名
	p->Fattrib = attrib;			//目录项属性为目录而非文件
	p->Addr = b;					//该新目录的首块号
	p->Fsize = 0;					//子目录的长度约定为0
	q = (FCB*)Disk[b];
	for (i = 0; i < kk; i++, q++)
		q->FileName[0] = '\0';	//置空目录项标志*/
	q = (FCB*)Disk[b];
	strcpy(q->FileName, "..");	//新目录中的第一个目录项名是“..”
	q->Fattrib = (char)16;		//目录项属性为目录而非文件
	q->Addr = fs;					//该目录的首块号是父目录的首块号
	q->Fsize = 0;					//子目录的长度约定为0
	return b;					//成功创建，返回
}

/////////////////////////////////////////////////////////////////

int ProcessPath(char* path, char*& Name, int k, int n, char attrib)
{
	
	short i, len, s;
	FCB* fcbp;

	if (n && k != n) {    // n=0, 参数个数k任意, n>0, 必须k=n
		cout << "\n命令参数个数错误！\n";
		return -1;
	}

	len = strlen(path);		//计算路径字符串长度

	// 检查路径是否为绝对路径（以 C:/ 或 / 开头）
	if (path[1] == ':' && path[2] == '/') {
		// 绝对路径，以 C:/ 开头
		s = ROOT_BLOCK;  // 绝对路径从根目录开始
		strcpy(temppath, "/");
	}
	else if (path[0] == '/') {
		// 根目录开头的路径
		s = ROOT_BLOCK;  // 绝对路径从根目录开始
		strcpy(temppath, "/");
	}
	else {
		// 相对路径
		s = curpath.fblock;  // 从当前路径开始，利用此函数获取当前路径的目录块号
		strcpy(temppath, curpath.cpath);
		if (temppath[strlen(temppath) - 1] != '/') {
			strcat(temppath, "/");
		}
	}

	// 找到最后一个目录分隔符 '/' 提取目录名
	for (i = len - 1; i >= 0; i--) {
		if (path[i] == '/') {
			break;
		}
	}

	Name = &path[i + 1];  // 取路径中最后一个名字

	//如果路径中包含 /（即有父目录），需要查找父目录：
	if (i != -1) {
		path[i] = '\0';
		s = FindPath(path, attrib, 1, fcbp);
		if (s < 1) {
			cout << "\n路径名错误！\n";
			return -3;
		}
	}

	return s;
}



/////////////////////////////////////////////////////////////////

int MdComd(int k)		//md命令处理函数
{
	// 命令形式：md <目录名>
	// 功能：在指定路径下创建指定目录，若没有指定路径，则在当前目录下创建指定目录。
	// 对于重名目录给出错误信息。目录与文件也不能重名。

	// 命令中加“属性”参数，用于创建指定属性的子目录。命令形式如下：
	//		md <目录名>|[ <属性>]
	// 属性包括R、H、S以及它们的组合(不区分大小写，顺序也不限)。例如：
	//		md user rh
	// 其功能是在当前目录中创建具有“只读”和“隐藏”属性的子目录user。

	short i, s, s0, kk;
	char attrib = (char)16, * DirName;	// attrib表示目录的属性，16表示目录属性
	FCB* p;			//文件控制块指针，用来处理目录项

	kk = SIZE / sizeof(FCB);		// ？有用吗？

	if (k < 1)
	{
		cout << "\n错误：命令中没有目录名。\n";
		return -1;
	}
	if (k > 2)
	{
		cout << "\n错误：命令参数太多。\n";
		return -1;
	}

	s = ProcessPath(comd[1], DirName, k, 0, attrib);	//获取路径名，首块号

	if (s < 0)			//路径处理失败
		return s;		//失败，返回

	if (!IsName(DirName))		//若名字不符合规则
	{
		cout << "\n命令中的新目录名错误。\n";
		return -1;
	}

	i = FindFCB(DirName, s, attrib, p);			//检查目录DirName是否已存在

	//若目录已存在
	if (i > 0)
	{
		cout << "\n错误：目录重名！\n";
		return -1;
	}
	
	//
	if (k == 1)
	{
		//使用默认参数
		//char attrib = (char)16;  // 16代表目录属性


	}

	if (k == 2)		//命令形式：md <目录名> |<属性符>
	{
		char attr = '\0';//用于存储解析后的属性值
		int ret = GetAttrib(comd[2], attr);
		if (ret == -1) return ret; // 如果属性解析失败，返回错误
		attrib |= attr; // 将解析的属性值合并到目录属性中
	}
	s0 = FindBlankFCB(s, p);//找空白目录项
	if (s0 < 0)			//磁盘满
		return s0;
	s0 = M_NewDir(DirName, p, s, attrib);	//在p所指位置创建一新子目录项
	if (s0 < 0)		//创建失败
	{
		cout << "\n磁盘空间已满，创建目录失败。\n";
		return -1;
	}
	return 1;		//新目录创建成功，返回
}

/////////////////////////////////////////////////////////////////

int RdComd(int k)
{
	// 若指定目录为空，则删除之，否则，给出"非空目录不能删除"的提示。
	// 不能删除当前目录。

	short i, j, count = 0, fs, s0, s;
	char attrib = (char)16, * DirName;
	FCB* p, * fcbp;
	fs = ProcessPath(comd[1], DirName, k, 1, attrib);	//返回DirName的父目录的首块号
	if (fs < 0)
		return fs;				//失败，返回
	s0 = s = FindFCB(DirName, fs, attrib, fcbp);//取DirName的首块号
	if (s < 1)
	{
		cout << "\n要删除的目录不存在。\n";
		return -1;
	}
	if (s == curpath.fblock)
	{
		cout << "\n不能删除当前目录。\n";
		return 0;
	}
	while (s > 0)		//循环查找，直到目录尾部
	{
		p = (FCB*)Disk[s];
		for (i = 0; i < 4; i++, p++)
		{
			if (p->FileName[0] != (char)0xe5 && p->FileName[0] != '\0')//累计非空目录项
				count++;
		}
		//s0=s;			//记下上一个盘块号
		s = FAT[s];		//取下一个盘块号
	}
	if (count > 1)
	{
		cout << "\n目录" << DirName << "非空，不能删除。\n";
		return -1;
	}
	//s0=fcbp->Addr;		//取DirName的首块号
	while (s0 > 0)			//归还目录DirName所占的磁盘空间
	{
		s = FAT[s0];			//记下第s0块的后续块号		
		FAT[s0] = 0;			//回收第s0块
		FAT[0]++;			//空闲盘块数增1
		s0 = s;				//后续块号赋予s0
	}
	fcbp->FileName[0] = (char)0xe5;	//删除DirName的目录项
	if (strcmp(temppath, "/") == 0)	//所删除的子目录在根目录
		return 1;
	//所删除的子目录DirName不在根目录时，对其父目录作以下处理
	s0 = s = fs;				//取DirName父目录的首块号
	while (s > 0)				//整理DirName的父目录空间(回收无目录项的盘块)
	{
		p = (FCB*)Disk[s];
		for (j = i = 0; i < 4; i++, p++)
			if (p->FileName[0] != (char)0xe5 && p->FileName[0] != '\0')//累计非空目录项
				j++;
		if (j == 0)
		{
			FAT[s0] = FAT[s];		//调整指针
			FAT[s] = 0;			//回收s号盘块
			FAT[0]++;			//空闲盘块数增1
			s = FAT[s0];
		}
		else
		{
			s0 = s;				//记下上一个盘块号
			s = FAT[s];			//s指向下一个盘块
		}
	}
	return 1;
}

/////////////////////////////////////////////////////////////////

int TypeComd(int k)		//type命令处理函数(显示文件内容)
{
	// 显示文件内容：type <文件名>，显示指定文件的内容。
	// 若指定文件不存在，则给出错误信息。

	short i, s, size, jj = 0;
	short i_uof;
	char attrib = '\0', * FileName;
	char* Buffer;
	char gFileName[PATH_LEN];	//存放文件全路径名
	FCB* fcbp;

	if (k < 1)
	{
		for (i_uof = S - 1; i_uof >= 0; i_uof--)
		{
			if (uof[i_uof].state != 0)
			{
				break;
			}
		}
		if (i_uof < 0)
		{
			cout << "\n无当前操作文件可显示。" << endl;
			return -1;
		}
		s = uof[i_uof].faddr;
		fcbp = uof[i_uof].fp;
	}
	else {
		s = ProcessPath(comd[1], FileName, k, 0, '\020');//取FileName所在目录的首块号
		if (s < 1)			//路径错误
			return s;		//失败，返回
		s = FindFCB(FileName, s, attrib, fcbp);		//取FileName的首块号(查其存在性)
		strcpy(gFileName, temppath);
		i = strlen(temppath);
		if (temppath[i - 1] != '/')
			strcat(gFileName, "/");
		strcat(gFileName, FileName);	//构造文件的全路径名
		if (s < 0)
		{
			cout << "\n文件" << gFileName << "不存在。\n";
			return -3;
		}
		if (s == 0)
			cout << "\n文件" << gFileName << "是空文件\n";
	}
	size = fcbp->Fsize;
	Buffer = new char[size + 1];		//分配动态内存空间
	while (s > 0)
	{
		for (i = 0; i < SIZE; i++, jj++)
		{
			if (jj == size)
				break;
			Buffer[jj] = Disk[s][i];
		}
		if (i < SIZE)
			break;
		s = FAT[s];
	}
	Buffer[jj] = '\0';
	cout << Buffer << endl;
	delete[] Buffer;		//释放分配的动态内存空间
	return 1;
}


/////////////////////////////////////////////////////////////////

int blockf(int k)	//block命令处理函数(显示文件或目录占用的盘块号)
{
	short s;
	char attrib = '\040';		//32表示任意(文件或子目录)目录项都可以
	FCB* fcbp;

	if (k != 1)
	{
		cout << "\n命令中参数个数错误。\n";
		return -1;
	}
	s = FindPath(comd[1], attrib, 1, fcbp);	//找指定目录(的首块号)
	if (s < 1)
	{
		cout << "\n路径名错误！" << endl;
		return -2;
	}
	cout << "\n" << temppath << "占用的盘块号为：";
	while (s > 0)
	{
		cout << s << "  ";
		s = FAT[s];
	}
	cout << endl;
	return 1;
}

/////////////////////////////////////////////////////////////////

void Put_UOF(char* gFileName, int i, short status, FCB* fcbp)
{
	strcpy(uof[i].fname, gFileName);	//复制文件全路径名
	uof[i].attr = fcbp->Fattrib;		//复制文件属性
	uof[i].faddr = fcbp->Addr;		//文件的首块号(0代表空文件)
	uof[i].fsize = fcbp->Fsize;
	uof[i].fp = fcbp;
	uof[i].state = status;					//打开状态
	if (fcbp->Fsize > 0)				//若文件非空
		uof[i].readp = 1;				//读指针指向文件开头
	else
		uof[i].readp = 0;				//读指针指向空位置
	uof[i].writep = fcbp->Fsize + 1;	//写指针指向文件末尾
}

/////////////////////////////////////////////////////////////////

int FindBlankFCB(short s, FCB*& fcbp1)	//寻找首块号为s的目录中的空目录项
{
	short i, s0;
	while (s > 0)			//在首块号为s的目录找空登记栏，直到目录尾部
	{
		fcbp1 = (FCB*)Disk[s];
		for (i = 0; i < 4; i++, fcbp1++)
			if (fcbp1->FileName[0] == (char)0xe5 || fcbp1->FileName[0] == '\0')
			{
				fcbp1->Addr = fcbp1->Fsize = 0;		//假设为空目录项
				return 1;						//找到空目录项，成功返回
			}
		s0 = s;		//记下上一个盘块号
		s = FAT[s];	//取下一个盘块号
	}
	if (strcmp(temppath, "/") == 0)	//若是根目录
	{
		cout << "\n根目录已满，不能再创建目录项。\n";
		return -1;
	}
	s = getblock();	//取一空闲盘快
	if (s < 0)		//无空闲盘快
	{
		cout << "\n磁盘空间已满，创建目录失败。\n";
		return -1;
	}
	FAT[s0] = s;		//构成FAT链
	fcbp1 = (FCB*)Disk[s];
	for (i = 0; i < 4; i++, fcbp1++)
		fcbp1->FileName[0] = '\0';	//置空目录标志
	fcbp1 = (FCB*)Disk[s];
	fcbp1->Addr = fcbp1->Fsize = 0;		//假设为空目录项
	return 1;
}

/////////////////////////////////////////////////////////////////

int CreateComd(int k)		//create命令处理函数：建立新文件  
{	
	//接受一个整数参数k，这个参数代表命令行参数的数量
	// 创建文件：create <文件名> [<文件属性>]，创建一个指定名字的新文件，
	// 即在目录中增加一目录项，不考虑文件的内容。对于重名文件给出错误信息。

	short i, i_uof, s0, s;			//用于循环和存储中间结果
	char attrib = '\0', * FileName;	//声明了一个字符变量attrib用于存储文件属性，和一个字符指针FileName。
	char gFileName[PATH_LEN];	//存放文件全路径名
	char ch, * p;
	FCB* fcbp1;
	if (k > 2 || k < 1)					//参数检查
	{
		cout << "\n命令中参数个数不对。\n";
		return -1;
	}
	//调用ProcessPath函数处理第一个命令行参数，获取文件名和所在目录的首块号。
	s = ProcessPath(comd[1], FileName, k, 0, '\020');//取FileName所在目录的首块号
	if (s < 1)			//路径错误
		return s;		//失败，返回
	//检查文件名是否合法，调用IsName
	if (!IsName(FileName))		//若名字不符合规则
	{
		cout << "\n命令中的新文件名错误。\n";
		return -2;
	}
	// 调用FindFCB函数查找文件控制块，检查文件是否已存在
	s0 = FindFCB(FileName, s, attrib, fcbp1);	//取FileName的首块号(查其存在性)
	//如果s0大于0，表示文件已存在，输出错误信息并返回-2
	if (s0 > 0)
	{
		cout << "\n有同名文件，不能建立。\n";
		return -2;
	}
	//构造全局路径
	strcpy(gFileName, temppath);
	i = strlen(temppath);
	if (temppath[i - 1] != '/')
		strcat(gFileName, "/");
	strcat(gFileName, FileName);	//构造文件的全路径名

	//处理第二个命令行参数，即文件属性。根据属性设置attrib的值
	if (k == 2)
	{
		p = comd[2];
		while (*p != '\0')	//处理文件属性
		{
			ch = *p;
			ch = tolower(ch);
			switch (ch)
			{
			case 'r': attrib = attrib | (char)1;
				break;
			case 'h': attrib = attrib | (char)2;
				break;
			case 's': attrib = attrib | (char)4;
				break;
			default: cout << "\n输入的文件属性错误。\n";
				return -3;
			}
			p++;
		}
	}
	//循环查找UOF中的空表项
	for (i_uof = 0; i_uof < S; i_uof++)			//在UOF中找空表项
		if (uof[i_uof].state == 0)
			break;
	if (i_uof == S)
	{
		cout << "\nUOF已满，不能创建文件。\n";
		return -4;
	}
	i = FindBlankFCB(s, fcbp1);			//寻找首块号为s的目录中的空目录项
	if (i < 0)
	{
		cout << "\n创建文件失败。\n";
		return i;
	}
	//保存文件信息到目录项，将文件名和属性保存到找到的空目录项中
	strcpy(fcbp1->FileName, FileName);	//目录项中保存文件名
	fcbp1->Fattrib = attrib;				//复制文件属性
	fcbp1->Addr = 0;						//空文件首块号设为0
	fcbp1->Fsize = 0;						//空文件长度为0
	Put_UOF(gFileName, i_uof, 1, fcbp1);	//建立UOF登记项
	cout << "\n文件" << gFileName << "建立成功\n";
	return 1;							//文件创建成功，返回
}

/////////////////////////////////////////////////////////////////

int Check_UOF(char* Name)		//检查UOF中有无命令中指定的文件
{
	int i;
	for (i = 0; i < S; i++)			//查用户打开文件表UOF
	{
		if (uof[i].state == 0)	//空表项
			continue;
		if (strcmp(Name, uof[i].fname) == 0)	//找到
			break;
	}
	return i;
}

/////////////////////////////////////////////////////////////////

int OpenComd(int k)			//open命令处理函数：打开文件 
{
	// 命令形式：open <文件名>
	// 若指定文件存在且尚未打开，则打开之，并在用户打开文件表（UOF）中登
	// 记该文件的有关信息。若指定文件已经打开，则显示"文件已打开"的信息；
	// 若指定文件不存在，则给出错误信息。只读文件打开后只能读不能写。

	short i, s0, s;
	char attrib = '\0', * FileName;
	char gFileName[PATH_LEN];	//存放文件全路径名
	FCB* fcbp;

	s0 = ProcessPath(comd[1], FileName, k, 1, '\20');//取FileName所在目录的首块号
	if (s0 < 1)			//路径错误
		return s0;		//失败，返回
	s = FindFCB(FileName, s0, attrib, fcbp);		//取FileName的首块号(查其存在性)
	if (s < 0)
	{
		cout << "\n要打开的文件不存在。\n";
		return -2;
	}
	strcpy(gFileName, temppath);
	i = strlen(temppath);
	if (temppath[i - 1] != '/')
		strcat(gFileName, "/");
	strcat(gFileName, FileName);	//构造文件的全路径名
	i = Check_UOF(gFileName);		//查UOF
	if (i < S)					//该文件已在UOF中
	{
		cout << "\n文件" << gFileName << "原先已经打开!\n";
		return -3;
	}
	for (i = 0; i < S; i++)			//在UOF中找空表项
		if (uof[i].state == 0)
			break;
	if (i == S)
	{
		cout << "\nUOF已满，不能打开文件。\n";
		return -4;
	}
	Put_UOF(gFileName, i, 2, fcbp);
	cout << "\n文件" << gFileName << "打开成功。\n";
	return 1;
}

/////////////////////////////////////////////////////////////////

int getblock()	//获得一个空闲盘块，供fappend()函数调用
{
	short b;
	if (FAT[0] == 0)	//FAT[0]中是磁盘空闲块数
		return -1;	//磁盘已满(已无空闲盘块)
	for (b = ffbp; b < K; b++)
		if (!FAT[b])
			break;
	if (b == K)
	{
		for (b = 1; b < ffbp; b++)
			if (!FAT[b]) break;
	}
	ffbp = b + 1;
	if (ffbp == K) ffbp = 1;
	FAT[0]--;	//盘块数减1
	FAT[b] = -1;	//置盘块已分配标志(此处不妨假设其为文件尾)
	return b;	//返回取得的空闲盘块号
}



///////////////////////////////////////////////////////////////
int WriteComd(int k)		//write命令的处理函数
{
	// 写文件：write <文件名> [<位置>[ insert]]，命令中若无"位置"参数，则在写指
	// 针所指位置写入文件内容；若提供"位置"参数，则在对应位置写入内容。位置可以
	// 是整数n，是指在文件的第n个字节处开始写入(位置从1开始编号)。"位置" 还可以
	// 是 "append"（前3个字符有效，不区分大小写），表示在文件尾部写入信息；若有
	// 参数 "insert"（前3个字符有效，不区分大小写），则新写入的内容插入到对应位
	// 置，对应位置开始的原内容后移。若无参数 "insert" ，写入的内容代替文件原先
	// 的内容(对应位置的内容)。写入完毕调整文件长度和写指针值。
	// 若文件未打开或文件不存在，分别给出错误信息。

	// 可以有如下几种命令形式：
	//	write <文件名> ——在写指针当前所指位置写，写入内容代替原内容(代替方式或改写方式)
	//	write <文件名> <n>——在文件开头第n个字节处写，改写方式
	//	write <文件名> insert——在写指针所指位置写，写入处开始的原内容后移(插入方式)
	//	write <文件名> <n> insert——在文件开头第n个字节处写，插入方式
	//	write <文件名> append——在文件尾部写(添加方式)

	//【思考】如何使参数“insert”、“append”只要前3个字符对就可以，但多于3个字符也行。例如：
	// 对于“insert”，输入ins、inse、inser、insert(不区分大小写)都可以，输入其它不行。

#define BSIZE 40*SIZE+1
	short int ii, ii_uof, len0, len, len1, pos, ins = 0;
	short int bn0, bn1, jj, count = 0;
	char attrib = '\0', Buffer[BSIZE];		//为方便计，假设一次最多写入2560字节
	char* buf;
	FCB* fcbp;

	short int length_to_write = -1; // 新增变量来存储要写入的长度

	if (k < 1)
	{
		cout << "\n命令中没有文件名。\n";
		return -1;
	}
	FindPath(comd[1], attrib, 0, fcbp);	//构成全路径且去掉“..”存于temppath中
	ii_uof = Check_UOF(temppath);			//查UOF
	if (ii_uof == S)
	{
		cout << "\n文件" << temppath << "未打开或不存在，不能写文件。\n";
		return -2;
	}
	if (uof[ii_uof].attr & '\01' && uof[ii_uof].state != 1)
	{	//只读文件不是创建状态不能写
		cout << "\n" << temppath << "是只读文件，不能写。\n";
		return -3;
	}
	if (k == 1)
		pos = uof[ii_uof].writep;	//从写指针所指位置开始写(write <文件名>)
	else		//k=2或3
	{
		if (_strnicmp(comd[2], "app", 3) == 0)
			pos = uof[ii_uof].fsize + 1;	//文件尾部添加模式(write <文件名> append)
		else if (_strnicmp(comd[2], "ins", 3) == 0)
		{
			pos = uof[ii_uof].writep;	//从当前写指针位置开始写
			ins = 1;					//插入模式(write <文件名> insert)
		}
		else
		{
			pos = atoi(comd[2]);		//从命令中指定位置写(write <文件名> <n>)
			if (pos <= 0)
			{
				cout << "\n命令中提供的写入位置错误。\n";
				return -4;
			}
			if (k == 3)
			{
				if (_strnicmp(comd[3], "ins", 3) == 0)
					ins = 1;			//插入模式(write <文件名> <n> insert)
				else
				{
					cout << "\n命令参数" << comd[2] << "," << comd[3] << "错误\n";
					return -5;
				}
			}
		}
	}
	if (pos <= 0)
	{
		cout << "\n命令中提供的写入位置错误。\n";
		return -1;
	}
	if (pos >= uof[ii_uof].fsize + 1)
	{
		pos = uof[ii_uof].fsize + 1;
		ins = 0;						//这种情况不会是插入方式
	}

	pos--;							//使pos从0开始

	cout << "\n请输入写入文件的内容(最多允许输入" << sizeof(Buffer) - 1 << "个字节)：\n";
	cin.getline(Buffer, BSIZE);
	len1 = strlen(Buffer);
	if (len1 == 0)			//输入长度为0,不改变文件
		return 0;
	fcbp = uof[ii_uof].fp;
	len0 = uof[ii_uof].fsize;				//取文件原来的长度值
	if (len0 == 0)						//若是空文件
	{
		ii = buffer_to_file(fcbp, Buffer);
		if (ii == 0)	//写文件失败
			return ii;
		uof[ii_uof].fsize = uof[ii_uof].fp->Fsize;
		uof[ii_uof].faddr = uof[ii_uof].fp->Addr;
		uof[ii_uof].readp = 1;
		uof[ii_uof].writep = uof[ii_uof].fsize + 1;
		return 1;
	}
	//以下处理文件非空的情况
	len = len1 + pos + ins * (len0 - pos);		//计算写入完成后文件的长度
	bn0 = len0 / SIZE + (short)(len0 % SIZE > 0);	//文件原来占用的盘块数
	bn1 = len / SIZE + (short)(len % SIZE > 0);		//写入后文件将占用的盘块数
	if (FAT[0] < bn1 - bn0)
	{
		cout << "\n磁盘空间不足,不能写入文件.\n";
		return -1;
	}
	buf = new char[len + 1];
	if (buf == 0)
	{
		cout << "\n分配内存失败。\n";
		return -1;
	}
	file_to_buffer(fcbp, buf);		//文件读到buf
	if (ins)	//若是插入方式
	{
		for (ii = len0; ii >= pos; ii--)
			buf[ii + len1] = buf[ii];	//后移,空出后插入Buffer
		jj = pos;
		ii = 0;
		while (Buffer[ii] != '\0')		//Buffer插入到buf
			buf[jj++] = Buffer[ii++];
	}
	else		//若是改写方式
		strcpy(&buf[pos], Buffer);
	buffer_to_file(fcbp, buf);
	delete[] buf;
	uof[ii_uof].fsize = uof[ii_uof].fp->Fsize;
	uof[ii_uof].writep = uof[ii_uof].fsize + 1;
	cout << "\n写文件" << uof[ii_uof].fname << "成功.\n";
	return 1;
}

////////////////////////////////////////////////////////////////


int CloseComd(int k)				//close命令的处理函数：关闭文件 
{
	// close <文件名>，若指定文件已打开，则关闭之，即从UOF中删除该文件
	// 对应的表项。若文件未打开或文件不存在，分别给出有关信息。

	int i_uof;
	char attrib = '\0';
	FCB* p;
	if (k < 1)
	{
		for (i_uof = S - 1; i_uof >= 0; i_uof--) {
			if (uof[i_uof].state != 0)
			{
				break;
			}
		}
		if (i_uof < 0) {
			cout << "\n文件" << temppath << "未打开或不存在，不能关闭。\n";
			return -1;
		}
	}
	else {
		FindPath(comd[1], attrib, 0, p);	//构成全路径且去掉“..”存于temppath中
		i_uof = Check_UOF(temppath);		//查UOF
		if (i_uof == S)
		{
			cout << "\n文件" << temppath << "未打开或不存在，不能关闭。\n";
			return -1;
		}

	}

	uof[i_uof].state = 0;		 //在UOF中清除该文件登记栏
	p = uof[i_uof].fp;			 //取该文件的目录项位置指针
	p->Addr = uof[i_uof].faddr;  //保存文件的首块号
	p->Fsize = uof[i_uof].fsize; //保存文件的大小
	cout << "\n关闭文件" << uof[i_uof].fname << "成功。\n";

	return 1;
}

/////////////////////////////////////////////////////////////////

short int SAVE_bn(short bb)
{
	// 在udtab中存储被删除文件的块号

	short i = 0, b0, b = 0, bs;  // 初始化 b 为 0
	if (bb == 0)		// 被删除文件是空文件
		return bb;

	bs = getblock(); // 获取新的块
	short* pb = (short*)Disk[bs];
	b = bs; // 将 b 初始化为第一个块号

	while (bb > 0)
	{
		pb[i] = bb;
		bb = FAT[bb];
		i++;

		// 当当前块已满时，获取新的块并将其链接到之前的块
		if (i == SIZE / 2)
		{
			i = 0;
			b0 = b;
			b = getblock(); // 获取新的块
			FAT[b0] = b;    // 将新块链接到前一个块
			pb = (short*)Disk[b];
		}
	}

	pb[i] = -1;  // 表示链表的末尾
	return bs;   // 返回第一个块的编号
}


int ReplaceComd(int k)
{
	short pathStatus, sourceBlock, targetBlock;		//首块号存储
	char attribute = '\0', * sourceFileName, * targetFileName;
	char targetPathName[PATH_LEN];  // 目标目录文件名路径
	char currentPath[PATH_LEN];     // 当前或指定的目录路径
	FCB* sourceFCB = nullptr, * targetFCB = nullptr; // 初始化为nullptr
	short newBlock;                 // 新的空白块索引

	// 检查参数数量是否合法
	if (k < 1 || k > 3)
	{
		cout << "\n命令中参数个数太多或太少。\n";
		return -1;
	}

	// 处理单个参数的情况：使用当前路径作为目标路径
	if (k == 1) {
		strcpy(currentPath, curpath.cpath);
		pathStatus = ProcessPath(comd[1], sourceFileName, k, 1, '\20');
		if (pathStatus < 1) return pathStatus;	//判断路径是否有效
		sourceBlock = FindFCB(sourceFileName, pathStatus, attribute, sourceFCB);

		strcat(currentPath, "/");
		strcat(currentPath, sourceFCB->FileName);
		strcpy(targetPathName, currentPath);

		pathStatus = ProcessPath(targetPathName, targetFileName, k, 1, '\20');
		if (pathStatus < 1) return pathStatus;
		targetBlock = FindFCB(targetFileName, pathStatus, attribute, targetFCB);

		if (targetBlock < 0)
		{
			cout << "\n目录 " << currentPath << "中没有可取代的同名文件。\n";
			return -1;
		}

		if (strcmp(targetFileName, comd[1]) == 0) {
			cout << "\n文件不能自己取代自己\n";
			return -1;
		}
	}

	// 处理两个参数的情况：指定路径和文件名
	if (k == 2) {
		pathStatus = ProcessPath(comd[1], sourceFileName, k, 2, '\20');
		if (pathStatus < 1) return pathStatus;
		sourceBlock = FindFCB(sourceFileName, pathStatus, attribute, sourceFCB);

		strcpy(currentPath, comd[2]);
		strcat(currentPath, "/");
		strcat(currentPath, sourceFileName);  // 构建完整的目标文件路径

		pathStatus = ProcessPath(currentPath, targetFileName, k, 2, '\20');
		if (pathStatus < 1) return pathStatus;
		targetBlock = FindFCB(targetFileName, pathStatus, attribute, targetFCB);

		if (targetBlock < 0)
		{
			cout << "\n目录 " << comd[2] << "中没有可取代的同名文件。\n";
			return -1;
		}
	}

	// 检查目标文件是否具有系统或隐藏属性
	if (targetFCB->Fattrib >= 2 && targetFCB->Fattrib <= 7) {
		cout << "\n文件具有系统或隐藏属性，无法替换。\n";
		return -1;
	}

	// 检查目标文件是否只读，并提示用户是否继续替换
	if (targetFCB->Fattrib == 1) {
		char confirmFlag;
		cout << "\n目标文件只读，是否修改(Y|n): ";
		cin >> confirmFlag;
		if (tolower(confirmFlag) == 'n') return -1;  // 用户选择不替换
	}

	*targetFCB = *sourceFCB;  // 替换目标文件的FCB为源文件的FCB

	short prevBlock = 0;
	// 开始复制文件内容
	while (targetBlock > 0)
	{
		newBlock = getblock();  // 获取新的空白块
		if (prevBlock == 0)
			targetFCB->Addr = newBlock;  // 更新目标文件的首块号
		else
			FAT[prevBlock] = newBlock;  // 链接新块到文件尾

		memcpy(Disk[newBlock], Disk[targetBlock], SIZE);  // 复制盘块内容
		targetBlock = FAT[targetBlock];  // 准备复制下一个盘块
		prevBlock = newBlock;
	}
	return 1;
}


/////////////////////////////////////////////////////////////////

void Del1Ud(short a)
{
	// 在udtab表中删除一项，并前移后续表项

	short i, b, b0;
	b = udtab[a].fb;
	while (b > 0)
	{	//回收存储文件块号的磁盘空间
		b0 = b;
		b = FAT[b];
		FAT[b0] = 0;
		FAT[0]++;
	}
	for (i = a; i < Udelp - 1; i++)		//udtab表中表项前移一个位置
		udtab[i] = udtab[i + 1];
	Udelp--;
}

/////////////////////////////////////////////////////////////////

int PutUdtab(FCB* fp)
{
	//在udtab中加入一表项

	short bb, bn, n, m, size;
	size = fp->Fsize;
	bn = size / SIZE + (size % SIZE > 0) + 1;	//文件的盘块号个数(含-1)
	n = SIZE / sizeof(short);			//每个盘块可存储的盘块号数
	m = bn / n + (short)(bn % n > 0);			//共需m个盘块存储文件的块号
	if (Udelp == DM)
		Del1Ud(0);
	if (m > FAT[0])
	{
		cout << "\n磁盘空间不足,不能保存删除恢复信息,该文件删除后将不能恢复.\n";
		return -1;
	}
	strcpy(udtab[Udelp].gpath, temppath);
	strcpy(udtab[Udelp].ufname, fp->FileName);
	bb = udtab[Udelp].ufaddr = fp->Addr;
	udtab[Udelp].fb = SAVE_bn(bb);	//保存被删除文件的盘块号
	Udelp++;						//调整指针位置
	return 1;
}

/////////////////////////////////////////////////////////////////

void DelFile(FCB* fcbp)
{
	// 删除文件的实际操作：更新FAT表，释放磁盘块，清空目录项
	short s = fcbp->Addr;

	// 释放文件占用的磁盘块
	while (s > 0)
	{
		short next = FAT[s];  // 获取下一个磁盘块
		FAT[s] = 0;  // 释放当前磁盘块
		FAT[0]++;    // 增加空闲盘块数
		s = next;    // 移动到下一个磁盘块
	}

	// 清空文件的目录项
	fcbp->FileName[0] = (char)0xe5;  // 标记为已删除
	fcbp->Addr = 0;
	fcbp->Fsize = 0;
}

int DelComd(int k) // del (删除文件) 命令处理函数
{
	// 删除文件：del <文件名> 或 del *
	// 删除指定的文件，或者当前目录下的所有文件，清除其目录项和回收
	// 其所占用的磁盘空间。对于只读文件，删除前应询问用户，得到同意后
	// 方能删除。当指定文件正在使用时，显示"文件正在使用，不能删除"
	// 的信息。当指定文件不存在时给出错误信息。
	// 删除文件时，将该文件的有关信息记录到删除文件恢复信息表 udtab 中，
	// 以备将来恢复时使用。

	short i, s0, s, count = 0;
	char yn, attr;
	char attrib = '\0', * FileName;
	char gFileName[PATH_LEN];  // 存放文件全路径名
	FCB* fcbp;

	if (k == 1 && strcmp(comd[1], "*") == 0) // 如果命令是 "del *"
	{
		// 删除当前目录下的所有文件
		s = curpath.fblock;  // 获取当前目录的首块号
		while (s > 0)
		{
			fcbp = (FCB*)Disk[s]; // 获取当前目录的第一个盘块
			for (i = 0; i < 4; i++, fcbp++)
			{
				if (fcbp->FileName[0] != '\0' && fcbp->FileName[0] != (char)0xe5)
				{
					// 如果是有效的文件或目录项，进行删除
					cout << "删除文件: " << fcbp->FileName << endl;
					DelFile(fcbp);  // 调用删除文件的函数
					count++;
				}
			}
			s = FAT[s];  // 获取下一个盘块
		}
		if (count == 0)
		{
			cout << "\n当前目录没有文件可以删除。" << endl;
		}
		else
		{
			cout << "\n删除了 " << count << " 个文件。" << endl;
		}
	}
	else if (k == 1) // 如果命令是 "del <文件名>"
	{
		// 删除指定的文件
		s0 = ProcessPath(comd[1], FileName, k, 1, '\020'); // 取 FileName 所在目录的首块号
		if (s0 < 1) // 路径错误
			return s0;

		s = FindFCB(FileName, s0, attrib, fcbp); // 查找文件
		if (s < 0)
		{
			cout << "\n要删除的文件不存在。\n";
			return -2;
		}

		// 构造文件的全路径名
		strcpy(gFileName, temppath);
		i = strlen(temppath);
		if (temppath[i - 1] != '/')
			strcat(gFileName, "/");
		strcat(gFileName, FileName);

		// 检查该文件是否正在使用
		i = Check_UOF(gFileName);
		if (i < S) // 如果文件正在使用
		{
			cout << "\n文件 " << gFileName << " 正在使用，不能删除！\n";
			return -3;
		}

		// 检查文件是否是只读文件
		attr = fcbp->Fattrib & '\01';
		if (attr == '\01')
		{
			cout << "\n文件 " << gFileName << " 是只读文件，你确定要删除它吗？(y/n) ";
			cin >> yn;
			if (yn != 'Y' && yn != 'y')
				return 0; // 用户选择不删除，返回
		}

		// 将被删除文件的有关信息保存到删除文件恢复表 udtab 中
		i = PutUdtab(fcbp); // 被删除文件的有关信息保存到 udtab 表
		if (i < 0) // 如果因为磁盘空间不足，不能保存被删除文件的信息
		{
			cout << "\n你是否仍要删除文件 " << gFileName << " ? (y/n) : ";
			cin >> yn;
			if (yn == 'N' || yn == 'n')
				return 0; // 用户选择不删除，返回
		}

		// 删除文件的目录项
		fcbp->FileName[0] = (char)0xe5;

		// 回收文件所占的磁盘空间
		while (s > 0)
		{
			s0 = s;
			s = FAT[s];
			FAT[s0] = 0;   // 释放磁盘块
			FAT[0]++;      // 增加空闲盘块数
		}
		cout << "\n文件 " << gFileName << " 删除成功。\n";
	}
	else
	{
		cout << "\n命令参数错误。\n";
		return -1; // 参数错误
	}

	return 1;
}




	
/////////////////////////////////////////////////////////////////

int Udfile(FCB* fdp, short s0, char* fn, short& cc)
{
	// 在目录中找到被删除文件(文件名首字符为'\0xe5')的目录项后调用此函数
	// 本函数在udtab表中逐个查找，当找到与被删除文件的路径相同、名字(首字
	// 符除外)相同、首块号相同的表项时，显示“可能可以恢复字样”，询问用
	// 户得到肯定答复后，即开始恢复工作。恢复中若发现发生重名冲突时，由用
	// 户输入新文件名解决。恢复中若发现文件原先占用的盘块已作它用，则恢复
	// 失败。无论恢复成功与否，都将删除udtab中对应的表项。

	int i, j;
	char yn[11], Fname[INPUT_LEN];
	short* stp, b, b0, b1, s;
	FCB* fcbp;

	for (i = 0; i < Udelp; i++)
	{
		if (strcmp(udtab[i].gpath, temppath) == 0 && strcmp(&udtab[i].ufname[1], fn) == 0
			&& udtab[i].ufaddr == fdp->Addr)
		{
			cout << "\n文件" << udtab[i].ufname << "可能可以恢复，是否恢复它？(y/n) ";
			cin.getline(yn, 10);
			if (yn[0] == 'y' || yn[0] == 'Y')
			{
				if (udtab[i].ufaddr > 0)
				{
					b = udtab[i].fb;			//取存储被删文件盘块号的第一个块号
					stp = (short*)Disk[b];	//stp指向该盘块
					b0 = stp[0];				//取被删除文件的第一个块号到b0
					j = 1;
					while (b0 > 0)
					{
						if (FAT[b0] != 0)		//若被删除文件的盘块已经不空闲
						{
							cout << "\n文件" << udtab[i].ufname << "已不能恢复。\n";
							Del1Ud(i);		//删除udtab表中第i项(该表项已无用)
							return -1;
						}
						b0 = stp[j++];		//取被删除文件的下一个块号到b0
						if (j == SIZE / 2 && b0 != -1)
						{
							b = FAT[b];
							j = 0;
							stp = (short*)Disk[b];
						}
					}
					b = udtab[i].fb;
					stp = (short*)Disk[b];
					b0 = b1 = stp[0];
					j = 1;
					while (b1 > 0)
					{
						b1 = stp[j];
						FAT[b0] = b1;
						FAT[0]--;
						b0 = b1;
						j++;
						if (j == SIZE / 2 && b1 != -1)
						{
							b = FAT[b];
							j = 0;
							stp = (short*)Disk[b];
						}
					}
				}
				s = FindFCB(udtab[i].ufname, s0, '\0', fcbp);
				fdp->FileName[0] = udtab[i].ufname[0];	//恢复文件名
				if (s >= 0)	//有重名文件
				{
					cout << "\n该目录中已经存在名为" << udtab[i].ufname << "的文件，"
						<< "请为被恢复文件输入一个新的名字：";
					while (1)
					{
						cin.getline(Fname, INPUT_LEN);
						if (IsName(Fname))	//若输入的名字符合规则
						{
							s = FindFCB(Fname, s0, '\0', fcbp);	//查输入名字有否重名
							if (s >= 0)
								cout << "\n输入的文件名发生重名冲突。\n请重新输入文件名：";
							else
								break;			//输入名字合法且无重名文件存在。退出循环
						}
						else					//输入名字不符合命名规则
							cout << "\n输入的文件名不合法。\n请重新输入文件名：";
					}
					strcpy(fdp->FileName, Fname);
				}
				cc++;		//被恢复文件数增1
				Del1Ud(i);	//删除udtab表中第i项
			}
		}
	}
	return 0;
}

/////////////////////////////////////////////////////////////////

int UndelComd(int k)		//undel命令
{
	// 命令形式：undel [<目录名>]
	// 命令功能：恢复指定目录中被删除的文件
	// 具体有如下2种命令形式：
	//		undel——恢复当前目录中被删除的文件
	//		undel <目录名>——恢复指定目录中被删除的文件

	short i, s, s0, cc = 0;		//cc是恢复文件计数变量
	char* fn;
	FCB* fcbp1;
	if (k > 1)
	{
		cout << "\n命令不能有参数。\n";
		return -1;
	}
	if (k < 1)		//若命令中无参数
	{
		strcpy(temppath, curpath.cpath);
		s0 = s = curpath.fblock;
	}
	else
	{
		s0 = s = FindPath(comd[1], '\020', 1, fcbp1);
		if (s < 0)
		{
			cout << "\n命令中所给的路径错误。\n";
			return -2;
		}
	}
	while (s > 0)			//在首块号为s的目录找被删除文件的表项，直到目录尾部
	{
		fcbp1 = (FCB*)Disk[s];
		for (i = 0; i < 4; i++, fcbp1++)
		{
			if (fcbp1->FileName[0] == (char)0xe5)		//找到可能进行删除恢复的目录项
			{
				fn = &(fcbp1->FileName[1]);
				Udfile(fcbp1, s0, fn, cc);
			}
		}
		s = FAT[s];	//取下一个盘块号
	}
	cout << "\n共恢复了 " << cc << " 个被删除的文件。\n"; 
	return 1;
}

/////////////////////////////////////////////////////////////////
int ReadComd(int k)
{

	// 读文件：read <文件名> [<位置m> [<字节数n>]，从已打开的文件读文件内容并显示。若无
	// “位置”参数，则从读指针所指位置开始读。若有"位置"参数，则从指定位置处开始读。位
	// 置m是指从文件开头第m个字节处读（m从1开始编号）。若无"字节数"参数，则从指定位置读
	// 到文件末尾；若有"字节数n"参数，则从指定位置开始读n个字节。每读一个字节，读指针后
	// 移一个字节。若文件未打开或文件不存在，分别给出错误信息。
	// read命令有如下几种形式：
	//		read <文件名>——从读指针开始读文件，一直读到文件末尾为止。
	//		read <文件名> <位置m>——从文件第m个字节开始，一直读到文件末尾为止。
	//		read <文件名> <位置m> <字节数n>>——从文件第m个字节开始，共读n个字节。
	// 说明：刚打开的文件，其读指针指向文件开头(即读指针等于1)，约定空文件的读指针等于0。

	// 读取文件：read <文件名> [|pm] [|ln]
	// 支持读取当前操作文件以及从指定位置和长度读取的多种形式。

	short int ii_uof = -1;
	short int pos = -1;
	short int length = -1;
	short int s;
	char attrib = '\0', * FileName;
	char Buffer[256];
	FCB* fcbp;

	if (k == 0) {
		// 形式(1)：read - 读取当前操作文件从读指针位置到文件尾部
		for (int i = 0; i < S; i++) {
			if (uof[i].state != 0) { // 找到一个正在操作的文件
				ii_uof = i;
				break;
			}
		}
		if (ii_uof == -1) {
			cout << "\n没有找到正在操作的文件，不能读取。\n";
			return -1;
		}
		pos = uof[ii_uof].readp;
		length = uof[ii_uof].fsize - pos + 1;
	}
	else {
		for (int i = 1; i <= k; ++i) {
			if (comd[i][0] == '|') {
				if (tolower(comd[i][1]) == 'p') {
					pos = atoi(&comd[i][2]);
					if (pos <= 0) {
						cout << "\n命令中的位置参数错误。\n";
						return -2;
					}
				}
				else if (tolower(comd[i][1]) == 'l') {
					length = atoi(&comd[i][2]);
					if (length <= 0) {
						cout << "\n命令中的长度参数错误。\n";
						return -2;
					}
				}
				else {
					cout << "\n命令参数错误：" << comd[i] << "\n";
					return -2;
				}
			}
			else if (ii_uof == -1) {
				s = ProcessPath(comd[i], FileName, k, 0, '\020'); // 查找文件路径
				if (s < 1) return s;
				ii_uof = Check_UOF(temppath);
				if (ii_uof == S) {
					cout << "\n文件" << temppath << "未打开，不能读取。\n";
					return -2;
				}
			}
			else {
				cout << "\n命令参数错误：" << comd[i] << "\n";
				return -2;
			}
		}
		if (ii_uof == -1) {
			// 没有指定文件名，默认读取当前操作文件
			for (int i = 0; i < S; i++) {
				if (uof[i].state != 0) { // 找到一个正在操作的文件
					ii_uof = i;
					break;
				}
			}
			if (ii_uof == -1) {
				cout << "\n没有找到正在操作的文件，不能读取。\n";
				return -1;
			}
		}
		if (pos == -1) pos = uof[ii_uof].readp; // 默认从读指针位置开始
		if (length == -1) length = uof[ii_uof].fsize - pos + 1; // 默认读取到文件尾部
	}

	if (pos <= 0 || pos > uof[ii_uof].fsize) {
		cout << "\n读取位置超出文件长度范围。\n";
		return -3;
	}
	if (pos + length - 1 > uof[ii_uof].fsize) {
		length = uof[ii_uof].fsize - pos + 1; // 调整读取长度，确保不超过文件尾部
	}

	fcbp = uof[ii_uof].fp;
	s = fcbp->Addr; // 文件的首块号
	int count = 0;
	while (s > 0 && count < pos - 1) {
		for (int i = 0; i < SIZE && count < pos - 1; ++i, ++count);
		s = FAT[s];
	}

	count = 0;
	while (s > 0 && count < length) {
		for (int i = 0; i < SIZE && count < length; ++i, ++count) {
			Buffer[count] = Disk[s][i];
		}
		s = FAT[s];
	}
	Buffer[count] = '\0';

	cout << "\n读取的文件内容如下：\n" << Buffer << "\n";
	uof[ii_uof].readp = pos + length; // 更新读指针位置

	return 1;
}


/////////////////////////////////////////////////////////////////

int FcComd(int k) {
	if (k < 2) {
		cout << "\n命令中缺少文件名。\n";
		return -1;
	}

	short s1, s2, size1, size2;
	char attrib = '\0', * FileName1, * FileName2;
	FCB* fcbp1;
	FCB* fcbp2;
	char gFileName1[PATH_LEN], gFileName2[PATH_LEN];
	char* Buffer1;
	char* Buffer2;

	// 处理第一个文件路径
	s1 = ProcessPath(comd[1], FileName1, k, 0, '\020'); // 获取第一个文件的首块号
	if (s1 < 1) {
		cout << "\n文件" << comd[1] << "不存在。\n";
		return -1;
	}
	s1 = FindFCB(FileName1, s1, attrib, fcbp1);
	if (s1 < 0) {
		cout << "\n文件" << comd[1] << "不存在。\n";
		return -1;
	}

	// 处理第二个文件路径
	s2 = ProcessPath(comd[2], FileName2, k, 0, '\020'); // 获取第二个文件的首块号
	if (s2 < 1) {
		cout << "\n文件" << comd[2] << "不存在。\n";
		return -1;
	}
	s2 = FindFCB(FileName2, s2, attrib, fcbp2);
	if (s2 < 0) {
		cout << "\n文件" << comd[2] << "不存在。\n";
		return -1;
	}

	// 获取文件大小
	size1 = fcbp1->Fsize;
	size2 = fcbp2->Fsize;

	// 比较大小是否一致
	if (size1 != size2) {
		cout << "文件大小不同，内容不一致。\n";
		return 0;
	}

	// 分配缓冲区
	Buffer1 = new char[size1 + 1];
	Buffer2 = new char[size2 + 1];

	// 读取第一个文件内容
	short jj = 0;
	while (s1 > 0) {
		for (short i = 0; i < SIZE; i++, jj++) {
			if (jj == size1)
				break;
			Buffer1[jj] = Disk[s1][i];
		}
		if (jj == size1)
			break;
		s1 = FAT[s1];
	}

	// 读取第二个文件内容
	jj = 0; // 重置计数器
	while (s2 > 0) {
		for (short i = 0; i < SIZE; i++, jj++) {
			if (jj == size2)
				break;
			Buffer2[jj] = Disk[s2][i];
		}
		if (jj == size2)
			break;
		s2 = FAT[s2];
	}

	// 比较两个缓冲区内容
	for (short i = 0; i < size1; i++) {
		if (Buffer1[i] != Buffer2[i]) {
			cout << "文件内容不同。\n";
			cout << "第一个不同字节位置: " << i << endl;
			cout << "文件1内容: " << static_cast<int>(Buffer1[i]) << endl;
			cout << "文件2内容: " << static_cast<int>(Buffer2[i]) << endl;
			delete[] Buffer1;
			delete[] Buffer2;
			return 0;
		}
	}

	// 如果完全相同
	cout << "文件内容相同。\n";
	delete[] Buffer1;
	delete[] Buffer2;
	return 1;
}





/////////////////////////////////////////////////////////////////

int CopyComd(int k)		//copy命令的处理函数：复制文件 
{
	// 复制文件：copy <源文件名> [<目标文件名>]
	// 命令功能：为目标文件建立目录项，分配新的盘块，并将源文件的内容复制到目标文件中
	// 和其他命令一样，这里的“文件名”，是指最后一个名字是文件的路径名。
	// 若目标文件与源文件所在的目录相同，则只能进行更名复制，此时目标文件名不能省；
	// 若目标文件与源文件所在的目录不同，则既可更名复制也可同名复制，同名复制时目标文件名可省。
	// 例如，命令
	//		copy mail email
	// (1) 若当前目录中不存在email(目录或文件)，则该命令将当前目录中的文件mail，复制成
	//     当前目录下的文件email;
	// (2) 若当前目录下存在email，但email是子目录名，则将当前目录中的文件mail，复制到当
	//     前目录中的email子目录内，文件名与源文件相同(同名复制)；此时若email目录内已经
	//     存在文件或目录mail，则出现重名错误；
	// (3) 若当前目录内存在email文件，则出现重名错误；
	// (4) 若当前目录内不存在源文件mail(或者虽然有mail，但它是子目录名)，则也报错。
	//【特例】命令中无目标文件时，将源文件同名复制到当前目录中。例如，当前目录为/usr
	//		copy /box
	// 则上述命令把根目录中的文件box复制到当前目录/usr中，文件名仍为box。

	//【注】在同一目录中，各目录项不能重名（不管是文件名还是子目录名）。

	// 学生还可考虑使用通配符的多文件同名复制的情况(目标文件与源文件所在目录必须不同)。

	/*
	修改 1：支持父目录和根目录复制
		 2：覆盖同名文件
		 3：子目录处理
	
	
	*/


	short int i, size, s01, s02, s1, s2, s22, b, b0, bnum;
	char attrib = '\0', * FileName1, * FileName2;
	char gFileName[PATH_LEN]; // 存放文件全路径名
	FCB* fcbp, * fcbp1, * fcbp2;

	if (k < 1 || k > 2) {
		cout << "\n命令中参数太多或太少。\n";
		return -1;
	}

	// 获取源文件路径和目录块号
	s01 = ProcessPath(comd[1], FileName1, k, 0, '\20');
	if (s01 < 1) { // 源文件路径错误
		cout << "\n源文件 " << comd[1] << " 不存在。\n";
		return s01;
	}
	s1 = FindFCB(FileName1, s01, attrib, fcbp);
	if (s1 < 0) {
		cout << "\n源文件 " << comd[1] << " 不存在。\n";
		return -1;
	}
	fcbp1 = fcbp; // 保存源文件的 FCB

	// 构造源文件完整路径
	strcpy(gFileName, temppath);
	i = strlen(temppath);
	if (temppath[i - 1] != '/')
		strcat(gFileName, "/");
	strcat(gFileName, FileName1);

	// 检查源文件是否已打开
	i = Check_UOF(gFileName);
	if (i < S) {
		cout << "\n文件 " << gFileName << " 已经打开，不能复制！\n";
		return -2;
	}

	// 获取目标路径和目录块号
	if (k == 1) { // 命令中无目标文件，目标是当前目录
		s02 = curpath.fblock;
		FileName2 = FileName1;
	}
	else {
		if (strcmp(comd[2], "..") == 0) { // 处理父目录
			if (curpath.fblock == ROOT_BLOCK) { // 根目录无父目录
				cout << "\n当前目录是根目录，无法复制到父目录。\n";
				return -1;
			}
			// 获取父目录块号
			char parentPath[PATH_LEN];
			strcpy(parentPath, curpath.cpath);
			strcat(parentPath, "/..");
			s02 = ProcessPath(parentPath, FileName2, k, 1, '\20');
			FileName2 = FileName1; // 文件名保持不变
		}
		else if (strcmp(comd[2], "/") == 0) { // 处理根目录
			s02 = ROOT_BLOCK;
			FileName2 = FileName1; // 文件名保持不变
		}
		else { // 处理其他路径
			s02 = ProcessPath(comd[2], FileName2, k, 0, '\20');
			if (s02 < 1) {
				return s02; // 目标路径错误
			}
		}
	}

	if (!IsName(FileName2)) { // 检查目标文件名合法性
		cout << "\n命令中的目标文件名错误。\n";
		return -2;
	}

	// 检查目标文件是否存在
	s2 = FindFCB(FileName2, s02, '\040', fcbp);
	if (s2 >= 0) {
		if (fcbp->Fattrib & 0x10) { // 同名子目录存在
			s22 = s2;
			b = FindFCB(FileName1, s22, attrib, fcbp);
			if (b >= 0) {
				cout << "\n目标目录中存在同名文件或子目录，无法复制。\n";
				return -4;
			}
			FileName2 = FileName1; // 同名复制
		}
		else { // 同名文件存在
			cout << "目标目录中已存在同名文件 " << FileName2 << "，是否覆盖？(y/n): ";
			char choice;
			cin >> choice;
			if (choice != 'y' && choice != 'Y') {
				cout << "\n操作取消。\n";
				return -1;
			}
			// 删除目标文件
			FAT[fcbp->Addr] = 0; // 释放磁盘块
			fcbp->Fsize = 0;
			fcbp->Addr = 0;
		}
	}
	else {
		s22 = s02; // 目标目录块号
	}

	// 查找目标目录中的空闲 FCB
	i = FindBlankFCB(s22, fcbp2);
	if (i < 0) {
		cout << "\n复制文件失败。\n";
		return i;
	}

	// 检查磁盘空间
	size = fcbp1->Fsize;
	bnum = size / SIZE + (short)(size % SIZE > 0);
	if (FAT[0] < bnum) {
		cout << "\n磁盘空间已满，不能复制文件。\n";
		return -6;
	}

	// 复制源文件 FCB 信息
	*fcbp2 = *fcbp1;
	strcpy(fcbp2->FileName, FileName2);

	// 开始复制文件内容
	b0 = 0;
	while (s1 > 0) {
		b = getblock();
		if (b0 == 0)
			fcbp2->Addr = b; // 设置目标文件首块号
		else
			FAT[b0] = b;
		memcpy(Disk[b], Disk[s1], SIZE); // 复制磁盘块
		s1 = FAT[s1];
		b0 = b;
	}

	cout << "\n文件 " << gFileName << " 已成功复制到 " << comd[2] << "。\n";
	return 1;
}

/////////////////////////////////////////////////////////////////

int FseekComd(int k)	//fseek命令的处理函数 
{
	// 命令形式：fseek <文件名> <n>
	// 功能：将读、写指针移到指定位置n处

	int i_uof, n;
	char attrib = '\0';
	FCB* fcbp;

	if (k != 2)
	{
		cout << "\n命令参数个数错误。本命令必须有2个参数。\n";
		return -1;
	}
	n = atoi(comd[2]);
	FindPath(comd[1], attrib, 0, fcbp);		//构成全路径且去掉“..”存于temppath中
	i_uof = Check_UOF(temppath);		//查UOF
	if (i_uof == S)
	{
		cout << "\n文件" << temppath << "未打开或不存在，不能操作。\n";
		return -2;					//操作失败返回
	}
	if (uof[i_uof].fsize == 0)		//空文件
	{
		cout << "\n" << temppath << "是空文件，不能进行此操作。\n";
		return -3;
	}
	if (n <= 0 || n > uof[i_uof].fsize + 1)
	{
		cout << "\n位置参数错误。该参数必须在1和" << uof[i_uof].fsize + 1 << "之间。\n";
		return -4;
	}
	uof[i_uof].readp = n;				//读指针设定为n
	uof[i_uof].writep = n;			//写指针设定为n
	return 1;						//修改成功，返回
}

/////////////////////////////////////////////////////////////////

int RenComd(int k)	//ren命令的处理函数：文件改名
{
	// 命令形式：ren <原文件名> <新文件名>
	// 若原文件不存在，给出错误信息。
	// 若原文件存在，但正在使用，也不能改名，同样显示出错信息。
	// 应检查新文件名是否符合命名规则。

	short i, s0, s;
	char attrib = '\0', * FileName;
	char gFileName[PATH_LEN];	//存放文件全路径名
	FCB* fp, * fcbp;
	s0 = ProcessPath(comd[1], FileName, k, 2, '\20');//取FileName所在目录的首块号
	if (s0 < 1)			//路径错误
		return s0;		//失败，返回
	s = FindFCB(FileName, s0, attrib, fcbp);		//取FileName的首块号(查其存在性)
	if (s < 0)
	{
		cout << "\n要改名的文件不存在。\n";
		return -2;
	}
	strcpy(gFileName, temppath);
	i = strlen(temppath);
	if (temppath[i - 1] != '/')
		strcat(gFileName, "/");
	strcat(gFileName, FileName);	//构造文件的全路径名
	i = Check_UOF(gFileName);		//查UOF
	if (i < S)					//该文件已在UOF中
	{
		cout << "\n文件" << gFileName << "已经打开，不能改名!\n";
		return -3;
	}
	if (IsName(comd[2]))
	{
		fp = fcbp;						//保存指向要改名文件目录项的指针
		s = FindFCB(comd[2], s0, attrib, fcbp);	//查新文件名是否重名
		if (s < 0)			//不重名
		{
			strcpy(fp->FileName, comd[2]);
			return 1;		//正确返回
		}
		cout << "\n存在与新文件名同名的文件。\n";
		return -5;
	}
	cout << "\n命令中提供的新文件名错误。\n";
	return -4;
}

/////////////////////////////////////////////////////////////////

int AttribComd(int k)	//attrib命令的处理函数：修改文件或目录属性 
{
	// 显示修改文件属性：attrib <文件名> [±<属性>]。若命令中无"文件属性"参数，
	// 则显示指定文件的属性；若命令中有"文件属性"参数，则修改指定文件的属性。"文
	// 件属性"的形式有“+r或+h或+s”和“-r或-h或-s”两种形式，前者为设置指定文件
	// 为"只读"或"隐藏"或"系统"属性，后者为去掉指定文件的"只读"或"隐藏"或"系统"
	// 属性。各属性可组合使用且顺序不限。例如：
	//		attrib user/boy +r +h
	// 其功能是设置当前目录下user子目录中的文件boy为只读、隐藏文件。又如
	//		attrib /usr/user/box -h -r -s
	// 上述命令的功能是取消文件/usr/user/box的"隐藏"、"只读"、"系统"属性。
	// 当命令中指定的文件已打开或不存在，给出错误信息；
	// 当命令中提供的参数错误，也显示出错信息。

	short i, j, i_uof, s;
	char Attrib, attrib = '\40';
	char Attr[5], Attr1[4] = "RHS";
	char attr[6][3] = { "+r","+h","+s","-r","-h","-s" };
	char or_and[6] = { '\01','\02','\04','\036','\035','\033' };
	FCB* fcbp;

	if (k < 1)		//命令太少
	{
		cout << "\n命令中没有指定文件名。\n";
		return -1;
	}
	if (k > 3)
	{
		cout << "\n命令中参数过多\n";
		return -1;
	}

	s = FindPath(comd[1], attrib, 1, fcbp);	//寻找指定的文件或目录并返回其首块号
	if (s < 0)
	{
		cout << '\n' << temppath << "文件或目录不存在。\n";
		return -2;
	}
	if (k == 1)		//显示文件/目录的属性
	{
		Attrib = fcbp->Fattrib & '\07';		//通过与二进制07即111进行按位与操作获取Fattrib末三位即属性信息
		if (Attrib == '\0')
			strcpy(Attr, "普通");
		else
		{
			for (i = 0; i < 3; i++)
			{
				if (Attrib & or_and[i])
					Attr[i] = Attr1[i];
				else
					Attr[i] = ' ';
			}
			Attr[i] = '\0';
		}
		cout << "\n" << temppath << "的属性是：" << Attr << endl;
		return 1;
	}

	////////////////////////////////////////////////////////////////////////////////////
	//以下为原本，现重构外层添加条件判断 if(k==2)
	if (k == 2) {
		if (fcbp->Fattrib <= '\07')		//若是文件，要查其是否已被打开
		{
			i_uof = Check_UOF(temppath);	//查UOF
			if (i_uof < S)
			{
				cout << "\n文件" << temppath << "正打开着，不能修改属性。\n";
				return -3;
			}
		}
		/*
		for (i = 2; i <= k; i++)		//处理属性参数
		{
			for (j = 0; j < 6; j++)
				if (_stricmp(comd[i], attr[j]) == 0)
				{
					if (j < 3)
						fcbp->Fattrib = fcbp->Fattrib | or_and[j];
					else
						fcbp->Fattrib = fcbp->Fattrib & or_and[j];
					break;
				}
			if (j == 6)
			{
				cout << "\n命令中的属性参数错误。\n";
				return -4;
			}

		}
		*/
		for (i = 2; i <= k; i++) { // 处理属性参数
			bool found = false; // 添加一个标志变量来标记是否找到匹配的属性参数
			for (j = 0; j < 6; j++) {
				if (_stricmp(comd[i], attr[j]) == 0) {
					if (j < 3)
						fcbp->Fattrib = fcbp->Fattrib | or_and[j];
					else
						fcbp->Fattrib = fcbp->Fattrib & or_and[j];
					found = true; // 找到匹配的属性参数，设置标志为 true
					break; // 跳出内层循环
				}
			}
			if (!found) { // 如果没有找到匹配的属性参数
				cout << "\n命令中的属性参数错误。\n";
				return -4;
			}
		}
		return 1;	//修改属性完成，返回
	}
	//上为原本，现重构
	//////////////////////////////////////////////////////////////////////
}

/////////////////////////////////////////////////////////////////

int RewindComd(int k)	//rewind命令的处理函数：读、写指针移到文件开头 
{
	// 命令形式：rewind <文件名>
	// 对指定文件操作，同时该文件变为当前操作文件

	int i_uof;
	char attrib = '\0';
	FCB* fcbp;

	if (k != 1)
	{
		cout << "\n命令参数个数错误。本命令只能有1个参数。\n";
		return -1;
	}
	FindPath(comd[1], attrib, 0, fcbp);		//构成全路径且去掉“..”存于temppath中
	i_uof = Check_UOF(temppath);		//查UOF
	if (i_uof == S)
	{
		cout << "\n文件" << temppath << "未打开或不存在，不能操作。\n";
		return -1;					//操作失败返回
	}
	if (uof[i_uof].faddr > 0)			//若是空文件
		uof[i_uof].readp = 1;			//读指针设定为0
	else
		uof[i_uof].readp = 0;			//非空文件的读指针设定为1
	uof[i_uof].writep = 1;			//文件的写指针设定为1
	return 1;						// 修改成功，返回
}

/////////////////////////////////////////////////////////////////

void UofComd()	//uof命令，显示当前用户“打开文件表”
{
	//显示用户已打开文件表UOF的内容

	int i, k;
	char ch;
	for (k = i = 0; i < S; i++)
		k += uof[i].state;
	if (k > 0)
	{
		cout << "\n打开文件表UOF的内容如下:\n\n"
			<< "文件名                       文件属性  "
			<< "首块号  文件长度  状态  读指针  写指针\n";
		for (i = 0; i < S; i++)
		{
			if (uof[i].state == 0)
				continue;					//空目录项
			cout.setf(ios::left);
			cout << setw(32) << uof[i].fname;	//显示文件名 
			ch = uof[i].attr;
			switch (ch)
			{
			case '\0': cout << "普通    ";
				break;
			case '\01': cout << "R       ";
				break;
			case '\02': cout << "H       ";
				break;
			case '\03': cout << "RH      ";
				break;
			case '\04': cout << "S       ";
				break;
			case '\05': cout << "RS      ";
				break;
			case '\06': cout << "HS      ";
				break;
			case '\07': cout << "RHS     ";
				break;
			default: cout << "错误    ";
			}
			cout << setw(8) << uof[i].faddr;	//首块号
			cout << setw(8) << uof[i].fsize;	//文件大小
			k = uof[i].state;
			if (k == 1)
				cout << " 建立   ";			//状态为“建立” 
			else
				cout << " 打开   ";			//状态为“打开” 
			cout << setw(8) << uof[i].readp;
			cout << uof[i].writep << endl;		//读指针 
		}
	}
	else
		cout << "目前尚无打开的文件。\n";
}
/*

  */
  /////////////////////////////////////////////////////////////////

void save_FAT()	//保存文件分配表FAT到磁盘文件FAT.txt
{
	int i;
	ofstream ffo;
	ffo.open("E:\\操作系统作业\\mine\\FAT2008.txt");
	for (i = 0; i < K; i++)
		ffo << FAT[i] << ' ';
	ffo.close();
}

/////////////////////////////////////////////////////////////////

void save_Disk() //保存盘块中的文件内容
{
	int i;
	short* p = (short*)Disk[0];
	p[0] = ffbp;
	p[1] = Udelp;
	ofstream ffo("E:\\操作系统作业\\mine\\Disk2008.dat", ios::binary);
	for (i = 0; i < K; i++)
		ffo.write((char*)&Disk[i], SIZE);
	ffo.close();
}

/////////////////////////////////////////////////////////////////

void save_UdTab()	//保存被删除文件信息表
{
	int i;
	ofstream ffo("E:\\操作系统作业\\mine\\UdTab2008.dat", ios::binary);
	for (i = 0; i < DM; i++)
		ffo.write((char*)&udtab[i], sizeof(udtab[0]));
	ffo.close();
}

/////////////////////////////////////////////////////////////////

int FindFCB(char* Name, int s, char attrib, FCB*& fcbp)
{
	// 从第s块开始，查找名字为Name且符合属性attrib的目录项
	// 给定名字Name没有找到返回负数，找到返回非负数(找目录时返回恒正)
	// 函数正确返回时，引用参数指针变量fcbp指向Name目录项。

	int i;
	char ch, Attrib;
	while (s > 0)
	{
		fcbp = (FCB*)Disk[s];
		for (i = 0; i < 4; i++, fcbp++)		//每个盘块4个目录项
		{
			ch = fcbp->FileName[0];
			if (ch == (char)0xe5)
				continue;
			if (ch == '\0')
				return -1;		//路径错误(至该目录尾部仍未找到)
			if (strcmp(Name, fcbp->FileName) == 0)	//名字找到
			{
				if (attrib == '\040')		//attrib为32时，文件、子目录不限
					return fcbp->Addr;
				Attrib = fcbp->Fattrib;
				if (attrib == '\020' && Attrib >= attrib)	//子目录属性
					return fcbp->Addr;
				if (attrib == '\0' && Attrib <= '\07')		//文件属性(找的是文件)
					return fcbp->Addr;
				return -1;			//名字符合但属性不对仍然没有找到
			}
		}
		s = FAT[s];		//取下一个盘块号
	}
	return -2;
}

/////////////////////////////////////////////////////////////////

int FindPath(char* pp, char attrib, int ffcb, FCB*& fcbp)
{	
	// pp为指向字符串的指针，attrib用于查找路径中文件或目录的属性
	// ffcb：如果非零，则在函数成功返回时，fcbp 将指向路径最后一个目录的目录项的FCB。
	// fcbp：引用传递的FCB指针，用于返回找到的FCB。
	// 查找命令中给定的路径，确定路径的正确性，并返回路径中最后一个
	// 名字(目录名)代表的目录的地址(首块号)；对路径进行处理（去掉路
	// 径中的“..”），构成一个全路径名存于temppath中；当函数参数ffcb
	// 非零时，通过调用FindFCB( )函数，使本函数成功返回时,FCB类型的
	// 引用参数指针变量fcbp指向路径最后一个目录的目录项。

	short i, j, len, s = 0;
	char paths[60][FILENAME_LEN];	//分解路径用(路径中最多不超过60个名字)
	char* q, Name[PATH_LEN];

	strcpy(temppath, "/");
	if (strcmp(pp, "/") == 0)	//是根目录
		return 1;			//返回根目录的首块号
	if (*pp == '/')			//绝对路径，从根目录开始
	{
		s = 1;				//根目录的首块号
		pp++;
	}
	else
	{
		s = curpath.fblock;	//相对路径，从当前目录开始
		strcpy(temppath, curpath.cpath);
	}
	j = 0;
	while (*pp != '\0')	//对命令中的路径分解
	{
		for (i = 0; i < PATH_LEN; i++, pp++)
		{
			if (*pp != '/' && *pp != '\0')
				Name[i] = *pp;
			else
			{
				if (i > 0)
				{
					Name[i] = '\0';
					if (i > FILENAME_LEN - 1)	//名字过长则截取前FILENAME_LEN-1个字符
						Name[FILENAME_LEN - 1] = '\0';
					strcpy(paths[j], Name);
					j++;
				}
				else
					return -1;		//路径错误
				if (*pp == '/') pp++;
				break;			//已处理到字符串尾部
			}
		}
	}
	for (i = 0; i < j; i++)
	{
		if (strcmp(paths[i], "..") == 0)
		{
			if (strcmp(temppath, "/") == 0)
				return -1;		//路径错误(根目录无父目录)
			len = strlen(temppath);
			q = &temppath[len - 1];
			while (*q != '/') q--;
			*q = '\0';
			if (*temppath == '\0')
			{
				*temppath = '/';
				temppath[1] = '\0';
			}
		}
		else
		{
			if (strcmp(temppath, "/") != 0)
				strcat(temppath, "/");
			strcat(temppath, paths[i]);
		}
		if (ffcb)
		{
			s = FindFCB(paths[i], s, attrib, fcbp);
			if (s < 0)
				return s;
		}
	}
	return s;
}

/////////////////////////////////////////////////////////////////

void FatComd()	//若输入"fat"
{
	cout << "\n当前磁盘剩余空闲块数为" << FAT[0] << endl;
}

/////////////////////////////////////////////////////////////////

void CheckComd()		//check命令
{
	int j = 0, i = 2;
	cout << "\n当前磁盘空闲是：" << FAT[0] << endl;
	for ( j = 0, i = 2; i < K; i++)
		if (FAT[i] == 0)
			j++;
	FAT[0] = j;
	cout << "重新检查后，磁盘的空闲块是：" << FAT[0] << endl;
	cout << "\nffbp=" << ffbp << endl;
	cout << "Udelp=" << Udelp << endl;
}

/////////////////////////////////////////////////////////////////

void ExitComd()		//exit命令处理
{
	char yn;
	CloseallComd(0);		//关闭所有打开的文件以防数据丢失
	cout << "\n退出时FAT、Disk、Udtab是否要存盘？(y/n) ";
	cin >> yn;
	if (yn == 'Y' || yn == 'y')
	{
		save_FAT();			//FAT表存盘
		save_Disk();		//磁盘块中存储的内容
		save_UdTab();		//保存被删除文件信息表
	}
	//delete [] Disk;
	exit(0);
}

/////////////////////////////////////////////////////////////////

bool isunname(char ch)
{
	char cc[] = "\"*+,/:;<=>?[\\]| ";
	for (int i = 0; i < 16; i++)
		if (ch == cc[i])
			return true;
	return false;
}

/////////////////////////////////////////////////////////////////

bool IsName(char* Name)
{
	// 判断名字是否符合如下规则：
	// (1) 名字长度不能超过FILENAME_LEN-1个字节，即10个字节。
	//     允许输入的名字超过10个字符，但只有前10个字符有效；
	// (2) 名字一般由字母（区分大小写）、数字、下划线等组成，名字允许是汉字；
	// (3) 名字不能包含以下16个字符之一：
	//		" * + , / : ; < = > ? [ \ ] | space(空格)
	// (4) 名字中允许包含字符“.”，但它不能是名字的第一个字符，故“.”、
	//    “.abc”、“..”和“..abc”等都是不合法的名字。

	int i, len, Len = FILENAME_LEN - 1;
	bool yn = true;
	char ch;
	len = strlen(Name);
	if (len == 0)
		return false;
	if (Name[0] == '.')		//名字第一个字符不能是字符'.'
		return false;
	if (len > Len)			//若名字过长，截去多余的尾部
	{
		Name[Len] = '\0';
		len = Len;
	}
	for (i = 0; i < len; i++)
	{
		ch = Name[i];
		if (isunname(ch))	//若名字中含有不合法符号
		{
			yn = false;
			break;
		}
	}
	if (!yn)
		cout << "\n名字中不能包含字符'" << ch << "'。\n";
	return yn;
}

/////////////////////////////////////////////////////////////////

void PromptComd(void)			//prompt命令
{
	dspath = !dspath;
}

/////////////////////////////////////////////////////////////////

void UdTabComd(void)			//udtab命令
{
	//显示删除文件恢复表udtab的内容

	cout << "\n恢复被删除文件信息表(UdTab)内容如下：\n\n";
	cout << "文件路径名                      " << "文件名        "
		<< "首块号      " << "存储块号" << endl;
	for (int i = 0; i < Udelp; i++)
		cout << setiosflags(ios::left) << setw(32) << udtab[i].gpath
		<< setw(15) << udtab[i].ufname << setw(12) << udtab[i].ufaddr
		<< setw(8) << udtab[i].fb << endl;
}

/////////////////////////////////////////////////////////////////
//自加功能，打印目前操作文件目录
void ShowOprateComd() {
	
	// 遍历 UOF (用户打开文件表)
	bool found = false;
	for (int i = 0; i < S; i++) {
		if (uof[i].state != 0) { // 找到一个有效的文件记录
			cout << "当前操作文件目录: " << uof[i].fname << endl;
			cout << "文件名: " << uof[i].fp->FileName << endl;
			cout << "文件属性: " << static_cast<int>(uof[i].attr) << endl;
			cout << "文件首块号: " << uof[i].faddr << endl;
			cout << "文件大小: " << uof[i].fsize << " 字节" << endl;
			cout << "读指针位置: " << uof[i].readp << endl;
			cout << "写指针位置: " << uof[i].writep << endl;
			found = true;
			break; // 假设只打印第一个有效的打开文件
		}
	}
	if (!found) {
		cout << "当前没有正在操作的文件。" << endl;
	}

}


//////////////////////////////////////////////////////////////////////////////////


int file_to_buffer(FCB* fcbp, char* Buffer)	//文件内容读到Buffer,返回文件长度
{
	//文件内容读到Buffer,返回文件长度

	short s, len, i, j = 0;

	len = fcbp->Fsize;				//取文件长度
	s = fcbp->Addr;					//取文件首块号
	while (s > 0)
	{
		for (i = 0; i < SIZE; i++, j++)
		{
			if (j >= len)				//已读完该文件
				break;
			Buffer[j] = Disk[s][i];
		}
		s = FAT[s];					//取下一个盘块
	}
	Buffer[j] = '\0';
	return len;						//返回文件长度
}

/////////////////////////////////////////////////////////////////

/* 写入comd时缓冲区溢出，重构
int buffer_to_file(FCB* fcbp, char* Buffer)	//Buffer写入文件
{
	//成功写入文件，返回1；写文件失败，返回0

	short bn1, bn2, i, j, s, s0, len, size, count = 0;

	len = strlen(Buffer);	//取字符串Buffer长度
	s0 = s = fcbp->Addr;		//取文件首块号
	if (len == 0)
	{
		fcbp->Addr = fcbp->Fsize = 0;	//文件变为空文件
		releaseblock(s);			//释放文件占用的磁盘空间
		return 1;
	}
	size = fcbp->Fsize;	//取文件长度
	bn1 = len / SIZE + (short)(len % SIZE > 0);		//Buffer若存盘占用的盘块数
	bn2 = size / SIZE + (short)(size % SIZE > 0);	//文件原先内容占用的盘块数
	if (FAT[0] < bn1 - bn2)
	{
		cout << "\n磁盘空间不足，不能将信息写入文件。\n";
		return 0;
	}
	if (s == 0)				//若是空文件
	{
		s0 = s = getblock();	//为其分配首个盘块
		fcbp->Addr = s0;		//记下首块号
	}
	j = 0;
	while (j < len)		//Buffer写入FilName2
	{
		if (s < 0)
		{
			s = getblock();
			FAT[s0] = s;
		}
		for (i = 0; i < SIZE; i++, j++)
		{
			if (j == len)
				break;
			if (Buffer[j] == '\\' && Buffer[j + 1] == 'n')
			{
				Disk[s][i] = '\n';
				j++;
				count++;
			}
			else
				Disk[s][i] = Buffer[j];
		}
		s0 = s;
		s = FAT[s];
	}
	if (s > 0)
	{
		FAT[s0] = -1;			//目标文件结束盘块标记
		releaseblock(s);	//若FileName2仍有盘块未使用，应释放它们
	}
	fcbp->Fsize = len - count;		//改变文件的长度
	return 1;
	

}
*/
int buffer_to_file(FCB* fcbp, char* Buffer) {
	short bn1, bn2, i, j, s, s0, len, size, count = 0;

	len = strlen(Buffer); // 取字符串Buffer长度
	s0 = s = fcbp->Addr; // 取文件首块号
	if (len == 0) {
		fcbp->Addr = fcbp->Fsize = 0; // 文件变为空文件
		releaseblock(s); // 释放文件占用的磁盘空间
		return 1;
	}
	size = fcbp->Fsize; // 取文件长度
	bn1 = len / SIZE + (short)(len % SIZE > 0); // Buffer若存盘占用的盘块数
	bn2 = size / SIZE + (short)(size % SIZE > 0); // 文件原先内容占用的盘块数
	if (FAT[0] < bn1 - bn2) {
		cout << "\n磁盘空间不足，不能将信息写入文件。\n";
		return 0;
	}
	if (s == 0) { // 若是空文件
		s0 = s = getblock(); // 为其分配首个盘块
		fcbp->Addr = s0; // 记下首块号
	}
	j = 0;
	while (j < len) { // Buffer写入文件
		if (s < 0) {
			s = getblock();
			if (s0 == 0) s0 = s; // 如果是第一个盘块，更新s0
			FAT[s0] = s;
		}
		for (i = 0; i < SIZE; i++, j++) {
			if (j >= len) break; // 确保不会超出Buffer的范围
			if (Buffer[j] == '\\' && j + 1 < len && Buffer[j + 1] == 'n') {
				Disk[s][i] = '\n';
				j++;
			}
			else {
				Disk[s][i] = Buffer[j];
			}
		}
		s0 = s;
		s = FAT[s];
	}
	if (s > 0) {
		FAT[s0] = -1; // 目标文件结束盘块标记
		releaseblock(s); // 若仍有盘块未使用，应释放它们
	}
	fcbp->Fsize = len; // 更新文件长度，不应减去count，因为count用于处理转义字符，不是实际的字符数
	return 1;
}
/////////////////////////////////////////////////////////////////

void releaseblock(short s)	//回收磁盘空间
{	//释放s开始的盘块链
	short s0;
	while (s > 0)				//循环操作，直到盘块链尾部
	{
		s0 = s;				//s0记下当前块号		
		s = FAT[s];			//s指向下一个盘块
		FAT[s0] = 0;			//释放盘块s0
		FAT[0]++;			//空闲盘块数增1
	}
}

/////////////////////////////////////////////////////////////////

int ParseCommand(char* p)  // 将输入的命令行分解成命令和参数等
{
	int i, j, k, g = 0;
	for (i = 0; i < CK; i++)                    // 初始化为空comd[][] 
		comd[i][0] = '\0';
	for (k = 0; k < CK; k++)
	{   // 分解命令及其参数，comd[0]中是命令，comd[1],comd[2]...是参数
		for (i = 0; *p != '\0'; i++, p++)
			if (*p != ' ')              // 空格是命令、参数之间的分隔符
				comd[k][i] = *p;        // 取命令标识符
			else
			{
				comd[k][i] = '\0';
				if (strlen(comd[k]) == 0) k--;
				p++;
				break;
			}
		if (*p == '\0')
		{
			comd[k][i] = *p;
			break;
		}
	}
	// 处理命令与参数间无空格的情况
	for (i = 0; comd[0][i] != '\0'; i++)
		if (comd[0][i] == '/' || comd[0][i] == '.'|| comd[0][i]=='*')  // 处理cd/，cd..，dir/usr等情况
            break;
    if (comd[0][i] != '\0')          // comd[0]中存在字符'/'或'.'
    {
        if (k > 0)
            for (j = k; j > 0; j--)
                strcpy(comd[j + 1], comd[j]);    // 后移已有参数
        strcpy(comd[1], &comd[0][i]);
        comd[0][i] = '\0';
        k++;    // 多出一个参数
    }
    return k;
}



/////////////////////////////////////////////////////////////////
void CloseallComd(int disp)    //closeall命令，关闭当前用户的所有文件
{
	int i_uof, j, k;
	FCB* p;
	for (k = i_uof = 0; i_uof < S; i_uof++)
	{
		j = uof[i_uof].state;	//UOF中状态>0为有效登记项
		if (j > 0)
		{
			k++;  //已打开文件计数
			uof[i_uof].state = 0;			//在UOF中清除该文件登记栏
			p = uof[i_uof].fp;			//取该文件的目录项位置指针
			p->Addr = uof[i_uof].faddr;	//保存文件的首块号
			p->Fsize = uof[i_uof].fsize;	//保存文件的大小
			cout << "\n文件" << uof[i_uof].fname << "已关闭.\n";
		}
	}
	if (!disp)
		return;
	if (k == 0)
		cout << "\n你没有打开文件，故无文件可关闭。\n\n";
	else
		cout << "\n共关闭 " << k << " 个文件.\n\n";
}
////////////////////////////////////////////////////////////////////////////////
// 执行批处理文件中的命令序列
int BatchComd(int k) {
	// 初始化变量
	short pathStatus, fcbStatus;
	char* fileName;
	FCB* fileFCB;

	// 检查传入的参数数量是否为1（即批处理文件名）
	if (k != 1) {
		cout << "\n命令中参数个数不正确。\n";
		return -1;
	}

	// 解析文件路径并检查文件是否存在
	pathStatus = ProcessPath(comd[1], fileName, k, 1, '\0');
	fcbStatus = FindFCB(fileName, pathStatus, '\0', fileFCB);

	// 初始化读取文件所需的变量
	short blockIndex, fileBlock, blockNum, totalRead = 0, charsToRead;
	char buffer[SIZE + 1];

	// 检查文件是否已打开并在用户打开文件表(UOF)中
	strcat(temppath, fileName);
	int fileIndex = Check_UOF(temppath);

	// 如果文件未打开或不存在，则返回错误
	if (fileIndex == S) {
		cout << "\n文件" << temppath << "未打开或不存在，不能读文件。\n";
		return -2;
	}

	// 如果文件为空，则返回
	if (uof[fileIndex].readp == 0) {
		cout << "\n文件" << temppath << "是空文件。\n";
		return 1;
	}

	// 计算从当前读指针位置到文件末尾的字节数
	int readPos = uof[fileIndex].readp;
	if (readPos > uof[fileIndex].fsize) {
		cout << "\n读指针已指向文件尾部，无可读信息。\n";
		return 1;
	}
	charsToRead = uof[fileIndex].fsize - readPos + 1;

	// 计算读取起始块和偏移量
	blockNum = (readPos - 1) / SIZE;
	int offset = (readPos - 1) % SIZE;
	fileBlock = uof[fileIndex].faddr;

	// 寻找读入的第一个盘块号
	for (int i = 0; i < blockNum; i++) {
		blockIndex = fileBlock;
		fileBlock = FAT[fileBlock];
	}

	// 逐块读取文件并执行命令
	int bufferIndex;
	while (totalRead < charsToRead) {
		// 将文件内容读取到缓冲区
		for (bufferIndex = offset; bufferIndex < SIZE; bufferIndex++) {
			buffer[bufferIndex - offset] = Disk[fileBlock][bufferIndex];
			totalRead++;
			if (totalRead == charsToRead) break;
		}
		buffer[bufferIndex - offset] = '\0';

		// 执行缓冲区中的命令
		ExecuteBufferCommands(buffer, bufferIndex - offset);
		offset = 0;
		fileBlock = FAT[fileBlock];  // 准备读下一个盘块
	}
}
////////////////////////////////////////////////////////////////////////////
// 执行缓冲区中的命令
void ExecuteBufferCommands(char* buffer, int length) {
	char command[INPUT_LEN];
	int cmdIndex = 0;
	// 逐行解析并执行命令
	for (int i = 0; i < length; i++) {
		command[cmdIndex++] = buffer[i];
		if (buffer[i] == '\n') {
			command[cmdIndex - 1] = '\0';
			cmdIndex = 0;
			ProcessAndExecuteCommand(command);
		}
	}
	command[cmdIndex] = '\0';
	if (strcmp(command, "") != 0) {
		ProcessAndExecuteCommand(command);
	}
}

// 解析并执行单个命令
void ProcessAndExecuteCommand(char* command) {
	cout << "\nC:";
	if (dspath) cout << curpath.cpath;
	cout << ">" << command << endl;
	// 解析命令
	int cmdCount = ParseCommand(command);
	// 执行命令
	ExecComd(cmdCount);
	cout << endl;
}