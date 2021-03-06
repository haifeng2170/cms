/*
The MIT License (MIT)

Copyright (c) 2017- cms(hsc)

Author: 天空没有乌云/kisslovecsh@foxmail.com

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/
#include <flvPool/cms_flv_pool.h>
#include <common/cms_utility.h>
#include <common/cms_char_int.h>
#include <log/cms_log.h>
#include <protocol/cms_flv.h>
#include <common/cms_char_int.h>
#include <static/cms_static_common.h>
#include <mem/cms_fix_mem.h>
#include <app/cms_app_info.h>
#include <app/cms_parse_args.h>
#include <mem/cms_mf_mem.h>
using namespace std;

#define MapHashStreamIter map<HASH,StreamSlice *>::iterator
#define SetHashIter set<HASH>::iterator
#define VectorTTKKIter vector<TTandKK *>::iterator
#define VectorSliceIter vector<Slice *>::iterator

#ifdef __CMS_POOL_MEM__

static CmsFixMem *sliceFixMem[APP_ALL_MODULE_THREAD_NUM];
static CLock sliceLockFixMem[APP_ALL_MODULE_THREAD_NUM];

static CmsFixMem *tt8kkFixMem[APP_ALL_MODULE_THREAD_NUM];
static CLock tt8kkLockFixMem[APP_ALL_MODULE_THREAD_NUM];

static void initFlvFixMem()
{
	for (int i = 0; i < APP_ALL_MODULE_THREAD_NUM; i++)
	{
		sliceFixMem[i] = xmallocFixMem(sizeof(Slice), 250);
		sliceFixMem[i]->idx = i;
		tt8kkFixMem[i] = xmallocFixMem(sizeof(TTandKK), 1000);
		tt8kkFixMem[i]->idx = i;
	}
}

static void releaseFlvFixMem()
{
	for (int i = 0; i < APP_ALL_MODULE_THREAD_NUM; i++)
	{
		xfreeFixMem(sliceFixMem[i]);
		xfreeFixMem(tt8kkFixMem[i]);
	}
}

static void *mallocSlice(int i)
{
	void *ptr = NULL;
	sliceLockFixMem[i].Lock();
	ptr = xmallocFix(sliceFixMem[i]);
	sliceLockFixMem[i].Unlock();
	return ptr;
}

static void freeSlice(int i, void *ptr)
{
	sliceLockFixMem[i].Lock();
	xfreeFix(sliceFixMem[i], ptr);
	sliceLockFixMem[i].Unlock();
}


static void *mallocTTandKK(int i)
{
	void *ptr = NULL;
	tt8kkLockFixMem[i].Lock();
	ptr = xmallocFix(tt8kkFixMem[i]);
	tt8kkLockFixMem[i].Unlock();
	return ptr;
}

static void freeTTandKK(int i, void *ptr)
{
	tt8kkLockFixMem[i].Lock();
	xfreeFix(tt8kkFixMem[i], ptr);
	tt8kkLockFixMem[i].Unlock();
}

#endif //__CMS_POOL_MEM__


#ifdef __CMS_CYCLE_MEM__
void atomicInc(StreamSlice *ss)
{

	if (ss != NULL)
	{
		__sync_add_and_fetch(&ss->mionly, 1);//当数据超时，且没人使用时，删除
	}
}

void atomicDec(StreamSlice *ss)
{
	if (ss != NULL)
	{
		__sync_sub_and_fetch(&ss->mionly, 1);
	}
}

#endif //__CMS_CYCLE_MEM__

void atomicInc(Slice *s)
{
	if (s != NULL)
	{		
#ifdef __CMS_CYCLE_MEM__
		int ionly = __sync_add_and_fetch(&s->mionly, 1);//当数据超时，且没人使用时，删除
		if (ionly > 1 && s->mcycMem)
		{
			//s->mionly == 1 时，只有 StreamSlice 引用了Slice 这时不需要增加
			atomicInc(s->mss);
		}
#else
		__sync_add_and_fetch(&s->mionly, 1);//当数据超时，且没人使用时，删除
#endif		
	}
}

void atomicDec(Slice *s)
{
	if (s != NULL)
	{
#ifdef __CMS_CYCLE_MEM__		
		int ionly = 0;
		if ((ionly = __sync_sub_and_fetch(&s->mionly, 1)) == 0)//当数据超时，且没人使用时，删除
#else
		if (__sync_sub_and_fetch(&s->mionly, 1) == 0)//当数据超时，且没人使用时，删除
#endif	

		{
			if (s->mData)
			{
#ifdef __CMS_CYCLE_MEM__
				if (s->mcycMem)
				{
					xfreeCycleBuf(s->mcycMem, s->mData);
				}
				else
				{
					//只有发送时临时创建的Slice才不在循环内存内 请看 mergeKeyFrame 函数调用
					//或者 metaData、首帧音频和视频也不在循环内存内
					xfree(s->mData);
				}
#else
				xfree(s->mData);
#endif		
			}
			if (s->mpMajorHash)
			{
				xfree(s->mpMajorHash);
			}
			// 			if (s->mpHash) //浅拷贝内存不能释放
			// 			{
			// 				xfree(s->mpHash);
			// 			}
			if (s->mpUrl)
			{
				xfree(s->mpUrl);
			}
			if (s->mpVideoType)
			{
				xfree(s->mpVideoType);
			}
			if (s->mpAudioType)
			{
				xfree(s->mpAudioType);
			}
			if (s->mpReferUrl)
			{
				xfree(s->mpReferUrl);
			}
			if (s->mpRemoteIP)
			{
				xfree(s->mpRemoteIP);
			}
			if (s->mpHost)
			{
				xfree(s->mpHost);
			}
#ifdef __CMS_POOL_MEM__
			freeSlice(s->midxFixMem, s);
#else
			xfree(s);
#endif			
		}
#ifdef __CMS_CYCLE_MEM__
		if (ionly >= 1 && s->mcycMem)
		{
			//表示 s->mss 增加被引用过
			atomicDec(s->mss); //释放StreamSlice引用
		}
#endif
	}
}

CAutoSlice::CAutoSlice(Slice *s)
{
	ms = s;
}

CAutoSlice::~CAutoSlice()
{
	if (ms)
	{
		atomicDec(ms);
		ms = NULL;
	}
}

Slice *newSlice(uint32 i)
{
#ifdef __CMS_POOL_MEM__
	Slice *s = (Slice*)mallocSlice(i);
#else
	Slice *s = (Slice*)xmalloc(sizeof(Slice));
#endif
	s->midxFixMem = i;

	s->mionly = 1;
	s->miDataType = DATA_TYPE_NONE;
	s->misHaveMediaInfo = false;
	s->misPushTask = false;
	s->misNoTimeout = false;
	s->misMetaData = false;
	s->misRemove = false;
	s->miNotPlayTimeout = 0;
	s->muiTimestamp = 0;

	s->mllP2PIndex = 0;
	s->mllIndex = 0;
	s->mllOffset = 0;
	s->mllStartTime = 0;
	s->mData = NULL;
	s->miDataLen = 0;

	s->misKeyFrame = false;
	s->miMediaRate = 0;
	s->miVideoRate = 0;
	s->miAudioRate = 0;
	s->miVideoFrameRate = 0;
	s->miAudioFrameRate = 0;
	s->miAudioChannelID = -1;

	s->misH264 = false;
	s->misH265 = false;

	s->mllCacheTT = 0;
	s->miPlayStreamTimeout = 0;
	s->misRealTimeStream = false;
	s->miFirstPlaySkipMilSecond = -1;
	s->miAutoBitRateMode = AUTO_DROP_BITRATE_OPEN;
	s->miAutoBitRateFactor = 10;
	s->miAutoFrameFactor = 3;
	s->miBufferAbsolutely = 0;
	s->misResetStreamTimestamp = false;

	s->miLiveStreamTimeout = 0;
	s->miNoHashTimeout = 0;

	s->mpMajorHash = NULL;
	s->mpHash = NULL;
	s->mpUrl = NULL;
	s->mpVideoType = NULL;
	s->mpAudioType = NULL;
	s->mpReferUrl = NULL;
	s->mpRemoteIP = NULL;
	s->mpHost = NULL;
#ifdef __CMS_CYCLE_MEM__
	s->mcycMem = NULL;
	s->mss = NULL;
#endif
	return s;
}

StreamSlice *newStreamSlice()
{
	StreamSlice *ss = new StreamSlice;
#ifdef __CMS_CYCLE_MEM__
	ss->mionly = 0;
#endif
	ss->mptrHash = NULL;
	ss->mllNearKeyFrameIdx = -1;
	ss->muiTheLastVideoTimestamp = 0;

	ss->misPushTask = false;
	ss->mnoTimeout = false;
	ss->miNotPlayTimeout = 0;
	ss->mllAccessTime = 0;
	ss->mllCreateTime = 0;

	ss->miVideoFrameCount = 0;
	ss->miAudioFrameCount = 0;
	ss->miVideoFrameRate = 30;
	ss->miAudioFrameRate = 43;
	ss->miMediaRate = 0;
	ss->miVideoRate = 0;
	ss->miAudioRate = 0;
	ss->miVideoFrameRate = 0;
	ss->miAudioFrameRate = 0;
	ss->muiKeyFrameDistance = 0;
	ss->muiLastKeyFrameDistance = 0;

	ss->mllLastSliceIdx = 0;

	ss->mfirstVideoSlice = NULL;
	ss->mllFirstVideoIdx = -1;
	ss->mfirstAudioSlice = NULL;
	ss->mllFirstAudioIdx = -1;
	ss->misH264 = false;
	ss->misH265 = false;
	ss->mllVideoAbsoluteTimestamp = -1;
	ss->mllAudioAbsoluteTimestamp = -1;
	ss->misHaveMetaData = false;
	ss->mllMetaDataIdx = -1;
	ss->mllMetaDataP2PIdx = -1;
	ss->mmetaDataSlice = NULL;
	ss->misNoTimeout = false;

	ss->mllLastMemSize = 0;
	ss->mllMemSize = 0;
	ss->mllMemSizeTick = 0;

	ss->mllCacheTT = 0;
	ss->miPlayStreamTimeout = 0;
	ss->misRealTimeStream = false;
	ss->miFirstPlaySkipMilSecond = -1;
	ss->miAutoBitRateMode = AUTO_DROP_CHANGE_BITRATE_CLOSE;
	ss->miAutoBitRateFactor = 10;
	ss->miAutoFrameFactor = 3;
	ss->miBufferAbsolutely = 0;
	ss->misResetStreamTimestamp = false;

	//边缘才会用到的保存流断开时的状态
	/*ss->misNeedJustTimestamp = false;
	ss->misRemove = false;
	ss->misHasBeenRemove = false;
	ss->mllRemoveTimestamp = 0;
	ss->muiLastVideoTimestamp = 0;
	ss->muiLastAudioTimestamp = 0;
	ss->muiLast2VideoTimestamp = 0;
	ss->muiLast2AudioTimestamp = 0;*/

	ss->mllUniqueID = -1;

	//边推才有效
	ss->miLiveStreamTimeout = 0;
	ss->miNoHashTimeout = 0;
	return ss;
}

