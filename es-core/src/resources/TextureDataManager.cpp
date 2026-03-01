#include "resources/TextureDataManager.h"

#include "resources/TextureData.h"
#include "resources/TextureResource.h"
#include "Log.h"
#include "Settings.h"

TextureDataManager::TextureDataManager()
{
	unsigned char data[5 * 5 * 4];
	mBlank = std::shared_ptr<TextureData>(new TextureData(false));
	for (int i = 0; i < (5 * 5); ++i)
	{
		data[i*4] = (i % 2) * 255;
		data[i*4+1] = (i % 2) * 255;
		data[i*4+2] = (i % 2) * 255;
		data[i*4+3] = 0;
	}
	mBlank->initFromRGBA(data, 5, 5);
	mLoader = new TextureLoader;
}

TextureDataManager::~TextureDataManager()
{
	delete mLoader;
}

std::shared_ptr<TextureData> TextureDataManager::add(const TextureResource* key, bool tiled)
{
	remove(key);
	std::shared_ptr<TextureData> data(new TextureData(tiled));
	mTextures.push_front(data);
	mTextureLookup[key] = mTextures.cbegin();
	return data;
}

void TextureDataManager::remove(const TextureResource* key)
{
	// Find the entry in the list
	auto it = mTextureLookup.find(key);
	if (it != mTextureLookup.cend())
	{
		// Cancel any pending async load for this texture
		mLoader->remove(*(*it).second);
		// Remove the list entry
		mTextures.erase((*it).second);
		// And the lookup
		mTextureLookup.erase(it);
	}
}

std::shared_ptr<TextureData> TextureDataManager::get(const TextureResource* key, bool enableLoading)
{
	// If it's in the cache then we want to remove it from it's current location and
	// move it to the top
	std::shared_ptr<TextureData> tex;
	auto it = mTextureLookup.find(key);
	if (it != mTextureLookup.cend())
	{
		tex = *(*it).second;
		// Remove the list entry
		mTextures.erase((*it).second);
		// Put it at the top
		mTextures.push_front(tex);
		// Store it back in the lookup
		mTextureLookup[key] = mTextures.cbegin();

		// Make sure it's loaded or queued for loading.
		// Skip textures whose load previously failed (e.g. file not found) so
		// we don't keep re-queuing them every frame they are rendered.
		if (enableLoading && tex->loadStatus() == TextureData::LoadStatus::LOADING)
			load(tex);
	}
	return tex;
}

bool TextureDataManager::bind(const TextureResource* key)
{
	std::shared_ptr<TextureData> tex = get(key);
	bool bound = false;
	if (tex != nullptr)
		bound = tex->uploadAndBind();
	if (!bound)
		mBlank->uploadAndBind();
	return bound;
}

size_t TextureDataManager::getTotalSize()
{
	size_t total = 0;
	for (auto tex : mTextures)
	{
		// Only count textures whose dimensions are known — calling width()/height()
		// on an unloaded texture triggers a synchronous load().
		if (tex->loadStatus() == TextureData::LoadStatus::LOADED)
			total += tex->width() * tex->height() * 4;
	}
	return total;
}

size_t TextureDataManager::getCommittedSize()
{
	size_t total = 0;
	for (auto tex : mTextures)
		total += tex->getVRAMUsage();
	return total;
}

size_t TextureDataManager::getQueueSize()
{
	return mLoader->getQueueSize();
}

void TextureDataManager::load(std::shared_ptr<TextureData> tex, bool block)
{
	// See if it's already loaded or has permanently failed
	if (tex->loadStatus() != TextureData::LoadStatus::LOADING)
		return;
	// Not loaded. Make sure there is room
	size_t max_texture = (size_t)Settings::getInstance()->getInt("MaxVRAM") * 1024 * 1024;

	// if max_texture is 0, then texture memory should be considered unlimited
	if (max_texture > 0)
	{
		size_t size = TextureResource::getTotalMemUsage();
		for (auto it = mTextures.crbegin(); it != mTextures.crend(); ++it)
		{
			if (size < max_texture)
				break;
			//size -= (*it)->getVRAMUsage();
			(*it)->releaseVRAM();
			(*it)->releaseRAM();
			// It may be already in the loader queue. In this case it wouldn't have been using
			// any VRAM yet but it will be. Remove it from the loader queue
			mLoader->remove(*it);
			size = TextureResource::getTotalMemUsage();
		}
	}
	if (!block)
		mLoader->load(tex);
	else
		tex->load();
}

