/*
The MIT License (MIT)

Copyright (c) 2017- cms(hsc)

Author: ���û������/kisslovecsh@foxmail.com

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
#include <protocol/cms_flv_transmission.h>
#include <flvPool/cms_flv_pool.h>
#include <log/cms_log.h>
#include <assert.h>


CFlvTransmission::CFlvTransmission(CProtocol *protocol, bool isPushTask/* = false*/)
{
	mllMetaDataIdx = -1;
	mllFirstVideoIdx = -1;
	mllFirstAudioIdx = -1;
	mllTransIdx = -1;
	misChangeFirstVideo = false;
	misTaskRestart = false;
	mchangeFristVideoTimes = 0;
	mprotocol = protocol;
	misWaterMark = false;
	mwaterMarkOriHashIdx = 0;
	mcacheTT = 0;
	msliceFrameRate = 0;
	misPushTask = isPushTask;
	mfastBitRate = new CFastBitRate;
	mdurationtt = new CDurationTimestamp();
	mfirstPlay = new CFirstPlay();
	mjumpLastXSeconds = new CJumpLastXSeconds();
	mullTransUid = 0;
	misRestart8AdjustTimestamp = false;
}

CFlvTransmission::~CFlvTransmission()
{
	if (mfastBitRate)
	{
		delete mfastBitRate;
	}
	if (mdurationtt)
	{
		delete mdurationtt;
	}
	if (mfirstPlay)
	{
		delete mfirstPlay;
	}
	if (mjumpLastXSeconds)
	{
		delete mjumpLastXSeconds;
	}
}

void CFlvTransmission::setHash(uint32 hashIdx, HASH &hash)
{
	mreadHashIdx = hashIdx;
	mreadHash = hash;
}

void CFlvTransmission::setWaterMarkHash(uint32 hashIdx, HASH &hash)
{
	misWaterMark = true;
	mwaterMarkOriHashIdx = hashIdx;
	mwaterMarkOriHash = hash;
}

int CFlvTransmission::doMetaData()
{
	int ret = CMS_ERROR;
	Slice *s = NULL;
	if (CFlvPool::instance()->readMetaData(mreadHashIdx, mreadHash, &s) == FlvPoolCodeError)
	{
		return ret;
	}
	ret = CMS_OK;
	if (s != NULL)
	{
		mllMetaDataIdx = s->mllIndex;
		ret = mprotocol->sendMetaData(s);
		atomicDec(s);
		logs->debug(">>>>>%s [CFlvTransmission::doMetaData] %s doMetaData send metaData",
			mprotocol->remoteAddr().c_str(), mprotocol->getUrl().c_str());
	}
	return ret;
}

int CFlvTransmission::doFirstVideoAudio(bool isVideo)
{
	int ret = CMS_ERROR;
	Slice *s = NULL;
	if (CFlvPool::instance()->readFirstVideoAudioSlice(mreadHashIdx, mreadHash, &s, isVideo) == FlvPoolCodeError)
	{
		return ret;
	}
	ret = CMS_OK;
	if (s != NULL)
	{
		if (isVideo)
		{
			misChangeFirstVideo = true;
			mchangeFristVideoTimes++;

			mllFirstVideoIdx = s->mllIndex;
			logs->debug(">>>>>%s [CFlvTransmission::doFirstVideoAudio] %s doFirstVideoAudio send first video",
				mprotocol->remoteAddr().c_str(), mprotocol->getUrl().c_str());
		}
		else
		{
			mllFirstAudioIdx = s->mllIndex;
			logs->debug(">>>>>%s [CFlvTransmission::doFirstVideoAudio] %s doFirstVideoAudio send first audio",
				mprotocol->remoteAddr().c_str(), mprotocol->getUrl().c_str());
		}
		ret = mprotocol->sendVideoOrAudio(s, 0);
		atomicDec(s);
	}
	return ret;
}