struct RoutinueParam
{
	uint32 i;
	CFlvPool *pInstance;
};

CFlvPool *CFlvPool::minstance = NULL;
CFlvPool::CFlvPool()
{
	misRun = false;
	for (int i = 0; i < APP_ALL_MODULE_THREAD_NUM; i++)
	{
		mtid[i] = 0;
	}
#ifdef __CMS_CYCLE_MEM__
	mtidCycMem = 0;
#endif
}

CFlvPool::~CFlvPool()
{
	for (int i = 0; i < APP_ALL_MODULE_THREAD_NUM; i++)
	{
		mqueueLock[i].Lock();
		while (!mqueueSlice[i].empty())
		{
			Slice *s = mqueueSlice[i].front();
			mqueueSlice[i].pop();
			if (s)
			{
				if (s->mData)
				{
					xfree(s->mData);
				}
				xfree(s);
			}
		}
		mqueueLock[i].Unlock();

		mhashSliceLock[i].WLock();
		for (MapHashStreamIter iterM = mmapHashSlice[i].begin(); iterM != mmapHashSlice[i].end();)
		{
			StreamSlice *ss = iterM->second;
			releaseSS(ss);
			mmapHashSlice[i].erase(iterM++);
		}
		mhashSliceLock[i].UnWLock();
	}
#ifdef __CMS_CYCLE_MEM__
	mcycMemLoc.Lock();
	std::list<StreamSlice *>::iterator it = mlistCycMem.begin();
	for (; it != mlistCycMem.end(); )
	{
		releaseSS(*it);
		mlistCycMem.erase(it++);
	}
	mcycMemLoc.Unlock();
#endif

}

void *CFlvPool::routinue(void *param)
{
	RoutinueParam *rp = (RoutinueParam *)param;
	CFlvPool *pInstance = (CFlvPool *)rp->pInstance;
	uint32 i = rp->i;
	pInstance->thread(i);
	return NULL;
}

void CFlvPool::thread(uint32 i)
{
	logs->debug("### CFlvPool thread=%d ###", gettid());
	setThreadName("cms-flv");
	bool is;
	Slice *s;
	while (misRun)
	{
		is = pop(i, &s);
		if (is)
		{
			handleSlice(i, s);
		}
		else
		{
			cmsSleep(10);
		}
	}
}

#ifdef __CMS_CYCLE_MEM__
void *CFlvPool::routinueDelayReleaseCycMem(void *param)
{
	RoutinueParam *rp = (RoutinueParam *)param;
	CFlvPool *pInstance = (CFlvPool *)rp->pInstance;
	pInstance->threadDelayReleaseCycMem();
	return NULL;
}

void CFlvPool::threadDelayReleaseCycMem()
{
	logs->debug("### CFlvPool threadDelayReleaseCycMem tid=%d ###", gettid());
	setThreadName("cms-flv-dl");
	StreamSlice *ss;
	while (misRun)
	{
		mcycMemLoc.Lock();
		mcycMemLoc.WaitTime(1000);
		std::list<StreamSlice *>::iterator it = mlistCycMem.begin();
		for (; it != mlistCycMem.end(); )
		{
			ss = *it;
// 			printf("ss->mionly=%d\n", ss->mionly);
			if (ss->mionly == 0)
			{
				//没有引用可以删除了
				releaseSS(ss);
				mlistCycMem.erase(it++);
				logs->debug("CFlvPool::threadDelayReleaseCycMem release cycle mem");
			}
			else
			{
				it++;
			}
		}
		mcycMemLoc.Unlock();
	}
}

void CFlvPool::pushSS(StreamSlice *ss)
{
	mcycMemLoc.Lock();
	mcycMemLoc.Signal();
	mlistCycMem.push_back(ss);
	mcycMemLoc.Unlock();
}

void CFlvPool::ss2s(StreamSlice *ss, Slice *s)
{
	s->mss = ss;
}

#endif

bool CFlvPool::run()
{
	misRun = true;
	for (int i = 0; i < APP_ALL_MODULE_THREAD_NUM; i++)
	{
		RoutinueParam *rp = new RoutinueParam;
		rp->i = i;
		rp->pInstance = this;
		int res = cmsCreateThread(&mtid[i], routinue, rp, false);
		if (res == -1)
		{
			char date[128] = { 0 };
			getTimeStr(date);
			logs->error("%s ***** file=%s,line=%d cmsCreateThread error *****", date, __FILE__, __LINE__);
			return false;
		}
	}
#ifdef __CMS_POOL_MEM__
	initFlvFixMem();
#endif
#ifdef __CMS_CYCLE_MEM__
	RoutinueParam *rp = new RoutinueParam;
	rp->i = 0;
	rp->pInstance = this;
	int res = cmsCreateThread(&mtidCycMem, routinueDelayReleaseCycMem, rp, false);
	if (res == -1)
	{
		char date[128] = { 0 };
		getTimeStr(date);
		logs->error("%s ***** file=%s,line=%d cmsCreateThread error *****", date, __FILE__, __LINE__);
		return false;
	}
#endif
	return true;
}

CFlvPool *CFlvPool::instance()
{
	if (minstance == NULL)
	{
		minstance = new CFlvPool;
	}
	return minstance;
}

void CFlvPool::freeInstance()
{
	if (minstance)
	{
		delete minstance;
		minstance = NULL;
	}
}

void CFlvPool::stop()
{
	logs->debug("##### CFlvPool::stop begin #####");
	misRun = false;
	for (int i = 0; i < APP_ALL_MODULE_THREAD_NUM; i++)
	{
		mqueueLock[i].Lock(); //发送信号停止
		mqueueLock[i].Signal();
		mqueueLock[i].Unlock();
		cmsWaitForThread(mtid[i], NULL);
		mtid[i] = 0;
	}
#ifdef __CMS_POOL_MEM__
	releaseFlvFixMem();
#endif
	logs->debug("##### CFlvPool::stop finish #####");
}

