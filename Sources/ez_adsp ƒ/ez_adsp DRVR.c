/*  � 1989 Ricardo Batista */#include "ADSP.h"#define		open				0#define		prime				1#define		control				2#define		status				3#define		close				4/*	 � 1989 Apple Computer, Inc. by Ricardo Batista				EZ_ADSP driver interface version 1.0, March 6 1989.	The ez_adsp driver loads itself automatically in any machine, then it loads an	STR with id 268 to be used as the 'type' registered in the network. STR 269 is	loaded as the name of a desk accessory to be opened when a connection is	detected (and the name is not null).  To open the ez_adsp driver issue an	OpenDriver("\p.ez_adsp",&refNum); call, when you are done with it don't close	the driver.*//*	Structure used to give information about a file transfer follows.  The driver	recognizes a file transfer when two empty messages are sent with the EOM bit	set and the next message (with the EOM bit set) is the following structure.	Only the size of the structure is checked and if it matches a file transfer	is assumed.  Following the file information message is the data fork with the	EOM bit set at the end, then the resource fork is sent again with the EOM bit	set at the end.  Notice that if either fork is empty only an EOM bit constitutes	the message.*/typedef struct {	char	file_name[66];	OSType 	file_creator;	OSType	file_type;	long	data_len;	long	res_len;	int		finder_flags;	int		info;} file_info;				/* Control call csCode's to the ez_adsp driver */#define		openConn			100#define		closeConn			101#define		autoFile			102#define		sendFile			103#define		disable				104#define		enable				105#define		deskFlag			106/*	openConn, 	opening a connection, csParam[0]-[1] contains an AddrBlock to connect to	closeConn, 	closes a connection and starts waiting for a remote connection if we are				enabled to answer a call, default is enabled.	autoFile,	csParam[0] contains a flag to set the autoFile parameter, if true the				driver recognizes a file transfer and saves the received file in the				disk the user selects. False by default.	sendFile,	this takes a pointer to a file name in csParam[0]-[1], and a volume				reference number in csParam[2], the file is then transfered to the remote				connection.	disable,	disables answering to a remote connection.	enable,		enables answering to a remote connection, default.	deskFlag,	sets the desk accesory opened flag to csParam[0], if false, then as soon				as a connection is detected the desk accessory who's name was stored in				the resource STR 269 (if any) is opened, then the flag is set to true./*				/* Status call csCode's to the ez_adsp driver */#define		getBuf				120/*	getBuf,		getBuf returns the number of bytes left to read in csParam[0]-[1],				csParam[2] contains the connection state (2 = waiting for a connection,				3 = opening a connection, 4 = connection opened, 5 = closing a connection,				6 = connection closed and no waiting)				csParam[3] conatins the userFlags, csParam[4] the attention code (if any)				and csParam[5]-[6] a pointer to the attention data. Normally you will only				look at csParam[0]-[1].*/#define		idealSize		512#define		uPtr		unsigned char *DSPParamBlock ADSP;TRCCB CCB;int ADSPID;int mySocket;long signature;Boolean deskOpened;Boolean autoSave;char OutBuffer[attnBufSize];char InBuffer[attnBufSize];char attnBuffer[attnBufSize];AddrBlock address;char desk[40];int emptyEOM;Boolean canAnswer;int oldState;int main(ioParam* param, DCtlPtr d, int call);Boolean abort(void);int InitComm(void);int OpenComm(Boolean waitForCall);int CloseComm(void);int DoName(int Socket);int file(cntrlParam* p);int recFile(void);int main(param, d, call)ioParam *param;DCtlPtr d;int call;{	int err = 0;	cntrlParam *p;	long len;	WindowPtr w;	if (d->dCtlStorage == 0L) {		if (call == open) {			SysBeep(1);			return(openErr);		}		return(openErr);	}	if ((call != open) && (signature != 'RICK')) {		return(notOpenErr);	}	switch (call) {		case open:			d->dCtlFlags |= dNeedLock | dNeedTime;			d->dCtlDelay = 25;			if (signature != 'RICK') {				signature = 'RICK';				err = OpenDriver("\p.MPP",&ADSPID);				if (!err)					err = OpenDriver("\p.DSP",&ADSPID);				if (err)					return(err);				err = InitComm();				if (!err)					err = OpenComm(TRUE);			}			break;		case prime:			if ((char) (param->ioTrap) == aWrCmd) {				ADSP.csCode = dspWrite;				ADSP.u.ioParams.reqCount = param->ioReqCount;				ADSP.u.ioParams.dataPtr = (uPtr) param->ioBuffer;				ADSP.u.ioParams.eom = param->ioPosMode;				ADSP.u.ioParams.flush = 0;	/* or ONE ? */				err = PBControl(&ADSP,FALSE);				param->ioActCount = ADSP.u.ioParams.actCount;			}			else			if ((char) (param->ioTrap) == aRdCmd) {				ADSP.csCode = dspRead;				ADSP.u.ioParams.reqCount = param->ioReqCount;				ADSP.u.ioParams.dataPtr = (uPtr) param->ioBuffer;				err = PBControl(&ADSP,FALSE);				param->ioActCount = ADSP.u.ioParams.actCount;				param->ioPosMode = ADSP.u.ioParams.eom;				if (ADSP.u.ioParams.eom && autoSave) {					if (emptyEOM > 1) {						if (param->ioActCount == sizeof(file_info)) {							emptyEOM = 0;							d->dCtlFlags -= dNeedTime;							err = recFile();							d->dCtlFlags |= dNeedLock | dNeedTime;							param->ioActCount = 0;						}					}					if (param->ioActCount == 0)						emptyEOM++;				}				else					emptyEOM = 0;			}			break;		case control:			p = (cntrlParam*) param;			switch (p->csCode) {				case openConn:					err = CloseComm();					BlockMove(&(p->csParam[0]),&address,4L);					err = OpenComm(FALSE);					if (err)						OpenComm(TRUE);					else						deskOpened = TRUE;					break;				case closeConn:					err = CloseComm();					if (!err)						err = OpenComm(TRUE);					break;				case autoFile:					autoSave = p->csParam[0];					break;				case sendFile:					d->dCtlFlags -= dNeedTime;					err = file(p);					d->dCtlFlags |= dNeedLock | dNeedTime;					break;				case disable:					if (CCB.state == sPassive)						err = CloseComm();					canAnswer = FALSE;					break;				case enable:					canAnswer = TRUE;					if (CCB.state == sClosed)						err = OpenComm(TRUE);					break;				case deskFlag:					deskOpened = p->csParam[0];					break;				case accRun:					if (!deskOpened) {						if ((CCB.state == sOpen) && (oldState != sOpen)) {							w = FrontWindow();							if (((WindowPeek) w)->windowKind != dBoxProc) {								deskOpened = TRUE;								if (desk[0])									OpenDeskAcc(desk);							}						}					}					oldState = CCB.state;					if ((CCB.state == sClosed) && canAnswer)						err = OpenComm(TRUE);					break;				default:					err = controlErr;					break;			}			break;		case status:			p = (cntrlParam*) param;			switch (p->csCode) {				case getBuf:					if (CCB.state == sOpen) {						ADSP.csCode = dspStatus;						err = PBControl(&ADSP,FALSE);						len = ADSP.u.statusParams.recvQPending;						BlockMove(&len,&(p->csParam[0]),4L);					}					else {						p->csParam[0] = 0;						p->csParam[1] = 0;					}					p->csParam[2] = CCB.state;					p->csParam[3] = CCB.userFlags;					p->csParam[4] = CCB.attnCode;					CCB.userFlags = 0;					BlockMove(&CCB.attnPtr,&(p->csParam[5]),4L);					break;				default:					err = statusErr;					break;			}			break;		case close:			err = CloseComm();			ADSP.csCode = dspRemove;			ADSP.u.closeParams.abort = TRUE;			err = PBControl(&ADSP,FALSE);			break;	}	return(err);	asm {		dc.l '� 19'		dc.l '89 A'		dc.l 'pple'		dc.l ' Com'		dc.l 'pute'		dc.l 'r, I'		dc.l 'nc. '		dc.l 'Rica'		dc.l 'rdo '		dc.l 'Bati'		dc.l 'sta.'	}}int OpenComm(waitForCall)Boolean waitForCall;{	int err = 0, net, node;	ADSP.csCode = dspOpen;	ADSP.ccbRefNum = CCB.refNum;	ADSP.u.openParams.localCID = 0;	ADSP.u.openParams.remoteCID = 0;	ADSP.u.openParams.remoteAddress = address;	ADSP.u.openParams.filterAddress.aSocket = 0;	ADSP.u.openParams.filterAddress.aNode = 0;	ADSP.u.openParams.filterAddress.aNet = 0;	ADSP.u.openParams.sendSeq = 0;	ADSP.u.openParams.sendWindow = idealSize;	ADSP.u.openParams.recvSeq = 0;	ADSP.u.openParams.attnSendSeq = 0;	ADSP.u.openParams.attnRecvSeq = 0;	ADSP.u.openParams.ocInterval = 6;	ADSP.u.openParams.ocMaximum = 3;	if (waitForCall) {		if (canAnswer) {			ADSP.u.openParams.remoteAddress.aNet = 0;			ADSP.u.openParams.remoteAddress.aNode = 0;			ADSP.u.openParams.remoteAddress.aSocket = 0;			ADSP.u.openParams.ocMode = 	ocPassive;			err = PBControl(&ADSP,TRUE);		}	}	else {		err = GetNodeAddress(&node,&net);		if (net == address.aNet) {			if ((node == address.aNode) && (CCB.localSocket == address.aSocket))				return(-1);		}		ADSP.u.openParams.ocMode = ocRequest;		err = PBControl(&ADSP,FALSE);	}	return(err);}int InitComm(){	int err;	desk[0] = 0;	emptyEOM = 0;	deskOpened = autoSave = FALSE;	canAnswer = TRUE;	ADSP.ioNamePtr = 0L;	ADSP.ioCRefNum = ADSPID;	ADSP.ioCompletion = 0L;	ADSP.ccbRefNum = 0;	ADSP.csCode = dspInit;	ADSP.u.initParams.ccbPtr = &CCB;	ADSP.u.initParams.userRoutine = 0L;	ADSP.u.initParams.sendQSize = attnBufSize;	ADSP.u.initParams.recvQSize = attnBufSize;	ADSP.u.initParams.sendQueue = (uPtr) &OutBuffer[0];	ADSP.u.initParams.recvQueue = (uPtr) &InBuffer[0];	ADSP.u.initParams.attnPtr = (uPtr) &attnBuffer[0];	ADSP.u.initParams.localSocket = 0;	CCB.refNum = 0;	err = PBControl(&ADSP,FALSE);	CCB.refNum = ADSP.ccbRefNum;	if (!err)		DoName(ADSP.u.initParams.localSocket);	return(err);}int DoName(Socket)int Socket;{	StringHandle str = 0L;	EntityName name;	long nameSize;	Ptr rp;	int err;	MPPParamBlock p;	NamesTableEntry *NTPtr;	int index;	str = (StringHandle) GetString(-16096);	if (str) {		HLock(str);		BlockMove(*str,&(name.objStr[0]),33L);		HUnlock(str);		ReleaseResource(str);	}	if (name.objStr[0] == 0) {		name.objStr[0] = 1;		name.objStr[1] = '?';	}	str = (StringHandle) GetString(268);	if (str) {		HLock(str);		BlockMove(*str,&(name.typeStr[0]),33L);		HUnlock(str);		ReleaseResource(str);	}	str = (StringHandle) GetString(269);	if (str) {		HLock(str);		BlockMove(*str,&(desk[1]),33L);		HUnlock(str);		ReleaseResource(str);		if (desk[1]) {			desk[0] = desk[1] + 1;			desk[1] = 0;		}	}	name.zoneStr[0] = 1;	name.zoneStr[1] = '*';		nameSize = sizeof(NamesTableEntry);	asm {		move.l nameSize,d0		_NewPtr SYS+CLEAR		move.l a0,NTPtr	}	NTPtr->nteAddress.aSocket = Socket;	p.NBPinterval = 3;	p.NBPcount = 3;	p.NBPverifyFlag = TRUE;	p.NBPntQElPtr = (Ptr) NTPtr;	BlockMove(name.objStr,NTPtr->entityData,33L);	index = name.objStr[0] + 1;	BlockMove(name.typeStr,&(NTPtr->entityData[index]),33L);	index += name.typeStr[0] + 1;	BlockMove(name.zoneStr,&(NTPtr->entityData[index]),33L);	err = PRegisterName(&p,FALSE);	while (err == nbpDuplicate) {		name.objStr[0]++;		name.objStr[name.objStr[0]] = '1';		BlockMove(name.objStr,NTPtr->entityData,33L);		index = name.objStr[0] + 1;		BlockMove(name.typeStr,&(NTPtr->entityData[index]),33L);		index += name.typeStr[0] + 1;		BlockMove(name.zoneStr,&(NTPtr->entityData[index]),33L);		err = PRegisterName(&p,FALSE);	}	return(err);}int CloseComm(){	ADSP.csCode = dspClose;	ADSP.u.closeParams.abort = TRUE;	return(PBControl(&ADSP,FALSE));}/*	The format of a file being sent is two EOM's in a row, then the finder info	with the EOM bit set, then the data fork, with the EOM at the end and finally	the resource fork, with the EOM bit set at the end.*/int file(p)cntrlParam* p;{	Ptr file_name;	int vRefNum, vol = 0, err = 0, refNum;	long rlen = 0L, dlen = 0L, len;	char buffer[512];	file_info f;	fileParam fp;	BlockMove(&(p->csParam[0]),&file_name,4L);	vRefNum = p->csParam[2];	ADSP.csCode = dspWrite;	ADSP.u.ioParams.reqCount = 5;	ADSP.u.ioParams.dataPtr = (uPtr) "File:";	ADSP.u.ioParams.eom = TRUE;	ADSP.u.ioParams.flush = TRUE;	err = PBControl(&ADSP,FALSE);			/* Flush whatever data is there */	if (err)		return(err);	ADSP.u.ioParams.reqCount = 0;	ADSP.u.ioParams.eom = TRUE;				/* write just an EOM */	err = PBControl(&ADSP,FALSE);	if (err)		return(err);	err = PBControl(&ADSP,FALSE);				/* Once more, another EOM */	if (err)		return(err);	ADSP.u.ioParams.flush = FALSE;	fp.ioCompletion = 0L;	fp.ioFVersNum = 0;	fp.ioFDirIndex = 0;	fp.ioNamePtr = (StringPtr) file_name;	fp.ioVRefNum = vRefNum;	err = PBGetFInfo(&fp,FALSE);	if (err)		return(err);	f.info = 'FT';	dlen = f.data_len = fp.ioFlLgLen;	rlen = f.res_len = fp.ioFlRLgLen;	f.file_type = fp.ioFlFndrInfo.fdType;	f.file_creator = fp.ioFlFndrInfo.fdCreator;	f.finder_flags = fp.ioFlFndrInfo.fdFlags;	BlockMove(file_name,&f.file_name[0],65L);	ADSP.u.ioParams.reqCount = sizeof(file_info);	ADSP.u.ioParams.dataPtr = (uPtr) &f;	ADSP.u.ioParams.eom = TRUE;	err = PBControl(&ADSP,FALSE);	if (err)		return(err);	err = FSOpen(file_name,vRefNum,&refNum);	if (err)		return(err);	ADSP.u.ioParams.dataPtr = (uPtr) &buffer[0];	ADSP.u.ioParams.eom = FALSE;	do {		if (dlen > 512)			len = 512;		else			len = dlen;		dlen -= len;		if (dlen == 0)			ADSP.u.ioParams.eom = TRUE;		if (len)			err = FSRead(refNum,&len,&buffer[0]);		ADSP.u.ioParams.reqCount = len;		err = PBControl(&ADSP,FALSE);		if (err) {			FSClose(refNum);			return(err);		}	} while (dlen > 0L);	FSClose(refNum);	ADSP.u.ioParams.eom = FALSE;	err = OpenRF(file_name,vRefNum,&refNum);	if (err)		return(err);	do {		if (rlen > 512)			len = 512;		else			len = rlen;		rlen -= len;		if (rlen == 0)			ADSP.u.ioParams.eom = TRUE;		if (len)			err = FSRead(refNum,&len,&buffer[0]);		ADSP.u.ioParams.reqCount = len;		err = PBControl(&ADSP,FALSE);		if (err) {			FSClose(refNum);			return(err);		}	} while (rlen > 0L);	FSClose(refNum);	return(noErr);}int recFile(){	SFReply reply;	file_info info;	long dlen = 0, rlen = 0, len = 0;	int err, refNum;	Point where;	char buffer[512];	FInfo finder;	where.h = where.v = 100;	BlockMove(ADSP.u.ioParams.dataPtr,&info,(long) sizeof(file_info));	SFPutFile(where,"\pSave as :",info.file_name,0L,&reply);	if (!reply.good)		return(readErr);	dlen = info.data_len;	rlen = info.res_len;	ADSP.csCode = dspRead;	ADSP.u.ioParams.dataPtr = (uPtr) buffer;	err = Create(reply.fName,reply.vRefNum,info.file_creator,info.file_type);	if (err == dupFNErr) {		err = FSDelete(reply.fName,reply.vRefNum);		err = Create(reply.fName,reply.vRefNum,info.file_type,info.file_creator);	}	if (err)		return(err);	err = FSOpen(reply.fName,reply.vRefNum,&refNum);	if (err)		return(err);	do {		if (dlen > 512)			len = 512;		else			len = dlen;		dlen -= len;		ADSP.u.ioParams.reqCount = len;		err = PBControl(&ADSP,FALSE);		if (err) {			FSClose(refNum);			return(err);		}		if (len)			err = FSWrite(refNum,&len,&buffer[0]);	} while (dlen > 0L);	FSClose(refNum);	err = OpenRF(reply.fName,reply.vRefNum,&refNum);	if (err)		return(err);	do {		if (rlen > 512)			len = 512;		else			len = rlen;		rlen -= len;		ADSP.u.ioParams.reqCount = len;		err = PBControl(&ADSP,FALSE);		if (err) {			FSClose(refNum);			return(err);		}		if (len)			err = FSWrite(refNum,&len,&buffer[0]);	} while (rlen > 0L);	FSClose(refNum);	err = GetFInfo(reply.fName,reply.vRefNum,&finder);	finder.fdFlags = info.finder_flags;	err = SetFInfo(reply.fName,reply.vRefNum,&finder);	return(err);}