/*  PCSX2 - PS2 Emulator for PCs
 *  Copyright (C) 2002-2014  PCSX2 Dev Team
 *
 *  PCSX2 is free software: you can redistribute it and/or modify it under the terms
 *  of the GNU Lesser General Public License as published by the Free Software Found-
 *  ation, either version 3 of the License, or (at your option) any later version.
 *
 *  PCSX2 is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 *  without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 *  PURPOSE.  See the GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along with PCSX2.
 *  If not, see <http://www.gnu.org/licenses/>.
 */

#include "PrecompiledHeader.h"
#include "AsyncFileReader.h"
#include "common/FileSystem.h"

#ifdef __ANDROID__
bool FlatFileReader::USE_AIO = true;
#endif

FlatFileReader::FlatFileReader(bool shareWrite)
	: shareWrite(shareWrite)
{
	m_blocksize = 2048;
	m_fd = -1;
#ifdef __ANDROID__
	m_result = -1;
#endif
	m_aio_context = 0;
}

FlatFileReader::~FlatFileReader(void)
{
	Close();
}

bool FlatFileReader::Open(std::string fileName)
{
	m_filename = std::move(fileName);

#ifdef __ANDROID__
	if (USE_AIO)
#endif
	{
		int err = io_setup(64, &m_aio_context);
		if (err)
			return false;
	}

	m_fd = FileSystem::OpenFDFile(m_filename.c_str(), O_RDONLY, 0);

	return (m_fd != -1);
}

int FlatFileReader::ReadSync(void* pBuffer, uint sector, uint count)
{
	BeginRead(pBuffer, sector, count);
	return FinishRead();
}

void FlatFileReader::BeginRead(void* pBuffer, uint sector, uint count)
{
	u64 offset;
	offset = sector * (s64)m_blocksize + m_dataoffset;

	u32 bytesToRead = count * m_blocksize;

#ifdef __ANDROID__
	if (!USE_AIO)
	{
		m_result = pread(m_fd, pBuffer, bytesToRead, offset);
		return;
	}
#endif

	struct iocb iocb;
	struct iocb* iocbs = &iocb;

	io_prep_pread(&iocb, m_fd, pBuffer, bytesToRead, offset);
	io_submit(m_aio_context, 1, &iocbs);
}

int FlatFileReader::FinishRead(void)
{
#ifdef __ANDROID__
	if (!USE_AIO)
		return (m_result > 0);
#endif

	int min_nr = 1;
	int max_nr = 1;
	struct io_event events[max_nr];

	int event = io_getevents(m_aio_context, min_nr, max_nr, events, NULL);
	if (event < 1)
		return -1;

	return 1;
}

void FlatFileReader::CancelRead(void)
{
	// Will be done when m_aio_context context is destroyed
	// Note: io_cancel exists but need the iocb structure as parameter
	// int io_cancel(aio_context_t ctx_id, struct iocb *iocb,
	//                struct io_event *result);
}

void FlatFileReader::Close(void)
{
	if (m_fd != -1)
	{
		close(m_fd);
		m_fd = -1;
	}

#ifdef __ANDROID__
	m_result = -1;
#endif

#ifdef __ANDROID__
	if (USE_AIO)
#endif
	{
		io_destroy(m_aio_context);
		m_aio_context = 0;
	}
}

uint FlatFileReader::GetBlockCount(void) const
{
#if defined(__HAIKU__) || defined(__APPLE__) || defined(__FreeBSD__)
	struct stat sysStatData;
	if (fstat(m_fd, &sysStatData) < 0)
		return 0;
#else
	struct stat64 sysStatData;
	if (fstat64(m_fd, &sysStatData) < 0)
		return 0;
#endif

	return (int)(sysStatData.st_size / m_blocksize);
}