uint32 CFlvPool::hashIdx(HASH &hash)
{
	uint32 i = bigUInt32((char *)hash.data);
	return i % APP_ALL_MODULE_THREAD_NUM;
}

void CFlvPool::push(uint32 i, Slice *s)
{
	if (g_isTestServer)
	{
		//作为压测服务 不需要保存数据
		atomicDec(s);
	}
	else
	{
		mqueueLock[i].Lock();
		if (mqueueSlice[i].empty())
		{
			mqueueLock[i].Signal();
		}
		mqueueSlice[i].push(s);
		mqueueLock[i].Unlock();
	}
}

bool CFlvPool::pop(uint32 i, Slice **s)
{
	bool res = false;
	mqueueLock[i].Lock();
	while (mqueueSlice[i].empty())
	{
		if (!misRun)
		{
			goto End;
		}
		mqueueLock[i].Wait();
	}
	*s = mqueueSlice[i].front();
	mqueueSlice[i].pop();
	res = true;
End:
	mqueueLock[i].Unlock();
	return res;
}

bool CFlvPool::justJump2VideoLastXSecond(uint32 i, HASH &hash, uint32 &st, uint32 &ts, int64 &transIdx)
{
	transIdx = -1;
	bool isSucc = false;
	mhashSliceLock[i].RLock();
	MapHashStreamIter iterM = mmapHashSlice[i].find(hash);
	if (iterM != mmapHashSlice[i].end())
	{
		StreamSlice * ss = iterM->second;
		ss->mLock.RLock();

		size_t avSize = ss->mavSlice.size();
		size_t vkSize = ss->mvKeyFrameIdx.size();
		if (vkSize > 0 && avSize > 0)
		{
			int64 minIdx = ss->mavSliceIdx.at(0);
			for (int ii = ((int)vkSize) - 1; ii >= 0; ii--)
			{
				int64 keyIdx = ss->mvKeyFrameIdx.at(ii);
				Slice *s = ss->mavSlice.at(keyIdx - minIdx);
				uint32 leftBuf = 0;
				if (s->miVideoFrameRate > 0 && s->miAudioFrameRate > 0)
				{
					int avFrameRate = s->miVideoFrameRate + s->miAudioFrameRate;
					leftBuf = (uint32)((int)((int64)avSize - (keyIdx - minIdx)) * 1000 / avFrameRate);
				}
				if ((leftBuf > 0 && leftBuf > DropVideoMinSeconds && s->muiTimestamp <= st - DropVideoMinSeconds) ||
					(leftBuf == 0 && s->muiTimestamp <= st - DropVideoMinSeconds))
				{ //可能卡顿导致 时间戳满足3秒 但帧数不满足 需要判断同时满足
					isSucc = true;
					transIdx = keyIdx - 1;
					ts = s->muiTimestamp;
					logs->debug(">>>>>>hsc justJump2VideoLastXSecond read last task %s seek keyframe,idx=%lld, st=%u, s->muiTimestamp=%u.",
						s->mpUrl, transIdx, st, s->muiTimestamp);
					break;
				}
			}
		}

		ss->mLock.UnRLock();
	}
	mhashSliceLock[i].UnRLock();
	return isSucc;
}

int  CFlvPool::readFirstVideoAudioSlice(uint32 i, HASH &hash, Slice **s, bool isVideo)
{
	int ret = FlvPoolCodeError;
	mhashSliceLock[i].RLock();
	MapHashStreamIter iterM = mmapHashSlice[i].find(hash);
	if (iterM != mmapHashSlice[i].end())
	{
		StreamSlice * ss = iterM->second;
		ss->mLock.RLock();
		if (isVideo)
		{
			*s = ss->mfirstVideoSlice;
		}
		else
		{
			*s = ss->mfirstAudioSlice;
		}
		if (*s)
		{
			atomicInc(*s);
		}
		ss->mLock.UnRLock();
		ret = FlvPoolCodeOK;
	}
	mhashSliceLock[i].UnRLock();
	return ret;
}

int  CFlvPool::readSlice(uint32 i, HASH &hash, int64 &llIdx, Slice **s, int &sliceNum, bool isTrans, int64 llMetaDataIdx, int64 llFirstVideoIdx, int64 llFirstAudioIdx,
	bool &isExist, bool &isTaskRestart, bool isPublishTask, bool &isMetaDataChanged, bool &isFirstVideoAudioChanged, uint64 &transUid)
{
	*s = NULL;
	int ret = FlvPoolCodeError;
	mhashSliceLock[i].RLock();
	MapHashStreamIter iterM = mmapHashSlice[i].find(hash);
	if (iterM != mmapHashSlice[i].end())
	{
		ret = FlvPoolCodeOK;
		StreamSlice *ss = iterM->second;
		ss->mLock.RLock();

		isExist = true;
		//check metaData
		isPublishTask = ss->misPushTask;
		if (ss->misHaveMetaData && (llMetaDataIdx == -1 || ss->mllMetaDataIdx == llIdx + 1))
		{
			isMetaDataChanged = true;
		}
		//check metaData end
		//check first video audio changed
		if (ss->mfirstVideoSlice)
		{
			if (llFirstVideoIdx == -1 || ss->mllFirstVideoIdx == llIdx + 1)
			{
				isFirstVideoAudioChanged = true;
			}
		}
		if (ss->mfirstAudioSlice)
		{
			if (llFirstAudioIdx == -1 || ss->mllFirstAudioIdx == llIdx + 1)
			{
				isFirstVideoAudioChanged = true;
			}
		}
		//check first video audio changed end

		do
		{
			int64 duration = 0;
			if (!isTrans)
			{
				duration = 5;
			}
			size_t size = ss->mavSlice.size();
			if (size > 0)
			{
				int64 minIdx = ss->mavSliceIdx[0];
				int64 maxIdx = (int64)size + minIdx - 1;
				int nkf = ss->mvKeyFrameIdx.size();
				bool isKeyFrame = false;
				if (transUid != 0 && transUid != ss->muid)
				{
					//任务重启过
					logs->info(">>>>> [CFlvPool::readSlice] readSlice %s maybe has been restart.", ss->mstrUrl.c_str());
					ret = FlvPoolCodeRestart;
				}
				else if (llIdx + 1 > maxIdx)
				{
					ret = FlvPoolCodeNoData;
				}
				else if (llIdx != -1 && llIdx + 1 < minIdx)
				{
					//出现丢帧情况,为了是播放不花屏，要从关键帧发送（针对H264）
					if (((ss->misH264 || ss->misH265) && (nkf > 0 || getTimeUnix() - ss->mllCreateTime > duration)) ||
						(ss->mstrVideoType != getVideoType(VideoTypeAVC) && ss->mstrVideoType != getVideoType(VideoTypeHEVC)))
					{
						if (nkf > 0)
						{
							//有关键帧
							isKeyFrame = true;
							int64 iKey = ss->mvKeyFrameIdx.at(nkf - 1);
							if (!ss->misRealTimeStream)
							{
								iKey = ss->mvKeyFrameIdx.at((nkf - 1) / 2);
								if (iKey < ss->mllFirstVideoIdx || iKey < ss->mllFirstAudioIdx)
								{
									//首帧变化过,直接从最新关键帧发送
									iKey = ss->mvKeyFrameIdx.at(nkf - 1);
								}
							}
							*s = ss->mavSlice[iKey - minIdx];
							sliceNum = maxIdx - iKey;
						}
						else
						{
							if (ss->misRealTimeStream)
							{
								*s = ss->mavSlice[maxIdx - minIdx];
								sliceNum = maxIdx - minIdx;
							}
							else
							{
								*s = ss->mavSlice[(maxIdx - minIdx) / 2];
								sliceNum = (maxIdx - minIdx) / 2;
							}
						}
					}
					else
					{
						if (ss->misRealTimeStream)
						{
							*s = ss->mavSlice[maxIdx - minIdx];
							sliceNum = maxIdx - minIdx;
						}
						else
						{
							*s = ss->mavSlice[(maxIdx - minIdx) / 2];
							sliceNum = (maxIdx - minIdx) / 2;
						}
						logs->debug(">>>>> [CFlvPool::readSlice] readSlice %s 1 jump frame,do not have keyframe ingore.key frame num %d,is h264 %s,is h265 %s",
							ss->mstrUrl.c_str(), nkf, ss->misH264 ? "true" : "false", ss->misH265 ? "true" : "false");
					}
					//出现丢帧情况,为了是播放不花屏，要从关键帧发送（针对H264） 结束
				}
				else if (llIdx == -1)
				{
					logs->info(">>>>> [CFlvPool::readSlice] readSlice %s is real time stream=%s",
						ss->mstrUrl.c_str(), ss->misRealTimeStream ? "true" : "false");
					//出现丢帧情况,为了是播放不花屏，要从关键帧发送（针对H264）
					if (((ss->misH264 || ss->misH265) && (nkf > 0 || getTimeUnix() - ss->mllCreateTime > duration)) ||
						(ss->mstrVideoType != getVideoType(VideoTypeAVC) && ss->mstrVideoType != getVideoType(VideoTypeHEVC)))
					{
						if (nkf > 0)
						{
							//有关键帧
							isKeyFrame = true;
							int64 iKey = ss->mvKeyFrameIdx.at(nkf - 1);
							if (!ss->misRealTimeStream)
							{
								iKey = ss->mvKeyFrameIdx.at((nkf - 1) / 2);
								if (iKey < ss->mllFirstVideoIdx || iKey < ss->mllFirstAudioIdx)
								{
									//首帧变化过,直接从最新关键帧发送
									iKey = ss->mvKeyFrameIdx.at(nkf - 1);
								}
							}
							*s = ss->mavSlice[iKey - minIdx];
							sliceNum = maxIdx - iKey;
						}
						else
						{
							if (ss->misRealTimeStream)
							{
								*s = ss->mavSlice[maxIdx - minIdx];
								sliceNum = maxIdx - minIdx;
							}
							else
							{
								*s = ss->mavSlice[(maxIdx - minIdx) / 2];
								sliceNum = (maxIdx - minIdx) / 2;
							}
						}
					}
					else
					{
						if (ss->misRealTimeStream)
						{
							*s = ss->mavSlice[maxIdx - minIdx];
							sliceNum = maxIdx - minIdx;
						}
						else
						{
							*s = ss->mavSlice[(maxIdx - minIdx) / 2];
							sliceNum = (maxIdx - minIdx) / 2;
						}
						logs->info(">>>>> [CFlvPool::readSlice] readSlice %s 2 jump frame,do not have keyframe ingore.key frame num %d,is h264 %s,is h265 %s",
							ss->mstrUrl.c_str(), nkf, ss->misH264 ? "true" : "false", ss->misH265 ? "true" : "false");
					}
					//出现丢帧情况,为了是播放不花屏，要从关键帧发送（针对H264） 结束
				}
				else
				{
					*s = ss->mavSlice[llIdx + 1 - minIdx];
					sliceNum = maxIdx - (llIdx + 1);
				}
				transUid = ss->muid;
				if (isKeyFrame)
				{
					if (*s)
					{
						logs->info(">>>>> [CFlvPool::readSlice] readSlice %s find keyframe, left slice %lld",
							ss->mstrUrl.c_str(), maxIdx - (*s)->mllIndex);
					}
					else
					{
						logs->info(">>>>> [CFlvPool::readSlice] readSlice %s find keyframe",
							ss->mstrUrl.c_str());
					}
				}
			}
			else
			{
				ret = FlvPoolCodeNoData;
			}
		} while (0);
		if (*s)
		{
			atomicInc(*s); //当数据超时，且没人使用时，删除
		}
		ss->mLock.UnRLock();
	}
	else
	{
#ifdef __CMS_DEBUG__
		logs->info(">>>>> [CFlvPool::readSlice] not find task.");
#endif
		ret = FlvPoolCodeTaskNotExist;
	}
	mhashSliceLock[i].UnRLock();
	return ret;
}