TextureLoader::TextureLoader() : mExit(false)
{
	mMaxConcurrent = std::thread::hardware_concurrency();
	if (mMaxConcurrent < 2)
		mMaxConcurrent = 2;
	// No threads created here — they are spawned on demand by load().
}

TextureLoader::~TextureLoader()
{
	// Clear the pending queue and wait for all active threads to finish.
	std::unique_lock<std::mutex> lock(mMutex);
	mTextureDataQ.clear();
	mTextureDataLookup.clear();
	mExit = true;
	mDrainEvent.wait(lock, [this] { return mActiveLoads.empty(); });
}

void TextureLoader::workerFunc(std::shared_ptr<TextureData> textureData)
{
	while (textureData)
	{
		// Load the texture — long I/O operation, done outside the lock.
		// TextureData::load() is thread-safe (has its own mutex).
		// On failure it sets mLoadFailed internally; loadStatus() returns FAILED.
		try
		{
			textureData->load();
		}
		catch (...)
		{
			LOG(LogError) << "Unexpected exception loading texture";
		}

		// Under lock: clean up and check for next pending item
		std::unique_lock<std::mutex> lock(mMutex);
		mActiveLoads.erase(textureData.get());
		textureData.reset();

		// Try to pick up the next pending texture that still needs loading
		while (!mExit && !mTextureDataQ.empty())
		{
			std::shared_ptr<TextureData> next = mTextureDataQ.front();
			mTextureDataQ.pop_front();
			mTextureDataLookup.erase(next.get());

			if (next->loadStatus() == TextureData::LoadStatus::LOADING)
			{
				// Reuse this thread for the next item (transfer the concurrency slot)
				mActiveLoads.insert(next.get());
				textureData = std::move(next);
				break;
			}
			// else: already loaded or failed, skip
		}
	}

	// No more work or exiting — signal so destructor can proceed
	std::unique_lock<std::mutex> lock(mMutex);
	mDrainEvent.notify_all();
}

void TextureLoader::load(std::shared_ptr<TextureData> textureData)
{
	// Make sure it's not already loaded and hasn't permanently failed
	if (textureData->loadStatus() != TextureData::LoadStatus::LOADING)
		return;

	std::unique_lock<std::mutex> lock(mMutex);
	if (mExit)
		return;

	// Already being loaded by an active thread — skip
	if (mActiveLoads.count(textureData.get()) > 0)
		return;

	// Remove from pending queue if already queued (will re-add at front)
	auto td = mTextureDataLookup.find(textureData.get());
	if (td != mTextureDataLookup.cend())
	{
		mTextureDataQ.erase((*td).second);
		mTextureDataLookup.erase(td);
	}

	// Spawn a thread immediately if under concurrency limit, otherwise queue
	if (mActiveLoads.size() < mMaxConcurrent)
	{
		mActiveLoads.insert(textureData.get());
		std::thread(&TextureLoader::workerFunc, this, textureData).detach();
	}
	else
	{
		// Put it on the start of the queue as we want the newly requested textures to load first
		mTextureDataQ.push_front(textureData);
		mTextureDataLookup[textureData.get()] = mTextureDataQ.cbegin();
	}
}

void TextureLoader::remove(std::shared_ptr<TextureData> textureData)
{
	// Just remove it from the queue so we don't attempt to load it.
	// If it's currently being loaded by an active thread, let it finish —
	// we can't cancel in-progress I/O.
	std::unique_lock<std::mutex> lock(mMutex);
	auto td = mTextureDataLookup.find(textureData.get());
	if (td != mTextureDataLookup.cend())
	{
		mTextureDataQ.erase((*td).second);
		mTextureDataLookup.erase(td);
	}
}

size_t TextureLoader::getQueueSize()
{
	// Gets the amount of video memory that will be used once all textures in
	// the queue are loaded.  Only count textures whose dimensions are already
	// known — calling width()/height() on an unloaded texture triggers a
	// synchronous load() which blocks the main thread on NAS I/O.
	size_t mem = 0;
	std::unique_lock<std::mutex> lock(mMutex);
	for (auto tex : mTextureDataQ)
	{
		if (tex->loadStatus() == TextureData::LoadStatus::LOADED)
			mem += tex->width() * tex->height() * 4;
	}
	return mem;
}
