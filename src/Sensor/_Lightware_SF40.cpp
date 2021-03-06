/*
 *  Created on: Nov 4, 2016
 *      Author: yankai
 */
#include "_Lightware_SF40.h"

namespace kai
{

_Lightware_SF40::_Lightware_SF40()
{
	_ThreadBase();

	m_pIn = NULL;
	m_pOut = NULL;

	m_offsetAngle = 0.0;
	m_nDiv = 0;
	m_dAngle = 0;
	m_minDist = 1.0;
	m_maxDist = 50.0;
	m_showScale = 1.0;	//1m = 1pixel;
	m_mwlX = 3;
	m_mwlY = 3;
	m_strRecv = "";
	m_pSF40sender = NULL;
	m_pDist = NULL;
	m_pX = NULL;
	m_pY = NULL;
	m_MBS = 0;
	m_nTrajectory = -1;
	m_trajStep = 0.0;
	m_iLine = 0;
}

_Lightware_SF40::~_Lightware_SF40()
{
	DEL(m_pSF40sender);

	if (m_pIn)
	{
		m_pIn->close();
		delete m_pIn;
		m_pIn = NULL;
	}

	if (m_pOut)
	{
		m_pOut->close();
		delete m_pOut;
		m_pOut = NULL;
	}

	DEL(m_pX);
	DEL(m_pY);
}

bool _Lightware_SF40::init(void* pKiss)
{
	CHECK_F(!this->_ThreadBase::init(pKiss));
	Kiss* pK = (Kiss*) pKiss;
	pK->m_pInst = this;

	string presetDir = "";
	F_INFO(pK->root()->o("APP")->v("presetDir", &presetDir));

	//common in all input modes
	F_INFO(pK->v("offsetAngle", &m_offsetAngle));
	F_INFO(pK->v("minDist", &m_minDist));
	F_INFO(pK->v("maxDist", &m_maxDist));
	F_INFO(pK->v("showScale", &m_showScale));
	F_INFO(pK->v("MBS", (int* )&m_MBS));
	F_INFO(pK->v("nTrajectory", &m_nTrajectory));
	F_INFO(pK->v("trajectoryStep", &m_trajStep));

	F_ERROR_F(pK->v("nDiv", &m_nDiv));
	m_dAngle = DEG_AROUND / m_nDiv;
	m_pDist = new double[m_nDiv + 1];

	F_INFO(pK->v("mwlX", &m_mwlX));
	F_INFO(pK->v("mwlY", &m_mwlY));
	m_pX = new Filter();
	m_pY = new Filter();
	m_pX->startMedian(m_mwlX);
	m_pY->startMedian(m_mwlY);
	m_iLine = 0;

	m_lTrajectory.clear();

	//IO
	Kiss* pCC;
	string param = "";

	//input
	pCC = pK->o("input");
	CHECK_F(pCC->empty());
	F_ERROR_F(pCC->v("class", &param));
	if (param == "SerialPort")
	{
		m_pIn = new SerialPort();
		CHECK_F(!m_pIn->init(pCC));

		m_pSF40sender = new _Lightware_SF40_sender();
		m_pSF40sender->m_pSerialPort = (SerialPort*) m_pIn;
		m_pSF40sender->m_dAngle = m_dAngle;
		CHECK_F(!m_pSF40sender->init(pKiss));
	}
	else if (param == "File")
	{
		m_pIn = new File();
		F_ERROR_F(m_pIn->init(pCC));
	}
	else if (param == "TCP")
	{
		m_pIn = new TCP();
		F_ERROR_F(m_pIn->init(pCC));
	}

	//output
	pCC = pK->o("output");
	CHECK_T(pCC->empty());
	param = "";
	F_ERROR_F(pCC->v("class", &param));
	if (param == "SerialPort")
	{
		m_pOut = new SerialPort();
		CHECK_F(m_pOut->init(pCC));
	}
	else if (param == "File")
	{
		m_pOut = new File();
		F_ERROR_F(m_pOut->init(pCC));
	}
	else if (param == "TCP")
	{
		m_pOut = new TCP();
		F_ERROR_F(m_pOut->init(pCC));
	}

	return true;
}

bool _Lightware_SF40::link(void)
{
	CHECK_F(!this->_ThreadBase::link());
	Kiss* pK = (Kiss*) m_pKiss;

	return true;
}

bool _Lightware_SF40::start(void)
{
	if (m_pIn->type() == serialport)
	{
		CHECK_F(!m_pSF40sender->start());
	}

	m_bThreadON = true;
	int retCode = pthread_create(&m_threadID, 0, getUpdateThread, this);
	if (retCode != 0)
	{
		LOG_E(retCode);
		m_bThreadON = false;
		return false;
	}

	return true;
}

void _Lightware_SF40::update(void)
{
	while (m_bThreadON)
	{
		if (!m_pIn->isOpen())
		{
			if (!m_pIn->open())
			{
				this->sleepThread(USEC_1SEC);
				continue;
			}

			if (m_pIn->type() == serialport)
				m_pSF40sender->MBS(m_MBS);
		}

		this->autoFPSfrom();

		updateLidar();
		updatePosition();

		this->autoFPSto();
	}

}

void _Lightware_SF40::updateLidar(void)
{
	F_(readLine());

	m_iLine++;

	string str;
	istringstream sStr;

	//separate the command part
	vector<string> vStr;
	vStr.clear();
	sStr.str(m_strRecv);
	while (getline(sStr, str, ' '))
	{
		vStr.push_back(str);
	}
	CHECK_(vStr.size() < 2);

	string cmd = vStr.at(0);
	string result = vStr.at(1);

	//find the angle from cmd
	vector<string> vCmd;
	sStr.clear();
	sStr.str(cmd);
	while (getline(sStr, str, ','))
	{
		vCmd.push_back(str);
	}
	CHECK_(vCmd.size() < 2);
	double angle = atof(vCmd.at(1).c_str());

	//find the result
	vector<string> vResult;
	sStr.clear();
	sStr.str(result);
	while (getline(sStr, str, ','))
	{
		vResult.push_back(str);
	}
	CHECK_(vResult.size() < 1);
	double dist = atof(vResult.at(0).c_str());

	int iAngle = (int) (angle / m_dAngle);
	m_pDist[iAngle] = dist;

	m_strRecv.clear();
}

bool _Lightware_SF40::readLine(void)
{
	char buf;
	while (m_pIn->read((uint8_t*) &buf, 1) > 0)
	{
		if (buf == 0)
			continue;
		if (buf == LF || buf == CR)
		{
			if(m_strRecv.empty())continue;
			CHECK_T(m_pOut==NULL);

			if (!m_pOut->isOpen())
			{
				CHECK_T(!m_pOut->open());
			}

			m_pOut->writeLine((uint8_t*) m_strRecv.c_str(), m_strRecv.length());
			return true;
		}

		m_strRecv += buf;
	}

	return false;
}

void _Lightware_SF40::updatePosition(void)
{
	int i, nV;
	double pX = 0;
	double pY = 0;

	for (i = 0, nV = 0; i < m_nDiv; i++)
	{
		double dist = m_pDist[i];
		if (dist < m_minDist)
			continue;
		if (dist > m_maxDist)
			continue;

		double angle = (m_dAngle * i + m_offsetAngle) * DEG_RADIAN;
		pX += (dist * sin(angle));
		pY += -(dist * cos(angle));
		nV++;
	}

	pX /= nV;
	pY /= nV;

	m_pX->input(pX);
	m_pY->input(pY);

	//Update trajectory
	CHECK_(m_nTrajectory==0);

	vDouble2 vPos;
	if(m_lTrajectory.size()==0)
	{
		vPos.m_x = m_pX->v();
		vPos.m_y = m_pY->v();
		m_lTrajectory.push_back(vPos);
		return;
	}

	vPos = m_lTrajectory.back();
	double dTraj = abs(m_pX->v() - vPos.m_x) + abs(m_pY->v() - vPos.m_y);
	CHECK_(dTraj < m_trajStep);

	if (m_nTrajectory > 0)
	{
		if (m_lTrajectory.size() >= m_nTrajectory)
		{
			m_lTrajectory.pop_front();
		}
	}

	vPos.m_x = m_pX->v();
	vPos.m_y = m_pY->v();
	m_lTrajectory.push_back(vPos);

	//TODO: set new position when difference is bigger than a threshold

}

bool _Lightware_SF40::draw(void)
{
	CHECK_F(!this->_ThreadBase::draw());

	Window* pWin = (Window*)this->m_pWindow;
	Mat* pMat = pWin->getFrame()->getCMat();

	pWin->tabNext();
	putText(*pMat,
			"POS: (" + f2str(m_pX->v()) + "," + f2str(m_pY->v()) + ")",
			*pWin->getTextPos(), FONT_HERSHEY_SIMPLEX, 0.5,
			Scalar(0, 255, 0), 1);
	pWin->lineNext();

	putText(*pMat,
			"Output Line: " + i2str(m_iLine),
			*pWin->getTextPos(), FONT_HERSHEY_SIMPLEX, 0.5,
			Scalar(0, 255, 0), 1);
	pWin->lineNext();

	if (m_pIn)
		m_pIn->draw();

	if (m_pOut)
		m_pOut->draw();

	pWin->tabPrev();
	CHECK_T(m_nDiv <= 0);

	//Plot center as vehicle position
	Point pCenter(pMat->cols/2, pMat->rows/2);
	circle(*pMat, pCenter, 10, Scalar(0, 0, 255), 2);

	//Plot lidar result
	for (int i = 0; i < m_nDiv; i++)
	{
		double dist = m_pDist[i] * m_showScale;
		double angle = (m_dAngle * i + m_offsetAngle) * DEG_RADIAN;
		int pX = (dist * sin(angle));
		int pY = -(dist * cos(angle));

		circle(*pMat, pCenter+Point(pX, pY), 1, Scalar(0, 255, 0), 1);
	}

	//Plot trajectory
	CHECK_T(m_lTrajectory.size() < 2);

	vDouble2 vNow = m_lTrajectory.back();
	auto iTraj = m_lTrajectory.begin();
	vDouble2 vPosA = *iTraj;
	iTraj++;

	while (iTraj != m_lTrajectory.end())
	{
		vDouble2 vPosB = (vDouble2) *iTraj;

		line(*pMat,
				pCenter-Point(m_showScale*(vPosA.m_x - vNow.m_x), m_showScale*(vPosA.m_y - vNow.m_y)),
				pCenter-Point(m_showScale*(vPosB.m_x - vNow.m_x), m_showScale*(vPosB.m_y - vNow.m_y)),
				Scalar(0, 255, 255), 1);

		vPosA = vPosB;
		iTraj++;
	}

	return true;
}

}