bool  CFlvPool::isHaveMetaData(uint32 i, HASH &hash)
{
	bool isHave = false;
	mhashSliceLock[i].RLock();
	MapHashStreamIter iterM = mmapHashSlice[i].find(hash);
	if (iterM != mmapHashSlice[i].end())
	{
		StreamSlice * ss = iterM->second;
		ss->mLock.RLock();
		isHave = iterM->second->misHaveMetaData;
		ss->mLock.UnRLock();
	}
	mhashSliceLock[i].UnRLock();
	return isHave;
}

bool CFlvPool::isExist(uint32 i, HASH &hash)
{
	bool isHave = false;
	mhashSliceLock[i].RLock();
	MapHashStreamIter iterM = mmapHashSlice[i].find(hash);
	if (iterM != mmapHashSlice[i].end())
	{
		isHave = true;
	}
	mhashSliceLock[i].UnRLock();
	return isHave;
}

bool CFlvPool::isFirstVideoChange(uint32 i, HASH &hash, int64 &videoIdx)
{
	bool isChange = false;
	mhashSliceLock[i].RLock();
	MapHashStreamIter iterM = mmapHashSlice[i].find(hash);
	if (iterM != mmapHashSlice[i].end())
	{
		//logs->debug(">>>>>isFirstVideoChange url %s ss->mfirstVideoSlice=%lld,mfirstVideoSlice=%lld",iterM->second->mstrUrl.c_str(),iterM->second->mllFirstVideoIdx,videoIdx);
		StreamSlice * ss = iterM->second;
		ss->mLock.RLock();
		if (ss->mfirstVideoSlice &&
			(ss->mllFirstVideoIdx == videoIdx + 1 || videoIdx == -1))
		{
			isChange = true;
		}
		ss->mLock.UnRLock();
	}
	mhashSliceLock[i].UnRLock();
	return isChange;
}

bool CFlvPool::isFirstAudioChange(uint32 i, HASH &hash, int64 &audioIdx)
{
	bool isChange = false;
	mhashSliceLock[i].RLock();
	MapHashStreamIter iterM = mmapHashSlice[i].find(hash);
	if (iterM != mmapHashSlice[i].end())
	{
		//logs->debug(">>>>>isFirstAudioChange url %s ss->mllFirstAudioIdx=%lld,mllFirstAudioIdx=%lld",iterM->second->mstrUrl.c_str(),iterM->second->mllFirstAudioIdx,audioIdx);
		StreamSlice * ss = iterM->second;
		ss->mLock.RLock();
		if (ss->mfirstAudioSlice &&
			(ss->mllFirstAudioIdx == audioIdx + 1 || audioIdx == -1))
		{
			isChange = true;
		}
		ss->mLock.UnRLock();
	}
	mhashSliceLock[i].UnRLock();
	return isChange;
}

bool CFlvPool::isMetaDataChange(uint32 i, HASH &hash, int64 &metaIdx)
{
	bool isChange = false;
	mhashSliceLock[i].RLock();
	MapHashStreamIter iterM = mmapHashSlice[i].find(hash);
	if (iterM != mmapHashSlice[i].end())
	{
		//logs->debug(">>>>>isMetaDataChange url %s ss->mllMetaDataIdx=%lld,metaIdx=%lld",iterM->second->mstrUrl.c_str(),iterM->second->mllMetaDataIdx,metaIdx);
		StreamSlice * ss = iterM->second;
		ss->mLock.RLock();
		if (ss->mmetaDataSlice &&
			(ss->mllMetaDataIdx == metaIdx + 1 || metaIdx == -1))
		{
			isChange = true;
		}
		ss->mLock.UnRLock();
	}
	mhashSliceLock[i].UnRLock();
	return isChange;
}

int CFlvPool::readMetaData(uint32 i, HASH &hash, Slice **s)
{
	int ret = FlvPoolCodeError;
	*s = NULL;
	mhashSliceLock[i].RLock();
	MapHashStreamIter iterM = mmapHashSlice[i].find(hash);
	if (iterM != mmapHashSlice[i].end())
	{
		StreamSlice * ss = iterM->second;
		ss->mLock.RLock();
		if (ss->mmetaDataSlice)
		{
			*s = ss->mmetaDataSlice;
			atomicInc(*s); //当数据超时，且没人使用时，删除
		}
		ss->mLock.UnRLock();
		ret = FlvPoolCodeOK;
	}
	mhashSliceLock[i].UnRLock();
	return ret;
}

int CFlvPool::getFirstVideo(uint32 i, HASH &hash, Slice **s)
{
	int ret = FlvPoolCodeError;
	*s = NULL;
	mhashSliceLock[i].RLock();
	MapHashStreamIter iterM = mmapHashSlice[i].find(hash);
	if (iterM != mmapHashSlice[i].end())
	{
		StreamSlice * ss = iterM->second;
		ss->mLock.RLock();
		if (ss->mfirstVideoSlice)
		{
			*s = ss->mfirstVideoSlice;
			atomicInc(*s); //当数据超时，且没人使用时，删除
		}
		ss->mLock.UnRLock();
		ret = FlvPoolCodeOK;
	}
	mhashSliceLock[i].UnRLock();
	return ret;
}

