/****************************************************************************
 *
 *  This file is part of qutIM
 *
 *  Copyright (c) 2010 by Nigmatullin Ruslan <euroelessar@gmail.com>
 *
 ***************************************************************************
 *                                                                         *
 *   This file is part of free software; you can redistribute it and/or    *
 *   modify it under the terms of the GNU General Public License as        *
 *   published by the Free Software Foundation; either version 2 of the    *
 *   License, or (at your option) any later version.                       *
 *                                                                         *
 ***************************************************************************
 ****************************************************************************/

#include "sdlbackend.h"
#include <SDL/SDL.h>
#include <SDL/SDL_mixer.h>

enum SDLAudioDefinitions
{
	SSLAudioRate = 22050,
	SDLAudioFormat = MIX_DEFAULT_FORMAT,
	SDLAudioChannels = 2,
	SDLAudioBuffers = 8192
};

typedef QMap<int, SDLSoundData*> SDLChannelsMap;
Q_GLOBAL_STATIC(SDLChannelsMap, channelsMap)

class SDLSoundData
{
public:
	SDLSoundData(const QString &filename)
	{
		QByteArray file = filename.toUtf8();
		chunk = Mix_LoadWAV(file.constData());
		if (!chunk) {
			qWarning("Can't open '%s'", qPrintable(filename));
			return;
		}
	}
	~SDLSoundData()
	{
		QMutableMapIterator<int, SDLSoundData*> it(*channelsMap());
		while (it.hasNext()) {
			it.next();
			if (it.value() == this) {
				Mix_HaltChannel(it.key());
				it.remove();
			}
		}
		Mix_FreeChunk(chunk);
	}
	Mix_Chunk *chunk;
};

SDLSoundBackend::SDLSoundBackend()
{
	m_cache.setMaxCost(10);
	
	int hz = SSLAudioRate;
	
	int audioBuffers = (SSLAudioRate / 11025) * 4096;
	
	if (!audioBuffers) /* wrong parameter? */
		audioBuffers = 8192;
	
	if (SDL_Init(SDL_INIT_AUDIO | SDL_INIT_NOPARACHUTE) < 0) {
		qCritical("Can't init SDL");
		return;
	}
	
	if (Mix_OpenAudio(hz, SDLAudioFormat, SDLAudioChannels, audioBuffers)) {
		qCritical("Unable to open audio for SDL");
		return;
	}
	Mix_ChannelFinished(channelFinished);
}

SDLSoundBackend::~SDLSoundBackend()
{
	Mix_HaltChannel(-1);
	m_cache.clear();
	SDL_Quit();
}

void SDLSoundBackend::channelFinished(int channel)
{
	channelsMap()->remove(channel);
}

void SDLSoundBackend::playSound(const QString &filename)
{
	SDLSoundData *data = m_cache.object(filename);
	if (!data) {
		data = new SDLSoundData(filename);
		m_cache.insert(filename, data);
	}
	if (!data->chunk)
		return;
	int channel = Mix_PlayChannel(-1, data->chunk, 0);
	if (channel != -1)
		channelsMap()->insert(channel, data);
}

QStringList SDLSoundBackend::supportedFormats()
{
	return QStringList(QLatin1String("wav"));
}
