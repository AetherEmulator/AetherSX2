/*  PCSX2 - PS2 Emulator for PCs
 *  Copyright (C) 2002-2021  PCSX2 Dev Team
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

//#ifdef __ANDROID__

#ifdef __INTELLISENSE__
#include "C:\Repositories\pcsx2-arm\3rdparty\oboe\include\oboe\Oboe.h"
#else
#include "oboe/Oboe.h"
#endif

#include <memory>

#include "Global.h"
#include "SndOut.h"

struct OboeMod : public SndOutModule
{
	static constexpr u32 BUFFER_SIZE = 2048;
	static_assert((BUFFER_SIZE% SndOutPacketSize) == 0, "buffer size is multiple of snd out size");

	static OboeMod mod;

	s32 Init() override
	{
		if (!Open() || !Start())
			return -1;

		return 0;
	}

	const wchar_t* GetIdent() const override { return L"Oboe"; }
	const wchar_t* GetLongName() const override { return L"Android Oboe"; }

	~OboeMod()
	{
		Close();
	}

	bool Open()
	{
		oboe::AudioStreamBuilder builder;
		builder.setDirection(oboe::Direction::Output);
		builder.setPerformanceMode(oboe::PerformanceMode::LowLatency);
		builder.setSharingMode(oboe::SharingMode::Shared);
		builder.setFormat(oboe::AudioFormat::I16);
		builder.setChannelCount(oboe::ChannelCount::Stereo);
		builder.setDeviceId(oboe::kUnspecified);
		builder.setBufferCapacityInFrames(BUFFER_SIZE * 2);
		builder.setFramesPerDataCallback(BUFFER_SIZE);
		builder.setDataCallback(&s_data_cb);
		builder.setErrorCallback(&s_error_cb);

		Console.WriteLn("(OboeMod) Creating stream...");
		oboe::Result result = builder.openStream(m_stream);
		if (result != oboe::Result::OK)
		{
			Console.Error("(OboeMod) openStream() failed: %d", result);
			return false;
		}

		return true;
	}

	bool Start()
	{
		if (m_playing)
			return true;

		Console.WriteLn("(OboeMod) Starting stream...");
		m_stop_requested = false;

		oboe::Result result = m_stream->requestStart();
		if (result != oboe::Result::OK)
		{
			Console.Error("(OboeMod) requestStart failed: %d", result);
			return false;
		}

		m_playing = true;
		return true;
	}

	void Stop()
	{
		if (!m_playing)
			return;

		Console.WriteLn("(OboeMod) Stopping stream...");
		m_stop_requested = true;

		oboe::Result result = m_stream->requestStop();
		if (result != oboe::Result::OK)
		{
			Console.Error("(OboeMod) requestStop() failed: %d", result);
			return;
		}

		m_playing = false;
	}

	void Close() override
	{
		Console.WriteLn("(OboeMod) Closing stream...");

		if (m_playing)
			Stop();

		if (m_stream)
		{
			m_stream->close();
			m_stream.reset();
		}
	}

	struct DataCallback : public oboe::AudioStreamDataCallback
	{
		oboe::DataCallbackResult onAudioReady(oboe::AudioStream* audioStream, void* audioData, int32_t numFrames) override
		{
			StereoOut16* out = (StereoOut16*)audioData;
			pxAssertRel((numFrames & SndOutPacketSize) == 0, "Packet size is aligned");
			for (int32_t i = 0; i < numFrames; i += SndOutPacketSize)
				SndBuffer::ReadSamples(&out[i]);

			return oboe::DataCallbackResult::Continue;
		}
	};

	struct ErrorCallback : public oboe::AudioStreamErrorCallback
	{
		bool onError(oboe::AudioStream* stream, oboe::Result res) override
		{
			Console.Error("ErrorCB %d", res);
			if (res == oboe::Result::ErrorDisconnected && !mod.m_stop_requested)
			{
				Console.Error("Audio stream disconnected, trying reopening...");
				mod.Stop();
				mod.Close();
				if (!mod.Open() || !mod.Start())
					Console.Error("Failed to reopen stream after disconnection.");
				
				return true;
			}

			return false;
		}
	};

	int GetEmptySampleCount() override
	{
		const s64 pos = m_stream->getFramesRead();

		int playedSinceLastTime = (writtenSoFar - writtenLastTime) + (pos - positionLastTime);
		writtenLastTime = writtenSoFar;
		positionLastTime = pos;

		// Lowest resolution here is the SndOutPacketSize we use.
		return playedSinceLastTime;
	}

	s32 Test() const override { return 0; }

	void Configure(uptr parent) override {}

	void ReadSettings() override { }

	void WriteSettings() const override { }

	void SetApiSettings(wxString api) override { }

private:
	OboeMod() = default;

	std::shared_ptr<oboe::AudioStream> m_stream;

	s64 writtenSoFar = 0;
	s64 writtenLastTime = 0;
	s64 positionLastTime = 0;
	bool m_playing = false;
	bool m_stop_requested = false;

	static DataCallback s_data_cb;
	static ErrorCallback s_error_cb;
};

OboeMod OboeMod::mod;
OboeMod::DataCallback OboeMod::s_data_cb;
OboeMod::ErrorCallback OboeMod::s_error_cb;

SndOutModule* const OboeOut = &OboeMod::mod;

//#endif