int CFlvPool::getFirstAudio(uint32 i, HASH &hash, Slice **s)
{
	int ret = FlvPoolCodeError;
	*s = NULL;
	mhashSliceLock[i].RLock();
	MapHashStreamIter iterM = mmapHashSlice[i].find(hash);
	if (iterM != mmapHashSlice[i].end())
	{
		StreamSlice * ss = iterM->second;
		ss->mLock.RLock();
		if (ss->mfirstAudioSlice)
		{
			*s = ss->mfirstAudioSlice;
			atomicInc(*s); //当数据超时，且没人使用时，删除
		}
		ss->mLock.UnRLock();
		ret = FlvPoolCodeOK;
	}
	mhashSliceLock[i].UnRLock();
	return ret;
}

bool CFlvPool::isH264(uint32 i, HASH &hash)
{
	bool is = false;
	mhashSliceLock[i].RLock();
	MapHashStreamIter iterM = mmapHashSlice[i].find(hash);
	if (iterM != mmapHashSlice[i].end())
	{
		StreamSlice * ss = iterM->second;
		ss->mLock.RLock();
		is = ss->misH264;
		ss->mLock.UnRLock();
	}
	mhashSliceLock[i].UnRLock();
	return is;
}

bool CFlvPool::isH265(uint32 i, HASH &hash)
{
	bool is = false;
	mhashSliceLock[i].RLock();
	MapHashStreamIter iterM = mmapHashSlice[i].find(hash);
	if (iterM != mmapHashSlice[i].end())
	{
		StreamSlice * ss = iterM->second;
		ss->mLock.RLock();
		is = ss->misH265;
		ss->mLock.UnRLock();
	}
	mhashSliceLock[i].UnRLock();
	return is;
}

int64 CFlvPool::getMinIdx(uint32 i, HASH &hash)
{
	int64 idx = 0;
	mhashSliceLock[i].RLock();
	MapHashStreamIter iterM = mmapHashSlice[i].find(hash);
	if (iterM != mmapHashSlice[i].end())
	{
		StreamSlice * ss = iterM->second;
		ss->mLock.RLock();
		if (!ss->mavSlice.empty())
		{
			idx = ss->mavSliceIdx.at(0);
		}
		ss->mLock.UnRLock();
	}
	mhashSliceLock[i].UnRLock();
	return idx;
}

int64 CFlvPool::getMaxIdx(uint32 i, HASH &hash)
{
	int64 idx = 1;
	mhashSliceLock[i].RLock();
	MapHashStreamIter iterM = mmapHashSlice[i].find(hash);
	if (iterM != mmapHashSlice[i].end())
	{
		StreamSlice * ss = iterM->second;
		ss->mLock.RLock();
		size_t size = ss->mavSlice.size();
		if (size > 0)
		{
			idx = ss->mavSliceIdx.at(size - 1) + 1;
		}
		ss->mLock.UnRLock();
	}
	mhashSliceLock[i].UnRLock();
	return idx;
}

int  CFlvPool::getMediaRate(uint32 i, HASH &hash)
{
	int mediaRate = 0;
	mhashSliceLock[i].RLock();
	MapHashStreamIter iterM = mmapHashSlice[i].find(hash);
	if (iterM != mmapHashSlice[i].end())
	{
		StreamSlice * ss = iterM->second;
		ss->mLock.RLock();
		mediaRate = ss->miMediaRate;
		ss->mLock.UnRLock();
	}
	mhashSliceLock[i].UnRLock();
	return mediaRate;
}

void CFlvPool::updateAccessTime(uint32 i, HASH &hash)
{
	mhashSliceLock[i].RLock();
	MapHashStreamIter iterM = mmapHashSlice[i].find(hash);
	if (iterM != mmapHashSlice[i].end())
	{
		StreamSlice * ss = iterM->second;
		ss->mLock.RLock();
		ss->mllAccessTime = getTimeUnix();
		ss->mLock.UnRLock();
	}
	mhashSliceLock[i].UnRLock();
}

int  CFlvPool::getFirstPlaySkipMilSecond(uint32 i, HASH &hash)
{
	int milSecond = 0;
	mhashSliceLock[i].RLock();
	MapHashStreamIter iterM = mmapHashSlice[i].find(hash);
	if (iterM != mmapHashSlice[i].end())
	{
		StreamSlice * ss = iterM->second;
		ss->mLock.RLock();
		milSecond = ss->miFirstPlaySkipMilSecond;
		ss->mLock.UnRLock();
	}
	mhashSliceLock[i].UnRLock();
	return milSecond;
}

uint32 CFlvPool::getDistanceKeyFrame(uint32 i, HASH &hash)
{
	uint32 distanceKeyFrame = 0;
	mhashSliceLock[i].RLock();
	MapHashStreamIter iterM = mmapHashSlice[i].find(hash);
	if (iterM != mmapHashSlice[i].end())
	{
		StreamSlice * ss = iterM->second;
		ss->mLock.RLock();
		distanceKeyFrame = ss->muiKeyFrameDistance;
		ss->mLock.UnRLock();
	}
	mhashSliceLock[i].UnRLock();
	return distanceKeyFrame;
}

int  CFlvPool::getVideoFrameRate(uint32 i, HASH &hash)
{
	int videoFrameRate = 0;
	mhashSliceLock[i].RLock();
	MapHashStreamIter iterM = mmapHashSlice[i].find(hash);
	if (iterM != mmapHashSlice[i].end())
	{
		StreamSlice * ss = iterM->second;
		ss->mLock.RLock();
		videoFrameRate = ss->miVideoFrameRate;
		ss->mLock.UnRLock();
	}
	mhashSliceLock[i].UnRLock();
	return videoFrameRate;
}

int  CFlvPool::getAudioFrameRate(uint32 i, HASH &hash)
{
	int audioFrameRate = 0;
	mhashSliceLock[i].RLock();
	MapHashStreamIter iterM = mmapHashSlice[i].find(hash);
	if (iterM != mmapHashSlice[i].end())
	{
		StreamSlice * ss = iterM->second;
		ss->mLock.RLock();
		audioFrameRate = ss->miAudioFrameRate;
		ss->mLock.UnRLock();
	}
	mhashSliceLock[i].UnRLock();
	return audioFrameRate;
}

int64 CFlvPool::getCacheTT(uint32 i, HASH &hash)
{
	int64 cacheTT = 0;
	mhashSliceLock[i].RLock();
	MapHashStreamIter iterM = mmapHashSlice[i].find(hash);
	if (iterM != mmapHashSlice[i].end())
	{
		StreamSlice * ss = iterM->second;
		ss->mLock.RLock();
		cacheTT = ss->mllCacheTT;
		ss->mLock.UnRLock();
	}
	mhashSliceLock[i].UnRLock();
	return cacheTT;
}

uint32 CFlvPool::getKeyFrameDistance(uint32 i, HASH &hash)
{
	uint32 uiKeyFrameDistance = 0;
	mhashSliceLock[i].RLock();
	MapHashStreamIter iterM = mmapHashSlice[i].find(hash);
	if (iterM != mmapHashSlice[i].end())
	{
		StreamSlice * ss = iterM->second;
		ss->mLock.RLock();
		uiKeyFrameDistance = ss->muiKeyFrameDistance;
		ss->mLock.UnRLock();
	}
	mhashSliceLock[i].UnRLock();
	return uiKeyFrameDistance;
}

int CFlvPool::getAutoBitRateFactor(uint32 i, HASH &hash)
{
	int autoBitRateFactor = 0;
	mhashSliceLock[i].RLock();
	MapHashStreamIter iterM = mmapHashSlice[i].find(hash);
	if (iterM != mmapHashSlice[i].end())
	{
		StreamSlice * ss = iterM->second;
		ss->mLock.RLock();
		autoBitRateFactor = ss->miAutoBitRateFactor;
		ss->mLock.UnRLock();
	}
	mhashSliceLock[i].UnRLock();
	return autoBitRateFactor;
}