void CFlvTransmission::getSliceFrameRate()
{
	if (msliceFrameRate == 0)
	{
		int videoFrameRate = CFlvPool::instance()->getVideoFrameRate(mreadHashIdx, mreadHash);
		int audioFrameRate = CFlvPool::instance()->getAudioFrameRate(mreadHashIdx, mreadHash);
		if (videoFrameRate > 0 && audioFrameRate > 0)
		{
			msliceFrameRate = videoFrameRate = audioFrameRate;
		}
	}
}

int CFlvTransmission::doTransmission(bool &isSendData)
{
	isSendData = false;
	int ret = 0;
	Slice *s = NULL;
	Slice *ss = NULL;
	int flvPoolCode;
	uint32 uiTimestamp = 0;
	int	sliceNum = 0;
	bool needSend = false;
	int  dropPer = 0;
	bool isVideo = false;
	bool isAudio = false;
	bool isTransPlay = false;
	bool isExist = false;
	bool isPublishTask = false;
	bool isTaskRestart = false;
	bool isMetaDataChanged = false;
	bool isFirstVideoAudioChanged = false;
	getSliceFrameRate();
	uint32 tt = getTickCount();
	if (!mjumpLastXSeconds->isInit())
	{
		mjumpLastXSeconds->init(mprotocol->remoteAddr(), mprotocol->protocol(), mprotocol->getUrl(), mreadHashIdx, mreadHash);
	}
	do
	{
		isTransPlay = false;
		isExist = false;
		isPublishTask = false;
		isTaskRestart = false;
		isMetaDataChanged = false;
		isFirstVideoAudioChanged = false;

		//�ײ���֡��ʼ��
		if (!mfirstPlay->isInit())
		{
			//��������㷢�ͻ���
			int medieRate = CFlvPool::instance()->getMediaRate(mreadHashIdx, mreadHash);
			int sendBufSize = medieRate / 8 * 1024;
			if (sendBufSize < 32 * 1024)
			{
				sendBufSize = 32 * 1024;
			}
			if (sendBufSize > 512 * 1024)
			{
				sendBufSize = 512 * 1024;
			}
			mprotocol->setWriteBuffer(sendBufSize);
			//��������㷢�ͻ��� ����
			mfirstPlay->init(mreadHash, mreadHashIdx, mprotocol->remoteAddr(), mprotocol->protocol(), mprotocol->getUrl());
		}
		if (!misPushTask && !mfirstPlay->checkfirstPlay())
		{
			ret = 0;
			break;
		}
		//�ײ���֡��ʼ�� ����
		sliceNum = 0;
		flvPoolCode = CFlvPool::instance()->readSlice(mreadHashIdx, mreadHash, mllTransIdx, &s, sliceNum, isTransPlay,
			mllMetaDataIdx, mllFirstVideoIdx, mllFirstAudioIdx, isExist, isTaskRestart, isPublishTask, isMetaDataChanged, isFirstVideoAudioChanged, mullTransUid);
		CAutoSlice autoSlice(s);
		if (!isExist)
		{
			ret = 2;
			if ((misPushTask || mprotocol->isCmsConnection()) && mllTransIdx != -1)
			{
				ret = -1;
				logs->error("*** %s [CFlvTransmission::doTransmission] %s doTransmission task is missing ***",
					mprotocol->remoteAddr().c_str(), mprotocol->getUrl().c_str());
			}
			break;
		}

		if (misPushTask && !isPublishTask)
		{
			//ֻ�������������ת��
			ret = -1;
			logs->error("*** %s [CFlvTransmission::doTransmission] %s doTransmission is pushing task but read task is not publish task ***",
				mprotocol->remoteAddr().c_str(), mprotocol->getUrl().c_str());
			break;
		}

		if (flvPoolCode == FlvPoolCodeError)
		{
			if ((misPushTask || mprotocol->isCmsConnection()) && mllTransIdx != -1)
			{
				logs->error("*** %s [CFlvTransmission::doTransmission] %s doTransmission task is missing ***",
					mprotocol->remoteAddr().c_str(), mprotocol->getUrl().c_str());
				ret = -1;
			}
			break;
		}
		else if (flvPoolCode == FlvPoolCodeOK)
		{
			//metaData �ı�
			if (isMetaDataChanged)
			{
				if (doMetaData() == CMS_ERROR)
				{
					ret = -1;
					break;
				}
			}
			//��֡�ı�
			if (isFirstVideoAudioChanged)
			{
				if (CFlvPool::instance()->isFirstVideoChange(mreadHashIdx, mreadHash, mllFirstVideoIdx))
				{
					if (doFirstVideoAudio(true) == CMS_ERROR)
					{
						ret = -1;
						break;
					}
				}
				if (CFlvPool::instance()->isFirstAudioChange(mreadHashIdx, mreadHash, mllFirstAudioIdx))
				{
					if (doFirstVideoAudio(false) == CMS_ERROR)
					{
						ret = -1;
						break;
					}
				}
			}
			//�����֡�ı� �����������û��п��ܷ��͵��Ǿɵ�����֡ ����
			if (mllTransIdx == -1 && mllFirstVideoIdx != -1 && s->mllIndex < mllFirstVideoIdx)
			{
				mllTransIdx = mllFirstVideoIdx < mllFirstAudioIdx ? mllFirstVideoIdx - 1 : mllFirstAudioIdx - 1;
				continue;
			}
			if (mllTransIdx == -1 && mllFirstAudioIdx != -1 && s->mllIndex < mllFirstAudioIdx)
			{
				mllTransIdx = mllFirstVideoIdx < mllFirstAudioIdx ? mllFirstVideoIdx - 1 : mllFirstAudioIdx - 1;
				continue;
			}
			//�����֡�ı� �����������û��п��ܷ��͵��Ǿɵ�����֡ ���� ����
			if (!mfastBitRate->isInit())
			{
				mfastBitRate->init(mprotocol->remoteAddr(), mprotocol->protocol(), mprotocol->getUrl(), misWaterMark,
					mwaterMarkOriHashIdx, mreadHashIdx, mwaterMarkOriHash, mreadHash);
				mfastBitRate->setChangeBitRate();
			}
			if (!mdurationtt->isInit())
			{
				mdurationtt->init(mprotocol->remoteAddr(), mprotocol->protocol(), mprotocol->getUrl());
				mdurationtt->setResetTimestamp(true);
			}
			if (s)
			{
				needSend = true;
				//�ײ���֡
				if (!misPushTask && !mfirstPlay->checkShouldDropFrameCount(mllTransIdx, s))
				{
					ret = 0;
					continue;
				}
				needSend = !mfirstPlay->needDropFrame(s);
				if (misPushTask)
				{
					//����û���ײ���֡
					needSend = true;
				}
				//�ײ���֡ ����
				isVideo = s->miDataType == DATA_TYPE_VIDEO;
				isAudio = s->miDataType == DATA_TYPE_AUDIO;

				uiTimestamp = s->muiTimestamp;
				if (misRestart8AdjustTimestamp) //p2p �����޸�ʱ��� Ŀǰû��p2p�߼�
				{
					mdurationtt->resetDeltaTimestamp(uiTimestamp);
					misRestart8AdjustTimestamp = false;
				}
				bool isMergerFrame = false;
				if (isVideo && (mfastBitRate->isChangeBitRate() ||
					misTaskRestart ||
					(misChangeFirstVideo && mchangeFristVideoTimes > 1)))
				{
					misTaskRestart = false;
					misChangeFirstVideo = false;
					Slice *fs = NULL;
					if (CFlvPool::instance()->readFirstVideoAudioSlice(mreadHashIdx, mreadHash, &fs, true) == FlvPoolCodeError)
					{
						logs->info("*** %s [CFlvTransmission::doTransmission] %s merger key frame but not found first video ***",
							mprotocol->remoteAddr().c_str(), mprotocol->getUrl().c_str());
						ret = -1;
						break;
					}
					char *d = NULL;
					int32 dLen = 0;
					if (CFlvPool::instance()->mergeKeyFrame(fs->mData, fs->miDataLen, s->mData, s->miDataLen, &d, dLen, mprotocol->getUrl()))
					{
						ss = newSlice();
						atomicInc(ss);
						ss->mData = d;
						ss->miDataLen = dLen;
						ss->miDataType = s->miDataType;
						ss->mllIndex = s->mllIndex;
						ss->muiTimestamp = s->muiTimestamp;
						ss->misKeyFrame = s->misKeyFrame;
						isMergerFrame = true;
						//atomicDec(s); //CAutoSlice autoSlice(s) ���Զ��ͷ� ������Ȼ�ı䵫����Ҫ�ٴ��ͷ�
						s = ss;
					}
					atomicDec(fs);
				}
				//����л�������,��Ҫ�޸�ʱ���
				uiTimestamp = mfastBitRate->changeBitRateSetTimestamp(s->miDataType, uiTimestamp);
				//Ԥ��ʱ�����С�����
				uiTimestamp = mdurationtt->keepTimestampIncrease(isVideo, uiTimestamp);
				//Ԥ��ʱ�����С����� ����
				//����ʱ���
				uiTimestamp = mdurationtt->resetTimestamp(uiTimestamp, isVideo);
				//����ʱ��� ����				
				//����л�������,��Ҫ�޸�ʱ��� ����
				//��̬��֡
				if (mfastBitRate->needResetFlags(s->miDataType, uiTimestamp))
				{
					//ʱ�����С�������־
					mfastBitRate->resetDropFrameFlags();
				}
				if (isAudio)
				{
					mfastBitRate->setNo1VideoAudioTimestamp(false, uiTimestamp);
				}
				else if (isVideo)
				{
					mfastBitRate->setNo1VideoAudioTimestamp(true, uiTimestamp);
				}
				if (((mfastBitRate->getAutoBitRateMode() == AUTO_DROP_CHANGE_BITRATE_OPEN ||
					mfastBitRate->getAutoBitRateMode() == AUTO_DROP_BITRATE_OPEN) &&
					muiKeyFrameDistance < DropVideoKeyFrameLen) ||
					mfastBitRate->getAutoBitRateMode() == AUTO_CHANGE_BITRATE_OPEN)
				{
					if (mcacheTT == 0)
					{
						mcacheTT = CFlvPool::instance()->getCacheTT(mreadHashIdx, mreadHash);
						muiKeyFrameDistance = CFlvPool::instance()->getKeyFrameDistance(mreadHashIdx, mreadHash);
						mfastBitRate->setAutoBitRateFactor(CFlvPool::instance()->getAutoBitRateFactor(mreadHashIdx, mreadHash));
						mfastBitRate->setAutoFrameFactor(CFlvPool::instance()->getAutoFrameFactor(mreadHashIdx, mreadHash));
						logs->debug("%s [CFlvTransmission::doTransmission] %s cache=%lld,keyFrameDistance=%lu,autoBitRateFactor=%d,autoFrameFactor=%d",
							mprotocol->remoteAddr().c_str(), mprotocol->getUrl().c_str(), mcacheTT, muiKeyFrameDistance,
							mfastBitRate->getAutoBitRateFactor(), mfastBitRate->getAutoFrameFactor());
					}
					mfastBitRate->dropVideoFrame(mcacheTT, s->miDataType, msliceFrameRate, tt, uiTimestamp, sliceNum);
				}
				if (isVideo && misPushTask) //����û�ж�̬��֡
				{
					if (mfastBitRate->getTransCodeNeedDropVideo())//�Ƿ���Ҫ��֡
					{
						if (s->misKeyFrame && mfastBitRate->isDropEnoughTime(uiTimestamp))//�ж��Ƿ��������� ���趪֡��־
						{
							if (mfastBitRate->getLoseBufferTimes() <= 0)
							{
								mfastBitRate->setTransCodeNeedDropVideo(false);
							}
						}
						else
						{
							needSend = false;
							mfastBitRate->dropOneFrame();
						}
					}
					if (mfastBitRate->getTransCodeNoNeedDropVideo())//��̬���� ģ�ⶪ֡�߼� ʵ�ʲ���֡
					{
						if (s->misKeyFrame)
						{
							if (mfastBitRate->getLoseBufferTimes() <= 0)
							{
								mfastBitRate->setTransCodeNoNeedDropVideo(false);
							}
						}
						else
						{
							mfastBitRate->dropOneFrame();
						}
					}
				}
				else if (isAudio)
				{

				}
				dropPer = mfastBitRate->dropFramePer(tt, msliceFrameRate);
				if (dropPer > 0)
				{
					logs->info("%s [CFlvTransmission::doTransmission] %s doTransmission dropPer %d",
						mprotocol->remoteAddr().c_str(), mprotocol->getUrl().c_str(), dropPer);
				}
				//��̬��֡ ����
				if (needSend)
				{
					isSendData = true;
					ret = mprotocol->sendVideoOrAudio(s, uiTimestamp);
				}
				if (mllTransIdx != -1 &&
					mllTransIdx + 1 != s->mllIndex)
				{
					logs->info("*** %s [CFlvTransmission::doTransmission] %s doTransmission drop slice ***",
						mprotocol->remoteAddr().c_str(), mprotocol->getUrl().c_str());
				}

				if (!mfastBitRate->isChangeVideoBit())
				{
					mllTransIdx = s->mllIndex;
				}
				if (mjumpLastXSeconds->judge(s->miDataType, mllTransIdx, tt, sliceNum, msliceFrameRate, uiTimestamp))
				{
					misRestart8AdjustTimestamp = true;
					logs->debug("%s [CFlvTransmission::doTransmission] %s doTransmission task is been jump last x seconds,need adjust timestamp.",
						mprotocol->remoteAddr().c_str(), mprotocol->getUrl().c_str());
				}
				if (!isMergerFrame)
				{

				}
				else
				{
					//��Ϊ���ڸú������٣��ͷ�
					atomicDec(s);
					ss = NULL;
				}
				if (ret == CMS_ERROR)
				{
					ret = -1;
					break;
				}
				mjumpLastXSeconds->record();
				ret = 1;
				if (mprotocol->writeBuffSize() > 0)
				{
					break;
				}
			}
			else
			{
				break;
			}
		}
		else if (flvPoolCode == FlvPoolCodeNoData)
		{
			ret = 0;
			break;
		}
		else if (flvPoolCode == FlvPoolCodeRestart)
		{
			mllTransIdx = -1;
			mllMetaDataIdx = -1;
			mllFirstVideoIdx = -1;
			mllFirstAudioIdx = -1;

			mullTransUid = 0;
			misTaskRestart = true; //���п��������л���

			misRestart8AdjustTimestamp = true;
			//��ԭ��֡ת��״̬
			mfastBitRate->resetDropFrameFlags();
			mjumpLastXSeconds->reset();
			logs->debug("%s [CFlvTransmission::doTransmission] %s doTransmission task is been restart",
				mprotocol->remoteAddr().c_str(), mprotocol->getUrl().c_str());
			ret = 0;
			break;
		}
		else
		{
			logs->error("*** %s [CFlvTransmission::doTransmission] %s doTransmission unknow FlvPoolCode=%d ***",
				mprotocol->remoteAddr().c_str(), mprotocol->getUrl().c_str(), flvPoolCode);
			ret = -1;
			break;
		}
	} while (true);
	mprotocol->syncIO();
	return ret;
}