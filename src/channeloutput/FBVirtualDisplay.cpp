/*
 *   FBVirtualDisplay Channel Output for Falcon Player (FPP)
 *
 *   Copyright (C) 2015 the Falcon Player Developers
 *      Initial development by:
 *      - David Pitts (dpitts)
 *      - Tony Mace (MyKroFt)
 *      - Mathew Mrosko (Materdaddy)
 *      - Chris Pinkham (CaptainMurdoch)
 *      For additional credits and developers, see credits.php.
 *
 *   The Falcon Player (FPP) is free software; you can redistribute it
 *   and/or modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation; either version 2 of
 *   the License, or (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include <fcntl.h>
#include <linux/kd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "common.h"
#include "log.h"
#include "FBVirtualDisplay.h"
#include "Sequence.h"
#include "settings.h"

/////////////////////////////////////////////////////////////////////////////
// To disable interpolated scaling on the GPU, add this to /boot/config.txt:
// scaling_kernel=8

/*
 *
 */
FBVirtualDisplayOutput::FBVirtualDisplayOutput(unsigned int startChannel,
	unsigned int channelCount)
  : VirtualDisplayOutput(startChannel, channelCount),
	m_fbFd(0),
	m_ttyFd(0),
	m_screenSize(0)
{
	LogDebug(VB_CHANNELOUT, "FBVirtualDisplayOutput::FBVirtualDisplayOutput(%u, %u)\n",
		startChannel, channelCount);

	m_maxChannels = FPPD_MAX_CHANNELS;
	m_bytesPerPixel = 3;
}

/*
 *
 */
FBVirtualDisplayOutput::~FBVirtualDisplayOutput()
{
	LogDebug(VB_CHANNELOUT, "FBVirtualDisplayOutput::~FBVirtualDisplayOutput()\n");

	Close();
}

/*
 *
 */
int FBVirtualDisplayOutput::Init(Json::Value config)
{
	LogDebug(VB_CHANNELOUT, "FBVirtualDisplayOutput::Init()\n");

	if (!VirtualDisplayOutput::Init(config))
		return 0;

	m_fbFd = open("/dev/fb0", O_RDWR);
	if (!m_fbFd)
	{
		LogErr(VB_CHANNELOUT, "Error opening FrameBuffer device\n");
		return 0;
	}

	if (ioctl(m_fbFd, FBIOGET_VSCREENINFO, &m_vInfo))
	{
		LogErr(VB_CHANNELOUT, "Error getting FrameBuffer info\n");
		close(m_fbFd);
		return 0;
	}

	memcpy(&m_vInfoOrig, &m_vInfo, sizeof(struct fb_var_screeninfo));

	m_vInfo.bits_per_pixel = 24;
	m_vInfo.xres = m_vInfo.xres_virtual = m_width;
	m_vInfo.yres = m_vInfo.yres_virtual = m_height;

	// Config to set the screen back to when we are done
	// Once we determine how this interacts with omxplayer, this may change
	m_vInfoOrig.bits_per_pixel = 16;
	m_vInfoOrig.xres = m_vInfoOrig.xres_virtual = 640;
	m_vInfoOrig.yres = m_vInfoOrig.yres_virtual = 480;

	if (ioctl(m_fbFd, FBIOPUT_VSCREENINFO, &m_vInfo))
	{
		LogErr(VB_CHANNELOUT, "Error setting FrameBuffer info\n");
		close(m_fbFd);
		return 0;
	}

	if (ioctl(m_fbFd, FBIOGET_FSCREENINFO, &m_fInfo))
	{
		LogErr(VB_CHANNELOUT, "Error getting fixed FrameBuffer info\n");
		close(m_fbFd);
		return 0;
	}

	m_screenSize = m_vInfo.xres * m_vInfo.yres * m_vInfo.bits_per_pixel / 8;

	if (m_screenSize != (m_width * m_height * 3))
	{
		LogErr(VB_CHANNELOUT, "Error, screensize incorrect\n");
		ioctl(m_fbFd, FBIOPUT_VSCREENINFO, &m_vInfoOrig);
		close(m_fbFd);
		return 0;
	}

	m_ttyFd = open("/dev/console", O_RDWR);
	if (!m_ttyFd)
	{
		LogErr(VB_CHANNELOUT, "Error, unable to open /dev/console\n");
		ioctl(m_fbFd, FBIOPUT_VSCREENINFO, &m_vInfoOrig);
		close(m_fbFd);
		return 0;
	}

	// Hide the text console
	ioctl(m_ttyFd, KDSETMODE, KD_GRAPHICS);

	m_virtualDisplay = (char*)mmap(0, m_screenSize, PROT_READ | PROT_WRITE, MAP_SHARED, m_fbFd, 0);

	if ((char *)m_virtualDisplay == (char *)-1)
	{
		LogErr(VB_CHANNELOUT, "Error, unable to map /dev/fb0\n");
		ioctl(m_fbFd, FBIOPUT_VSCREENINFO, &m_vInfoOrig);
		close(m_fbFd);
		return 0;
	}

	bzero(m_virtualDisplay, m_screenSize);

	return 1;
}

/*
 *
 */
int FBVirtualDisplayOutput::Close(void)
{
	LogDebug(VB_CHANNELOUT, "FBVirtualDisplayOutput::Close()\n");

	munmap(m_virtualDisplay, m_screenSize);

	if (ioctl(m_fbFd, FBIOPUT_VSCREENINFO, &m_vInfoOrig))
		LogErr(VB_CHANNELOUT, "Error resetting variable info\n");

	close(m_fbFd);

	delete [] m_virtualDisplay;

	// Re-enable the text console
	ioctl(m_ttyFd, KDSETMODE, KD_TEXT);
	close(m_ttyFd);

	return VirtualDisplayOutput::Close();
}

/*
 *
 */
int FBVirtualDisplayOutput::RawSendData(unsigned char *channelData)
{
	LogExcess(VB_CHANNELOUT, "FBVirtualDisplayOutput::RawSendData(%p)\n",
		channelData);

	DrawPixels(channelData);

	return m_channelCount;
}