int	CFlvPool::getAutoFrameFactor(uint32 i, HASH &hash)
{
	int autoFrameFactor = 0;
	mhashSliceLock[i].RLock();
	MapHashStreamIter iterM = mmapHashSlice[i].find(hash);
	if (iterM != mmapHashSlice[i].end())
	{
		StreamSlice * ss = iterM->second;
		ss->mLock.RLock();
		autoFrameFactor = ss->miAutoFrameFactor;
		ss->mLock.UnRLock();
	}
	mhashSliceLock[i].UnRLock();
	return autoFrameFactor;
}

int	CFlvPool::readBitRateMode(uint32 i, HASH &hash)
{
	int autoBitRateMode = 0;
	mhashSliceLock[i].RLock();
	MapHashStreamIter iterM = mmapHashSlice[i].find(hash);
	if (iterM != mmapHashSlice[i].end())
	{
		StreamSlice * ss = iterM->second;
		ss->mLock.RLock();
		autoBitRateMode = ss->miAutoBitRateMode;
		ss->mLock.UnRLock();
	}
	mhashSliceLock[i].UnRLock();
	return autoBitRateMode;
}

std::string CFlvPool::readChangeBitRateSuffix(uint32 i, HASH &hash)
{
	std::string bitRateSuffix;
	/*mhashSliceLock[i].RLock();
	MapHashStreamIter iterM = mmapHashSlice[i].find(hash);
	if (iterM != mmapHashSlice[i].end())
	{
		StreamSlice * ss = iterM->second;
		ss->mLock.RLock();

		ss->mLock.UnRLock();
	}
	mhashSliceLock[i].UnRLock();*/
	return bitRateSuffix;
}

std::string CFlvPool::readCodeSuffix(uint32 i, HASH &hash)
{
	std::string codeSuffix;
	/*mhashSliceLock[i].RLock();
	MapHashStreamIter iterM = mmapHashSlice[i].find(hash);
	if (iterM != mmapHashSlice[i].end())
	{
		StreamSlice * ss = iterM->second;
		ss->mLock.RLock();

		ss->mLock.UnRLock();
	}
	mhashSliceLock[i].UnRLock();*/
	return codeSuffix;
}

bool CFlvPool::seekKeyFrame(uint32 i, HASH &hash, uint32 &st, int64 &transIdx)
{
	transIdx = -1;
	bool isSucc = false;
	mhashSliceLock[i].RLock();
	MapHashStreamIter iterM = mmapHashSlice[i].find(hash);
	if (iterM != mmapHashSlice[i].end())
	{
		StreamSlice * ss = iterM->second;
		ss->mLock.RLock();

		size_t avSize = ss->mavSlice.size();
		size_t vkSize = ss->mvKeyFrameIdx.size();
		if (vkSize > 0 && avSize > 0)
		{
			int64 minIdx = ss->mavSliceIdx.at(0);
			int64 keyIdx = ss->mvKeyFrameIdx.at(vkSize - 1);
			bool isReadLast = false;
			Slice *s = ss->mavSlice.at(keyIdx - minIdx);
			if (s->muiTimestamp <= st)
			{
				isSucc = true;
				transIdx = keyIdx - 1;
				isReadLast = true;
			}
			if (!isReadLast)
			{
				for (size_t i = 0; i < ss->mvKeyFrameIdx.size(); i++)
				{
					int64 keyIdx = ss->mvKeyFrameIdx.at(i);
					Slice *s = ss->mavSlice.at(keyIdx - minIdx);
					if (s->muiTimestamp >= st)
					{
						isSucc = true;
						transIdx = keyIdx - 1;
						break;
					}
				}
			}
		}

		ss->mLock.UnRLock();
	}
	mhashSliceLock[i].UnRLock();
	return isSucc;
}

void CFlvPool::handleSlice(uint32 i, Slice *s)
{
	bool isNew = false;
	StreamSlice *ss = NULL;
	MapHashStreamIter iterM = mmapHashSlice[i].find(s->mpHash);
	if (iterM == mmapHashSlice[i].end())
	{
		if (s->misRemove)
		{
			logs->debug(">>>>>[handleSlice] task %s is remove but not find hash", s->mpUrl);
			atomicDec(s);
#ifdef __CMS_CYCLE_MEM__
			xfreeCycleMem(s->mcycMem);
#endif
			return;
		}
		ss = newStreamSlice();
#ifdef __CMS_CYCLE_MEM__
		ss2s(ss, s);
#endif

		updateMediaInfo(ss, s);
		ss->mptrHash = s->mpHash;
		ss->miAutoBitRateMode = s->miAutoBitRateMode;
		ss->miAutoBitRateFactor = s->miAutoBitRateFactor;
		ss->miAutoFrameFactor = s->miAutoFrameFactor;
		ss->miBufferAbsolutely = s->miBufferAbsolutely;

		ss->muid = getVid();
		ss->mllCreateTime = getTimeUnix();
		ss->mllAccessTime = ss->mllCreateTime;
		ss->misPushTask = s->misPushTask;

		if (s->misMetaData)
		{
			ss->misHaveMetaData = true;
			ss->mllMetaDataIdx = s->mllIndex;
			ss->mllMetaDataP2PIdx = s->mllP2PIndex;
			ss->mmetaDataSlice = s;
			logs->debug(">>>>>[handleSlice] task %s new task and recv new metaData.", ss->mstrUrl.c_str());
		}
		else
		{
			if (s->miDataType == DATA_TYPE_FIRST_AUDIO)
			{
				ss->mfirstAudioSlice = s;
				ss->mllFirstAudioIdx = s->mllIndex;
				logs->debug(">>>>>[handleSlice] task %s new task and recv first audio.", ss->mstrUrl.c_str());
			}
			else if (s->miDataType == DATA_TYPE_FIRST_VIDEO)
			{
				ss->misH264 = s->misH264;
				ss->misH265 = s->misH265;
				ss->mfirstVideoSlice = s;
				ss->mllFirstVideoIdx = s->mllIndex;
				logs->debug(">>>>>[handleSlice] task %s new task and recv first video.", ss->mstrUrl.c_str());
			}
			else
			{
				//不是h264和h265
				ss->mllLastSliceIdx = s->mllIndex;
				ss->mavSlice.push_back(s);
				ss->mavSliceIdx.push_back(s->mllIndex);
				if (s->miDataType == DATA_TYPE_VIDEO)
				{
					ss->mllVideoAbsoluteTimestamp = (int64)s->muiTimestamp;
				}
				else if (s->miDataType == DATA_TYPE_AUDIO)
				{
					ss->mllAudioAbsoluteTimestamp = (int64)s->muiTimestamp;
				}
				logs->debug(">>>>>[handleSlice] task %s new task and is not h264 or h265.", ss->mstrUrl.c_str());
			}
			ss->mllMemSize = s->miDataLen;
		}
		mhashSliceLock[i].WLock();
		mmapHashSlice[i].insert(make_pair(s->mpHash, ss));
		mhashSliceLock[i].UnWLock();
		isNew = true;
	}
	else
	{
		ss = iterM->second;
	}

	if (s->misRemove)
	{
		logs->debug(">>>>>[handleSlice] task %s is been remove.", ss->mstrUrl.c_str());
		mhashSliceLock[i].WLock();
		mmapHashSlice[i].erase(iterM);
		mhashSliceLock[i].UnWLock();

#ifdef __CMS_CYCLE_MEM__
		pushSS(ss); //延迟删除
#else
		releaseSS(ss);
#endif		
		atomicDec(s);
	}
	else
	{
		ss->mLock.WLock();
#ifdef __CMS_CYCLE_MEM__
		ss2s(ss, s);
#endif
		if (s->misHaveMediaInfo)
		{
			updateMediaInfo(ss, s);
			ss->miAutoBitRateMode = s->miAutoBitRateMode;
			ss->miAutoBitRateFactor = s->miAutoBitRateFactor;
			ss->miAutoFrameFactor = s->miAutoFrameFactor;
			ss->miBufferAbsolutely = s->miBufferAbsolutely;
		}

		if (s->misMetaData)
		{
			if (s->mllIndex < ss->mllLastSliceIdx)
			{
				s->mllIndex = ss->mllLastSliceIdx;
			}
			if (ss->mmetaDataSlice)
			{
				if (!isNew)
				{
					//新任务不能删除 前面刚保存
					atomicDec(ss->mmetaDataSlice);
				}
			}
			ss->misHaveMetaData = true;
			ss->mllMetaDataIdx = s->mllIndex;
			ss->mllMetaDataP2PIdx = s->mllP2PIndex;
			ss->mmetaDataSlice = s;
			logs->debug(">>>>>[handleSlice] task %s task recv new metaData.", ss->mstrUrl.c_str());
		}
		else if (s->miDataType == DATA_TYPE_FIRST_AUDIO)
		{
			if (s->mllIndex < ss->mllLastSliceIdx)
			{
				s->mllIndex = ss->mllLastSliceIdx;
			}
			if (ss->mfirstAudioSlice)
			{
				if (!isNew)
				{
					//新任务不能删除 前面刚保存
					atomicDec(ss->mfirstAudioSlice);
				}
			}
			ss->mfirstAudioSlice = s;
			ss->mllFirstAudioIdx = s->mllIndex;
			logs->debug(">>>>>[handleSlice] task %s task recv first audio.", ss->mstrUrl.c_str());
		}
		else if (s->miDataType == DATA_TYPE_FIRST_VIDEO)
		{
			if (s->mllIndex < ss->mllLastSliceIdx)
			{
				s->mllIndex = ss->mllLastSliceIdx;
			}
			if (ss->mfirstVideoSlice)
			{
				if (!isNew)
				{
					//新任务不能删除 前面刚保存
					atomicDec(ss->mfirstVideoSlice);
				}
			}
			ss->misH264 = s->misH264;
			ss->misH265 = s->misH265;
			ss->mfirstVideoSlice = s;
			ss->mllFirstVideoIdx = s->mllIndex;
			logs->debug(">>>>>[handleSlice] task %s task recv first video.", ss->mstrUrl.c_str());
		}
		else
		{
			//普通帧
			if (s->mllIndex <= ss->mllLastSliceIdx)
			{
				s->mllIndex = ss->mllLastSliceIdx + 1;
			}
			ss->mllLastSliceIdx = s->mllIndex;
			ss->mavSlice.push_back(s);
			ss->mavSliceIdx.push_back(s->mllIndex);
			if (s->miDataLen > 0)
			{
				ss->mllMemSize += s->miDataLen;
			}
			if (s->miDataType == DATA_TYPE_VIDEO)
			{
				if (ss->mllVideoAbsoluteTimestamp == -1)
				{
					ss->mllVideoAbsoluteTimestamp = (int64)s->muiTimestamp;
				}
				if (s->misKeyFrame)
				{
					size_t size = ss->mvKeyFrameIdx.size();
					if (size > 0)
					{
						int64 minIdx = ss->mavSliceIdx.at(0);
						int64 keyIdx = ss->mvKeyFrameIdx.at(size - 1);
						Slice *ks = ss->mavSlice.at(keyIdx - minIdx);
						uint32 distance = s->muiTimestamp - ks->muiTimestamp;
						if (distance > 0)
						{
							if (distance > ss->muiKeyFrameDistance + 1000)
							{
								logs->debug(">>>>>[handleSlice] task %s cur keyframe gop=%u,old keyframe gop=%u",
									ss->mstrUrl.c_str(), distance, ss->muiKeyFrameDistance);
							}
							ss->muiLastKeyFrameDistance = ss->muiKeyFrameDistance;
							ss->muiKeyFrameDistance = distance;
						}
					}
					ss->mvKeyFrameIdx.push_back(s->mllIndex);
					ss->mllNearKeyFrameIdx = s->mllIndex;
					ss->mvP2PKeyFrameIdx.push_back(s->mllP2PIndex);
				}
				ss->muiTheLastVideoTimestamp = s->muiTimestamp;
#ifdef __CMS_POOL_MEM__
				TTandKK *tk = (TTandKK*)mallocTTandKK(s->midxFixMem);
				tk->midxFixMem = s->midxFixMem;
#else
				TTandKK *tk = (TTandKK*)xmalloc(sizeof(TTandKK));
#endif
				tk->mllIndex = s->mllIndex;
				tk->mllKeyIndex = ss->mllNearKeyFrameIdx;
				tk->muiTimestamp = s->muiTimestamp;
				ss->msliceTTKK.push_back(tk);
			}
			else if (s->miDataType == DATA_TYPE_AUDIO)
			{
				if (ss->mllAudioAbsoluteTimestamp == -1)
				{
					ss->mllAudioAbsoluteTimestamp = (int64)s->muiTimestamp;
				}
			}
			int64 cacheTT = ss->mllCacheTT;
			int64 cache2KeyFrame = int64(ss->muiKeyFrameDistance * 2 + 2000);
			if (cache2KeyFrame < cacheTT * 3)
			{
				cache2KeyFrame = cacheTT * 3;
			}
			ss->maxRelativeDuration = 0;
			ss->minRelativeDuration = 0;
			getRelativeDuration(ss, s, true, ss->maxRelativeDuration, ss->minRelativeDuration);
			//logs->debug(">>>>>[handleSlice] task %s maxRelativeDuration=%lld,minRelativeDuration=%lld,cacheTT=%lld",
			//	ss->mstrUrl.c_str(),ss->maxRelativeDuration,ss->minRelativeDuration,cacheTT);
			if ((s->miDataType == DATA_TYPE_VIDEO || s->miDataType == DATA_TYPE_AUDIO) &&
				ss->maxRelativeDuration > cacheTT)
			{
				while (!ss->mavSlice.empty())
				{
					Slice *st = ss->mavSlice.at(0);
					if ((ss->misH264 || ss->misH265) && st->miDataType == DATA_TYPE_VIDEO)
					{
						if (ss->mvKeyFrameIdx.size() == 2 &&
							ss->maxRelativeDuration < cacheTT * 2 &&
							ss->mvKeyFrameIdx.at(0) == st->mllIndex)
						{
							break;
						}
					}
					if (ss->mvKeyFrameIdx.size() > 0 &&
						ss->mvKeyFrameIdx.at(0) == st->mllIndex)
					{
						ss->mvKeyFrameIdx.erase(ss->mvKeyFrameIdx.begin());
						if (ss->mvP2PKeyFrameIdx.size() > 0 &&
							ss->mvP2PKeyFrameIdx.at(0) == st->mllP2PIndex)
						{
							ss->mvP2PKeyFrameIdx.erase(ss->mvP2PKeyFrameIdx.begin());
						}
					}
					if (!ss->msliceTTKK.empty() && ss->msliceTTKK.at(0)->mllIndex == st->mllIndex)
					{
#ifdef __CMS_POOL_MEM__
						TTandKK *tk = ss->msliceTTKK.at(0);
						freeTTandKK(tk->midxFixMem, tk);
#else
						xfree(ss->msliceTTKK.at(0));
#endif
						ss->msliceTTKK.erase(ss->msliceTTKK.begin());
					}
					//时间戳记录
					if (st->miDataType == DATA_TYPE_VIDEO)
					{
						ss->mllVideoAbsoluteTimestamp = st->muiTimestamp;
						ss->miVideoFrameCount--;
					}
					else if (st->miDataType == DATA_TYPE_AUDIO)
					{
						ss->mllAudioAbsoluteTimestamp = st->muiTimestamp;
						ss->miAudioFrameCount--;
					}
					ss->mavSlice.erase(ss->mavSlice.begin());
					ss->mavSliceIdx.erase(ss->mavSliceIdx.begin());
					if (st->miDataLen > 0)
					{
						ss->mllMemSize -= st->miDataLen;
					}
					atomicDec(st);
					//logs->debug(">>>>>[handleSlice] task %s remove one slice",ss->mstrUrl.c_str());
					break;
				}
			}
			if (ss->mllMemSize > ss->mllLastMemSize + 100 * 1024)
			{
				ss->mllLastMemSize = ss->mllMemSize;
#ifdef __CMS_CYCLE_MEM__
				int64 totalCycleMem = 0;
				if (s->mcycMem)
				{
					totalCycleMem = s->mcycMem->totalMemSize;
				}
				makeOneTaskMem(s->mpHash, ss->mllLastMemSize, totalCycleMem);
#else
				makeOneTaskMem(s->mpHash, ss->mllLastMemSize);
#endif
			}
			else if (ss->mllMemSize + 100 * 1024 < ss->mllLastMemSize)
			{
				ss->mllLastMemSize = ss->mllMemSize;
#ifdef __CMS_CYCLE_MEM__
				int64 totalCycleMem = 0;
				if (s->mcycMem)
				{
					totalCycleMem = s->mcycMem->totalMemSize;
				}
				makeOneTaskMem(s->mpHash, ss->mllLastMemSize, totalCycleMem);
#else
				makeOneTaskMem(s->mpHash, ss->mllLastMemSize);
#endif
			}
			int64 tt = getTimeUnix();
			if (tt - ss->mllMemSizeTick > 60)
			{
				logs->debug(">>>>>[handleSlice] task %s mem size %d KB", ss->mstrUrl.c_str(), ss->mllMemSize / 1024);
				ss->mllMemSizeTick = tt;
			}
		}
		ss->mLock.UnWLock();
	}
}

void CFlvPool::getRelativeDuration(StreamSlice *ss, Slice *s, bool isNewSlice,
	int64 &maxRelativeDuration, int64 &minRelativeDuration)
{
	maxRelativeDuration = minRelativeDuration = 0;
	if (s->miDataType == DATA_TYPE_VIDEO)
	{
		maxRelativeDuration = (int64)s->muiTimestamp - ss->mllVideoAbsoluteTimestamp;
		if (isNewSlice)
		{
			ss->miVideoFrameCount++;
		}
	}
	else
	{
		maxRelativeDuration = (int64)s->muiTimestamp - ss->mllAudioAbsoluteTimestamp;
		if (isNewSlice)
		{
			ss->miAudioFrameCount++;
		}
	}
	if (maxRelativeDuration < 0)
	{
		//时间戳变小了
		maxRelativeDuration = 0;
	}
	else
	{
		minRelativeDuration = maxRelativeDuration;
	}

	int totalFramRate = ss->miVideoFrameRate + ss->miAudioFrameRate;
	if (totalFramRate <= 0)
	{
		totalFramRate = 43 + 30;
	}
	int64 frameRateDuration = (int64)(ss->mavSlice.size() / totalFramRate * 1000);
	if (frameRateDuration > maxRelativeDuration)
	{
		maxRelativeDuration = frameRateDuration;
	}
	else
	{
		minRelativeDuration = frameRateDuration;
	}
}

void CFlvPool::clear()
{

}

void CFlvPool::delHash(uint32 i, HASH &hash)
{

}

void CFlvPool::addHash(uint32 i, HASH &hash)
{

}

void CFlvPool::checkTimeout()
{

}

bool CFlvPool::isTimeout(uint32 i, HASH &hash)
{
	return false;
}

bool CFlvPool::mergeKeyFrame(char *desc, int descLen, char *key, int keyLen, char **src, int32 &srcLen, std::string url)
{
	logs->debug(">>>>[mergeKeyFrame] meger desc frame and key frame video %s", url.c_str());
	if (descLen < 16 || keyLen < 11)
	{
		logs->error("***** %s MergeKeyFrame desc frame or key frame less than 11 bytes *****", url.c_str());
		return false;
	}
	//拷贝信息帧的信息
	int32 tagLen1 = int32(bigUInt16(desc + 11));
	if (tagLen1 > int32(descLen - (11 + 2)))
	{
		logs->error("***** %s MergeKeyFrame desc frame tag1 len error *****", url.c_str());
		return false;
	}
	char *descData1 = desc + 11 + 2;//desc[11+2 : 11+2+tagLen1]
	int32 descData1Len = tagLen1;
	if (int32(descLen) - (11 + 2 + tagLen1) < 3)
	{
		logs->error("***** %s MergeKeyFrame desc frame not have tag2 *****", url.c_str());
		return false;
	}
	int32 tagLen2 = int32(bigUInt16(desc + 11 + 2 + tagLen1 + 1));
	if (tagLen2 > int32(int32(descLen) - (11 + 2 + tagLen1 + 1 + 2)))
	{
		logs->error("***** %s MergeKeyFrame desc frame tag2 len error *****", url.c_str());
		return false;
	}
	char *descData2 = desc + 11 + 2 + tagLen1 + 1 + 2;//desc[11+2+tagLen1+1+2 : 11+2+tagLen1+1+2+tagLen2]
	int32 descData2Len = tagLen2;

	srcLen = 4 + 4 + descData1Len + 4 + descData2Len + (keyLen - 4);
	*src = (char*)xmalloc(srcLen);
	char *p = *src;
	memcpy(p, key, 4);
	p += 4;
	*p++ = char(tagLen1 >> 24);
	*p++ = char(tagLen1 >> 16);
	*p++ = char(tagLen1 >> 8);
	*p++ = char(tagLen1);
	memcpy(p, descData1, descData1Len);
	p += descData1Len;
	*p++ = char(tagLen2 >> 24);
	*p++ = char(tagLen2 >> 16);
	*p++ = char(tagLen2 >> 8);
	*p++ = char(tagLen2);
	memcpy(p, descData2, descData2Len);
	p += descData2Len;
	memcpy(p, key + 4, keyLen - 4);
	(*src)[1] = 0x01;
	return true;
}

void CFlvPool::updateMediaInfo(StreamSlice *ss, Slice *s)
{
	ss->miNotPlayTimeout = s->miNotPlayTimeout;
	if (ss->miNotPlayTimeout <= 0)
	{
		ss->miNotPlayTimeout = 1000 * 60 * 10;
	}
	ss->miFirstPlaySkipMilSecond = s->miFirstPlaySkipMilSecond;
	ss->misResetStreamTimestamp = s->misResetStreamTimestamp;
	if (s->mpMajorHash)
	{
		ss->mhMajorHash = s->mpMajorHash;
	}
	if (s->mpUrl)
	{
		ss->mstrUrl = s->mpUrl;
	}
	ss->miMediaRate = s->miMediaRate;
	ss->miVideoRate = s->miVideoRate;
	ss->miAudioRate = s->miAudioRate;
	ss->miVideoFrameRate = s->miVideoFrameRate;
	ss->miAudioFrameRate = s->miAudioFrameRate;
	ss->misNoTimeout = s->misNoTimeout;
	if (s->mpVideoType)
	{
		ss->mstrVideoType = s->mpVideoType;
	}
	if (s->mpAudioType)
	{
		ss->mstrAudioType = s->mpAudioType;
	}
	ss->miLiveStreamTimeout = s->miLiveStreamTimeout;
	ss->miNoHashTimeout = s->miNoHashTimeout;
	if (s->mpRemoteIP)
	{
		ss->mstrRemoteIP = s->mpRemoteIP;
	}
	if (s->mpHost)
	{
		ss->mstrHost = s->mpHost;
	}
	ss->misRealTimeStream = s->misRealTimeStream;
	ss->mllCacheTT = s->mllCacheTT;
	ss->misH264 = s->misH264;
	ss->misH265 = s->misH265;
	if (ss->mllCacheTT == 0)
	{
		ss->mllCacheTT = 1000 * 5;
	}
}

void CFlvPool::releaseSS(StreamSlice *ss)
{
#ifdef __CMS_CYCLE_MEM__
	CmsCycleMem *cycMem = NULL;
#endif
	for (VectorTTKKIter iterTTKK = ss->msliceTTKK.begin(); iterTTKK != ss->msliceTTKK.end();)
	{
#ifdef __CMS_POOL_MEM__
		TTandKK *tk = *iterTTKK;
		freeTTandKK(tk->midxFixMem, tk);
#else
		xfree(*iterTTKK);
#endif		
		iterTTKK = ss->msliceTTKK.erase(iterTTKK);
	}
	for (VectorSliceIter iterSI = ss->mavSlice.begin(); iterSI != ss->mavSlice.end();)
	{
#ifdef __CMS_CYCLE_MEM__
		if (!cycMem && *iterSI)
		{
			cycMem = (*iterSI)->mcycMem;
		}
#endif
		atomicDec(*iterSI);
		iterSI = ss->mavSlice.erase(iterSI);
	}
	if (ss->mfirstVideoSlice)
	{
#ifdef __CMS_CYCLE_MEM__
		if (!cycMem)
		{
			cycMem = ss->mfirstVideoSlice->mcycMem;
		}
#endif
		atomicDec(ss->mfirstVideoSlice);
	}
	if (ss->mfirstAudioSlice)
	{
#ifdef __CMS_CYCLE_MEM__
		if (!cycMem)
		{
			cycMem = ss->mfirstAudioSlice->mcycMem;
		}
#endif
		atomicDec(ss->mfirstAudioSlice);
	}
	if (ss->mmetaDataSlice)
	{
#ifdef __CMS_CYCLE_MEM__
		if (!cycMem)
		{
			cycMem = ss->mmetaDataSlice->mcycMem;
		}
#endif
		atomicDec(ss->mmetaDataSlice);
	}
	if (ss->mptrHash)
	{
		xfreeHash(ss->mptrHash);
	}
	delete ss;
#ifdef __CMS_CYCLE_MEM__
	xfreeCycleMem(cycMem);
#endif
}




